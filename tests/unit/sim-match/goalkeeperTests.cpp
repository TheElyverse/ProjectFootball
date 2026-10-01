#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

#include "ballPhysics.hpp"
#include "desiredRegion.hpp"
#include "goalkeeper.hpp"
#include "kickoffScenario.hpp"
#include "matchState.hpp"
#include "pitch.hpp"
#include "random.hpp"
#include "reception.hpp"
#include "referenceTactic.hpp"
#include "tactic.hpp"
#include "tacticalPhase.hpp"

using Catch::Matchers::WithinAbs;
using ElyverseFootball::SimCore::PlayerId;
using ElyverseFootball::SimCore::RandomNumberGenerator;
using ElyverseFootball::SimCore::SimTick;
using ElyverseFootball::SimCore::Vec2;
using ElyverseFootball::SimMatch::BallPhysics;
using ElyverseFootball::SimMatch::BallState;
using ElyverseFootball::SimMatch::BallStep;
using ElyverseFootball::SimMatch::callSweep;
using ElyverseFootball::SimMatch::defensiveLineDepth;
using ElyverseFootball::SimMatch::drawMisjudgement;
using ElyverseFootball::SimMatch::findBallClaim;
using ElyverseFootball::SimMatch::GoalEnd;
using ElyverseFootball::SimMatch::GoalkeeperConfig;
using ElyverseFootball::SimMatch::goalkeeperRegion;
using ElyverseFootball::SimMatch::goalkeeperTarget;
using ElyverseFootball::SimMatch::makeSevenASideKickoff;
using ElyverseFootball::SimMatch::MatchState;
using ElyverseFootball::SimMatch::Pitch;
using ElyverseFootball::SimMatch::PlayerMatchState;
using ElyverseFootball::SimMatch::ReceptionConfig;
using ElyverseFootball::SimMatch::sweepThreshold;
using ElyverseFootball::SimMatch::validate;
using ElyverseFootball::SimTactics::referenceTacticSpec;
using ElyverseFootball::SimTactics::Tactic;
using ElyverseFootball::SimTactics::TacticalPhase;

namespace {

constexpr TacticalPhase kPhase = TacticalPhase::kDefensiveBlock;
constexpr double kShotRange = 25.0;
constexpr double kSecondsPerTick = 1.0 / 30.0;
// Home's keeper is player 1 at index 0 and defends the goal at x = 0.
constexpr std::size_t kKeeper = 0;

// The sandbox kickoff with the reference tactic on both sides, the ball at
// `ball` -- owned by `owner` if given -- and home's keeper at `keeper`.
[[nodiscard]] MatchState scene(const Vec2 ball, const std::optional<PlayerId> owner = std::nullopt,
                               const Vec2 keeper = {.x = 2.0, .y = 20.0},
                               const double keeperPositioning = 0.5) {
  auto tactic = Tactic::create(referenceTacticSpec());
  REQUIRE(tactic.has_value());
  const auto fixture =
      makeSevenASideKickoff(Pitch(60.0, 40.0), {}, {.home = *tactic, .away = *tactic});
  REQUIRE(fixture.has_value());
  std::vector<PlayerMatchState> players(fixture->players().begin(), fixture->players().end());
  players.at(kKeeper).position = keeper;
  players.at(kKeeper).attributes.keeperPositioning = keeperPositioning;
  auto state = MatchState::create(
      {.pitch = fixture->pitch(),
       .players = std::move(players),
       .ball = {.position = ball, .velocity = {}, .owner = owner, .lastTouch = std::nullopt},
       .playersPerSide = fixture->playersPerSide()},
      fixture->tactics());
  REQUIRE(state.has_value());
  return *std::move(state);
}

[[nodiscard]] Vec2 target(const MatchState& state, const GoalkeeperConfig& config = {}) {
  return goalkeeperTarget(state, kKeeper, kPhase, config, kShotRange);
}

// How far a point lies from the line through `start` and `end`.
[[nodiscard]] double distanceToLine(const Vec2 point, const Vec2 start, const Vec2 end) {
  const Vec2 along = end - start;
  const Vec2 offset = point - start;
  return std::abs((along.x * offset.y) - (along.y * offset.x)) / along.length();
}

// The cosines of the angles at the ball between the keeper and either post:
// equal on the bisector.
[[nodiscard]] std::pair<double, double> anglesToPosts(const MatchState& state, const Vec2 keeper) {
  const auto goal = state.pitch().goal(GoalEnd::kMinX);
  const Vec2 ball = state.ball().position;
  const auto unit = [](const Vec2 vector) { return vector * (1.0 / vector.length()); };
  const Vec2 toKeeper = unit(keeper - ball);
  return {toKeeper.dot(unit(goal.postAtMinY() - ball)),
          toKeeper.dot(unit(goal.postAtMaxY() - ball))};
}

}  // namespace

