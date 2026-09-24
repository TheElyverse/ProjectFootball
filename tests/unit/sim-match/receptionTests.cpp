#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "ballMovement.hpp"
#include "matchCommand.hpp"
#include "matchSetup.hpp"
#include "matchSimulation.hpp"
#include "matchState.hpp"
#include "passing.hpp"
#include "reception.hpp"
#include "vec2.hpp"

using Catch::Matchers::WithinAbs;
using ElyverseFootball::SimCore::PlayerId;
using ElyverseFootball::SimCore::SimTick;
using ElyverseFootball::SimCore::Vec2;
using ElyverseFootball::SimMatch::BallClaim;
using ElyverseFootball::SimMatch::BallPhysics;
using ElyverseFootball::SimMatch::BallState;
using ElyverseFootball::SimMatch::BallTouch;
using ElyverseFootball::SimMatch::Contact;
using ElyverseFootball::SimMatch::findBallClaim;
using ElyverseFootball::SimMatch::findContact;
using ElyverseFootball::SimMatch::GiveBallCommand;
using ElyverseFootball::SimMatch::MatchSetup;
using ElyverseFootball::SimMatch::MatchSimulation;
using ElyverseFootball::SimMatch::MatchState;
using ElyverseFootball::SimMatch::MovePlayerCommand;
using ElyverseFootball::SimMatch::PassCommand;
using ElyverseFootball::SimMatch::Pitch;
using ElyverseFootball::SimMatch::planPassSpeed;
using ElyverseFootball::SimMatch::PlayerMatchState;
using ElyverseFootball::SimMatch::ReceptionConfig;
using ElyverseFootball::SimMatch::ScheduledCommand;
using ElyverseFootball::SimMatch::startMatch;
using ElyverseFootball::SimMatch::TeamSide;

namespace {

constexpr double kSecondsPerTick = 1.0 / 30.0;
const Pitch kPitch(60.0, 40.0);

[[nodiscard]] PlayerMatchState playerAt(const std::uint32_t playerId, const TeamSide side,
                                        const Vec2 position) {
  return {.playerId = PlayerId(playerId),
          .side = side,
          .position = position,
          .velocity = {},
          .attributes = {},
          .target = std::nullopt,
          .facing = {.x = 1.0, .y = 0.0}};
}

[[nodiscard]] BallState freeBall(const Vec2 position, const Vec2 velocity = {},
                                 const std::optional<BallTouch> lastTouch = std::nullopt) {
  return {
      .position = position, .velocity = velocity, .owner = std::nullopt, .lastTouch = lastTouch};
}

// Two a side: home 1 and 2, away 3 and 4.
[[nodiscard]] MatchState twoASide(const std::vector<Vec2>& positions, const BallState& ball) {
  auto state = MatchState::create({.pitch = kPitch,
                                   .players = {playerAt(1, TeamSide::kHome, positions.at(0)),
                                               playerAt(2, TeamSide::kHome, positions.at(1)),
                                               playerAt(3, TeamSide::kAway, positions.at(2)),
                                               playerAt(4, TeamSide::kAway, positions.at(3))},
                                   .ball = ball,
                                   .playersPerSide = 2});
  REQUIRE(state.has_value());
  return *std::move(state);
}

// Player 1 has the ball at the start and passes to (target) in tick 1 at the
// speed that arrives at 4 m/s; everything else runs as in a real match.
[[nodiscard]] MatchSimulation passFrom(const MatchState& state, const Vec2 target,
                                       const std::optional<double> speed = std::nullopt) {
  const double distance = std::sqrt((target - state.players()[0].position).lengthSquared());
  const MatchSetup setup{.initialState = state, .config = {}, .seed = 5, .commands = {}};
  MatchSimulation simulation = startMatch(setup);
  REQUIRE(
      simulation.schedule({.tick = SimTick(0), .command = GiveBallCommand{.playerId = PlayerId(1)}})
          .has_value());
  REQUIRE(simulation
              .schedule({.tick = SimTick(1),
                         .command = PassCommand{.playerId = PlayerId(1),
                                                .target = target,
                                                .speed = speed.value_or(planPassSpeed(
                                                    distance, BallPhysics{}, setup.config.passing)),
                                                .receiver = std::nullopt}})
              .has_value());
  return simulation;
}

// Steps until someone other than player 1 owns the ball, or the tick limit.
void stepUntilTaken(MatchSimulation& simulation, const int maxTicks) {
  for (int tick = 0; tick < maxTicks; ++tick) {
    REQUIRE(simulation.step().has_value());
    const auto owner = simulation.state().ball().owner;
    if (simulation.tick() > SimTick(2) && owner && *owner != PlayerId(1)) {
      return;
    }
  }
}

constexpr Contact kNoContact{.contactFraction = -1.0, .closestDistance = -1.0};

// The id of the claimant, 0 if nobody claims the ball.
[[nodiscard]] std::uint32_t claimant(const std::optional<BallClaim>& claim) {
  return claim ? claim->playerId.value() : 0U;
}

}  // namespace

