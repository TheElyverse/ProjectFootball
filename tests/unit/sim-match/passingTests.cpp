#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "ballMovement.hpp"
#include "kickoffScenario.hpp"
#include "matchCommand.hpp"
#include "matchSetup.hpp"
#include "matchSimulation.hpp"
#include "matchState.hpp"
#include "passing.hpp"
#include "random.hpp"
#include "vec2.hpp"

using Catch::Matchers::WithinAbs;
using ElyverseFootball::SimCore::PlayerId;
using ElyverseFootball::SimCore::RandomNumberGenerator;
using ElyverseFootball::SimCore::SimTick;
using ElyverseFootball::SimCore::Vec2;
using ElyverseFootball::SimMatch::BallPhysics;
using ElyverseFootball::SimMatch::BallState;
using ElyverseFootball::SimMatch::BallTouch;
using ElyverseFootball::SimMatch::executePass;
using ElyverseFootball::SimMatch::GiveBallCommand;
using ElyverseFootball::SimMatch::makeSevenASideKickoff;
using ElyverseFootball::SimMatch::MatchCommandErrorCode;
using ElyverseFootball::SimMatch::MatchConfig;
using ElyverseFootball::SimMatch::MatchSetup;
using ElyverseFootball::SimMatch::MatchSimulation;
using ElyverseFootball::SimMatch::PassCommand;
using ElyverseFootball::SimMatch::PassConfig;
using ElyverseFootball::SimMatch::PassIntent;
using ElyverseFootball::SimMatch::passReach;
using ElyverseFootball::SimMatch::Pitch;
using ElyverseFootball::SimMatch::planPassSpeed;
using ElyverseFootball::SimMatch::PlayerMatchState;
using ElyverseFootball::SimMatch::ScheduledCommand;
using ElyverseFootball::SimMatch::startMatch;

namespace {

// Execution without error, so a test can predict the ball exactly.
[[nodiscard]] PassConfig exact() {
  PassConfig config;
  config.directionError = 0.0;
  config.speedError = 0.0;
  return config;
}

[[nodiscard]] MatchSimulation matchOf(std::vector<ScheduledCommand> commands,
                                      const PassConfig passing = exact(),
                                      const std::uint64_t seed = 1) {
  auto state = makeSevenASideKickoff(Pitch(60.0, 40.0));
  REQUIRE(state.has_value());
  MatchConfig config;
  config.passing = passing;
  return startMatch(MatchSetup{.initialState = *std::move(state),
                               .config = config,
                               .seed = seed,
                               .commands = std::move(commands)});
}

[[nodiscard]] ScheduledCommand give(const std::int64_t tick, const std::uint32_t playerId) {
  return {.tick = SimTick(tick), .command = GiveBallCommand{.playerId = PlayerId(playerId)}};
}

[[nodiscard]] ScheduledCommand pass(const std::int64_t tick, const std::uint32_t playerId,
                                    const Vec2 target, const double speed) {
  return {.tick = SimTick(tick),
          .command = PassCommand{.playerId = PlayerId(playerId),
                                 .target = target,
                                 .speed = speed,
                                 .receiver = std::nullopt}};
}

void stepTimes(MatchSimulation& simulation, const int steps) {
  for (int step = 0; step < steps; ++step) {
    REQUIRE(simulation.step().has_value());
  }
}

[[nodiscard]] double lengthOf(const Vec2 vector) {
  return std::sqrt(vector.lengthSquared());
}

[[nodiscard]] BallState ballAt(const Vec2 position) {
  return {.position = position, .velocity = {}, .owner = std::nullopt, .lastTouch = std::nullopt};
}

[[nodiscard]] PlayerMatchState passerFacing(const Vec2 facing) {
  return {.playerId = PlayerId(1),
          .side = {},
          .position = {},
          .velocity = {},
          .attributes = {},
          .target = std::nullopt,
          .facing = facing};
}

// Player 4 (index 3) stands at (21, 20) in the kickoff fixture, facing +x.
constexpr std::uint32_t kPasser = 4;

}  // namespace

