#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>
#include <utility>
#include <vector>

#include "matchAnalyzer.hpp"
#include "matchSetup.hpp"
#include "matchSimulation.hpp"
#include "matchStats.hpp"
#include "referenceTactic.hpp"
#include "restart.hpp"
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
  REQUIRE(json["version"] == 3);
  REQUIRE(json["home"]["chanceQuality"] == 0.0);
  REQUIRE(json["home"]["penalties"] == 0);
  REQUIRE(json["finished"] == false);
  REQUIRE(json["score"]["home"] == 0);
  REQUIRE(json["home"]["points"].is_null());
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
  REQUIRE(keys == std::vector<std::string>{"format", "version", "ticks", "seconds", "finished",
                                           "score", "home", "away"});
  REQUIRE(text.back() == '\n');
}

TEST_CASE("A finished match writes its score and points", "[statsJson]") {
  MatchStats stats;
  stats.finished = true;
  stats.home.goals = 2;
  stats.home.points = 3;
  stats.away.goals = 1;
  stats.away.points = 0;
  const auto json = nlohmann::ordered_json::parse(toStatsJson(stats));
  REQUIRE(json["finished"] == true);
  REQUIRE(json["score"]["home"] == 2);
  REQUIRE(json["score"]["away"] == 1);
  REQUIRE(json["home"]["points"] == 3);
  REQUIRE(json["away"]["points"] == 0);
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

TEST_CASE("A penalty the simulation awards is worth its fixed chance", "[statsJson]") {
  using ElyverseFootball::SimMatch::awardFreeKick;
  using ElyverseFootball::SimMatch::GoalEnd;
  using ElyverseFootball::SimMatch::MatchState;
  using ElyverseFootball::SimMatch::MatchStateWriter;
  using ElyverseFootball::SimMatch::MatchStepContext;
  using ElyverseFootball::SimMatch::MatchSystem;
  using ElyverseFootball::SimMatch::RestartKind;
  using ElyverseFootball::SimMatch::TeamSide;
  const auto reference = ElyverseFootball::SimTactics::Tactic::create(
      ElyverseFootball::SimTactics::referenceElevenASideTacticSpec());
  REQUIRE(reference.has_value());
  auto setup =
      ElyverseFootball::SimMatch::makeTacticMatch({.home = *reference, .away = *reference}, 2);
  REQUIRE(setup.has_value());
  MatchSystem award{
      .name = "award",
      .update =
          [](const MatchStepContext& context, const MatchState& current, MatchStateWriter& next) {
            if (context.tick() == SimTick(0)) {
              (void)awardFreeKick(context, current, next,
                                  {.kind = RestartKind::kPenalty,
                                   .side = TeamSide::kHome,
                                   .spot = current.pitch().penaltySpot(GoalEnd::kMaxX)},
                                  {});
            }
          },
      .intervalTicks = 1,
      .phaseTicks = 0};
  std::vector<MatchSystem> systems{award};
  for (MatchSystem& system : ElyverseFootball::SimMatch::makeMatchSystems(setup->config)) {
    systems.push_back(std::move(system));
  }
  ElyverseFootball::SimMatch::MatchSimulation simulation(
      {.initialState = setup->initialState,
       .seed = setup->seed,
       .ticksPerSecond = setup->config.ticksPerSecond,
       .systems = std::move(systems),
       .commands = {}});
  MatchAnalyzer analyzer(contextOf(setup->initialState, setup->config.ticksPerSecond));
  // Until the penalty has been shot.
  while (analyzer.finish(simulation.tick()).home.shots == 0 && simulation.tick() < SimTick(900)) {
    REQUIRE(simulation.step().has_value());
    analyzer.observeStep(simulation.events());
  }
  const MatchStats stats = analyzer.finish(simulation.tick());
  REQUIRE(stats.home.penalties == 1);
  REQUIRE(stats.home.shots == 1);
  REQUIRE(stats.home.chanceQuality == 0.76);
}

TEST_CASE("Duels and shots add up over a longer eleven-a-side match", "[statsJson]") {
  const auto reference = ElyverseFootball::SimTactics::Tactic::create(
      ElyverseFootball::SimTactics::referenceElevenASideTacticSpec());
  REQUIRE(reference.has_value());
  const auto setup =
      ElyverseFootball::SimMatch::makeTacticMatch({.home = *reference, .away = *reference}, 3);
  REQUIRE(setup.has_value());
  ElyverseFootball::SimMatch::MatchSimulation simulation =
      ElyverseFootball::SimMatch::startMatch(*setup);
  MatchAnalyzer analyzer(contextOf(setup->initialState, setup->config.ticksPerSecond));
  while (simulation.tick() < SimTick(std::int64_t{30} * 900)) {
    REQUIRE(simulation.step().has_value());
    analyzer.observeStep(simulation.events());
  }
  const MatchStats stats = analyzer.finish(simulation.tick());
  // Not vacuous: there were duels and shots to count.
  REQUIRE(stats.home.duelsWon + stats.home.duelsLost > 0);
  REQUIRE(stats.home.shots + stats.away.shots > 0);
  REQUIRE(stats.home.duelsWon == stats.away.duelsLost);
  REQUIRE(stats.away.duelsWon == stats.home.duelsLost);
  REQUIRE(stats.home.aerialDuels == stats.away.aerialDuels);
  REQUIRE(stats.home.aerialDuelsWon + stats.away.aerialDuelsWon <= stats.home.aerialDuels);
  for (const auto* side : {&stats.home, &stats.away}) {
    REQUIRE(side->shotsOnTarget <= side->shots);
    REQUIRE(side->chanceQuality >= 0.0);
    REQUIRE(side->chanceQuality <= static_cast<double>(side->shots));
  }
  REQUIRE(stats.home.saves <= stats.away.shotsOnTarget);
  REQUIRE(stats.away.saves <= stats.home.shotsOnTarget);
}