TEST_CASE("Contact is the first moment player and ball are within reach", "[reception]") {
  // A ball rolling past a standing player at 0.5 m: it enters his 1 m reach
  // when 0.866 m before the closest point.
  const auto contact =
      findContact({.x = 5.0, .y = 0.5}, {.x = 5.0, .y = 0.5}, {}, {.x = 10.0, .y = 0.0}, 1.0);
  REQUIRE(contact.has_value());
  const Contact hit = contact.value_or(kNoContact);
  REQUIRE_THAT(hit.contactFraction, WithinAbs((5.0 - std::sqrt(0.75)) / 10.0, 1e-12));
  REQUIRE_THAT(hit.closestDistance, WithinAbs(0.5, 1e-12));

  // Already within reach at the start.
  REQUIRE(findContact({}, {}, {.x = 0.5, .y = 0.0}, {.x = 3.0, .y = 0.0}, 1.0)
              .value_or(kNoContact)
              .contactFraction == 0.0);
  // Never within reach.
  REQUIRE_FALSE(
      findContact({.x = 5.0, .y = 2.0}, {.x = 5.0, .y = 2.0}, {}, {.x = 10.0, .y = 0.0}, 1.0)
          .has_value());
  // A running player meets the ball although neither end position is close.
  REQUIRE(findContact({.x = 5.0, .y = -1.0}, {.x = 5.0, .y = 1.0}, {.x = 4.0, .y = 0.0},
                      {.x = 6.0, .y = 0.0}, 0.2)
              .has_value());
}

TEST_CASE("A receiver controls a reachable pass", "[reception]") {
  const MatchState state = twoASide({{.x = 10.0, .y = 20.0},
                                     {.x = 25.0, .y = 12.0},
                                     {.x = 45.0, .y = 30.0},
                                     {.x = 50.0, .y = 5.0}},
                                    freeBall({.x = 10.0, .y = 20.0}));
  MatchSimulation simulation = passFrom(state, {.x = 25.0, .y = 12.0});

  stepUntilTaken(simulation, 150);

  const BallState& ball = simulation.state().ball();
  REQUIRE(ball.owner == PlayerId(2));
  REQUIRE(ball.lastTouch ==
          BallTouch{.playerId = PlayerId(2), .tick = SimTick(simulation.tick().value() - 1)});
}

TEST_CASE("An opponent standing in the lane intercepts the pass", "[reception]") {
  // Player 3 stands half a meter off the line from 1 to 2.
  const MatchState state = twoASide({{.x = 10.0, .y = 20.0},
                                     {.x = 30.0, .y = 20.0},
                                     {.x = 20.0, .y = 20.5},
                                     {.x = 50.0, .y = 5.0}},
                                    freeBall({.x = 10.0, .y = 20.0}));
  MatchSimulation simulation = passFrom(state, {.x = 30.0, .y = 20.0});

  stepUntilTaken(simulation, 150);

  REQUIRE(simulation.state().ball().owner == PlayerId(3));
}

TEST_CASE("Simultaneous claims are decided by time, distance, then id", "[reception]") {
  const ReceptionConfig config;
  SECTION("the earlier contact wins") {
    // The ball rolls toward player 3, who meets it before player 1 would.
    const MatchState state = twoASide({{.x = 21.5, .y = 20.0},
                                       {.x = 5.0, .y = 5.0},
                                       {.x = 20.3, .y = 20.0},
                                       {.x = 55.0, .y = 5.0}},
                                      freeBall({.x = 19.0, .y = 20.0}, {.x = 20.0, .y = 0.0}));
    const auto claim = findBallClaim(state, state.ball(), {.x = 23.0, .y = 20.0}, SimTick(0),
                                     kSecondsPerTick, config);
    REQUIRE(claimant(claim) == 3);
  }
  SECTION("at the same moment the closer player wins") {
    // Both are within reach at the start; player 3 is closer.
    const MatchState state = twoASide({{.x = 20.0, .y = 20.9},
                                       {.x = 5.0, .y = 5.0},
                                       {.x = 20.0, .y = 19.5},
                                       {.x = 55.0, .y = 5.0}},
                                      freeBall({.x = 20.0, .y = 20.0}));
    const auto claim = findBallClaim(state, state.ball(), state.ball().position, SimTick(0),
                                     kSecondsPerTick, config);
    REQUIRE(claimant(claim) == 3);
  }
  SECTION("at the same moment and distance the lower id wins") {
    const MatchState state = twoASide({{.x = 5.0, .y = 5.0},
                                       {.x = 20.0, .y = 19.5},
                                       {.x = 20.0, .y = 20.5},
                                       {.x = 55.0, .y = 5.0}},
                                      freeBall({.x = 20.0, .y = 20.0}));
    const auto claim = findBallClaim(state, state.ball(), state.ball().position, SimTick(0),
                                     kSecondsPerTick, config);
    REQUIRE(claimant(claim) == 2);
  }
}

TEST_CASE("The passer cannot take his own pass back at once", "[reception]") {
  const MatchState state = twoASide(
      {{.x = 20.0, .y = 20.0}, {.x = 5.0, .y = 5.0}, {.x = 50.0, .y = 35.0}, {.x = 55.0, .y = 5.0}},
      freeBall({.x = 20.5, .y = 20.0}, {.x = 5.0, .y = 0.0},
               BallTouch{.playerId = PlayerId(1), .tick = SimTick(10)}));
  const ReceptionConfig config;

  REQUIRE_FALSE(findBallClaim(state, state.ball(), {.x = 20.7, .y = 20.0}, SimTick(12),
                              kSecondsPerTick, config)
                    .has_value());
  // 0.3 s = 9 ticks later he may.
  REQUIRE(claimant(findBallClaim(state, state.ball(), {.x = 20.7, .y = 20.0}, SimTick(19),
                                 kSecondsPerTick, config)) == 1);
}