TEST_CASE("A planned pass arrives at the arrival speed", "[passing]") {
  const BallPhysics physics;
  const PassConfig config;

  const double speed = planPassSpeed(20.0, physics, config);

  // v0² = arrival² + 2·a·d, so after d meters the ball rolls at arrival.
  REQUIRE_THAT(speed, WithinAbs(std::sqrt(16.0 + (2.0 * 1.5 * 20.0)), 1e-12));
  REQUIRE(planPassSpeed(1000.0, physics, config) == config.maxSpeed);
  REQUIRE_THAT(passReach(config.maxSpeed, physics), WithinAbs(22.0 * 22.0 / 3.0, 1e-9));
}

TEST_CASE("An exact pass leaves the foot at the intended speed toward its target", "[passing]") {
  RandomNumberGenerator random(7);
  const PassIntent intent{.passer = PlayerId(1),
                          .target = {.x = 13.0, .y = 14.0},
                          .speed = 10.0,
                          .receiver = std::nullopt};

  const Vec2 velocity = executePass(intent, ballAt({.x = 10.0, .y = 10.0}),
                                    passerFacing({.x = 1.0, .y = 0.0}), exact(), random);

  REQUIRE_THAT(velocity.x, WithinAbs(6.0, 1e-12));
  REQUIRE_THAT(velocity.y, WithinAbs(8.0, 1e-12));
}

TEST_CASE("Execution error stays within its bounds and uses two draws", "[passing]") {
  const PassConfig config;
  const PassIntent intent{.passer = PlayerId(1),
                          .target = {.x = 40.0, .y = 10.0},
                          .speed = 12.0,
                          .receiver = std::nullopt};
  double largestAngle = 0.0;
  for (std::uint64_t seed = 0; seed < 500; ++seed) {
    RandomNumberGenerator random(seed);
    const Vec2 velocity =
        executePass(intent, ballAt({.x = 10.0, .y = 10.0}), passerFacing({}), config, random);
    const double speed = lengthOf(velocity);
    REQUIRE(speed >= 12.0 * 0.95);
    REQUIRE(speed <= 12.0 * 1.05);
    const double angle = std::abs(std::atan2(velocity.y, velocity.x));
    REQUIRE(angle <= std::atan(config.directionError) + 1e-12);
    largestAngle = std::max(largestAngle, angle);

    RandomNumberGenerator expected(seed);
    (void)expected.nextU64();
    (void)expected.nextU64();
    REQUIRE(random.nextU64() == expected.nextU64());
  }
  // The error is real, not zero.
  REQUIRE(largestAngle > 0.5 * std::atan(config.directionError));
}

TEST_CASE("A pass is never harder than the maximum speed", "[passing]") {
  RandomNumberGenerator random(1);
  const PassIntent intent{.passer = PlayerId(1),
                          .target = {.x = 50.0, .y = 10.0},
                          .speed = 40.0,
                          .receiver = std::nullopt};

  REQUIRE(lengthOf(executePass(intent, ballAt({.x = 10.0, .y = 10.0}), passerFacing({}), exact(),
                               random)) == exact().maxSpeed);
}

TEST_CASE("A pass onto the ball itself is played along the passer's facing", "[passing]") {
  RandomNumberGenerator random(1);
  const PassIntent intent{.passer = PlayerId(1),
                          .target = {.x = 5.0, .y = 5.0},
                          .speed = 3.0,
                          .receiver = std::nullopt};

  REQUIRE(executePass(intent, ballAt({.x = 5.0, .y = 5.0}), passerFacing({.x = 0.0, .y = -1.0}),
                      exact(), random) == Vec2{.x = 0.0, .y = -3.0});
}

TEST_CASE("Executing a pass releases possession and kicks the ball", "[passing]") {
  const Vec2 target{.x = 21.5, .y = 5.0};
  MatchSimulation simulation = matchOf({give(0, kPasser), pass(10, kPasser, target, 9.0)});
  stepTimes(simulation, 10);
  const Vec2 kickedFrom = simulation.state().ball().position;
  REQUIRE(simulation.state().ball().owner == PlayerId(kPasser));

  stepTimes(simulation, 1);

  const auto& ball = simulation.state().ball();
  REQUIRE_FALSE(ball.owner.has_value());
  REQUIRE(ball.lastTouch == BallTouch{.playerId = PlayerId(kPasser), .tick = SimTick(10)});
  REQUIRE_FALSE(simulation.state().pendingPass().has_value());
  // One tick of rolling at 9 m/s toward the target, minus friction.
  const Vec2 line = (target - kickedFrom) * (1.0 / lengthOf(target - kickedFrom));
  REQUIRE_THAT(ball.velocity.dot(line), WithinAbs(9.0 - (1.5 / 30.0), 1e-9));
  REQUIRE_THAT(std::abs((ball.velocity.x * line.y) - (ball.velocity.y * line.x)),
               WithinAbs(0.0, 1e-9));
}

