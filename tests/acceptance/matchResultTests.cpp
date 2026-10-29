// A whole match with a final score (docs/match-clock.md#full-time): ten-minute
// eleven-a-side tactic matches, restarts on, end at full time with a result
// that agrees with their goals, the match statistics and every kickoff after
// a goal.

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <optional>
#include <variant>

#include "matchAnalyzer.hpp"
#include "matchClock.hpp"
#include "matchEvents.hpp"
#include "matchSetup.hpp"
#include "matchSimulation.hpp"
#include "referenceTactic.hpp"
#include "restartKind.hpp"
#include "scenarios.hpp"
#include "simTime.hpp"
#include "tactic.hpp"
#include "teamFrame.hpp"

using ElyverseFootball::SimAnalytics::contextOf;
using ElyverseFootball::SimAnalytics::MatchAnalyzer;
using ElyverseFootball::SimAnalytics::MatchStats;
using ElyverseFootball::SimCore::SimTick;
using ElyverseFootball::SimMatch::findPlayerIndex;
using ElyverseFootball::SimMatch::GoalScored;
using ElyverseFootball::SimMatch::makeTacticMatch;
using ElyverseFootball::SimMatch::MatchEvent;
using ElyverseFootball::SimMatch::matchResult;
using ElyverseFootball::SimMatch::MatchSimulation;
using ElyverseFootball::SimMatch::opponentOf;
using ElyverseFootball::SimMatch::RestartKind;
using ElyverseFootball::SimMatch::RestartTaken;
using ElyverseFootball::SimMatch::Score;
using ElyverseFootball::SimMatch::startMatch;
using ElyverseFootball::SimMatch::TeamSide;
using ElyverseFootball::SimTactics::referenceElevenASideTacticSpec;
using ElyverseFootball::SimTactics::Tactic;

TEST_CASE("Match result: a whole match ends with a final score", "[acceptance][result]") {
  const auto reference = Tactic::create(referenceElevenASideTacticSpec());
  REQUIRE(reference.has_value());
  int goals = 0;
  for (const std::uint64_t seed : {std::uint64_t{1}, std::uint64_t{2}, std::uint64_t{3}}) {
    CAPTURE(seed);
    auto setup = makeTacticMatch({.home = *reference, .away = *reference}, seed);
    REQUIRE(setup.has_value());
    setup->config.restarts.enabled = true;
    setup->config.clock.durationMinutes = 10.0;
    MatchSimulation simulation = startMatch(*setup);
    MatchAnalyzer analyzer(contextOf(setup->initialState, setup->config.ticksPerSecond));
    Score counted;
    // The side that conceded the last goal, until its kickoff.
    std::optional<TeamSide> kickoffDue;
    while (!matchResult(simulation.state()) && simulation.tick() < SimTick(40000)) {
      REQUIRE(simulation.step().has_value());
      analyzer.observeStep(simulation.events());
      for (const MatchEvent& event : simulation.events()) {
        if (const auto* goal = std::get_if<GoalScored>(&event)) {
          (goal->side == TeamSide::kHome ? counted.home : counted.away) += 1;
          REQUIRE(goal->score == counted);
          REQUIRE_FALSE(kickoffDue.has_value());
          kickoffDue = opponentOf(goal->side);
        } else if (const auto* restart = std::get_if<RestartTaken>(&event)) {
          if (kickoffDue) {
            // The conceding side kicks off next, whatever else happens first.
            REQUIRE(restart->kind == RestartKind::kKickoff);
            const auto taker = findPlayerIndex(simulation.state(), restart->player);
            REQUIRE(taker.has_value());
            REQUIRE(simulation.state().players()[taker.value_or(0)].side == kickoffDue);
            kickoffDue.reset();
          }
        }
      }
    }
    const auto result = matchResult(simulation.state());
    REQUIRE(result.has_value());
    REQUIRE(result->score == counted);
    REQUIRE(result->score == simulation.state().score());
    if (counted.home == counted.away) {
      REQUIRE_FALSE(result->winner.has_value());
    } else {
      REQUIRE(result->winner == (counted.home > counted.away ? TeamSide::kHome : TeamSide::kAway));
    }
    const MatchStats stats = analyzer.finish(simulation.tick());
    REQUIRE(stats.finished);
    REQUIRE(stats.home.goals == counted.home);
    REQUIRE(stats.away.goals == counted.away);
    REQUIRE(stats.home.points == (counted.home > counted.away    ? 3
                                  : counted.home == counted.away ? 1
                                                                 : 0));
    goals += counted.home + counted.away;
  }
  // Goals are scored, or the result would say little.
  REQUIRE(goals > 0);
}
