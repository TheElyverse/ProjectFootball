#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

#include "ballMovement.hpp"
#include "matchCommand.hpp"
#include "matchSetup.hpp"
#include "matchSimulation.hpp"
#include "matchState.hpp"
#include "passing.hpp"
#include "pursuit.hpp"
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
using ElyverseFootball::SimMatch::findInterception;
using ElyverseFootball::SimMatch::GiveBallCommand;
using ElyverseFootball::SimMatch::Interception;
using ElyverseFootball::SimMatch::makePursuitSystem;
using ElyverseFootball::SimMatch::MatchSetup;
using ElyverseFootball::SimMatch::MatchSimulation;
using ElyverseFootball::SimMatch::MatchState;
using ElyverseFootball::SimMatch::MatchStateWriter;
using ElyverseFootball::SimMatch::MatchStepContext;
using ElyverseFootball::SimMatch::MatchSystem;
using ElyverseFootball::SimMatch::MovePlayerCommand;
using ElyverseFootball::SimMatch::PassCommand;
using ElyverseFootball::SimMatch::Pitch;
using ElyverseFootball::SimMatch::planPassSpeed;
using ElyverseFootball::SimMatch::PlayerMatchState;
using ElyverseFootball::SimMatch::PursuitConfig;
using ElyverseFootball::SimMatch::ReceptionConfig;
using ElyverseFootball::SimMatch::ScheduledCommand;
using ElyverseFootball::SimMatch::startMatch;
using ElyverseFootball::SimMatch::stepFreeBall;
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
constexpr Interception kNoInterception{.point = {.x = -1.0, .y = -1.0}, .seconds = -1.0};

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

TEST_CASE("An opponent intercepts a pass by reaching it on its way", "[reception]") {
  // Player 3 stands two meters off the line from 1 to 2 and steps in.
  const MatchState state = twoASide({{.x = 10.0, .y = 20.0},
                                     {.x = 30.0, .y = 20.0},
                                     {.x = 20.0, .y = 22.0},
                                     {.x = 50.0, .y = 5.0}},
                                    freeBall({.x = 10.0, .y = 20.0}));
  MatchSimulation simulation = passFrom(state, {.x = 30.0, .y = 20.0});

  stepUntilTaken(simulation, 150);

  REQUIRE(simulation.state().ball().owner == PlayerId(3));
}

TEST_CASE("A receiver misses a pass he cannot reach", "[reception]") {
  // A hard pass far to the side of player 2: it crosses the touchline before
  // anyone gets there, then everyone goes after the loose ball.
  const MatchState state = twoASide({{.x = 10.0, .y = 20.0},
                                     {.x = 30.0, .y = 20.0},
                                     {.x = 45.0, .y = 30.0},
                                     {.x = 50.0, .y = 5.0}},
                                    freeBall({.x = 10.0, .y = 20.0}));
  MatchSimulation simulation = passFrom(state, {.x = 30.0, .y = 40.0}, 18.0);

  bool reachedTheLine = false;
  for (int tick = 0; tick < 90 && !reachedTheLine; ++tick) {
    REQUIRE(simulation.step().has_value());
    const BallState& ball = simulation.state().ball();
    reachedTheLine = ball.position.y == 40.0;
    if (tick > 2) {
      REQUIRE_FALSE(ball.owner.has_value());
    }
  }
  REQUIRE(reachedTheLine);
  REQUIRE(simulation.state().ball().lastTouch.value_or(BallTouch{}).playerId == PlayerId(1));

  // The loose ball is recovered afterwards.
  stepUntilTaken(simulation, 300);
  REQUIRE(simulation.state().ball().owner.has_value());
}

