#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

#include "ballPhysics.hpp"
#include "ids.hpp"
#include "matchSimulation.hpp"
#include "matchState.hpp"
#include "passing.hpp"
#include "pitch.hpp"
#include "random.hpp"
#include "shooting.hpp"
#include "simTime.hpp"
#include "vec2.hpp"

using Catch::Matchers::WithinAbs;
using ElyverseFootball::SimCore::PlayerId;
using ElyverseFootball::SimCore::RandomNumberGenerator;
using ElyverseFootball::SimCore::SimTick;
using ElyverseFootball::SimCore::Vec2;
using ElyverseFootball::SimMatch::ballAfter;
using ElyverseFootball::SimMatch::BallPhysics;
using ElyverseFootball::SimMatch::BallState;
using ElyverseFootball::SimMatch::BallTouch;
using ElyverseFootball::SimMatch::Deflection;
using ElyverseFootball::SimMatch::deflectShot;
using ElyverseFootball::SimMatch::executeShot;
using ElyverseFootball::SimMatch::Foot;
using ElyverseFootball::SimMatch::MatchSimulation;
using ElyverseFootball::SimMatch::MatchState;
using ElyverseFootball::SimMatch::MatchStateWriter;
using ElyverseFootball::SimMatch::MatchStepContext;
using ElyverseFootball::SimMatch::PassConfig;
using ElyverseFootball::SimMatch::Pitch;
using ElyverseFootball::SimMatch::PlayerMatchState;
using ElyverseFootball::SimMatch::ReceptionRecord;
using ElyverseFootball::SimMatch::ShotConditions;
using ElyverseFootball::SimMatch::shotConditions;
using ElyverseFootball::SimMatch::ShotConfig;
using ElyverseFootball::SimMatch::shotErrorFactor;
using ElyverseFootball::SimMatch::ShotIntent;
using ElyverseFootball::SimMatch::ShotStrike;
using ElyverseFootball::SimMatch::skillErrorFactor;
using ElyverseFootball::SimMatch::TeamSide;

namespace {

constexpr double kSecondsPerTick = 1.0 / 30.0;
constexpr Vec2 kFrom{.x = 48.0, .y = 20.0};

// Execution without error, so a test can predict the ball exactly.
[[nodiscard]] ShotConfig exact() {
  ShotConfig config;
  config.spreadAtZero = 0.0;
  config.spreadPerMeter = 0.0;
  config.speedError = 0.0;
  config.spinError = 0.0;
  return config;
}

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

[[nodiscard]] ShotIntent shotAt(const Vec2 target, const double height = 1.0) {
  return {.shooter = PlayerId(1), .target = target, .height = height, .speed = 25.0};
}

// Who strikes a shot and how: the seed of his errors, what widens them, and
// the shooter himself.
struct Striker {
  std::uint64_t seed = 1;
  double errorFactor = 1.0;
  PlayerMatchState shooter = playerAt(1, TeamSide::kHome, kFrom);
};

[[nodiscard]] ShotStrike strikeOf(const ShotIntent& intent, const ShotConfig& config,
                                  const Striker& striker = {}) {
  RandomNumberGenerator random(striker.seed);
  return executeShot(intent, kFrom, striker.shooter, striker.errorFactor, config, BallPhysics{},
                     random);
}

// The struck ball when it has come as far along the ground as its target.
[[nodiscard]] BallState atTarget(const ShotStrike& strike) {
  BallState ball{.position = kFrom,
                 .velocity = strike.velocity,
                 .owner = std::nullopt,
                 .lastTouch = std::nullopt};
  ball.verticalVelocity = strike.verticalVelocity;
  double low = 0.0;
  double high = 5.0;
  const double way = (strike.target - kFrom).length();
  for (int step = 0; step < 64; ++step) {
    const double middle = (low + high) / 2.0;
    ((ballAfter(ball, BallPhysics{}, middle).position - kFrom).length() < way ? low : high) =
        middle;
  }
  return ballAfter(ball, BallPhysics{}, high);
}

// Home 1 on the ball, away 2 where the test puts him.
[[nodiscard]] MatchState duel(const PlayerMatchState& shooter, const Vec2 opponent) {
  auto state = MatchState::create(
      {.pitch = Pitch(60.0, 40.0),
       .players = {shooter, playerAt(2, TeamSide::kAway, opponent)},
       .ball = {.position = kFrom, .velocity = {}, .owner = PlayerId(1), .lastTouch = std::nullopt},
       .playersPerSide = 1});
  REQUIRE(state.has_value());
  return *std::move(state);
}

[[nodiscard]] ShotConditions conditionsOf(const MatchState& state, const ShotIntent& intent,
                                          const SimTick now = SimTick(0)) {
  return shotConditions(state, 0, kFrom, intent, now, kSecondsPerTick, ShotConfig{}, PassConfig{});
}

}  // namespace

