#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

#include "ballMovement.hpp"
#include "kickoffScenario.hpp"
#include "matchCommand.hpp"
#include "matchSetup.hpp"
#include "matchSimulation.hpp"
#include "matchState.hpp"
#include "vec2.hpp"

using Catch::Matchers::WithinAbs;
using ElyverseFootball::SimCore::PlayerId;
using ElyverseFootball::SimCore::SimTick;
using ElyverseFootball::SimCore::Vec2;
using ElyverseFootball::SimMatch::BallPhysics;
using ElyverseFootball::SimMatch::carriedBallPosition;
using ElyverseFootball::SimMatch::GiveBallCommand;
using ElyverseFootball::SimMatch::kDefaultCarryDistance;
using ElyverseFootball::SimMatch::makeBallMovementSystem;
using ElyverseFootball::SimMatch::makeSevenASideKickoff;
using ElyverseFootball::SimMatch::MatchCommandErrorCode;
using ElyverseFootball::SimMatch::MatchConfig;
using ElyverseFootball::SimMatch::MatchSetup;

using ElyverseFootball::SimMatch::MatchSimulation;
using ElyverseFootball::SimMatch::MatchState;
using ElyverseFootball::SimMatch::MatchStateErrorCode;
using ElyverseFootball::SimMatch::MatchStateSpec;
using ElyverseFootball::SimMatch::MatchStateWriter;
using ElyverseFootball::SimMatch::MatchStepContext;
using ElyverseFootball::SimMatch::MovePlayerCommand;
using ElyverseFootball::SimMatch::Pitch;
using ElyverseFootball::SimMatch::PlayerMatchState;
using ElyverseFootball::SimMatch::ScheduledCommand;
using ElyverseFootball::SimMatch::startMatch;

namespace {

[[nodiscard]] MatchState kickoff(const Vec2 ballVelocity = {}) {
  auto state = makeSevenASideKickoff(Pitch(60.0, 40.0), ballVelocity);
  REQUIRE(state.has_value());
  return *std::move(state);
}

// A match whose carriers never pass, so possession changes only through the
// commands of a test.
[[nodiscard]] MatchSimulation matchOf(std::vector<ScheduledCommand> commands,
                                      MatchState state = kickoff()) {
  MatchConfig config;
  config.decisions.minHoldSeconds = 1.0e6;
  return startMatch(MatchSetup{.initialState = std::move(state),
                               .config = config,
                               .seed = 1,
                               .commands = std::move(commands)});
}

[[nodiscard]] ScheduledCommand give(const std::int64_t tick, const std::uint32_t playerId) {
  return {.tick = SimTick(tick), .command = GiveBallCommand{.playerId = PlayerId(playerId)}};
}

[[nodiscard]] ScheduledCommand move(const std::int64_t tick, const std::uint32_t playerId,
                                    const Vec2 target) {
  return {.tick = SimTick(tick),
          .command = MovePlayerCommand{.playerId = PlayerId(playerId), .target = target}};
}

// Player ids 1..14 sit at index id - 1 in the kickoff fixture.
[[nodiscard]] const PlayerMatchState& playerOf(const MatchSimulation& simulation,
                                               const std::uint32_t playerId) {
  return simulation.state().players()[playerId - 1];
}

// The documented rule: a controlled ball sits carryDistance ahead of its
// owner along his facing and moves with his velocity.
void requireBallWith(const MatchSimulation& simulation, const std::uint32_t playerId) {
  const auto& ball = simulation.state().ball();
  const PlayerMatchState& owner = playerOf(simulation, playerId);
  REQUIRE(ball.owner == PlayerId(playerId));
  REQUIRE(ball.position == carriedBallPosition(owner, BallPhysics{}, simulation.state().pitch()));
  REQUIRE(ball.velocity == owner.velocity);
}

void stepTimes(MatchSimulation& simulation, const int steps) {
  for (int step = 0; step < steps; ++step) {
    REQUIRE(simulation.step().has_value());
  }
}

}  // namespace

TEST_CASE("A free ball has no owner", "[possession]") {
  const MatchState state = kickoff();

  REQUIRE_FALSE(state.ball().owner.has_value());
  REQUIRE_FALSE(state.ball().isControlled());
}

