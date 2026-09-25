#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "matchSetup.hpp"
#include "matchStats.hpp"
#include "tactic.hpp"

namespace ElyverseFootball::Benchmark {

// The standard configuration with restarts on: over whole matches, a ball
// waiting on the line would blur every statistic.
[[nodiscard]] SimMatch::MatchConfig benchmarkConfig();

// A series of tactic matches between two tactics (docs/sim-benchmark.md).
struct BenchmarkSpec {
  SimTactics::Tactic home;
  SimTactics::Tactic away;
  int matches = 10;
  std::uint64_t baseSeed = 1;
  // Ticks per match.
  std::int64_t ticks = 18'000;
  SimMatch::MatchConfig config = benchmarkConfig();
  // Matches run at once; the results do not depend on it.
  int jobs = 1;
};

// The seed of match `index`: SplitMix64 of the base seed advanced by index
// steps, so neighbouring indices get unrelated seeds and a match's seed does
// not depend on how many others run.
[[nodiscard]] std::uint64_t matchSeed(std::uint64_t baseSeed, int index) noexcept;

struct MatchResult {
  int index = 0;
  std::uint64_t seed = 0;
  SimAnalytics::MatchStats stats;
  // Wall-clock time of the match; never part of the results file.
  double wallSeconds = 0.0;
};

// Why a match could not be used: the step that failed or produced a
// non-finite value, or a statistic that is not a finite number.
struct BenchmarkFailure {
  int index = 0;
  std::uint64_t seed = 0;
  std::int64_t tick = 0;
  std::string message;
};

// "home.ppda is nan": the first statistic that is not a finite number.
[[nodiscard]] std::optional<std::string> firstInvalidStatistic(
    const SimAnalytics::MatchStats& stats);

// Plays one match of the series.
[[nodiscard]] std::expected<MatchResult, BenchmarkFailure> runMatch(const BenchmarkSpec& spec,
                                                                    int index);

// Plays every match, spec.jobs at a time, and returns the results by index.
// If any fails, the failure of the lowest index, whatever finished first.
// Throws std::invalid_argument for fewer than one match, job or tick.
[[nodiscard]] std::expected<std::vector<MatchResult>, BenchmarkFailure> runBenchmark(
    const BenchmarkSpec& spec);

// The results file: the spec, every match's statistics and each metric's
// summary over the matches, per side. Timing is left out, so the same spec
// always gives the same bytes.
[[nodiscard]] std::string toBenchmarkJson(const BenchmarkSpec& spec,
                                          std::span<const MatchResult> results);

}  // namespace ElyverseFootball::Benchmark
