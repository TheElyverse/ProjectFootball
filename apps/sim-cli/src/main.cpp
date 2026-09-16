#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iostream>
#include <optional>
#include <random>
#include <string>
#include <string_view>

#include "sim_core/rng.hpp"
#include "sim_core/sim_time.hpp"
#include "sim_core/version.hpp"

namespace {
struct CliOptions {
  std::optional<std::uint64_t> seed;
  std::string replay_out = "replay_metadata.json";
};

CliOptions parse_args(int argc, char** argv) {
  CliOptions options;
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg = argv[i];
    if (arg == "--seed" && i + 1 < argc) {
      options.seed = std::stoull(argv[++i]);
    } else if (arg == "--replay-out" && i + 1 < argc) {
      options.replay_out = argv[++i];
    }
  }
  return options;
}

// Only the CLI (an adapter, not the simulation core) may pull entropy from a
// non-deterministic source, and only to pick the *master* seed when the user
// didn't supply one -- it is recorded in the output so the run stays
// reproducible from that point on.
std::uint64_t resolve_seed(const CliOptions& options) {
  if (options.seed) {
    return *options.seed;
  }
  std::random_device rd;
  return (static_cast<std::uint64_t>(rd()) << 32) | rd();
}

std::string iso8601_now() {
  const std::time_t now = std::time(nullptr);
  std::tm utc_tm{};
#if defined(_WIN32)
  gmtime_s(&utc_tm, &now);
#else
  gmtime_r(&now, &utc_tm);
#endif
  char buffer[32];
  std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &utc_tm);
  return buffer;
}
}  // namespace

int main(int argc, char** argv) {
  const CliOptions options = parse_args(argc, argv);
  const std::uint64_t seed = resolve_seed(options);

  // "Empty simulation": a clock that exists and could tick, with no domain
  // state yet. This proves the core + CLI + replay-metadata wiring per the
  // P0 exit criteria without pretending real simulation content exists.
  const ElyverseFootball::SimCore::SimClock clock(1.0 / 30.0);
  ElyverseFootball::SimCore::Rng execution_rng(ElyverseFootball::SimCore::derive_seed(
      seed, ElyverseFootball::SimCore::RngDomain::kExecution));
  (void)execution_rng.next_u64();

  std::ofstream out(options.replay_out);
  if (!out) {
    std::cerr << "Failed to open " << options.replay_out << " for writing\n";
    return EXIT_FAILURE;
  }

  out << "{\n"
      << "  \"schemaVersion\": 1,\n"
      << "  \"coreVersion\": \"" << ElyverseFootball::SimCore::core_version() << "\",\n"
      << "  \"createdAt\": \"" << iso8601_now() << "\",\n"
      << "  \"seed\": " << seed << ",\n"
      << "  \"gameTime\": " << clock.tick().value() << "\n"
      << "}\n";

  std::cout << "Started empty simulation. Wrote replay metadata to " << options.replay_out << "\n";
  return EXIT_SUCCESS;
}
