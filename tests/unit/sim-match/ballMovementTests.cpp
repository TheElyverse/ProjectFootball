#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>
#include <variant>
#include <vector>

#include "ballMovement.hpp"
#include "ids.hpp"
#include "kickoffScenario.hpp"
#include "matchCommand.hpp"
#include "matchEvents.hpp"
#include "matchSimulation.hpp"
#include "matchState.hpp"
#include "playerMovement.hpp"
#include "reception.hpp"
#include "vec2.hpp"

using Catch::Matchers::WithinAbs;
using ElyverseFootball::SimCore::PlayerId;
using ElyverseFootball::SimCore::SimTick;
using ElyverseFootball::SimCore::Vec2;
using ElyverseFootball::SimMatch::BallPhysics;
using ElyverseFootball::SimMatch::BallState;
using ElyverseFootball::SimMatch::BallTouch;
using ElyverseFootball::SimMatch::Contact;
using ElyverseFootball::SimMatch::findContact;
using ElyverseFootball::SimMatch::kDefaultRollingDeceleration;
using ElyverseFootball::SimMatch::kDefaultTicksPerSecond;
using ElyverseFootball::SimMatch::makeBallMovementSystem;
using ElyverseFootball::SimMatch::makePlayerMovementSystem;
using ElyverseFootball::SimMatch::makeSevenASideKickoff;
using ElyverseFootball::SimMatch::MatchSimulation;
using ElyverseFootball::SimMatch::MatchState;
using ElyverseFootball::SimMatch::MatchSystem;
using ElyverseFootball::SimMatch::MovePlayerCommand;
using ElyverseFootball::SimMatch::PassIntercepted;
using ElyverseFootball::SimMatch::Pitch;
using ElyverseFootball::SimMatch::PlayerMatchState;
using ElyverseFootball::SimMatch::ReceptionConfig;
using ElyverseFootball::SimMatch::rollingDistance;
using ElyverseFootball::SimMatch::ScheduledCommand;
using ElyverseFootball::SimMatch::stepFreeBall;
using ElyverseFootball::SimMatch::TeamSide;

namespace {

[[nodiscard]] BallState freeBall(const Vec2 position, const Vec2 velocity) {
  return {
      .position = position, .velocity = velocity, .owner = std::nullopt, .lastTouch = std::nullopt};
}

constexpr double kSecondsPerTick = 1.0 / kDefaultTicksPerSecond;
const Pitch kPitch(60.0, 40.0);

// A contact that cannot happen, to read a checked optional contact through.
constexpr Contact kNoContact{.contactFraction = -1.0, .closestDistance = -1.0};

// Rolls the ball tick by tick and records every state, including the first.
[[nodiscard]] std::vector<BallState> roll(BallState ball, const int ticks,
                                          const BallPhysics physics = {}) {
  std::vector<BallState> states{ball};
  for (int tick = 0; tick < ticks; ++tick) {
    ball = stepFreeBall(ball, physics, kPitch, kSecondsPerTick);
    states.push_back(ball);
  }
  return states;
}

[[nodiscard]] MatchSimulation simulationOf(const Vec2 ballVelocity,
                                           std::vector<MatchSystem> systems,
                                           std::vector<ScheduledCommand> commands = {}) {
  auto state = makeSevenASideKickoff(kPitch, ballVelocity);
  REQUIRE(state.has_value());
  return MatchSimulation({.initialState = *std::move(state),
                          .seed = 3,
                          .ticksPerSecond = kDefaultTicksPerSecond,
                          .systems = std::move(systems),
                          .commands = std::move(commands)});
}

}  // namespace

TEST_CASE("A ball at rest stays at rest", "[ballMovement]") {
  const BallState ball = freeBall({.x = 30.0, .y = 20.0}, {});

  for (const BallState& state : roll(ball, 60)) {
    REQUIRE(state == ball);
  }
}

TEST_CASE("Friction slows a rolling ball without reversing it", "[ballMovement]") {
  const Vec2 initialVelocity{.x = 6.0, .y = -2.0};
  const auto states = roll(freeBall({.x = 20.0, .y = 25.0}, initialVelocity), 200);

  constexpr double kSpeedLossPerTick = kDefaultRollingDeceleration * kSecondsPerTick;
  for (std::size_t tick = 1; tick < states.size(); ++tick) {
    CAPTURE(tick);
    const double before = std::sqrt(states[tick - 1].velocity.lengthSquared());
    const double after = std::sqrt(states[tick].velocity.lengthSquared());
    REQUIRE(after <= before);
    REQUIRE_THAT(before - after, WithinAbs(std::min(before, kSpeedLossPerTick), 1e-12));
    // Never against its initial direction.
    REQUIRE(states[tick].velocity.dot(initialVelocity) >= 0.0);
    REQUIRE((states[tick].position - states[tick - 1].position).dot(initialVelocity) >= 0.0);
  }
  REQUIRE(states.back().velocity == Vec2{});
}