TEST_CASE("The keeper stands on the bisector of the angle the posts make", "[goalkeeper]") {
  for (const Vec2 ball : {Vec2{.x = 30.0, .y = 20.0}, Vec2{.x = 25.0, .y = 6.0},
                          Vec2{.x = 20.0, .y = 34.0}, Vec2{.x = 14.0, .y = 12.0}}) {
    CAPTURE(ball.x, ball.y);
    const MatchState state = scene(ball);
    const Vec2 keeper = target(state);
    const auto [low, high] = anglesToPosts(state, keeper);
    REQUIRE_THAT(low, WithinAbs(high, 1e-9));
    // Off his line, toward the ball's side of the goal.
    REQUIRE(keeper.x > 0.0);
    REQUIRE((keeper.y - 20.0) * (ball.y - 20.0) >= 0.0);
  }
}

TEST_CASE("Far from the ball the keeper stands at a share of his defensive line", "[goalkeeper]") {
  const MatchState state = scene({.x = 45.0, .y = 20.0});
  const GoalkeeperConfig config;
  const double line =
      defensiveLineDepth(state, ElyverseFootball::SimMatch::TeamSide::kHome, kPhase);
  const double area = state.pitch().penaltyArea(GoalEnd::kMinX).max.x;
  REQUIRE(45.0 > line);
  REQUIRE_THAT(target(state).x, WithinAbs(std::min(config.highDepthShare * line, area), 1e-9));
}

TEST_CASE("The closer the ball, the nearer his line the keeper stands", "[goalkeeper]") {
  double previous = std::numeric_limits<double>::infinity();
  for (const double x : {40.0, 30.0, 20.0, 12.0, 6.0}) {
    CAPTURE(x);
    const double depth = target(scene({.x = x, .y = 20.0})).x;
    REQUIRE(depth <= previous);
    REQUIRE(depth >= GoalkeeperConfig{}.lineDepth);
    previous = depth;
  }
}

TEST_CASE("Against an opponent on the ball within shooting range he is on his line",
          "[goalkeeper]") {
  // Away's striker, player 14, on the ball 20 m out.
  const Vec2 ball{.x = 20.0, .y = 20.0};
  REQUIRE_THAT(target(scene(ball, PlayerId(14))).x, WithinAbs(GoalkeeperConfig{}.lineDepth, 1e-9));
  // Loose, the same ball leaves him higher.
  REQUIRE(target(scene(ball)).x > GoalkeeperConfig{}.lineDepth);
}

TEST_CASE("With his side on the ball the keeper stays behind his defenders", "[goalkeeper]") {
  // Home's centre back, player 2, on the ball in the middle of the pitch.
  const Vec2 ball{.x = 40.0, .y = 20.0};
  const double without = target(scene(ball)).x;
  const double with = target(scene(ball, PlayerId(2))).x;
  REQUIRE_THAT(with, WithinAbs(std::max(GoalkeeperConfig{}.lineDepth,
                                        without * GoalkeeperConfig{}.possessionDepthShare),
                               1e-9));
}