TEST_CASE("Pass speed and friction decide how far the pass goes", "[passing]") {
  const Vec2 target{.x = 21.5, .y = 2.0};
  SECTION("a planned pass reaches its target") {
    MatchSimulation probe = matchOf({give(0, kPasser)});
    stepTimes(probe, 1);
    const double distance = lengthOf(target - probe.state().ball().position);
    const double speed = planPassSpeed(distance, BallPhysics{}, exact());

    MatchSimulation simulation = matchOf({give(0, kPasser), pass(1, kPasser, target, speed)});
    stepTimes(simulation, 300);

    // Nobody stops it: it rolls past the target and on until the touchline.
    const auto& ball = simulation.state().ball();
    REQUIRE(ball.position.y == 0.0);
  }
  SECTION("a soft pass stops short") {
    MatchSimulation simulation = matchOf({give(0, kPasser), pass(1, kPasser, target, 4.0)});
    stepTimes(simulation, 1);
    const Vec2 start = simulation.state().ball().position;
    stepTimes(simulation, 300);

    const auto& ball = simulation.state().ball();
    REQUIRE(ball.velocity == Vec2{});
    REQUIRE_THAT(lengthOf(ball.position - start), WithinAbs(passReach(4.0, BallPhysics{}), 0.2));
    REQUIRE(lengthOf(ball.position - start) < lengthOf(target - start));
  }
}

TEST_CASE("A player without the ball cannot pass", "[passing]") {
  SECTION("someone else owns it") {
    MatchSimulation simulation =
        matchOf({give(0, kPasser), pass(5, 5, {.x = 30.0, .y = 30.0}, 10.0)});
    stepTimes(simulation, 6);
    REQUIRE(simulation.state().ball().owner == PlayerId(kPasser));
    REQUIRE_FALSE(simulation.state().pendingPass().has_value());
  }
  SECTION("the ball is free") {
    MatchSimulation simulation = matchOf({pass(0, kPasser, {.x = 30.0, .y = 30.0}, 10.0)});
    stepTimes(simulation, 1);
    REQUIRE(simulation.state().ball().velocity == Vec2{});
    REQUIRE_FALSE(simulation.state().ball().lastTouch.has_value());
    REQUIRE_FALSE(simulation.state().pendingPass().has_value());
  }
}

TEST_CASE("Execution error follows the seed", "[passing]") {
  const auto kickedVelocity = [](const std::uint64_t seed) {
    MatchSimulation simulation = matchOf(
        {give(0, kPasser), pass(1, kPasser, {.x = 40.0, .y = 30.0}, 12.0)}, PassConfig{}, seed);
    stepTimes(simulation, 2);
    return simulation.state().ball().velocity;
  };

  REQUIRE(kickedVelocity(3) == kickedVelocity(3));
  REQUIRE_FALSE(kickedVelocity(3) == kickedVelocity(4));
}

TEST_CASE("Pass commands are validated when scheduled", "[passing]") {
  MatchSimulation simulation = matchOf({});

  REQUIRE(simulation.schedule(pass(0, kPasser, {.x = 1.0, .y = 1.0}, 0.0)).error().code ==
          MatchCommandErrorCode::kInvalidPassSpeed);
  REQUIRE(simulation.schedule(pass(0, 99, {.x = 1.0, .y = 1.0}, 5.0)).error().code ==
          MatchCommandErrorCode::kUnknownPlayer);
  REQUIRE(simulation
              .schedule({.tick = SimTick(0),
                         .command = PassCommand{.playerId = PlayerId(kPasser),
                                                .target = {.x = 1.0, .y = 1.0},
                                                .speed = 5.0,
                                                .receiver = PlayerId(77)}})
              .error()
              .code == MatchCommandErrorCode::kUnknownPlayer);
}
