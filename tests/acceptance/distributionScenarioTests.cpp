// The goalkeeper distribution scenarios (docs/goalkeeper-distribution.md):
// home's keeper, player 1, throws the ball out of his hands to a free centre
// back, and goes long from his feet when a high press closes his short
// options; away's keeper, player 8, takes a goal kick from his goal area.
// With nothing on, home's keeper clears the ball before he would concede a
// corner for holding it too long.

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "ids.hpp"
#include "matchEvents.hpp"
#include "matchSetup.hpp"
#include "matchSimulation.hpp"
#include "matchState.hpp"
#include "passCandidate.hpp"
#include "pitch.hpp"
#include "playerMovement.hpp"
#include "pursuit.hpp"
#include "reception.hpp"
#include "restart.hpp"
#include "restartKind.hpp"
#include "scenarios.hpp"
#include "simTime.hpp"
#include "tacticalMovement.hpp"
#include "vec2.hpp"

using ElyverseFootball::SimCore::PlayerId;
using ElyverseFootball::SimCore::SimTick;
using ElyverseFootball::SimCore::Vec2;
using ElyverseFootball::SimMatch::AerialContest;
using ElyverseFootball::SimMatch::BallState;
using ElyverseFootball::SimMatch::BallTouch;
using ElyverseFootball::SimMatch::DecisionDiagnostic;
using ElyverseFootball::SimMatch::DecisionOutcome;
using ElyverseFootball::SimMatch::findScenario;
using ElyverseFootball::SimMatch::GoalEnd;
using ElyverseFootball::SimMatch::goalKickSpot;
using ElyverseFootball::SimMatch::hasReleasedBall;
using ElyverseFootball::SimMatch::LooseBallRecovered;
using ElyverseFootball::SimMatch::MatchEvent;
using ElyverseFootball::SimMatch::MatchSetup;
using ElyverseFootball::SimMatch::MatchSimulation;
using ElyverseFootball::SimMatch::MatchState;
using ElyverseFootball::SimMatch::MatchSystem;
using ElyverseFootball::SimMatch::PassAttempted;
using ElyverseFootball::SimMatch::PassCandidate;
using ElyverseFootball::SimMatch::PassIntercepted;
using ElyverseFootball::SimMatch::PassReceived;
using ElyverseFootball::SimMatch::PassRejection;
using ElyverseFootball::SimMatch::PlayerMatchState;
using ElyverseFootball::SimMatch::RestartKind;
using ElyverseFootball::SimMatch::RestartTaken;
using ElyverseFootball::SimMatch::startMatch;

