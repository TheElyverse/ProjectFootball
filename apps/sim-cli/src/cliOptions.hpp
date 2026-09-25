#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace ElyverseFootball::Cli {

enum class CliMode : std::uint8_t {
  kRun,            // run a scenario and record a replay
  kPlay,           // play a replay file back and verify it
  kListScenarios,  // print the scenario catalog
  kHelp,           // print usage
};

// The run length unless --ticks states another: ten seconds at 30 Hz.
inline constexpr std::int64_t kDefaultTicks = 300;
// Longer than any match, short enough to reject a typo'd extra digit or two.
// Also the longest replay --play runs.
inline constexpr std::int64_t kMaxTicks = 10'000'000;

struct CliOptions {
  CliMode mode = CliMode::kRun;
  bool tui = false;
  std::string scenario = "kickoff";
  // Empty means the CLI picks a random master seed and reports it.
  std::optional<std::uint64_t> seed;
  std::int64_t ticks = kDefaultTicks;
  std::string replayOut = "replay.json";
  std::string playPath;
};

inline constexpr std::string_view kUsage =
    "usage: sim-cli [--scenario <name>] [--seed <u64>] [--ticks <n>] [--replay-out <path>] "
    "[--tui]\n"
    "       sim-cli --play <replay.json> [--tui]\n"
    "       sim-cli --list-scenarios\n"
    "       sim-cli --help";

// Parses argv (program name first). --help wins over everything else.
// Rejects unknown options, missing or invalid values, --play combined with
// --list-scenarios, and --play or --list-scenarios combined with options that
// only apply to a new run, whatever the order, with a message naming the
// offending argument.
[[nodiscard]] std::expected<CliOptions, std::string> parseCliOptions(std::span<char* const> args);

}  // namespace ElyverseFootball::Cli
