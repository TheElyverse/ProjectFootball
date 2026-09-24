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

#include "kickoffScenario.hpp"
#include "matchSetup.hpp"
#include "replay.hpp"
#include "replayJson.hpp"
#include "simTime.hpp"
#include "terminalUi.hpp"

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

// The kickoff fixture on the 60 x 40 m example pitch, not yet run: the CLI
// records a replay of zero ticks. Running a match comes with scenario
// selection.
std::expected<ElyverseFootball::SimMatch::MatchSetup, std::string> kickoffSetup(
    const std::uint64_t seed) {
  auto state = ElyverseFootball::SimMatch::makeSevenASideKickoff(
      ElyverseFootball::SimMatch::Pitch(60.0, 40.0));
  if (!state) {
    return std::unexpected("the kickoff fixture is invalid: " + state.error().front().message);
  }
  return ElyverseFootball::SimMatch::MatchSetup{
      .initialState = *std::move(state), .config = {}, .seed = seed, .commands = {}};
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

  const auto setup = kickoffSetup(seed);
  if (!setup) {
    std::cerr << "sim-cli: " << setup.error() << "\n";
    return EXIT_FAILURE;
  }
  const auto replay = ElyverseFootball::SimReplay::recordMatch(
      *setup, ElyverseFootball::SimCore::SimTick(0),
      ElyverseFootball::SimReplay::kDefaultCheckpointIntervalTicks, iso8601Now());
  if (!replay) {
    std::cerr << "sim-cli: the simulation failed in system '" << replay.error().systemName << "'\n";
    return EXIT_FAILURE;
  }
  if (const auto saved = ElyverseFootball::SimReplay::saveReplay(*replay, options.replayOut);
      !saved) {
    std::cerr << "sim-cli: " << saved.error().message << "\n";
    return EXIT_FAILURE;
  }

  if (options.tui) {
    ElyverseFootball::Cli::showSimulationSummary(seed, replay->finalTick, options.replayOut);
  } else {
    std::cout << "Started empty simulation. Wrote replay to " << options.replayOut << "\n";
  }
  return EXIT_SUCCESS;
}