namespace {

constexpr std::uint64_t kSeeds = 10;
constexpr PlayerId kHomeKeeper{1};
constexpr PlayerId kAwayKeeper{8};
constexpr std::size_t kAwayKeeperIndex = 7;

[[nodiscard]] MatchSetup setupOf(const std::string_view scenario, const std::uint64_t seed) {
  const auto* definition = findScenario(scenario);
  REQUIRE(definition != nullptr);
  auto setup = definition->make(seed);
  REQUIRE(setup.has_value());
  return *std::move(setup);
}

[[nodiscard]] MatchSimulation start(const MatchSetup& setup) {
  MatchSimulation simulation = startMatch(setup);
  simulation.setCollectDiagnostics(true);
  return simulation;
}

[[nodiscard]] MatchSimulation start(const std::string_view scenario, const std::uint64_t seed) {
  return start(setupOf(scenario, seed));
}

// keeper-long-kick with restarts on, the ball in the keeper's hands and every
// pass too unlikely to be on under the press -- his holding midfielder marked
// too, and nobody moving to get free (frozen()): he took the ball at tick 0,
// and the clock of the eight seconds runs from there.
[[nodiscard]] MatchSetup nothingOn(const std::uint64_t seed) {
  MatchSetup setup = setupOf("keeper-long-kick", seed);
  setup.config.restarts.enabled = true;
  setup.config.decisions.scoring.minCompletion = 1.0;
  const MatchState& state = setup.initialState;
  std::vector<PlayerMatchState> players(state.players().begin(), state.players().end());
  players[3].position = {.x = 12.0, .y = 21.0};
  BallState ball = state.ball();
  ball.held = true;
  ball.lastTouch = BallTouch{.playerId = kHomeKeeper, .tick = SimTick(0)};
  auto touched = MatchState::create({.pitch = state.pitch(),
                                     .players = std::move(players),
                                     .ball = ball,
                                     .playersPerSide = state.playersPerSide()},
                                    state.tactics());
  REQUIRE(touched.has_value());
  setup.initialState = *std::move(touched);
  return setup;
}

// The match of a setup without the systems that move players: nobody gets
// free, and nobody comes for the ball.
[[nodiscard]] MatchSimulation frozen(const MatchSetup& setup) {
  std::vector<MatchSystem> systems = ElyverseFootball::SimMatch::makeMatchSystems(setup.config);
  std::erase_if(systems, [](const MatchSystem& system) {
    return system.name == ElyverseFootball::SimMatch::kTacticalMovementSystemName ||
           system.name == ElyverseFootball::SimMatch::kPursuitSystemName ||
           system.name == ElyverseFootball::SimMatch::kPlayerMovementSystemName;
  });
  MatchSimulation simulation({.initialState = setup.initialState,
                              .seed = setup.seed,
                              .ticksPerSecond = setup.config.ticksPerSecond,
                              .systems = std::move(systems),
                              .commands = {}});
  simulation.setCollectDiagnostics(true);
  return simulation;
}

// The first restart up to `until`.
[[nodiscard]] std::optional<RestartTaken> firstRestart(MatchSimulation& simulation,
                                                       const SimTick until) {
  while (simulation.tick() < until) {
    REQUIRE(simulation.step().has_value());
    for (const MatchEvent& event : simulation.events()) {
      if (const auto* taken = std::get_if<RestartTaken>(&event)) {
        return *taken;
      }
    }
  }
  return std::nullopt;
}

// A keeper's first distribution: the decision that chose it, the pass it
// became, whether he had released the ball from his hands then, how high the
// ball got, and what became of it -- who received, intercepted or recovered
// it, or went up for it.
struct Distribution {
  std::optional<DecisionDiagnostic> decision;
  std::optional<PassAttempted> pass;
  bool released = false;
  double peak = 0.0;
  std::optional<MatchEvent> settled;

  // The option he chose.
  [[nodiscard]] PassCandidate chosen() const {
    REQUIRE(decision.has_value());
    const DecisionDiagnostic made = decision.value_or(DecisionDiagnostic{});
    REQUIRE(made.chosen.has_value());
    return made.candidates.at(made.chosen.value_or(0));
  }
};

// A contest nobody won leaves the ball flying.
[[nodiscard]] bool settles(const MatchEvent& event) {
  if (const auto* contest = std::get_if<AerialContest>(&event)) {
    return contest->winner.has_value();
  }
  return std::holds_alternative<PassReceived>(event) ||
         std::holds_alternative<PassIntercepted>(event) ||
         std::holds_alternative<LooseBallRecovered>(event);
}

// Steps until the keeper's first pass has been settled, or `until`.
[[nodiscard]] Distribution distribute(MatchSimulation& simulation, const PlayerId keeper,
                                      const std::size_t keeperIndex, const SimTick until) {
  Distribution run;
  while (simulation.tick() < until && !run.settled) {
    REQUIRE(simulation.step().has_value());
    if (!run.pass) {
      for (const DecisionDiagnostic& decision : simulation.diagnostics()) {
        if (decision.player == keeper && decision.outcome == DecisionOutcome::kPassed) {
          run.decision = decision;
        }
      }
    }
    for (const MatchEvent& event : simulation.events()) {
      const auto* attempted = std::get_if<PassAttempted>(&event);
      if (!run.pass && attempted != nullptr && attempted->passer == keeper) {
        run.pass = *attempted;
        run.released = hasReleasedBall(simulation.state(), keeperIndex);
      } else if (run.pass && !run.settled && settles(event)) {
        run.settled = event;
      }
    }
    if (run.pass) {
      run.peak = std::max(run.peak, simulation.state().ball().height);
    }
  }
  return run;
}

}  // namespace