TEST_CASE("An exact shot arrives at the aimed point and height", "[shooting]") {
  for (const double height : {0.1, 0.4, 1.0, 2.2}) {
    CAPTURE(height);
    const ShotIntent intent = shotAt({.x = 60.0, .y = 21.5}, height);
    const ShotStrike strike = strikeOf(intent, exact());

    REQUIRE(strike.target == intent.target);
    REQUIRE(strike.height == height);
    REQUIRE_THAT(strike.velocity.length(), WithinAbs(25.0, 1e-12));
    REQUIRE(strike.verticalVelocity > 0.0);
    const BallState arrived = atTarget(strike);
    REQUIRE_THAT(arrived.position.x, WithinAbs(60.0, 1e-9));
    REQUIRE_THAT(arrived.position.y, WithinAbs(21.5, 1e-9));
    REQUIRE_THAT(arrived.height, WithinAbs(height, 1e-9));
  }
}

TEST_CASE("A shot aimed at the grass or below it rolls", "[shooting]") {
  for (const double height : {0.0, -0.5}) {
    const ShotStrike strike = strikeOf(shotAt({.x = 60.0, .y = 21.5}, height), exact());

    REQUIRE(strike.height == 0.0);
    REQUIRE(strike.verticalVelocity == 0.0);
    REQUIRE_THAT(strike.velocity.length(), WithinAbs(25.0, 1e-12));
  }
}

TEST_CASE("A clean strike carries the topspin of the shooter's technique", "[shooting]") {
  PlayerMatchState shooter = playerAt(1, TeamSide::kHome, kFrom);
  shooter.attributes.shotTechnique = 1.0;
  REQUIRE(strikeOf(shotAt({.x = 60.0, .y = 20.0}), exact(), {.shooter = shooter}).spin ==
          ShotConfig{}.topspin);
  shooter.attributes.shotTechnique = 0.0;
  REQUIRE(strikeOf(shotAt({.x = 60.0, .y = 20.0}), exact(), {.shooter = shooter}).spin == 0.0);
}

TEST_CASE("A shot leaves the foot no steeper than the launch slope", "[shooting]") {
  // Two meters up from two meters away is steeper than 45 degrees.
  ShotConfig config = exact();
  const ShotIntent intent{.shooter = PlayerId(1),
                          .target = kFrom + Vec2{.x = 2.0, .y = 0.0},
                          .height = 20.0,
                          .speed = 25.0};
  REQUIRE(strikeOf(intent, config).verticalVelocity == 25.0);
  config.maxLaunchSlope = 0.5;
  REQUIRE(strikeOf(intent, config).verticalVelocity == 12.5);
}

TEST_CASE("A shot aimed at the ball itself leaves along the shooter's facing", "[shooting]") {
  const ShotStrike strike = strikeOf(shotAt(kFrom, 0.0), exact());
  REQUIRE(strike.velocity == Vec2{.x = 25.0, .y = 0.0});
}

TEST_CASE("Execution error stays within its spread and uses six draws", "[shooting]") {
  const ShotConfig config;
  const ShotIntent intent = shotAt({.x = 60.0, .y = 20.0});
  // Twelve meters out: 0.2 + 0.04 * 12.
  constexpr double kSpread = 0.68;
  double widest = 0.0;
  double highest = 0.0;
  for (std::uint64_t seed = 1; seed <= 500; ++seed) {
    RandomNumberGenerator random(seed);
    const ShotStrike strike = executeShot(intent, kFrom, playerAt(1, TeamSide::kHome, kFrom), 1.0,
                                          config, BallPhysics{}, random);
    widest = std::max(widest, std::abs(strike.target.y - 20.0));
    highest = std::max(highest, std::abs(strike.height - 1.0));
    REQUIRE(strike.target.x == 60.0);
    REQUIRE(strike.height >= 0.0);
    REQUIRE(std::abs(strike.velocity.length() - 25.0) <= 25.0 * config.speedError);
    REQUIRE(std::abs(strike.spin - (config.topspin * 0.5)) <= config.spinError);

    RandomNumberGenerator reference(seed);
    for (int draw = 0; draw < 6; ++draw) {
      static_cast<void>(reference.nextUniform());
    }
    REQUIRE(random.nextUniform() == reference.nextUniform());
  }
  REQUIRE(widest <= kSpread);
  REQUIRE(widest > 0.5 * kSpread);
  REQUIRE(highest <= kSpread);
  REQUIRE(highest > 0.5 * kSpread);
}

