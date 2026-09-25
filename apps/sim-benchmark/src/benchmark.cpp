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

// Each metric's summary over the matches, for one side.
[[nodiscard]] Json sideSummaryJson(const std::span<const MatchResult> results, const bool home) {
  Json json = Json::object();
  const std::vector<SimAnalytics::Metric> names =
      SimAnalytics::metricsOf(SimAnalytics::TeamStats{});
  for (std::size_t metric = 0; metric < names.size(); ++metric) {
    std::vector<double> values;
    for (const MatchResult& result : results) {
      const auto& value =
          SimAnalytics::metricsOf(home ? result.stats.home : result.stats.away).at(metric).value;
      if (value) {
        values.push_back(*value);
      }
    }
    json[std::string(names.at(metric).name)] = summaryJson(SimAnalytics::summarize(values));
  }
  return json;
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
  json["results"] = Json::array();
  for (const MatchResult& result : results) {
    Json stats = Json::parse(SimAnalytics::toStatsJson(result.stats));
    stats.erase("format");
    stats.erase("version");
    json["results"].push_back({{"index", result.index},
                               {"seed", std::to_string(result.seed)},
                               {"stats", std::move(stats)}});
  }
  return json.dump(2) + "\n";
}

}  // namespace ElyverseFootball::Benchmark