TEST_CASE("A state may start with the ball at a player's feet", "[possession]") {
  const MatchState free = kickoff();
  MatchStateSpec spec{.pitch = free.pitch(),
                      .players = {free.players().begin(), free.players().end()},
                      .ball = free.ball(),
                      .playersPerSide = free.playersPerSide()};
  spec.ball.owner = PlayerId(7);
  const auto state = MatchState::create(spec);
  REQUIRE(state.has_value());

  MatchSimulation simulation = matchOf({}, *state);
  stepTimes(simulation, 1);

  requireBallWith(simulation, 7);
}

TEST_CASE("The ball cannot belong to a player who is not in the state", "[possession]") {
  const MatchState free = kickoff();
  MatchStateSpec spec{.pitch = free.pitch(),
                      .players = {free.players().begin(), free.players().end()},
                      .ball = free.ball(),
                      .playersPerSide = free.playersPerSide()};
  spec.ball.owner = PlayerId(42);

  const auto state = MatchState::create(spec);

  REQUIRE_FALSE(state.has_value());
  REQUIRE(state.error().front().code == MatchStateErrorCode::kUnknownBallOwner);
  REQUIRE(state.error().front().message ==
          "the ball belongs to player 42, who is not in the state");
}

TEST_CASE("A controlled ball follows its carrier", "[possession]") {
  MatchSimulation simulation = matchOf(
      {give(0, 7), move(0, 7, {.x = 50.0, .y = 10.0}), move(40, 7, {.x = 20.0, .y = 35.0})});

  for (int tick = 0; tick < 240; ++tick) {
    REQUIRE(simulation.step().has_value());
    CAPTURE(tick);
    requireBallWith(simulation, 7);
  }
  // He arrived and stopped, and so did the ball.
  REQUIRE(playerOf(simulation, 7).position == Vec2{.x = 20.0, .y = 35.0});
  REQUIRE(simulation.state().ball().velocity == Vec2{});
  const double carried = std::sqrt(
      (simulation.state().ball().position - playerOf(simulation, 7).position).lengthSquared());
  REQUIRE_THAT(carried, WithinAbs(kDefaultCarryDistance, 1e-12));
}

TEST_CASE("Taking a rolling ball leaves no stale velocity", "[possession]") {
  // Straight up from the center spot, between the two forwards, so nobody
  // reaches it on its own.
  MatchSimulation simulation = matchOf({give(10, 4)}, kickoff({.x = 0.0, .y = 9.0}));
  stepTimes(simulation, 10);
  REQUIRE(simulation.state().ball().velocity.lengthSquared() > 0.0);

  stepTimes(simulation, 1);

  // Player 4 stands still: the ball is at his feet and at rest.
  requireBallWith(simulation, 4);
  REQUIRE(simulation.state().ball().velocity == Vec2{});
}

TEST_CASE("Possession passes from one player to another without conflict", "[possession]") {
  MatchSimulation simulation =
      matchOf({give(0, 3), move(0, 3, {.x = 30.0, .y = 10.0}), give(30, 12)});
  stepTimes(simulation, 30);
  requireBallWith(simulation, 3);

  stepTimes(simulation, 1);

  requireBallWith(simulation, 12);
}

TEST_CASE("Giving the ball to an unknown player is rejected", "[possession]") {
  MatchSimulation simulation = matchOf({});

  const auto result = simulation.schedule(give(0, 99));

  REQUIRE_FALSE(result.has_value());
  REQUIRE(result.error().code == MatchCommandErrorCode::kUnknownPlayer);
}

TEST_CASE("A system cannot hand the ball to an unknown player", "[possession]") {
  MatchSimulation simulation(
      {.initialState = kickoff(),
       .seed = 1,
       .ticksPerSecond = 30,
       .systems = {{.name = "bad owner",
                    .update = [](const MatchStepContext&, const MatchState&,
                                 MatchStateWriter& next) { next.setBallOwner(PlayerId(99)); }}},
       .commands = {}});

  REQUIRE_THROWS_AS((void)simulation.step(), std::invalid_argument);
}

TEST_CASE("The carry distance must be finite and not negative", "[possession]") {
  BallPhysics physics;
  physics.carryDistance = -0.1;
  REQUIRE_THROWS_AS(makeBallMovementSystem(physics), std::invalid_argument);
  physics.carryDistance = 0.0;
  REQUIRE_NOTHROW(makeBallMovementSystem(physics));
}