TEST_CASE("Accuracy scatters the aim, technique the lift, the pace and the spin", "[shooting]") {
  const ShotIntent intent = shotAt({.x = 60.0, .y = 20.0});
  const auto strikeWith = [&](const double accuracy, const double technique) {
    PlayerMatchState shooter = playerAt(1, TeamSide::kHome, kFrom);
    shooter.attributes.shotAccuracy = accuracy;
    shooter.attributes.shotTechnique = technique;
    return strikeOf(intent, ShotConfig{}, {.seed = 7, .shooter = shooter});
  };
  const ShotStrike average = strikeWith(0.5, 0.5);
  const ShotStrike pinpoint = strikeWith(1.0, 0.5);
  const ShotStrike clean = strikeWith(0.5, 1.0);

  const auto across = [](const ShotStrike& strike) { return std::abs(strike.target.y - 20.0); };
  const auto upward = [](const ShotStrike& strike) { return std::abs(strike.height - 1.0); };
  const auto pace = [](const ShotStrike& strike) {
    return std::abs(strike.velocity.length() - 25.0);
  };
  REQUIRE_THAT(across(pinpoint), WithinAbs(0.1 * across(average), 1e-12));
  REQUIRE_THAT(upward(pinpoint), WithinAbs(0.1 * upward(average), 1e-12));
  REQUIRE_THAT(pace(pinpoint), WithinAbs(pace(average), 1e-12));

  REQUIRE_THAT(across(clean), WithinAbs(across(average), 1e-12));
  REQUIRE_THAT(upward(clean), WithinAbs(0.1 * upward(average), 1e-12));
  REQUIRE_THAT(pace(clean), WithinAbs(0.1 * pace(average), 1e-9));
}

TEST_CASE("The error factor widens every error of a strike", "[shooting]") {
  const ShotIntent intent = shotAt({.x = 60.0, .y = 20.0});
  const ShotStrike calm = strikeOf(intent, ShotConfig{}, {.seed = 11});
  const ShotStrike spoiled = strikeOf(intent, ShotConfig{}, {.seed = 11, .errorFactor = 2.0});

  REQUIRE_THAT(spoiled.target.y - 20.0, WithinAbs(2.0 * (calm.target.y - 20.0), 1e-12));
  REQUIRE_THAT(spoiled.velocity.length() - 25.0,
               WithinAbs(2.0 * (calm.velocity.length() - 25.0), 1e-9));
  REQUIRE_THAT(spoiled.spin - 15.0, WithinAbs(2.0 * (calm.spin - 15.0), 1e-9));
}

TEST_CASE("However badly struck, a shot keeps half its pace", "[shooting]") {
  for (std::uint64_t seed = 1; seed <= 100; ++seed) {
    const double speed = strikeOf(shotAt({.x = 60.0, .y = 20.0}), ShotConfig{},
                                  {.seed = seed, .errorFactor = 1000.0})
                             .velocity.length();
    REQUIRE(speed >= 12.5);
    REQUIRE(speed <= 37.5);
  }
}

TEST_CASE("A skill sets how wide an error is", "[shooting]") {
  REQUIRE_THAT(skillErrorFactor(1.0), WithinAbs(0.1, 1e-12));
  REQUIRE_THAT(skillErrorFactor(0.5), WithinAbs(1.0, 1e-12));
  REQUIRE_THAT(skillErrorFactor(0.0), WithinAbs(1.9, 1e-12));
}

TEST_CASE("A shot in the clear, standing, on the strong foot has no conditions", "[shooting]") {
  const MatchState state = duel(playerAt(1, TeamSide::kHome, kFrom), {.x = 10.0, .y = 10.0});
  const ShotConditions conditions = conditionsOf(state, shotAt({.x = 60.0, .y = 20.0}));

  REQUIRE(conditions == ShotConditions{});
  REQUIRE(shotErrorFactor(conditions, state.players()[0].attributes, ShotConfig{}) == 1.0);
}