TEST_CASE("Distribution: the keeper throws the ball out to a free centre back",
          "[acceptance][distribution]") {
  for (std::uint64_t seed = 1; seed <= kSeeds; ++seed) {
    CAPTURE(seed);
    MatchSimulation simulation = start("keeper-build-up", seed);
    REQUIRE(simulation.state().ball().held);
    const Distribution run = distribute(simulation, kHomeKeeper, 0, SimTick(150));
    REQUIRE(run.decision.has_value());
    REQUIRE(run.decision.value_or(DecisionDiagnostic{}).held);
    REQUIRE_FALSE(run.chosen().lofted);
    REQUIRE(run.pass.has_value());
    const auto receiver = run.pass.value_or(PassAttempted{}).intendedReceiver;
    REQUIRE((receiver == PlayerId(2) || receiver == PlayerId(3)));
    // Out of his hands, along the ground, and his hands are barred until
    // someone else has touched it.
    REQUIRE(run.released);
    REQUIRE(run.peak == 0.0);
    REQUIRE(run.settled.has_value());
    const MatchEvent settled = run.settled.value_or(MatchEvent{});
    const auto* received = std::get_if<PassReceived>(&settled);
    REQUIRE(received != nullptr);
    REQUIRE(received->receiver == receiver);
    REQUIRE_FALSE(simulation.state().tactical(0).handsReleased.has_value());
  }
}

TEST_CASE("Distribution: a keeper holding the ball facing his own goal turns and throws it out",
          "[acceptance][distribution]") {
  for (std::uint64_t seed = 1; seed <= kSeeds; ++seed) {
    CAPTURE(seed);
    MatchSimulation simulation = start("keeper-turned", seed);
    REQUIRE(simulation.state().ball().held);
    REQUIRE(simulation.state().players()[0].facing.x < 0.0);
    const Distribution run = distribute(simulation, kHomeKeeper, 0, SimTick(150));
    REQUIRE(run.pass.has_value());
    const auto receiver = run.pass.value_or(PassAttempted{}).intendedReceiver;
    REQUIRE((receiver == PlayerId(2) || receiver == PlayerId(3)));
  }
}

TEST_CASE("Distribution: under a high press the keeper goes long", "[acceptance][distribution]") {
  for (std::uint64_t seed = 1; seed <= kSeeds; ++seed) {
    CAPTURE(seed);
    MatchSimulation simulation = start("keeper-long-kick", seed);
    REQUIRE_FALSE(simulation.state().ball().held);
    const Distribution run = distribute(simulation, kHomeKeeper, 0, SimTick(240));
    REQUIRE(run.decision.has_value());
    REQUIRE_FALSE(run.decision.value_or(DecisionDiagnostic{}).held);
    REQUIRE(run.chosen().lofted);
    REQUIRE(run.pass.has_value());
    const auto receiver = run.pass.value_or(PassAttempted{}).intendedReceiver;
    REQUIRE((receiver == PlayerId(5) || receiver == PlayerId(6) || receiver == PlayerId(7)));
    // Through the air, and somebody gets to it where it comes down.
    REQUIRE(run.peak > 5.0);
    REQUIRE(run.settled.has_value());
  }
}

