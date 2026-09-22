#include <array>
#include <charconv>
#include <cstdlib>
#include <ctime>
#include <expected>
#include <fstream>
#include <iostream>
#include <optional>
#include <random>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

#include "random.hpp"
#include "simTime.hpp"
#include "terminalUi.hpp"
#include "version.hpp"

namespace {
struct CliOptions {
  bool tui = false;
  std::optional<std::uint64_t> seed;
  std::string replayOut = "replay_metadata.json";
};

constexpr std::string_view kUsage = "usage: sim-cli [--tui] [--seed <u64>] [--replay-out <path>]";

// std::span has no bounds-checked at() (unlike std::vector/std::array), so this
// is span's missing at(): the one place a bounds check plus the actual element
// access happen together, instead of trusting every call site to have checked
// first. Mirrors what std::vector::at()/gsl::at() do internally.
[[nodiscard]] char* checkedAt(const std::span<char* const> args, const std::size_t index) {
  if (index >= args.size()) {
    throw std::out_of_range("sim-cli: argument index out of range");
  }
  return args[index];  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
}

std::expected<std::uint64_t, std::string> parseSeed(const std::string_view token) {
  std::uint64_t value = 0;
  // NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic) -- std::from_chars only takes a
  // raw [begin, end) pointer range, there is no std::string_view overload.
  const auto [ptr, ec] = std::from_chars(token.data(), token.data() + token.size(), value);
  if (ec != std::errc{} || ptr != token.data() + token.size()) {
    // NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    return std::unexpected("invalid --seed value '" + std::string(token) + "'");
  }
  return value;
}

// Takes a span instead of (argc, char**) so indexing goes through
// std::span::operator[] rather than raw pointer arithmetic on argv.
std::expected<CliOptions, std::string> parseArgs(const std::span<char* const> args) {
  CliOptions options;
  for (std::size_t i = 1; i < args.size(); ++i) {
    const std::string_view arg = checkedAt(args, i);
    if (arg == "--tui") {
      options.tui = true;
    } else if (arg == "--seed") {
      if (i + 1 >= args.size()) {
        return std::unexpected("--seed requires a value");
      }
      const auto seed = parseSeed(checkedAt(args, ++i));
      if (!seed) {
        return std::unexpected(seed.error());
      }
      options.seed = *seed;
    } else if (arg == "--replay-out") {
      if (i + 1 >= args.size()) {
        return std::unexpected("--replay-out requires a value");
      }
      options.replayOut = checkedAt(args, ++i);
    } else {
      return std::unexpected("unknown argument '" + std::string(arg) + "'");
    }
  }
  return options;
}

// Only the CLI (an adapter, not the simulation core) may pull entropy from a
// non-deterministic source, and only to pick the *master* seed when the user
// didn't supply one -- it is recorded in the output so the run stays
// reproducible from that point on.
std::uint64_t resolveSeed(const CliOptions& options) {
  if (options.seed) {
    return *options.seed;
  }
  std::random_device randomDevice;
  return (static_cast<std::uint64_t>(randomDevice()) << 32U) | randomDevice();
}

std::string iso8601Now() {
  const std::time_t now = std::time(nullptr);
  std::tm utcTm{};
#ifdef _WIN32
  gmtime_s(&utcTm, &now);
#else
  gmtime_r(&now, &utcTm);
#endif
  std::array<char, 32> buffer{};
  std::strftime(buffer.data(), buffer.size(), "%Y-%m-%dT%H:%M:%SZ", &utcTm);
  return buffer.data();
}
}  // namespace

int main(int argc, char** argv) {
  std::expected<CliOptions, std::string> parsed;
  try {
    parsed = parseArgs(std::span<char* const>(argv, static_cast<std::size_t>(argc)));
  } catch (const std::out_of_range&) {
    // checkedAt()'s bounds check should be unreachable given parseArgs()'s own
    // guards; if it ever fires (e.g. a future refactor drops a guard), report
    // it the same way as any other malformed input instead of letting the
    // exception escape main() and std::terminate().
    parsed = std::unexpected("internal error: argument index out of range");
  }
  if (!parsed) {
    std::cerr << "sim-cli: " << parsed.error() << "\n" << kUsage << "\n";
    return EXIT_FAILURE;
  }
  const CliOptions& options = *parsed;
  if (options.tui && !ElyverseFootball::Cli::hasInteractiveTerminal()) {
    std::cerr << "sim-cli: --tui requires an interactive terminal on stdin and stdout\n";
    return EXIT_FAILURE;
  }
  const std::uint64_t seed = resolveSeed(options);

  // "Empty simulation": a clock that exists and could tick, with no domain
  // state yet. This proves the core + CLI + replay-metadata wiring per the
  // P0 exit criteria without pretending real simulation content exists.
  constexpr ElyverseFootball::SimCore::SimClock clock(1.0 / 30.0);
  ElyverseFootball::SimCore::RandomNumberGenerator executionRng(
      ElyverseFootball::SimCore::deriveSeed(
          seed, ElyverseFootball::SimCore::RandomNumberGeneratorDomain::kExecution));
  (void)executionRng.nextU64();

  std::ofstream out(options.replayOut);
  if (!out) {
    std::cerr << "Failed to open " << options.replayOut << " for writing\n";
    return EXIT_FAILURE;
  }

  // seed is quoted deliberately: a JSON number may be decoded as a double and
  // rounded above 2^53, which would silently change the simulation. See
  // docs/replay-metadata.md.
  out << "{\n"
      << "  \"schemaVersion\": 1,\n"
      << R"(  "coreVersion": ")" << ElyverseFootball::SimCore::coreVersion() << "\",\n"
      << R"(  "createdAt": ")" << iso8601Now() << "\",\n"
      << R"(  "seed": ")" << seed << "\",\n"
      << "  \"gameTime\": " << clock.tick().value() << "\n"
      << "}\n";

  out.close();
  if (!out) {
    std::cerr << "Failed to write replay metadata to " << options.replayOut << "\n";
    return EXIT_FAILURE;
  }

  if (options.tui) {
    ElyverseFootball::Cli::showSimulationSummary(seed, clock.tick(), options.replayOut);
  } else {
    std::cout << "Started empty simulation. Wrote replay metadata to " << options.replayOut << "\n";
  }
  return EXIT_SUCCESS;
}