TEST_CASE("The nearest opponent puts a shooter under the pressure of a passer", "[shooting]") {
  const MatchState state =
      duel(playerAt(1, TeamSide::kHome, kFrom), kFrom + Vec2{.x = 0.0, .y = 1.5});
  const ShotConditions conditions = conditionsOf(state, shotAt({.x = 60.0, .y = 20.0}));

  REQUIRE_THAT(conditions.pressure, WithinAbs(0.5, 1e-12));
  REQUIRE_THAT(shotErrorFactor(conditions, {}, ShotConfig{}), WithinAbs(1.5, 1e-12));
}

TEST_CASE("Running across or away from his shot unbalances a shooter", "[shooting]") {
  const ShotIntent intent = shotAt({.x = 60.0, .y = 20.0});
  const auto imbalanceAt = [&](const Vec2 velocity) {
    PlayerMatchState shooter = playerAt(1, TeamSide::kHome, kFrom);
    shooter.velocity = velocity;
    return conditionsOf(duel(shooter, {.x = 10.0, .y = 10.0}), intent).imbalance;
  };
  // Default top speed 7.5 m/s.
  REQUIRE(imbalanceAt({.x = 7.5, .y = 0.0}) == 0.0);
  REQUIRE_THAT(imbalanceAt({.x = 0.0, .y = 3.75}), WithinAbs(0.5, 1e-12));
  REQUIRE_THAT(imbalanceAt({.x = 3.0, .y = 3.75}), WithinAbs(0.5, 1e-12));
  REQUIRE_THAT(imbalanceAt({.x = -7.5, .y = 0.0}), WithinAbs(1.0, 1e-12));
}

TEST_CASE("A target on the side of the weaker foot is struck with it", "[shooting]") {
  const auto weakFootFor = [](const Foot strongFoot, const double targetY) {
    PlayerMatchState shooter = playerAt(1, TeamSide::kHome, kFrom);
    shooter.attributes.strongFoot = strongFoot;
    return conditionsOf(duel(shooter, {.x = 10.0, .y = 10.0}), shotAt({.x = 60.0, .y = targetY}))
        .weakFoot;
  };
  // Facing +x, left is the side of growing y.
  REQUIRE(weakFootFor(Foot::kRight, 32.0));
  REQUIRE_FALSE(weakFootFor(Foot::kRight, 20.0));
  REQUIRE_FALSE(weakFootFor(Foot::kRight, 8.0));
  REQUIRE(weakFootFor(Foot::kLeft, 8.0));
  REQUIRE_FALSE(weakFootFor(Foot::kLeft, 21.0));
  REQUIRE_FALSE(weakFootFor(Foot::kLeft, 32.0));
}

TEST_CASE("The weaker foot costs what it lacks in accuracy", "[shooting]") {
  const ShotConditions weak{.pressure = 0.0, .imbalance = 0.0, .weakFoot = true, .unsettled = 0.0};
  REQUIRE(shotErrorFactor(weak, {.weakFootAccuracy = 1.0}, ShotConfig{}) == 1.0);
  REQUIRE(shotErrorFactor(weak, {.weakFootAccuracy = 0.5}, ShotConfig{}) == 1.5);
  REQUIRE(shotErrorFactor(weak, {.weakFootAccuracy = 0.0}, ShotConfig{}) == 2.0);
}

TEST_CASE("A ball only just received is unsettled, the harder it came the more", "[shooting]") {
  // Writes the shooter's reception of a ball at this speed in tick 0, and his
  // last touch of the ball in this tick.
  const auto received = [](const double ballSpeed, const SimTick lastTouch = SimTick(0)) {
    MatchSimulation simulation(
        {.initialState = duel(playerAt(1, TeamSide::kHome, kFrom), {.x = 10.0, .y = 10.0}),
         .seed = 1,
         .ticksPerSecond = 30,
         .systems =
             {{.name = "reception",
               .update =
                   [ballSpeed, lastTouch](const MatchStepContext& context,
                                          const MatchState& /*state*/, MatchStateWriter& next) {
                     if (context.tick() == SimTick(0)) {
                       next.setBallLastTouch(BallTouch{.playerId = PlayerId(1), .tick = lastTouch});
                       next.setLastReception(ReceptionRecord{
                           .player = PlayerId(1), .tick = SimTick(0), .ballSpeed = ballSpeed});
                     }
                   }}},
         .commands = {}});
    REQUIRE(simulation.step().has_value());
    return simulation.state();
  };
  const ShotIntent intent = shotAt({.x = 60.0, .y = 20.0});

  REQUIRE(conditionsOf(received(20.0), intent, SimTick(0)).unsettled == 1.0);
  REQUIRE(conditionsOf(received(40.0), intent, SimTick(0)).unsettled == 1.0);
  REQUIRE_THAT(conditionsOf(received(10.0), intent, SimTick(0)).unsettled, WithinAbs(0.5, 1e-12));
  REQUIRE_THAT(conditionsOf(received(20.0), intent, SimTick(15)).unsettled, WithinAbs(0.5, 1e-12));
  REQUIRE(conditionsOf(received(20.0), intent, SimTick(30)).unsettled == 0.0);
  // A ball he lost and won back since is not the one he received.
  REQUIRE(conditionsOf(received(20.0, SimTick(10)), intent, SimTick(15)).unsettled == 0.0);
}