TEST_CASE("Out wide near his line the keeper guards the near post", "[goalkeeper]") {
  const MatchState state = scene({.x = 1.0, .y = 2.0}, PlayerId(14));
  const Vec2 keeper = target(state);
  const auto goal = state.pitch().goal(GoalEnd::kMinX);
  REQUIRE(keeper.y < 20.0);
  REQUIRE(keeper.y >= goal.postAtMinY().y - keeper.x);
  REQUIRE(keeper.x <= GoalkeeperConfig{}.lineDepth);
}

TEST_CASE("A keeper takes up his place as exactly as his positioning allows", "[goalkeeper]") {
  const Vec2 ball{.x = 30.0, .y = 10.0};
  const GoalkeeperConfig config;
  SECTION("a perfect keeper stands on his target") {
    RandomNumberGenerator random(7);
    const auto region = goalkeeperRegion(scene(ball, std::nullopt, {}, 1.0), kKeeper, kPhase,
                                         config, kShotRange, random);
    REQUIRE(region.center == region.tacticalTarget);
  }
  SECTION("a poor one is off it, never by more than the spreads") {
    bool off = false;
    for (std::uint64_t seed = 1; seed <= 50; ++seed) {
      RandomNumberGenerator random(seed);
      const MatchState state = scene(ball, std::nullopt, {}, 0.0);
      const auto region = goalkeeperRegion(state, kKeeper, kPhase, config, kShotRange, random);
      const double miss = (region.center - region.tacticalTarget).length();
      REQUIRE(miss <= std::hypot(config.positionErrorAlong, config.positionErrorAcross) + 1e-9);
      off = off || miss > 0.1;
      // Across his line to the ball the miss is the smaller spread.
      REQUIRE(distanceToLine(region.center, state.pitch().goal(GoalEnd::kMinX).center, ball) <=
              config.positionErrorAcross + 1e-9);
    }
    REQUIRE(off);
  }
  SECTION("the same draws give the same place") {
    RandomNumberGenerator first(3);
    RandomNumberGenerator second(3);
    const MatchState state = scene(ball, std::nullopt, {}, 0.3);
    REQUIRE(goalkeeperRegion(state, kKeeper, kPhase, config, kShotRange, first) ==
            goalkeeperRegion(state, kKeeper, kPhase, config, kShotRange, second));
  }
}

TEST_CASE("The sweeping dial sets the head start the keeper needs", "[goalkeeper]") {
  const GoalkeeperConfig config;
  REQUIRE_THAT(sweepThreshold(0.0, config), WithinAbs(config.cautiousMargin, 1e-12));
  REQUIRE_THAT(sweepThreshold(1.0, config), WithinAbs(config.boldMargin, 1e-12));
  REQUIRE_THAT(sweepThreshold(0.5, config),
               WithinAbs((config.cautiousMargin + config.boldMargin) / 2.0, 1e-12));
}

TEST_CASE("The keeper comes when his head start reaches the threshold", "[goalkeeper]") {
  const GoalkeeperConfig config;
  // At sweeping 0.5 he wants 0.15 s.
  REQUIRE(callSweep(1.0, 1.2, 0.0, 0.5, false, config).coming);
  REQUIRE_FALSE(callSweep(1.0, 1.1, 0.0, 0.5, false, config).coming);
  // A bolder keeper comes even a little behind the attacker.
  REQUIRE(callSweep(1.0, 0.9, 0.0, 1.0, false, config).coming);
  // Misjudging the ball can send him or keep him home.
  REQUIRE(callSweep(1.0, 1.1, 0.1, 0.5, false, config).coming);
  REQUIRE_FALSE(callSweep(1.0, 1.2, -0.1, 0.5, false, config).coming);
  // Once out, he only turns back when his margin falls clearly short.
  REQUIRE(callSweep(1.0, 1.1, 0.0, 0.5, true, config).coming);
  REQUIRE_FALSE(callSweep(1.0, 1.0, 0.0, 0.5, true, config).coming);
  // No attacker can get there: he always comes.
  const auto alone = callSweep(3.0, std::nullopt, 0.0, 0.0, false, config);
  REQUIRE(alone.coming);
  REQUIRE_FALSE(alone.attackerSeconds.has_value());
}