TEST_CASE("Distribution: the keeper takes a goal kick from his goal area",
          "[acceptance][distribution]") {
  for (std::uint64_t seed = 1; seed <= kSeeds; ++seed) {
    CAPTURE(seed);
    MatchSimulation simulation = start("goal-kick", seed);
    const std::optional<RestartTaken> restart = firstRestart(simulation, SimTick(150));
    REQUIRE(restart.has_value());
    const RestartTaken goalKick = restart.value_or(RestartTaken{});
    REQUIRE(goalKick.kind == RestartKind::kGoalKick);
    REQUIRE(goalKick.player == kAwayKeeper);

    // The ball lies on the front edge of his goal area, at his feet.
    const auto& state = simulation.state();
    const Vec2 spot = goalKickSpot(state.pitch(), goalKick.position);
    REQUIRE(state.pitch().goalArea(GoalEnd::kMaxX).contains(spot));
    REQUIRE(state.ball().position == spot);
    REQUIRE(state.ball().owner == kAwayKeeper);
    REQUIRE_FALSE(state.ball().held);
    REQUIRE(state.players()[kAwayKeeperIndex].position.x > spot.x);

    // And he plays it from there.
    const Distribution run = distribute(simulation, kAwayKeeper, kAwayKeeperIndex,
                                        SimTick(simulation.tick().value() + 150));
    REQUIRE(run.pass.has_value());
    REQUIRE(ElyverseFootball::SimCore::distance(run.pass.value_or(PassAttempted{}).from, spot) <
            0.5);
  }
}

TEST_CASE("Distribution: with nothing on, the keeper clears the ball within eight seconds",
          "[acceptance][distribution]") {
  for (std::uint64_t seed = 1; seed <= kSeeds; ++seed) {
    CAPTURE(seed);
    MatchSimulation simulation = frozen(nothingOn(seed));
    // Six seconds at 30 ticks a second, and the step that plays the pass.
    const Distribution run = distribute(simulation, kHomeKeeper, 0, SimTick(240));
    REQUIRE(run.pass.has_value());
    REQUIRE(run.pass.value_or(PassAttempted{}).tick >= SimTick(181));
    REQUIRE(run.chosen().rejection == PassRejection::kUnlikely);
    REQUIRE(run.released);
  }
}

TEST_CASE("Distribution: a keeper holding the ball longer than eight seconds concedes a corner",
          "[acceptance][distribution]") {
  for (std::uint64_t seed = 1; seed <= kSeeds; ++seed) {
    CAPTURE(seed);
    MatchSetup setup = nothingOn(seed);
    setup.config.decisions.distribution.clearSeconds = 1000.0;
    MatchSimulation simulation = frozen(setup);
    const std::optional<RestartTaken> restart = firstRestart(simulation, SimTick(300));
    REQUIRE(restart.has_value());
    const RestartTaken corner = restart.value_or(RestartTaken{});
    REQUIRE(corner.kind == RestartKind::kCorner);
    // Eight seconds at 30 ticks a second, counted from tick 0.
    REQUIRE(corner.tick == SimTick(241));
    // At the corner of his goal line nearer the ball, by away.
    REQUIRE(corner.position == Vec2{.x = 0.0, .y = 40.0});
    const auto& state = simulation.state();
    REQUIRE(state.ball().owner == corner.player);
    REQUIRE_FALSE(state.ball().held);
    const auto taker = ElyverseFootball::SimMatch::findPlayerIndex(state, corner.player);
    REQUIRE(taker.has_value());
    REQUIRE(state.players()[taker.value_or(0)].side == ElyverseFootball::SimMatch::TeamSide::kAway);
    REQUIRE(state.players()[taker.value_or(0)].position == corner.position);
  }
}

TEST_CASE("Distribution: a clearance chosen at the holding limit is played without a corner",
          "[acceptance][distribution]") {
  for (std::uint64_t seed = 1; seed <= kSeeds; ++seed) {
    CAPTURE(seed);
    MatchSetup setup = nothingOn(seed);
    // He clears it at tick 240, eight seconds in, and the ball system plays
    // it in the step after, when he has held it beyond the limit.
    setup.config.decisions.distribution.clearSeconds = 7.99;
    MatchSimulation simulation = frozen(setup);
    const Distribution run = distribute(simulation, kHomeKeeper, 0, SimTick(242));
    REQUIRE(run.pass.has_value());
    REQUIRE(run.pass.value_or(PassAttempted{}).tick == SimTick(241));
    REQUIRE_FALSE(std::ranges::any_of(simulation.events(), [](const MatchEvent& event) {
      return std::holds_alternative<RestartTaken>(event);
    }));
  }
}