TEST_CASE("The conditions of a shot multiply", "[shooting]") {
  const ShotConditions all{.pressure = 1.0, .imbalance = 1.0, .weakFoot = true, .unsettled = 1.0};
  REQUIRE(shotErrorFactor(all, {.weakFootAccuracy = 0.0}, ShotConfig{}) == 16.0);
}

TEST_CASE("A deflected shot keeps a share of its speed, off its line and lifted", "[shooting]") {
  const ShotConfig config;
  BallState ball{.position = {.x = 55.0, .y = 20.0},
                 .velocity = {.x = 20.0, .y = 0.0},
                 .owner = std::nullopt,
                 .lastTouch = std::nullopt};
  ball.height = 0.4;
  ball.verticalVelocity = 2.0;
  ball.spin = 15.0;
  bool blocked = false;
  bool flewOn = false;
  for (std::uint64_t seed = 1; seed <= 200; ++seed) {
    RandomNumberGenerator random(seed);
    const Deflection deflection = deflectShot(ball, config, random);
    const double kept = deflection.ball.velocity.length() / 20.0;

    REQUIRE(deflection.ball.position == ball.position);
    REQUIRE(deflection.ball.height == ball.height);
    REQUIRE(deflection.ball.spin == 0.0);
    REQUIRE(kept >= config.minDeflectedSpeed - 1e-12);
    REQUIRE(kept <= config.maxDeflectedSpeed + 1e-12);
    REQUIRE(deflection.blocked == (kept < config.blockedBelow));
    // Never back the way it came: at most 45 degrees off its line.
    REQUIRE(deflection.ball.velocity.x >= std::abs(deflection.ball.velocity.y));
    REQUIRE(deflection.ball.verticalVelocity >= 2.0 * config.minDeflectedSpeed);
    REQUIRE(deflection.ball.verticalVelocity <=
            (2.0 * config.maxDeflectedSpeed) + config.deflectionLift);
    blocked = blocked || deflection.blocked;
    flewOn = flewOn || !deflection.blocked;

    RandomNumberGenerator reference(seed);
    for (int draw = 0; draw < 3; ++draw) {
      static_cast<void>(reference.nextUniform());
    }
    REQUIRE(random.nextUniform() == reference.nextUniform());
  }
  REQUIRE(blocked);
  REQUIRE(flewOn);
}

TEST_CASE("An invalid shooting configuration is rejected", "[shooting]") {
  REQUIRE_NOTHROW(validate(ShotConfig{}));
  REQUIRE_NOTHROW(validate(exact()));

  const auto rejects = [](const auto& change) {
    ShotConfig config;
    change(config);
    REQUIRE_THROWS_AS(validate(config), std::invalid_argument);
  };
  rejects([](ShotConfig& config) { config.spreadAtZero = -0.1; });
  rejects([](ShotConfig& config) { config.spreadPerMeter = std::nan(""); });
  rejects([](ShotConfig& config) { config.maxLaunchSlope = 0.0; });
  rejects([](ShotConfig& config) { config.pressureErrorFactor = -1.0; });
  rejects([](ShotConfig& config) { config.unsettledSeconds = 0.0; });
  rejects([](ShotConfig& config) { config.unsettledBallSpeed = 0.0; });
  rejects([](ShotConfig& config) { config.blockRadius = 0.0; });
  rejects([](ShotConfig& config) { config.minDeflectedSpeed = 0.95; });
  rejects([](ShotConfig& config) { config.maxDeflectedSpeed = 1.5; });
  rejects([](ShotConfig& config) { config.blockedBelow = -0.1; });
  rejects([](ShotConfig& config) { config.deflectionLift = -1.0; });
}
