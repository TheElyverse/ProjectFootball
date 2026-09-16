#include <cstdlib>
#include <ctime>
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

CliOptions parseArgs(int argc, char** argv) {
  CliOptions options;
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg = argv[i];
    if (arg == "--seed" && i + 1 < argc) {
      options.seed = std::stoull(argv[++i]);
    } else if (arg == "--replay-out" && i + 1 < argc) {
      options.replayOut = argv[++i];
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
  std::random_device rd;
  return (static_cast<std::uint64_t>(rd()) << 32) | rd();
}

std::string iso8601Now() {
  const std::time_t now = std::time(nullptr);
  std::tm utcTm{};
#if defined(_WIN32)
  gmtime_s(&utcTm, &now);
#else
  gmtime_r(&now, &utcTm);
#endif
  char buffer[32];
  std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &utcTm);
  return buffer;
}
}  // namespace

int main(int argc, char** argv) {
  const CliOptions options = parseArgs(argc, argv);
  const std::uint64_t seed = resolveSeed(options);

  // "Empty simulation": a clock that exists and could tick, with no domain
  // state yet. This proves the core + CLI + replay-metadata wiring per the
  // P0 exit criteria without pretending real simulation content exists.
  const ElyverseFootball::SimCore::SimClock clock(1.0 / 30.0);
  ElyverseFootball::SimCore::Rng executionRng(ElyverseFootball::SimCore::deriveSeed(
      seed, ElyverseFootball::SimCore::RandomNumberGeneratorDomain::kExecution));
  (void)executionRng.nextU64();

  std::ofstream out(options.replayOut);
  if (!out) {
    std::cerr << "Failed to open " << options.replayOut << " for writing\n";
    return EXIT_FAILURE;
  }

  out << "{\n"
      << "  \"schemaVersion\": 1,\n"
      << "  \"coreVersion\": \"" << ElyverseFootball::SimCore::coreVersion() << "\",\n"
      << "  \"createdAt\": \"" << iso8601Now() << "\",\n"
      << "  \"seed\": " << seed << ",\n"
      << "  \"gameTime\": " << clock.tick().value() << "\n"
      << "}\n";

  std::cout << "Started empty simulation. Wrote replay metadata to " << options.replayOut << "\n";
  return EXIT_SUCCESS;
}
