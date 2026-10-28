// The carry scenarios (docs/on-ball.md#ball-carrying): a carrier with nobody to
// pass to carries the ball into open space with touch after touch, and takes it
// past a covering defender with a dribble.

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "ids.hpp"
#include "matchEvents.hpp"
#include "matchFixtures.hpp"
#include "matchSetup.hpp"
#include "matchSimulation.hpp"
#include "matchState.hpp"
#include "matchStateHash.hpp"
#include "scenarios.hpp"
#include "simTime.hpp"

using ElyverseFootball::SimCore::PlayerId;
using ElyverseFootball::SimCore::SimTick;
using ElyverseFootball::SimMatch::BallWon;
using ElyverseFootball::SimMatch::DecisionDiagnostic;
using ElyverseFootball::SimMatch::DecisionOutcome;
using ElyverseFootball::SimMatch::DribbleAttempted;
using ElyverseFootball::SimMatch::DribbleEnded;
using ElyverseFootball::SimMatch::DribbleOutcome;
using ElyverseFootball::SimMatch::hashMatchState;
using ElyverseFootball::SimMatch::MatchEvent;
using ElyverseFootball::SimMatch::MatchSimulation;
using ElyverseFootball::SimMatch::MatchState;
using ElyverseFootball::SimMatch::startMatch;
using ElyverseFootball::TestSupport::setupOf;

namespace {

constexpr PlayerId kCarrier{1};
constexpr PlayerId kDefender{8};

// While the ball rolls ahead of its owner after a touch, it is still his, on
// the pitch, and on the ground.
void checkTouch(const MatchState& state) {
  const auto& ball = state.ball();
  if (!ball.touch) {
    return;
  }
  REQUIRE(ball.owner.has_value());
  REQUIRE_FALSE(ball.held);
  REQUIRE(ball.height == 0.0);
  REQUIRE(state.pitch().contains(ball.position));
}

}  // namespace

TEST_CASE("Carry: in open space he carries the ball forward touch after touch",
          "[acceptance][carry]") {
  for (std::uint64_t seed = 1; seed <= 10; ++seed) {
    CAPTURE(seed);
    MatchSimulation simulation = startMatch(setupOf("carry-into-space", seed));
    simulation.setCollectDiagnostics(true);
    std::optional<DecisionOutcome> first;
    int touches = 0;
    while (simulation.tick() < SimTick(120)) {
      REQUIRE(simulation.step().has_value());
      const MatchState& state = simulation.state();
      checkTouch(state);
      if (state.ball().touch && !simulation.previousState().ball().touch) {
        ++touches;
      }
      for (const DecisionDiagnostic& decision : simulation.diagnostics()) {
        if (decision.player == kCarrier && !first) {
          first = decision.outcome;
        }
      }
      for (const MatchEvent& event : simulation.events()) {
        REQUIRE_FALSE(std::holds_alternative<BallWon>(event));
      }
    }
    // He carries first, keeps the ball all along, and has taken it at least
    // ten meters up the pitch in four seconds, with a touch at a time.
    REQUIRE(first == DecisionOutcome::kCarried);
    REQUIRE(touches >= 3);
    REQUIRE(simulation.state().ball().owner == kCarrier);
    REQUIRE(simulation.state().ball().position.x >= 30.0);
  }
}

TEST_CASE("Carry: he dribbles past a covering defender", "[acceptance][carry]") {
  constexpr std::uint64_t kSeeds = 20;
  int attempted = 0;
  int beaten = 0;
  for (std::uint64_t seed = 1; seed <= kSeeds; ++seed) {
    CAPTURE(seed);
    MatchSimulation simulation = startMatch(setupOf("dribble-past-defender", seed));
    std::optional<DribbleAttempted> attempt;
    std::optional<DribbleEnded> ended;
    while (!ended && simulation.tick() < SimTick(300)) {
      REQUIRE(simulation.step().has_value());
      checkTouch(simulation.state());
      for (const MatchEvent& event : simulation.events()) {
        // His dribbles only: a shot instead and the restart after it can give
        // away the ball, and their dribbles are not this scenario's.
        const auto* started = std::get_if<DribbleAttempted>(&event);
        if (started != nullptr && started->carrier == kCarrier) {
          REQUIRE_FALSE(attempt.has_value());
          attempt = *started;
        }
        const auto* over = std::get_if<DribbleEnded>(&event);
        if (over != nullptr && over->carrier == kCarrier) {
          // Every dribble that ends was attempted first, by the same players.
          REQUIRE(attempt.has_value());
          REQUIRE(over->carrier == attempt.value_or(DribbleAttempted{}).carrier);
          REQUIRE(over->opponent == attempt.value_or(DribbleAttempted{}).opponent);
          ended = *over;
        }
      }
    }
    if (!attempt) {
      continue;
    }
    ++attempted;
    REQUIRE(attempt->opponent == kDefender);
    // The touch goes past him, toward the goal.
    REQUIRE(attempt->target.x > attempt->from.x);
    REQUIRE(ended.has_value());
    if (ended && ended->outcome == DribbleOutcome::kBeaten) {
      ++beaten;
      // He has the ball beyond the defender.
      const MatchState& state = simulation.state();
      REQUIRE(state.ball().owner == kCarrier);
      REQUIRE(state.ball().position.x > state.players()[7].position.x);
    }
  }
  CAPTURE(attempted, beaten);
  // At the time of writing 11 attempts out of twenty, ten beating him.
  REQUIRE(attempted >= 10);
  REQUIRE(beaten * 2 >= attempted);
}

TEST_CASE("Carry: the scenarios run alike every time", "[acceptance][carry]") {
  for (const std::string_view name : {"carry-into-space", "dribble-past-defender"}) {
    CAPTURE(name);
    const auto run = [name] {
      MatchSimulation simulation = startMatch(setupOf(name, 7));
      while (simulation.tick() < SimTick(300)) {
        REQUIRE(simulation.step().has_value());
      }
      return hashMatchState(simulation.state());
    };
    REQUIRE(run() == run());
  }
}
