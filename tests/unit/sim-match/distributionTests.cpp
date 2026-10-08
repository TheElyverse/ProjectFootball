#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

#include "ballPhysics.hpp"
#include "distribution.hpp"
#include "matchSimulation.hpp"
#include "matchState.hpp"
#include "passCandidates.hpp"
#include "perception.hpp"
#include "vec2.hpp"

using Catch::Matchers::WithinAbs;
using ElyverseFootball::SimCore::PlayerId;
using ElyverseFootball::SimCore::SimTick;
using ElyverseFootball::SimCore::Vec2;
using ElyverseFootball::SimMatch::BallLanding;
using ElyverseFootball::SimMatch::BallPhysics;
using ElyverseFootball::SimMatch::BallState;
using ElyverseFootball::SimMatch::directnessBias;
using ElyverseFootball::SimMatch::DistributionConfig;
using ElyverseFootball::SimMatch::dominantContribution;
using ElyverseFootball::SimMatch::flightSpeed;
using ElyverseFootball::SimMatch::generateLongBallCandidates;
using ElyverseFootball::SimMatch::launchVerticalVelocity;
using ElyverseFootball::SimMatch::makePerceptionSystem;
using ElyverseFootball::SimMatch::MatchSimulation;
using ElyverseFootball::SimMatch::MatchState;
using ElyverseFootball::SimMatch::PassCandidate;
using ElyverseFootball::SimMatch::PassCandidateRules;
using ElyverseFootball::SimMatch::passContributions;
using ElyverseFootball::SimMatch::PassRejection;
using ElyverseFootball::SimMatch::Pitch;
using ElyverseFootball::SimMatch::PlayerMatchState;
using ElyverseFootball::SimMatch::predictBallLanding;
using ElyverseFootball::SimMatch::TeamSide;

namespace {

constexpr double kSecondsPerTick = 1.0 / 30.0;

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

// Three a side. Home 1 has the ball at (5, 20) and looks up the pitch; home 2
// stands 10 m ahead of him, home 3 at `three`. Away stands at `away`.
[[nodiscard]] MatchState longBallScene(const Vec2 three, const std::array<Vec2, 3>& away) {
  std::vector<PlayerMatchState> players{playerAt(1, TeamSide::kHome, {.x = 5.0, .y = 20.0}),
                                        playerAt(2, TeamSide::kHome, {.x = 15.0, .y = 20.0}),
                                        playerAt(3, TeamSide::kHome, three)};
  std::uint32_t next = 4;
  for (const Vec2 position : away) {
    players.push_back(playerAt(next++, TeamSide::kAway, position));
  }
  auto state = MatchState::create({.pitch = Pitch(60.0, 40.0),
                                   .players = std::move(players),
                                   .ball = {.position = {.x = 5.5, .y = 20.0},
                                            .velocity = {},
                                            .owner = PlayerId(1),
                                            .lastTouch = std::nullopt},
                                   .playersPerSide = 3});
  REQUIRE(state.has_value());
  return *std::move(state);
}

// Far from everything.
constexpr std::array<Vec2, 3> kBystanders{
    {{.x = 58.0, .y = 3.0}, {.x = 58.0, .y = 37.0}, {.x = 59.0, .y = 20.0}}};

// The state after one perception update, so every player remembers what he
// sees.
[[nodiscard]] MatchState perceived(MatchState state) {
  MatchSimulation simulation({.initialState = std::move(state),
                              .seed = 1,
                              .ticksPerSecond = 30,
                              .systems = {makePerceptionSystem({})},
                              .commands = {}});
  REQUIRE(simulation.step().has_value());
  return simulation.state();
}

[[nodiscard]] std::vector<PassCandidate> longBallsOf(const MatchState& state,
                                                     const PassCandidateRules& rules = {},
                                                     const DistributionConfig& config = {}) {
  return generateLongBallCandidates(state, 0, SimTick(1), kSecondsPerTick, rules, config);
}

[[nodiscard]] PassCandidate longBallTo(const std::vector<PassCandidate>& candidates,
                                       const std::uint32_t receiver) {
  const auto found = std::ranges::find(candidates, PlayerId(receiver), &PassCandidate::receiver);
  REQUIRE(found != candidates.end());
  return found == candidates.end() ? PassCandidate{} : *found;
}

}  // namespace

TEST_CASE("A long ball comes down on its target after the flight time", "[distribution]") {
  const BallPhysics physics;
  for (const double distance : {25.0, 40.0, 50.0}) {
    CAPTURE(distance);
    const auto speed = flightSpeed(distance, 3.0, physics);
    REQUIRE(speed.has_value());
    const auto rise = launchVerticalVelocity(
        {.speed = speed.value_or(0.0), .distance = distance, .height = 0.0}, physics);
    REQUIRE(rise.has_value());
    const BallState ball{.position = {},
                         .velocity = {.x = speed.value_or(0.0), .y = 0.0},
                         .owner = std::nullopt,
                         .lastTouch = std::nullopt,
                         .height = 0.0,
                         .verticalVelocity = rise.value_or(0.0),
                         .spin = 0.0};
    const auto predicted = predictBallLanding(ball, physics);
    REQUIRE(predicted.has_value());
    const BallLanding landing = predicted.value_or(BallLanding{});
    REQUIRE_THAT(landing.position.x, WithinAbs(distance, 1e-6));
    REQUIRE_THAT(landing.seconds, WithinAbs(3.0, 1e-6));
    // High enough to come down on a head, not along the grass.
    REQUIRE(landing.apexHeight > 5.0);
  }
  REQUIRE_FALSE(flightSpeed(0.0, 3.0, physics).has_value());
  REQUIRE_FALSE(flightSpeed(30.0, 0.0, physics).has_value());
}

