#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "matchAnalyzer.hpp"
#include "matchSetup.hpp"
#include "matchSimulation.hpp"
#include "matchStats.hpp"
#include "referenceTactic.hpp"
#include "scenarios.hpp"
#include "statsJson.hpp"
#include "tactic.hpp"

using ElyverseFootball::SimAnalytics::contextOf;
using ElyverseFootball::SimAnalytics::MatchAnalyzer;
using ElyverseFootball::SimAnalytics::MatchStats;
using ElyverseFootball::SimAnalytics::toStatsJson;
using ElyverseFootball::SimCore::SimTick;

namespace {

// The statistics of a one-minute reference tactic match.
[[nodiscard]] MatchStats referenceMatch(const std::uint64_t seed) {
  const auto reference = ElyverseFootball::SimTactics::Tactic::create(
      ElyverseFootball::SimTactics::referenceTacticSpec());
  REQUIRE(reference.has_value());
  const auto setup =
      ElyverseFootball::SimMatch::makeTacticMatch({.home = *reference, .away = *reference}, seed);
  REQUIRE(setup.has_value());
  ElyverseFootball::SimMatch::MatchSimulation simulation =
      ElyverseFootball::SimMatch::startMatch(*setup);
  MatchAnalyzer analyzer(contextOf(setup->initialState, setup->config.ticksPerSecond));
  while (simulation.tick() < SimTick(1800)) {
    REQUIRE(simulation.step().has_value());
    analyzer.observeStep(simulation.events());
  }
  return analyzer.finish(simulation.tick());
}

}  // namespace

TEST_CASE("Statistics are written in a fixed order with nulls for empty values", "[statsJson]") {
  MatchStats stats;
  stats.ticks = 300;
  stats.seconds = 10.0;
  stats.home.passes = 4;
  stats.home.passCompletion = 0.75;
  stats.home.regainsByThird = {1, 2, 3};
  const std::string text = toStatsJson(stats);
  const auto json = nlohmann::ordered_json::parse(text);

  REQUIRE(json["format"] == "elyverse-match-stats");
  REQUIRE(json["version"] == 1);
  REQUIRE(json["ticks"] == 300);
  REQUIRE(json["home"]["passes"] == 4);
  REQUIRE(json["home"]["passCompletion"] == 0.75);
  REQUIRE(json["home"]["regainsByThird"]["attacking"] == 3);
  REQUIRE(json["away"]["passCompletion"].is_null());
  REQUIRE(json["away"]["ppda"].is_null());
  std::vector<std::string> keys;
  for (const auto& [key, value] : json.items()) {
    keys.push_back(key);
  }
  REQUIRE(keys ==
          std::vector<std::string>{"format", "version", "ticks", "seconds", "home", "away"});
  REQUIRE(text.back() == '\n');
}

TEST_CASE("The same match gives byte-identical statistics", "[statsJson]") {
  const MatchStats first = referenceMatch(4);
  const MatchStats second = referenceMatch(4);
  REQUIRE(toStatsJson(first) == toStatsJson(second));
  REQUIRE(toStatsJson(first) != toStatsJson(referenceMatch(5)));

  // What holds for any match.
  REQUIRE(first.home.possessionShare + first.away.possessionShare == Catch::Approx(1.0));
  REQUIRE(first.home.turnovers == first.away.regains);
  REQUIRE(first.away.turnovers == first.home.regains);
  REQUIRE(first.home.passes > 0);
  REQUIRE(first.home.pitchControlShare.has_value());
}
