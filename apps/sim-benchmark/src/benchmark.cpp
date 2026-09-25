#include "benchmark.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <format>
#include <nlohmann/json.hpp>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "matchAnalyzer.hpp"
#include "matchSimulation.hpp"
#include "replay.hpp"
#include "replayJson.hpp"
#include "scenarios.hpp"
#include "seriesSummary.hpp"
#include "simTime.hpp"
#include "statsJson.hpp"
#include "tacticHash.hpp"
#include "version.hpp"

namespace ElyverseFootball::Benchmark {
namespace {

using Json = nlohmann::ordered_json;

[[nodiscard]] Json optionalJson(const std::optional<double>& value) {
  return value ? Json(*value) : Json(nullptr);
}

[[nodiscard]] Json summaryJson(const SimAnalytics::SeriesSummary& summary) {
  return {{"count", summary.count},
          {"mean", optionalJson(summary.mean)},
          {"variance", optionalJson(summary.variance)},
          {"ci95", summary.ciLow && summary.ciHigh ? Json{*summary.ciLow, *summary.ciHigh}
                                                   : Json(nullptr)}};
}

// The metric names, in metricsOf() order.
[[nodiscard]] std::vector<std::string_view> metricNames() {
  std::vector<std::string_view> names;
  for (const SimAnalytics::Metric& metric : SimAnalytics::metricsOf(SimAnalytics::TeamStats{})) {
    names.push_back(metric.name);
  }
  return names;
}

// Each metric's values over the side statistics, in their order.
[[nodiscard]] std::vector<std::vector<double>> valuesByMetric(
    const std::vector<const SimAnalytics::TeamStats*>& sides) {
  std::vector<std::vector<double>> values(metricNames().size());
  for (const SimAnalytics::TeamStats* side : sides) {
    const auto metrics = SimAnalytics::metricsOf(*side);
    for (std::size_t metric = 0; metric < metrics.size(); ++metric) {
      if (const auto& value = metrics.at(metric).value) {
        values.at(metric).push_back(*value);
      }
    }
  }
  return values;
}

[[nodiscard]] Json summariesJson(const std::vector<const SimAnalytics::TeamStats*>& sides) {
  Json json = Json::object();
  const auto names = metricNames();
  const auto values = valuesByMetric(sides);
  for (std::size_t metric = 0; metric < names.size(); ++metric) {
    json[std::string(names.at(metric))] = summaryJson(SimAnalytics::summarize(values.at(metric)));
  }
  return json;
}

// Each metric's summary over the matches, for one side.
[[nodiscard]] Json sideSummaryJson(const std::span<const MatchResult> results, const bool home) {
  std::vector<const SimAnalytics::TeamStats*> sides;
  for (const MatchResult& result : results) {
    sides.push_back(home ? &result.stats.home : &result.stats.away);
  }
  return summariesJson(sides);
}

// A style's statistics from every match it played, home or away, in pairing
// and match order.
[[nodiscard]] std::vector<const SimAnalytics::TeamStats*> styleSides(
    const std::size_t style, const std::span<const PairingResult> pairings) {
  std::vector<const SimAnalytics::TeamStats*> sides;
  for (const PairingResult& pairing : pairings) {
    for (const MatchResult& result : pairing.results) {
      if (pairing.home == style) {
        sides.push_back(&result.stats.home);
      }
      if (pairing.away == style) {
        sides.push_back(&result.stats.away);
      }
    }
  }
  return sides;
}

[[nodiscard]] Json resultsJson(const std::span<const MatchResult> results) {
  Json json = Json::array();
  for (const MatchResult& result : results) {
    Json stats = Json::parse(SimAnalytics::toStatsJson(result.stats));
    stats.erase("format");
    stats.erase("version");
    json.push_back({{"index", result.index},
                    {"seed", std::to_string(result.seed)},
                    {"stats", std::move(stats)}});
  }
  return json;
}

[[nodiscard]] BenchmarkSpec pairingSpec(const RoundRobinSpec& spec, const PairingResult& pairing) {
  BenchmarkSpec series{.home = spec.styles.at(pairing.home), .away = spec.styles.at(pairing.away)};
  series.matches = spec.matchesPerPairing;
  series.baseSeed = pairing.baseSeed;
  series.ticks = spec.ticks;
  series.config = spec.config;
  series.jobs = spec.jobs;
  return series;
}

[[nodiscard]] Json tacticJson(const SimTactics::Tactic& tactic) {
  return {{"name", tactic.name()},
          {"contentHash", std::format("{:016x}", SimTactics::contentHash(tactic))}};
}

}  // namespace

SimMatch::MatchConfig benchmarkConfig() {
  SimMatch::MatchConfig config;
  config.restarts.enabled = true;
  return config;
}

std::optional<std::string> firstInvalidStatistic(const SimAnalytics::MatchStats& stats) {
  for (const auto& [side, team] :
       {std::pair{"home", &stats.home}, std::pair{"away", &stats.away}}) {
    for (const SimAnalytics::Metric& metric : SimAnalytics::metricsOf(*team)) {
      if (metric.value && !std::isfinite(*metric.value)) {
        return std::format("{}.{} is {}", side, metric.name, *metric.value);
      }
    }
  }
  return std::nullopt;
}

std::uint64_t matchSeed(const std::uint64_t baseSeed, const int index) noexcept {
  // SplitMix64 (Steele, Lea, Flood 2014): one step of the golden-ratio
  // sequence, then its finalizer.
  std::uint64_t value = baseSeed + (static_cast<std::uint64_t>(index) + 1U) * 0x9e3779b97f4a7c15ULL;
  value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
  value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
  return value ^ (value >> 31U);
}

std::expected<MatchResult, BenchmarkFailure> runMatch(const BenchmarkSpec& spec, const int index) {
  const std::uint64_t seed = matchSeed(spec.baseSeed, index);
  const auto fail = [index, seed](const std::int64_t tick, std::string message) {
    return std::unexpected(BenchmarkFailure{
        .index = index, .seed = seed, .tick = tick, .message = std::move(message)});
  };
  auto setup = SimMatch::makeTacticMatch({.home = spec.home, .away = spec.away}, seed);
  if (!setup) {
    return fail(0, setup.error());
  }
  setup->config = spec.config;
  const auto started = std::chrono::steady_clock::now();
  SimMatch::MatchSimulation simulation = SimMatch::startMatch(*setup);
  SimAnalytics::MatchAnalyzer analyzer(
      SimAnalytics::contextOf(setup->initialState, setup->config.ticksPerSecond));
  while (simulation.tick() < SimCore::SimTick(spec.ticks)) {
    if (const auto stepped = simulation.step(); !stepped) {
      std::string message = std::format("system '{}' failed", stepped.error().systemName);
      for (const SimMatch::MatchStateError& error : stepped.error().errors) {
        message += "; " + error.message;
      }
      return fail(stepped.error().tick.value(), std::move(message));
    }
    analyzer.observeStep(simulation.events());
  }
  MatchResult result{
      .index = index,
      .seed = seed,
      .stats = analyzer.finish(simulation.tick()),
      .wallSeconds =
          std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count()};
  if (const auto problem = firstInvalidStatistic(result.stats)) {
    return fail(simulation.tick().value(), "invalid statistics: " + *problem);
  }
  return result;
}

std::expected<std::vector<MatchResult>, BenchmarkFailure> runBenchmark(const BenchmarkSpec& spec) {
  if (spec.matches < 1 || spec.jobs < 1 || spec.ticks < 1) {
    throw std::invalid_argument("runBenchmark: needs at least one match, job and tick");
  }
  std::vector<std::optional<std::expected<MatchResult, BenchmarkFailure>>> outcomes(
      static_cast<std::size_t>(spec.matches));
  std::atomic<int> next{0};
  const auto work = [&spec, &outcomes, &next] {
    for (int index = next++; index < spec.matches; index = next++) {
      outcomes.at(static_cast<std::size_t>(index)) = runMatch(spec, index);
    }
  };
  {
    std::vector<std::jthread> workers;
    for (int job = 1; job < std::min(spec.jobs, spec.matches); ++job) {
      workers.emplace_back(work);
    }
    work();
  }
  std::vector<MatchResult> results;
  results.reserve(outcomes.size());
  for (auto& outcome : outcomes) {
    // Every index ran: the workers joined above.
    if (!outcome || !*outcome) {
      return std::unexpected(outcome ? outcome->error() : BenchmarkFailure{});
    }
    results.push_back(**std::move(outcome));
  }
  return results;
}

std::string toBenchmarkJson(const BenchmarkSpec& spec, const std::span<const MatchResult> results) {
  Json json;
  json["format"] = "elyverse-benchmark";
  json["version"] = 1;
  json["coreVersion"] = std::string(SimCore::coreVersion());
  json["home"] = tacticJson(spec.home);
  json["away"] = tacticJson(spec.away);
  json["matches"] = spec.matches;
  // A string, like a replay's seed: JavaScript numbers cannot hold every
  // 64-bit value.
  json["baseSeed"] = std::to_string(spec.baseSeed);
  json["ticks"] = spec.ticks;
  json["restarts"] = spec.config.restarts.enabled;
  json["summary"] = {{"home", sideSummaryJson(results, true)},
                     {"away", sideSummaryJson(results, false)}};
  json["results"] = resultsJson(results);
  return json.dump(2) + "\n";
}

BenchmarkSpec seriesOf(const RoundRobinSpec& spec, const PairingResult& pairing) {
  return pairingSpec(spec, pairing);
}

std::expected<std::vector<PairingResult>, BenchmarkFailure> runRoundRobin(
    const RoundRobinSpec& spec) {
  std::vector<PairingResult> pairings;
  for (std::size_t home = 0; home < spec.styles.size(); ++home) {
    for (std::size_t away = 0; away < spec.styles.size(); ++away) {
      PairingResult pairing{.home = home,
                            .away = away,
                            .baseSeed = matchSeed(spec.baseSeed, static_cast<int>(pairings.size())),
                            .results = {}};
      auto results = runBenchmark(pairingSpec(spec, pairing));
      if (!results) {
        return std::unexpected(results.error());
      }
      pairing.results = *std::move(results);
      pairings.push_back(std::move(pairing));
    }
  }
  return pairings;
}

std::vector<Separation> separations(const RoundRobinSpec& spec,
                                    const std::span<const PairingResult> pairings) {
  const auto names = metricNames();
  std::vector<std::vector<SimAnalytics::SeriesSummary>> summaries;
  for (std::size_t style = 0; style < spec.styles.size(); ++style) {
    std::vector<SimAnalytics::SeriesSummary> byMetric;
    for (const auto& values : valuesByMetric(styleSides(style, pairings))) {
      byMetric.push_back(SimAnalytics::summarize(values));
    }
    summaries.push_back(std::move(byMetric));
  }
  std::vector<Separation> found;
  for (std::size_t metric = 0; metric < names.size(); ++metric) {
    for (std::size_t first = 0; first < summaries.size(); ++first) {
      for (std::size_t second = 0; second < summaries.size(); ++second) {
        const auto& high = summaries.at(first).at(metric);
        const auto& low = summaries.at(second).at(metric);
        if (first != second && high.ciLow && low.ciHigh && *high.ciLow > *low.ciHigh) {
          found.push_back(
              {.metric = std::string(names.at(metric)), .higher = first, .lower = second});
        }
      }
    }
  }
  return found;
}

std::string toRoundRobinJson(const RoundRobinSpec& spec,
                             const std::span<const PairingResult> pairings) {
  Json json;
  json["format"] = "elyverse-style-benchmark";
  json["version"] = 1;
  json["coreVersion"] = std::string(SimCore::coreVersion());
  json["styles"] = Json::array();
  for (const SimTactics::Tactic& style : spec.styles) {
    json["styles"].push_back(tacticJson(style));
  }
  json["matchesPerPairing"] = spec.matchesPerPairing;
  json["baseSeed"] = std::to_string(spec.baseSeed);
  json["ticks"] = spec.ticks;
  json["restarts"] = spec.config.restarts.enabled;
  json["styleSummary"] = Json::object();
  for (std::size_t style = 0; style < spec.styles.size(); ++style) {
    json["styleSummary"][spec.styles.at(style).name()] = summariesJson(styleSides(style, pairings));
  }
  json["separations"] = Json::array();
  for (const Separation& separation : separations(spec, pairings)) {
    json["separations"].push_back({{"metric", separation.metric},
                                   {"higher", spec.styles.at(separation.higher).name()},
                                   {"lower", spec.styles.at(separation.lower).name()}});
  }
  json["pairings"] = Json::array();
  for (const PairingResult& pairing : pairings) {
    json["pairings"].push_back({{"home", spec.styles.at(pairing.home).name()},
                                {"away", spec.styles.at(pairing.away).name()},
                                {"baseSeed", std::to_string(pairing.baseSeed)},
                                {"summary",
                                 {{"home", sideSummaryJson(pairing.results, true)},
                                  {"away", sideSummaryJson(pairing.results, false)}}},
                                {"results", resultsJson(pairing.results)}});
  }
  return json.dump(2) + "\n";
}

std::expected<void, BenchmarkFailure> verifyReplay(const BenchmarkSpec& spec, const int index) {
  const std::uint64_t seed = matchSeed(spec.baseSeed, index);
  auto setup = SimMatch::makeTacticMatch({.home = spec.home, .away = spec.away}, seed);
  if (!setup) {
    return std::unexpected(
        BenchmarkFailure{.index = index, .seed = seed, .tick = 0, .message = setup.error()});
  }
  setup->config = spec.config;
  const auto replay =
      SimReplay::recordMatch(*setup, SimCore::SimTick(spec.ticks),
                             SimReplay::kDefaultCheckpointIntervalTicks, "benchmark");
  if (!replay) {
    return std::unexpected(BenchmarkFailure{
        .index = index,
        .seed = seed,
        .tick = replay.error().tick.value(),
        .message = "recording failed in system '" + replay.error().systemName + "'"});
  }
  const auto parsed = SimReplay::parseReplayJson(SimReplay::toReplayJson(*replay));
  const auto playback = parsed ? SimReplay::playReplay(*parsed)
                               : std::expected<SimReplay::ReplayPlayback, SimReplay::ReplayError>(
                                     std::unexpected(parsed.error()));
  if (!playback) {
    const auto& divergence = playback.error().divergence;
    return std::unexpected(
        BenchmarkFailure{.index = index,
                         .seed = seed,
                         .tick = divergence ? divergence->firstDiverging.value() : 0,
                         .message = "replay does not reproduce: " + playback.error().message});
  }
  return {};
}

}  // namespace ElyverseFootball::Benchmark