TEST_CASE("The fastest player recovers a loose ball", "[reception]") {
  // Player 4 is 8 m away, everyone else farther.
  const MatchState state = twoASide({{.x = 10.0, .y = 20.0},
                                     {.x = 30.0, .y = 5.0},
                                     {.x = 50.0, .y = 35.0},
                                     {.x = 40.0, .y = 22.0}},
                                    freeBall({.x = 40.0, .y = 30.0}));
  MatchSimulation simulation =
      startMatch({.initialState = state, .config = {}, .seed = 1, .commands = {}});

  for (int tick = 0; tick < 120 && !simulation.state().ball().owner; ++tick) {
    REQUIRE(simulation.step().has_value());
  }

  REQUIRE(simulation.state().ball().owner == PlayerId(4));
  // He reached it within the control radius and did not need to stand on it.
  const double distance =
      std::sqrt((simulation.previousState().players()[3].position - Vec2{.x = 40.0, .y = 30.0})
                    .lengthSquared());
  REQUIRE(distance <= ReceptionConfig{}.controlRadius + 0.3);
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

TEST_CASE("An interception is the earliest point reached before the ball", "[reception]") {
  const BallState ball = freeBall({.x = 10.0, .y = 20.0}, {.x = 12.0, .y = 0.0});
  const PlayerMatchState onTheLine = playerAt(3, TeamSide::kAway, {.x = 30.0, .y = 22.0});

  const auto interception =
      findInterception(onTheLine, ball, BallPhysics{}, kPitch, PursuitConfig{});

  REQUIRE(interception.has_value());
  const Interception reached = interception.value_or(kNoInterception);
  REQUIRE(reached.point.y == 20.0);
  // He cannot get there before the ball: the point lies ahead of him on the
  // line, where he arrives no later than the ball.
  REQUIRE(reached.point.x > 20.0);
  REQUIRE(reached.seconds > 0.0);

  // A ball at rest is reached where it lies.
  const auto loose = findInterception(onTheLine, freeBall({.x = 35.0, .y = 22.0}), BallPhysics{},
                                      kPitch, PursuitConfig{});
  REQUIRE(loose.value_or(kNoInterception).point == Vec2{.x = 35.0, .y = 22.0});
}

TEST_CASE("The ball path is predicted no further than the horizon", "[reception]") {
  // Nobody can reach the ball within one second, so the pursuer heads for
  // where it is at the horizon: 1.0 s, although 0.3 s samples would step to
  // 1.2 s.
  const BallState ball = freeBall({.x = 10.0, .y = 20.0}, {.x = 12.0, .y = 0.0});
  const PlayerMatchState far = playerAt(3, TeamSide::kAway, {.x = 55.0, .y = 38.0});
  PursuitConfig config;
  config.sampleSeconds = 0.3;
  config.horizonSeconds = 1.0;

  const auto interception = findInterception(far, ball, BallPhysics{}, kPitch, config);

  const BallState atHorizon = stepFreeBall(ball, BallPhysics{}, kPitch, 1.0);
  REQUIRE(interception.has_value());
  REQUIRE_THAT(interception.value_or(kNoInterception).point.x,
               WithinAbs(atHorizon.position.x, 1e-9));
}

TEST_CASE("Pursuit rejects a configuration it cannot run in bounded time", "[reception]") {
  const auto rejects = [](const PursuitConfig& config) {
    REQUIRE_THROWS_AS(makePursuitSystem(BallPhysics{}, config), std::invalid_argument);
  };
  constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
  constexpr double kInfinity = std::numeric_limits<double>::infinity();

  rejects({.intervalTicks = 0, .sampleSeconds = 0.1, .horizonSeconds = 8.0});
  for (const double invalid : {0.0, -0.1, kNaN, kInfinity}) {
    rejects({.intervalTicks = 3, .sampleSeconds = invalid, .horizonSeconds = 8.0});
    rejects({.intervalTicks = 3, .sampleSeconds = 0.1, .horizonSeconds = invalid});
  }
  // More than kMaxPursuitSamples samples: 8 s in 1 ms steps, or 1001 steps.
  rejects({.intervalTicks = 3, .sampleSeconds = 0.001, .horizonSeconds = 8.0});
  rejects({.intervalTicks = 3, .sampleSeconds = 0.125, .horizonSeconds = 125.125});

  // Exactly at the limit is fine, and so are the defaults.
  REQUIRE_NOTHROW(makePursuitSystem(
      BallPhysics{}, {.intervalTicks = 3, .sampleSeconds = 0.125, .horizonSeconds = 125.0}));
  REQUIRE_NOTHROW(makePursuitSystem(BallPhysics{}, PursuitConfig{}));
}

TEST_CASE("One player per side goes after a free ball", "[reception]") {
  const MatchState state = twoASide({{.x = 10.0, .y = 20.0},
                                     {.x = 30.0, .y = 20.0},
                                     {.x = 45.0, .y = 30.0},
                                     {.x = 50.0, .y = 5.0}},
                                    freeBall({.x = 10.0, .y = 20.0}));
  MatchSimulation simulation = passFrom(state, {.x = 30.0, .y = 20.0});

  for (int tick = 0; tick < 4; ++tick) {
    REQUIRE(simulation.step().has_value());
  }

  const auto players = simulation.state().players();
  // The passer does not chase his own pass; player 2 and player 3 go.
  REQUIRE_FALSE(players[0].target.has_value());
  REQUIRE(players[1].target.has_value());
  REQUIRE(players[2].target.has_value());
  REQUIRE_FALSE(players[3].target.has_value());
}

TEST_CASE("Pursuit records each side's chaser", "[reception]") {
  const MatchState state = twoASide({{.x = 10.0, .y = 20.0},
                                     {.x = 30.0, .y = 20.0},
                                     {.x = 45.0, .y = 30.0},
                                     {.x = 50.0, .y = 5.0}},
                                    freeBall({.x = 10.0, .y = 20.0}));
  MatchSimulation simulation = passFrom(state, {.x = 30.0, .y = 20.0});
  for (int tick = 0; tick < 4; ++tick) {
    REQUIRE(simulation.step().has_value());
  }
  REQUIRE(simulation.state().chaser(TeamSide::kHome) == PlayerId(2));
  REQUIRE(simulation.state().chaser(TeamSide::kAway) == PlayerId(3));
}

TEST_CASE("A receiver stops chasing once he has the ball", "[reception]") {
  // Player 2 runs to meet a pass; once he has it, the interception point is
  // no longer his target and he does not run on to it.
  const MatchState state = twoASide({{.x = 10.0, .y = 20.0},
                                     {.x = 30.0, .y = 20.0},
                                     {.x = 55.0, .y = 38.0},
                                     {.x = 58.0, .y = 2.0}},
                                    freeBall({.x = 10.0, .y = 20.0}));
  MatchSimulation simulation = passFrom(state, {.x = 30.0, .y = 20.0});
  stepUntilTaken(simulation, 120);
  REQUIRE(simulation.state().ball().owner == PlayerId(2));
  // Pursuit runs every third tick; after its next run nobody chases.
  for (int tick = 0; tick < 3; ++tick) {
    REQUIRE(simulation.step().has_value());
  }
  const MatchState& after = simulation.state();
  REQUIRE_FALSE(after.chaser(TeamSide::kHome).has_value());
  REQUIRE_FALSE(after.chaser(TeamSide::kAway).has_value());
  for (const PlayerMatchState& player : after.players()) {
    CAPTURE(player.playerId.value());
    REQUIRE_FALSE(player.target.has_value());
  }
}

TEST_CASE("A player who is no longer the closest stops chasing", "[reception]") {
  // Away player 3 was sent after the ball at tick 0, far from it; player 4
  // stands next to it. When pursuit runs again, player 4 takes over and
  // player 3 stops where he is instead of running on to his old target.
  const MatchState state = twoASide(
      {{.x = 5.0, .y = 5.0}, {.x = 5.0, .y = 35.0}, {.x = 55.0, .y = 38.0}, {.x = 31.0, .y = 21.0}},
      freeBall({.x = 30.0, .y = 20.0}));
  const MatchSystem sendPlayer3{
      .name = "send player 3",
      .update =
          [](const MatchStepContext&, const MatchState&, MatchStateWriter& next) {
            next.setChaser(TeamSide::kAway, PlayerId(3));
            next.setPlayerTarget(2, Vec2{.x = 30.0, .y = 20.0});
          },
      .intervalTicks = 1000,
      .phaseTicks = 0};
  // Pursuit first runs in the step from tick 1, after player 3 was sent.
  MatchSystem pursuit = makePursuitSystem(BallPhysics{}, {});
  pursuit.phaseTicks = 1;
  MatchSimulation simulation({.initialState = state,
                              .seed = 5,
                              .ticksPerSecond = 30,
                              .systems = {sendPlayer3, pursuit},
                              .commands = {}});
  REQUIRE(simulation.step().has_value());
  REQUIRE(simulation.state().chaser(TeamSide::kAway) == PlayerId(3));
  REQUIRE(simulation.state().players()[2].target.has_value());

  REQUIRE(simulation.step().has_value());
  REQUIRE(simulation.state().chaser(TeamSide::kAway) == PlayerId(4));
  REQUIRE_FALSE(simulation.state().players()[2].target.has_value());
  REQUIRE(simulation.state().players()[3].target.has_value());
}

TEST_CASE("Only a player of the side can chase for it", "[reception]") {
  const auto chaseForHome =
      MatchSystem{.name = "chase",
                  .update = [](const MatchStepContext&, const MatchState&, MatchStateWriter& next) {
                    next.setChaser(TeamSide::kHome, PlayerId(3));
                  }};
  MatchSimulation simulation({.initialState = twoASide({{.x = 10.0, .y = 20.0},
                                                        {.x = 30.0, .y = 20.0},
                                                        {.x = 45.0, .y = 30.0},
                                                        {.x = 50.0, .y = 5.0}},
                                                       freeBall({.x = 30.0, .y = 20.0})),
                              .seed = 1,
                              .ticksPerSecond = 30,
                              .systems = {chaseForHome},
                              .commands = {}});
  REQUIRE_THROWS_AS((void)simulation.step(), std::invalid_argument);
}
