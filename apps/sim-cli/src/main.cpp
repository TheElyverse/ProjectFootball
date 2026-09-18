#include <array>
#include <charconv>
#include <cstdlib>
#include <ctime>
#include <expected>
#include <fstream>
#include <iostream>
#include <optional>
#include <random>
#include <string>
#include <string_view>

#include "random.hpp"
#include "simTime.hpp"
#include "version.hpp"

namespace {
struct CliOptions {
  std::optional<std::uint64_t> seed;
  std::string replayOut = "replay_metadata.json";
};

constexpr std::string_view kUsage =
    "usage: sim-cli [--seed <u64>] [--replay-out <path>]";

std::expected<std::uint64_t, std::string> parseSeed(const std::string_view token) {
  std::uint64_t value = 0;
  const auto [ptr, ec] = std::from_chars(token.data(), token.data() + token.size(), value);
  if (ec != std::errc{} || ptr != token.data() + token.size()) {
    return std::unexpected("invalid --seed value '" + std::string(token) + "'");
  }
  return value;
}

std::expected<CliOptions, std::string> parseArgs(const int argc, char** argv) {
  CliOptions options;
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg = argv[i];
    if (arg == "--seed") {
      if (i + 1 >= argc) {
        return std::unexpected("--seed requires a value");
      }
      const auto seed = parseSeed(argv[++i]);
      if (!seed) {
        return std::unexpected(seed.error());
      }
      options.seed = *seed;
    } else if (arg == "--replay-out") {
      if (i + 1 >= argc) {
        return std::unexpected("--replay-out requires a value");
      }
      options.replayOut = argv[++i];
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
  const std::expected<CliOptions, std::string> parsed = parseArgs(argc, argv);
  if (!parsed) {
    std::cerr << "sim-cli: " << parsed.error() << "\n" << kUsage << "\n";
    return EXIT_FAILURE;
  }
  const CliOptions& options = *parsed;
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

  out << "{\n"
      << "  \"schemaVersion\": 1,\n"
      << R"(  "coreVersion": ")" << ElyverseFootball::SimCore::coreVersion() << "\",\n"
      << R"(  "createdAt": ")" << iso8601Now() << "\",\n"
      << "  \"seed\": " << seed << ",\n"
      << "  \"gameTime\": " << clock.tick().value() << "\n"
      << "}\n";

  std::cout << "Started empty simulation. Wrote replay metadata to " << options.replayOut << "\n";
  return EXIT_SUCCESS;
}
