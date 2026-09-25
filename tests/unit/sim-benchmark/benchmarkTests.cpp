#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include "benchmark.hpp"
#include "matchStats.hpp"
#include "referenceTactic.hpp"
#include "tactic.hpp"

using ElyverseFootball::Benchmark::BenchmarkSpec;
using ElyverseFootball::Benchmark::firstInvalidStatistic;
using ElyverseFootball::Benchmark::matchSeed;
using ElyverseFootball::Benchmark::runBenchmark;
using ElyverseFootball::Benchmark::toBenchmarkJson;
using ElyverseFootball::SimTactics::referenceTacticSpec;
using ElyverseFootball::SimTactics::Tactic;

namespace {

// The reference tactic against a pressing variant of it, three matches of
// twenty seconds.
[[nodiscard]] BenchmarkSpec spec(const int jobs) {
  auto pressingSpec = referenceTacticSpec();
  pressingSpec.name = "press";
  pressingSpec.principles.pressingLine = 0.4;
  for (auto& phase : pressingSpec.phases) {
    phase.pressingIntensity = 1.0;
  }
  const auto reference = Tactic::create(referenceTacticSpec());
  const auto pressing = Tactic::create(pressingSpec);
  REQUIRE(reference.has_value());
  REQUIRE(pressing.has_value());
  BenchmarkSpec benchmark{.home = *pressing, .away = *reference};
  benchmark.matches = 3;
  benchmark.baseSeed = 11;
  benchmark.ticks = 600;
  benchmark.jobs = jobs;
  return benchmark;
}

}  // namespace

TEST_CASE("Match seeds are fixed, distinct and independent of the series length", "[benchmark]") {
  std::set<std::uint64_t> seeds;
  for (int index = 0; index < 1000; ++index) {
    seeds.insert(matchSeed(7, index));
  }
  REQUIRE(seeds.size() == 1000);
  REQUIRE(matchSeed(7, 3) == matchSeed(7, 3));
  REQUIRE(matchSeed(7, 3) != matchSeed(8, 3));
  REQUIRE(matchSeed(7, 0) != 7);
}

TEST_CASE("The results do not depend on how many matches run at once", "[benchmark]") {
  const BenchmarkSpec serial = spec(1);
  const auto one = runBenchmark(serial);
  const auto three = runBenchmark(spec(3));
  REQUIRE(one.has_value());
  REQUIRE(three.has_value());
  const std::string json = toBenchmarkJson(serial, *one);
  REQUIRE(json == toBenchmarkJson(spec(3), *three));

  const auto document = nlohmann::json::parse(json);
  REQUIRE(document.at("format") == "elyverse-benchmark");
  REQUIRE(document.at("home").at("name") == "press");
  REQUIRE(document.at("baseSeed") == "11");
  REQUIRE(document.at("restarts") == true);
  REQUIRE(document.at("results").size() == 3);
  for (std::size_t index = 0; index < 3; ++index) {
    const auto& result = document.at("results").at(index);
    REQUIRE(result.at("index") == index);
    REQUIRE(result.at("seed") == std::to_string(matchSeed(11, static_cast<int>(index))));
    REQUIRE(result.at("stats").at("ticks") == 600);
  }
  const auto& possession = document.at("summary").at("home").at("possessionShare");
  REQUIRE(possession.at("count") == 3);
  REQUIRE(possession.at("ci95").size() == 2);
  REQUIRE(possession.at("ci95").at(0) <= possession.at("mean"));
}

TEST_CASE("A statistic that is not a finite number is named", "[benchmark]") {
  ElyverseFootball::SimAnalytics::MatchStats stats;
  REQUIRE_FALSE(firstInvalidStatistic(stats).has_value());
  stats.away.ppda = std::numeric_limits<double>::quiet_NaN();
  REQUIRE(firstInvalidStatistic(stats) == "away.ppda is nan");
}

TEST_CASE("A benchmark needs a match, a job and a tick", "[benchmark]") {
  const auto runs = [](const BenchmarkSpec& benchmark) {
    const auto results = runBenchmark(benchmark);
    return results.has_value();
  };
  BenchmarkSpec empty = spec(1);
  empty.matches = 0;
  REQUIRE_THROWS_AS(runs(empty), std::invalid_argument);
  BenchmarkSpec idle = spec(1);
  idle.jobs = 0;
  REQUIRE_THROWS_AS(runs(idle), std::invalid_argument);
}

TEST_CASE("A round robin plays every ordered pairing with its own seeds", "[benchmark]") {
  const BenchmarkSpec base = spec(2);
  ElyverseFootball::Benchmark::RoundRobinSpec roundRobin;
  roundRobin.styles = {base.home, base.away};
  roundRobin.matchesPerPairing = 2;
  roundRobin.baseSeed = 4;
  roundRobin.ticks = 300;
  roundRobin.jobs = 2;
  const auto pairings = ElyverseFootball::Benchmark::runRoundRobin(roundRobin);
  REQUIRE(pairings.has_value());
  REQUIRE(pairings->size() == 4);
  std::set<std::uint64_t> seeds;
  for (std::size_t index = 0; index < pairings->size(); ++index) {
    const auto& pairing = pairings->at(index);
    REQUIRE(pairing.home == index / 2);
    REQUIRE(pairing.away == index % 2);
    REQUIRE(pairing.baseSeed == matchSeed(4, static_cast<int>(index)));
    REQUIRE(pairing.results.size() == 2);
    seeds.insert(pairing.results.front().seed);
  }
  REQUIRE(seeds.size() == 4);

  const auto json =
      nlohmann::json::parse(ElyverseFootball::Benchmark::toRoundRobinJson(roundRobin, *pairings));
  REQUIRE(json.at("format") == "elyverse-style-benchmark");
  REQUIRE(json.at("pairings").size() == 4);
  // Each style's summary counts its matches home and away: 2 + 2 + 2 x 2.
  REQUIRE(json.at("styleSummary").at("press").at("passes").at("count") == 8);
  REQUIRE(json.at("separations").is_array());
}

TEST_CASE("Styles separate on a metric when their intervals do not overlap", "[benchmark]") {
  using ElyverseFootball::Benchmark::MatchResult;
  using ElyverseFootball::Benchmark::PairingResult;
  using ElyverseFootball::Benchmark::Separation;
  const BenchmarkSpec base = spec(1);
  ElyverseFootball::Benchmark::RoundRobinSpec roundRobin;
  roundRobin.styles = {base.home, base.away};
  // Style 0 wins the ball back 10 or 11 times a match, style 1 twice or three
  // times; both pass 5 to 50 times, all over the place.
  PairingResult pairing{.home = 0, .away = 1, .baseSeed = 0, .results = {}};
  for (int index = 0; index < 6; ++index) {
    MatchResult result;
    result.stats.home.regains = 10 + (index % 2);
    result.stats.away.regains = 2 + (index % 2);
    result.stats.home.passes = index % 2 == 0 ? 5 : 50;
    result.stats.away.passes = index % 2 == 0 ? 50 : 5;
    pairing.results.push_back(result);
  }
  const std::vector<PairingResult> pairings{pairing};
  const auto found = ElyverseFootball::Benchmark::separations(roundRobin, pairings);
  REQUIRE(std::ranges::find(found, Separation{.metric = "regains", .higher = 0, .lower = 1}) !=
          found.end());
  REQUIRE(std::ranges::none_of(
      found, [](const Separation& separation) { return separation.metric == "passes"; }));
}
