#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "replay.hpp"

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
// The longest run --frames-out records: two minutes at 30 Hz. Every tick
// adds a full frame -- around 16 KB of JSON, and more while the recording is
// held in memory -- so a whole match would not fit, and the viewer is for
// looking at situations, not matches.
inline constexpr std::int64_t kMaxFrameTicks = 3'600;
// The scenario --home-tactic and --away-tactic play.
inline constexpr std::string_view kTacticMatchScenario = "tactic-match";

struct CliOptions {
  CliMode mode = CliMode::kRun;
  bool tui = false;
  std::string scenario = "kickoff";
  // Empty means the CLI picks a random master seed and reports it.
  std::optional<std::uint64_t> seed;
  std::int64_t ticks = kDefaultTicks;
  std::string replayOut = "replay.json";
  // Where to write debug frames for the web viewer; empty writes none.
  std::string framesOut;
  // Where to write the match statistics; empty writes none.
  std::string statsOut;
  // Ticks between the replay's checkpoints; 1 locates a divergence exactly.
  int checkpointInterval = SimReplay::kDefaultCheckpointIntervalTicks;
  std::string playPath;
  // With --play: where to write the decision trace, empty writes none, and
  // which players and ticks it covers; empty means all.
  std::string tracePath;
  std::vector<std::uint32_t> tracePlayers;
  std::optional<std::int64_t> traceFrom;
  std::optional<std::int64_t> traceTo;
  // Tactic files for the tactic-match scenario; empty plays the reference
  // tactic on that side.
  std::string homeTactic;
  std::string awayTactic;
};

inline constexpr std::string_view kUsage =
    "usage: sim-cli [--scenario <name>] [--seed <u64>] [--ticks <n>] [--replay-out <path>] "
    "[--frames-out <path>] [--stats-out <path>] [--checkpoint-interval <n>] [--tui]\n"
    "       sim-cli [--home-tactic <tactic.json>] [--away-tactic <tactic.json>] [run options]\n"
    "       sim-cli --play <replay.json> [--trace <trace.txt> [--trace-players <id,id,...>]\n"
    "               [--trace-from <tick>] [--trace-to <tick>]] [--tui]\n"
    "       sim-cli --list-scenarios\n"
    "       sim-cli --help";

// Parses argv (program name first). --help wins over everything else.
// Rejects unknown options, missing or invalid values, --play combined with
// --list-scenarios, --play or --list-scenarios combined with options that
// only apply to a new run, whatever the order, and --frames-out for a run of
// more than kMaxFrameTicks ticks, with a message naming the offending
// argument. --home-tactic and --away-tactic select the tactic-match scenario
// and reject any other.
[[nodiscard]] std::expected<CliOptions, std::string> parseCliOptions(std::span<char* const> args);

}  // namespace ElyverseFootball::Cli