TEST_CASE("A weaker keeper misjudges close balls more often", "[goalkeeper]") {
  const GoalkeeperConfig config;
  // Balls a tenth of a second inside and outside his threshold at sweeping
  // 0.5: the right call is to come for the first and stay for the second.
  const auto wrongCalls = [&config](const double anticipation) {
    int wrong = 0;
    for (std::uint64_t seed = 1; seed <= 1000; ++seed) {
      RandomNumberGenerator random(seed);
      const double misjudgement = drawMisjudgement(anticipation, config, random);
      wrong += callSweep(1.0, 1.25, misjudgement, 0.5, false, config).coming ? 0 : 1;
      wrong += callSweep(1.0, 1.05, misjudgement, 0.5, false, config).coming ? 1 : 0;
    }
    return wrong;
  };
  const int perfect = wrongCalls(1.0);
  const int strong = wrongCalls(0.8);
  const int weak = wrongCalls(0.2);
  CAPTURE(perfect, strong, weak);
  REQUIRE(perfect == 0);
  REQUIRE(strong > 0);
  REQUIRE(weak > 2 * strong);
}

TEST_CASE("A perfect judge does not misjudge", "[goalkeeper]") {
  RandomNumberGenerator random(11);
  for (int draw = 0; draw < 20; ++draw) {
    const double misjudgement = drawMisjudgement(1.0, GoalkeeperConfig{}, random);
    REQUIRE(misjudgement == 0.0);
    REQUIRE_FALSE(std::signbit(misjudgement));
  }
}

TEST_CASE("In his penalty area the keeper takes the ball with his hands", "[goalkeeper]") {
  const ReceptionConfig config;
  // A ball 1.5 m up, passing 1.1 m from him: above his feet, within his hands.
  const auto claimant = [&config](const Vec2 keeper, const Vec2 from) {
    MatchState state = scene(from, std::nullopt, keeper);
    BallState ball = state.ball();
    ball.velocity = {.x = 0.0, .y = 6.0};
    ball.height = 1.5;
    const BallStep step{.ball = {.position = from + Vec2{.x = 0.0, .y = 0.2},
                                 .velocity = ball.velocity,
                                 .owner = std::nullopt,
                                 .lastTouch = std::nullopt,
                                 .height = 1.5},
                        .seconds = kSecondsPerTick};
    const auto claim = findBallClaim(state, ball, step, BallPhysics{.gravity = 0.0}, SimTick(0),
                                     kSecondsPerTick, config);
    return claim ? std::optional(claim->playerId) : std::nullopt;
  };
  REQUIRE(config.controlHeight < 1.5);
  REQUIRE(config.handsHeight > 1.5);
  // Inside his area, the ball inside it too.
  REQUIRE(claimant({.x = 5.0, .y = 20.0}, {.x = 6.1, .y = 20.0}) == PlayerId(1));
  // Out of it, he is an outfield player.
  REQUIRE_FALSE(claimant({.x = 15.0, .y = 20.0}, {.x = 16.1, .y = 20.0}).has_value());
}

TEST_CASE("Goalkeeper configurations are validated", "[goalkeeper]") {
  REQUIRE_NOTHROW(validate(GoalkeeperConfig{}));
  const double nan = std::numeric_limits<double>::quiet_NaN();
  for (const auto& broken :
       std::vector<GoalkeeperConfig>{{.lineDepth = -1.0},
                                     {.highDepthShare = 1.5},
                                     {.possessionDepthShare = -0.1},
                                     {.positionErrorAlong = nan},
                                     {.cautiousMargin = std::numeric_limits<double>::infinity()},
                                     {.sweepHysteresis = -0.1},
                                     {.misjudgement = -1.0}}) {
    REQUIRE_THROWS_AS(validate(broken), std::invalid_argument);
  }
}