TEST_CASE("A rolling ball stops after its rolling distance at any tick rate", "[ballMovement]") {
  const double speed = GENERATE(0.01, 3.0, 8.0, 12.5);
  const double ticksPerSecond = GENERATE(30.0, 60.0, 7.0);
  CAPTURE(speed, ticksPerSecond);
  BallState ball = freeBall({.x = 5.0, .y = 20.0}, {.x = speed, .y = 0.0});

  for (int tick = 0; tick < 10000 && ball.velocity != Vec2{}; ++tick) {
    ball = stepFreeBall(ball, {}, kPitch, 1.0 / ticksPerSecond);
  }

  REQUIRE(ball.velocity == Vec2{});
  REQUIRE_THAT(ball.position.x - 5.0, WithinAbs(rollingDistance(speed, {}), 1e-9));
  REQUIRE(ball.position.y == 20.0);
}

TEST_CASE("A stronger rolling resistance stops the ball sooner", "[ballMovement]") {
  const BallState ball = freeBall({.x = 5.0, .y = 20.0}, {.x = 8.0, .y = 0.0});

  const BallState normal = roll(ball, 300).back();
  const BallState heavy = roll(ball, 300, {.rollingDeceleration = 4.0}).back();

  REQUIRE_THAT(heavy.position.x - 5.0, WithinAbs(8.0, 1e-9));
  REQUIRE(heavy.position.x < normal.position.x);
}

TEST_CASE("A ball leaving the pitch stops on the line where it crossed it", "[ballMovement]") {
  SECTION("over the touchline") {
    const auto states = roll(freeBall({.x = 30.0, .y = 38.0}, {.x = 3.0, .y = 6.0}), 60);
    // Crosses y = 40 after 2 m sideways, 1 m along the length.
    REQUIRE_THAT(states.back().position.x, WithinAbs(31.0, 1e-9));
    REQUIRE(states.back().position.y == 40.0);
    REQUIRE(states.back().velocity == Vec2{});
  }
  SECTION("over the goal line") {
    const auto states = roll(freeBall({.x = 2.0, .y = 20.0}, {.x = -9.0, .y = 0.0}), 60);
    REQUIRE(states.back().position == Vec2{.x = 0.0, .y = 20.0});
    REQUIRE(states.back().velocity == Vec2{});
  }
  SECTION("through a corner") {
    const auto states = roll(freeBall({.x = 58.0, .y = 38.0}, {.x = 8.0, .y = 8.0}), 60);
    REQUIRE(states.back().position == Vec2{.x = 60.0, .y = 40.0});
  }
  SECTION("from the line itself") {
    const BallState onLine = freeBall({.x = 60.0, .y = 10.0}, {.x = 5.0, .y = 0.0});
    REQUIRE(stepFreeBall(onLine, {}, kPitch, kSecondsPerTick) == freeBall(onLine.position, {}));
  }
  for (const BallState& state :
       roll(freeBall({.x = 30.0, .y = 20.0}, {.x = 25.0, .y = -18.0}), 120)) {
    REQUIRE(kPitch.contains(state.position));
  }
}

TEST_CASE("A ball rolling along the line stays in play", "[ballMovement]") {
  const auto states = roll(freeBall({.x = 10.0, .y = 0.0}, {.x = 5.0, .y = 0.0}), 120);

  REQUIRE_THAT(states.back().position.x, WithinAbs(10.0 + rollingDistance(5.0, {}), 1e-9));
  REQUIRE(states.back().position.y == 0.0);
}