TEST_CASE("The directness dial biases long balls either way", "[distribution]") {
  const DistributionConfig config;
  REQUIRE(directnessBias(0.5, config) == 0.0);
  REQUIRE_THAT(directnessBias(1.0, config), WithinAbs(config.directnessWeight, 1e-12));
  REQUIRE_THAT(directnessBias(0.0, config), WithinAbs(-config.directnessWeight, 1e-12));
}

TEST_CASE("Long balls go to teammates far enough away and within a kick", "[distribution]") {
  // Home 2 is 10 m away, home 3 30 m: a long ball to 2 is too short.
  const auto candidates =
      longBallsOf(perceived(longBallScene({.x = 35.0, .y = 12.0}, kBystanders)));
  REQUIRE(candidates.size() == 2);
  REQUIRE(std::ranges::all_of(candidates, &PassCandidate::lofted));
  REQUIRE(longBallTo(candidates, 2).rejection == PassRejection::kTooClose);
  const PassCandidate toThree = longBallTo(candidates, 3);
  REQUIRE(toThree.isValid());
  REQUIRE(toThree.interceptionRisk == 0.0);
  REQUIRE(toThree.progression > 0.0);
  // Valid first.
  REQUIRE(candidates.front().receiver == PlayerId(3));

  // The same teammate on the far touchline, 54 m away, is beyond the
  // hardest lofted kick.
  const auto far = longBallsOf(perceived(longBallScene({.x = 58.0, .y = 32.0}, kBystanders)));
  REQUIRE(longBallTo(far, 3).rejection == PassRejection::kOutOfReach);
}

TEST_CASE("An opponent who gets under a long ball in time makes it a fifty-fifty",
          "[distribution]") {
  // Away's 4 stands a meter from home 3: both are there long before the ball.
  const auto contested = longBallsOf(perceived(longBallScene(
      {.x = 35.0, .y = 12.0}, {{{.x = 36.0, .y = 12.0}, kBystanders[1], kBystanders[2]}})));
  const PassCandidate toThree = longBallTo(contested, 3);
  REQUIRE_THAT(toThree.interceptionRisk, WithinAbs(0.5, 1e-9));
  REQUIRE_THAT(toThree.completion, WithinAbs(0.5 * toThree.receiverConfidence, 1e-9));

  // Twenty-three meters off, he gets there almost a second after the ball.
  const auto open = longBallsOf(perceived(longBallScene(
      {.x = 35.0, .y = 12.0}, {{{.x = 58.0, .y = 12.0}, kBystanders[1], kBystanders[2]}})));
  REQUIRE(longBallTo(open, 3).interceptionRisk < 0.1);
}

TEST_CASE("The lofted bias adds to a long ball's utility and names the reason", "[distribution]") {
  const MatchState state = perceived(longBallScene({.x = 35.0, .y = 12.0}, kBystanders));
  PassCandidateRules rules;
  const PassCandidate neutral = longBallTo(longBallsOf(state, rules), 3);
  rules.scoring.loftedBias = 5.0;
  const PassCandidate eager = longBallTo(longBallsOf(state, rules), 3);
  REQUIRE_THAT(eager.utility - neutral.utility, WithinAbs(5.0, 1e-9));
  const auto parts = passContributions(eager, rules.scoring);
  REQUIRE(parts.directness == 5.0);
  REQUIRE(parts.total() == eager.utility);
  REQUIRE(dominantContribution(parts) == "directness");
  // A ground pass gets none of it.
  PassCandidate ground = eager;
  ground.lofted = false;
  REQUIRE(passContributions(ground, rules.scoring).directness == 0.0);
}

TEST_CASE("The distribution configuration is validated", "[distribution]") {
  REQUIRE_NOTHROW(ElyverseFootball::SimMatch::validate(DistributionConfig{}));
  const auto rejected = [](auto change) {
    DistributionConfig config;
    change(config);
    REQUIRE_THROWS_AS(ElyverseFootball::SimMatch::validate(config), std::invalid_argument);
  };
  rejected([](DistributionConfig& config) { config.holdSeconds = -1.0; });
  rejected([](DistributionConfig& config) { config.throwRange = 0.0; });
  rejected([](DistributionConfig& config) { config.flightSeconds = 0.0; });
  rejected([](DistributionConfig& config) { config.contestMarginSeconds = 0.0; });
  rejected([](DistributionConfig& config) { config.directnessWeight = -0.1; });
  rejected([](DistributionConfig& config) { config.minLongDistance = std::nan(""); });
}