TEST_CASE("An interception's position interpolates the contact, not the tick's start",
          "[ballMovement]") {
  // Away's player 8 stands where a fast free ball, moving in a straight
  // line this tick, comes within reception's control radius partway
  // through it, not at the tick's start or end.
  const Vec2 ballStart{.x = 0.0, .y = 20.0};
  const BallState startBall{.position = ballStart,
                            .velocity = {.x = 90.0, .y = 0.0},
                            .owner = std::nullopt,
                            .lastTouch = BallTouch{.playerId = PlayerId(1), .tick = SimTick(0)}};
  const BallState rolled = stepFreeBall(startBall, {}, kPitch, kSecondsPerTick);
  const Vec2 interceptorAt{.x = 2.0, .y = 20.0};
  const auto contact = findContact(interceptorAt, interceptorAt, ballStart, rolled.position,
                                   ReceptionConfig{}.controlRadius);
  REQUIRE(contact.has_value());
  const double fraction = contact.value_or(kNoContact).contactFraction;
  REQUIRE(fraction > 0.05);
  REQUIRE(fraction < 0.95);
  const Vec2 expected = ballStart + ((rolled.position - ballStart) * fraction);

  const auto player = [](const PlayerId::ValueType number, const TeamSide side,
                         const Vec2 position) {
    return PlayerMatchState{.playerId = PlayerId(number),
                            .side = side,
                            .position = position,
                            .velocity = {},
                            .attributes = {},
                            .target = std::nullopt,
                            .facing = {.x = 1.0, .y = 0.0}};
  };
  auto state = MatchState::create({.pitch = kPitch,
                                   .players = {player(1, TeamSide::kHome, {.x = -5.0, .y = 20.0}),
                                               player(8, TeamSide::kAway, interceptorAt)},
                                   .ball = startBall,
                                   .playersPerSide = 1});
  REQUIRE(state.has_value());
  MatchSimulation simulation({.initialState = *std::move(state),
                              .seed = 1,
                              .ticksPerSecond = kDefaultTicksPerSecond,
                              .systems = {makeBallMovementSystem({})},
                              .commands = {}});
  REQUIRE(simulation.step().has_value());
  const auto events = simulation.events();
  const auto found = std::ranges::find_if(
      events, [](const auto& event) { return std::holds_alternative<PassIntercepted>(event); });
  REQUIRE(found != events.end());
  const auto& intercepted = std::get<PassIntercepted>(*found);
  REQUIRE_THAT(intercepted.position.x, WithinAbs(expected.x, 1e-9));
  REQUIRE_THAT(intercepted.position.y, WithinAbs(expected.y, 1e-9));
  // Not the tick-start position: that would be the pre-fix, buggy value.
  REQUIRE_FALSE(intercepted.position == ballStart);
}

TEST_CASE("The ball moves independently of the players", "[ballMovement]") {
  // Straight up from the center spot while two players run elsewhere: a free
  // ball nobody reaches rolls the same whoever moves around it.
  const Vec2 kick{.x = 0.0, .y = 7.0};
  MatchSimulation ballOnly = simulationOf(kick, {makeBallMovementSystem({})});
  MatchSimulation withPlayers = simulationOf(
      kick, {makePlayerMovementSystem(), makeBallMovementSystem({})},
      {{.tick = SimTick(0),
        .command = MovePlayerCommand{.playerId = PlayerId(7), .target = {.x = 10.0, .y = 10.0}}},
       {.tick = SimTick(0),
        .command = MovePlayerCommand{.playerId = PlayerId(14), .target = {.x = 50.0, .y = 10.0}}}});

  for (int tick = 0; tick < 150; ++tick) {
    REQUIRE(ballOnly.step().has_value());
    REQUIRE(withPlayers.step().has_value());
    REQUIRE(ballOnly.state().ball() == withPlayers.state().ball());
  }
  REQUIRE_FALSE(withPlayers.state().players()[6].position ==
                ballOnly.state().players()[6].position);
}

TEST_CASE("Identical initial conditions produce identical ball trajectories", "[ballMovement]") {
  MatchSimulation first = simulationOf({.x = -4.5, .y = 2.25}, {makeBallMovementSystem({})});
  MatchSimulation second = simulationOf({.x = -4.5, .y = 2.25}, {makeBallMovementSystem({})});

  for (int tick = 0; tick < 200; ++tick) {
    REQUIRE(first.step().has_value());
    REQUIRE(second.step().has_value());
    REQUIRE(first.state().ball() == second.state().ball());
  }
}

TEST_CASE("The ball movement system rejects an invalid rolling deceleration", "[ballMovement]") {
  const double invalid = GENERATE(0.0, -1.0, std::numeric_limits<double>::infinity(),
                                  std::numeric_limits<double>::quiet_NaN());
  CAPTURE(invalid);
  REQUIRE_THROWS_AS(makeBallMovementSystem({.rollingDeceleration = invalid}),
                    std::invalid_argument);
}
