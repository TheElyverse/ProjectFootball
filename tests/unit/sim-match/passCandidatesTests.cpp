#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "matchSimulation.hpp"
#include "matchState.hpp"
#include "passCandidates.hpp"
#include "perception.hpp"
#include "vec2.hpp"

using Catch::Matchers::WithinAbs;
using ElyverseFootball::SimCore::PlayerId;
using ElyverseFootball::SimCore::SimTick;
using ElyverseFootball::SimCore::Vec2;
using ElyverseFootball::SimMatch::attackingDirection;
using ElyverseFootball::SimMatch::generatePassCandidates;
using ElyverseFootball::SimMatch::makePerceptionSystem;
using ElyverseFootball::SimMatch::MatchSimulation;
using ElyverseFootball::SimMatch::MatchState;
using ElyverseFootball::SimMatch::MatchStateWriter;
using ElyverseFootball::SimMatch::MatchStepContext;
using ElyverseFootball::SimMatch::MatchSystem;
using ElyverseFootball::SimMatch::PassCandidate;
using ElyverseFootball::SimMatch::PassCandidateRules;
using ElyverseFootball::SimMatch::PassRejection;
using ElyverseFootball::SimMatch::passRejectionName;
using ElyverseFootball::SimMatch::Pitch;
using ElyverseFootball::SimMatch::PlayerMatchState;
using ElyverseFootball::SimMatch::TeamSide;

namespace {

constexpr double kSecondsPerTick = 1.0 / 30.0;

[[nodiscard]] PlayerMatchState playerAt(const std::uint32_t playerId, const TeamSide side,
                                        const Vec2 position,
                                        const Vec2 facing = {.x = 1.0, .y = 0.0}) {
  return {.playerId = PlayerId(playerId),
          .side = side,
          .position = position,
          .velocity = {},
          .attributes = {},
          .target = std::nullopt,
          .facing = facing};
}

// Three a side. Home 1 has the ball at (20, 20) and looks toward the away
// goal; home 2 and 3 stand 15 m ahead, one on each side.
[[nodiscard]] MatchState threeASide(const std::vector<PlayerMatchState>& away,
                                    const Vec2 carrierFacing = {.x = 1.0, .y = 0.0}) {
  std::vector<PlayerMatchState> players{
      playerAt(1, TeamSide::kHome, {.x = 20.0, .y = 20.0}, carrierFacing),
      playerAt(2, TeamSide::kHome, {.x = 35.0, .y = 12.0}),
      playerAt(3, TeamSide::kHome, {.x = 35.0, .y = 28.0})};
  players.insert(players.end(), away.begin(), away.end());
  auto state = MatchState::create({.pitch = Pitch(60.0, 40.0),
                                   .players = std::move(players),
                                   .ball = {.position = {.x = 20.5, .y = 20.0},
                                            .velocity = {},
                                            .owner = PlayerId(1),
                                            .lastTouch = std::nullopt},
                                   .playersPerSide = 3});
  REQUIRE(state.has_value());
  return *std::move(state);
}

// Far from everything.
[[nodiscard]] std::vector<PlayerMatchState> bystanders() {
  return {playerAt(4, TeamSide::kAway, {.x = 55.0, .y = 3.0}),
          playerAt(5, TeamSide::kAway, {.x = 55.0, .y = 37.0}),
          playerAt(6, TeamSide::kAway, {.x = 58.0, .y = 20.0})};
}

// The state after one perception update, so every player remembers what he
// sees; extra systems run in the same steps.
[[nodiscard]] MatchState perceived(MatchState state, std::vector<MatchSystem> extra = {},
                                   const int steps = 1) {
  std::vector<MatchSystem> systems{makePerceptionSystem({})};
  for (MatchSystem& system : extra) {
    systems.push_back(std::move(system));
  }
  MatchSimulation simulation({.initialState = std::move(state),
                              .seed = 1,
                              .ticksPerSecond = 30,
                              .systems = std::move(systems),
                              .commands = {}});
  for (int step = 0; step < steps; ++step) {
    REQUIRE(simulation.step().has_value());
  }
  return simulation.state();
}

[[nodiscard]] std::vector<PassCandidate> candidatesOf(const MatchState& state,
                                                      const PassCandidateRules& rules = {},
                                                      const SimTick now = SimTick(1)) {
  return generatePassCandidates(state, 0, now, kSecondsPerTick, rules);
}

[[nodiscard]] PassCandidate candidateFor(const std::vector<PassCandidate>& candidates,
                                         const std::uint32_t receiver) {
  const auto found = std::ranges::find(candidates, PlayerId(receiver), &PassCandidate::receiver);
  REQUIRE(found != candidates.end());
  return found == candidates.end() ? PassCandidate{} : *found;
}

}  // namespace

TEST_CASE("Every remembered teammate is a candidate with its scores", "[passCandidates]") {
  const auto candidates = candidatesOf(perceived(threeASide(bystanders())));

  REQUIRE(candidates.size() == 2);
  const PassCandidate toTwo = candidateFor(candidates, 2);
  REQUIRE(toTwo.target == Vec2{.x = 35.0, .y = 12.0});
  REQUIRE_THAT(toTwo.distance, WithinAbs(std::sqrt((14.5 * 14.5) + 64.0), 1e-12));
  REQUIRE(toTwo.receiverConfidence == 1.0);
  REQUIRE(toTwo.interceptionRisk < 0.05);
  REQUIRE_THAT(toTwo.completion, WithinAbs(1.0 - toTwo.interceptionRisk, 1e-12));
  REQUIRE_THAT(toTwo.progression, WithinAbs(14.5 / 60.0, 1e-12));
  REQUIRE(toTwo.receiverPressure == 0.0);
  REQUIRE(toTwo.isValid());
}

TEST_CASE("A clear lane scores better than an obstructed one", "[passCandidates]") {
  // Away 4 stands in the lane to player 3, two thirds of the way.
  std::vector<PlayerMatchState> away = bystanders();
  away.front().position = {.x = 30.0, .y = 25.2};
  const auto candidates = candidatesOf(perceived(threeASide(away)));

  const PassCandidate clear = candidateFor(candidates, 2);
  const PassCandidate obstructed = candidateFor(candidates, 3);
  CAPTURE(clear.interceptionRisk, obstructed.interceptionRisk);
  REQUIRE(obstructed.interceptionRisk > 0.5);
  REQUIRE(clear.interceptionRisk < 0.1);
  REQUIRE(obstructed.completion < clear.completion);
  REQUIRE(obstructed.utility < clear.utility);
  REQUIRE(obstructed.receiverPressure > 0.0);
  REQUIRE(candidates.front().receiver == PlayerId(2));
}

TEST_CASE("Candidates use remembered positions, not the true ones", "[passCandidates]") {
  // The opponent steps into the lane to player 3 after the carrier last
  // looked: perception runs on ticks 0 and 3, the move happens in tick 1.
  const MatchSystem stepIntoLane{
      .name = "step into lane",
      .update = [](const MatchStepContext& context, const MatchState&, MatchStateWriter& next) {
        if (context.tick() == SimTick(1)) {
          next.setPlayerPosition(3, {.x = 30.0, .y = 25.2});
        }
      }};
  const MatchState state = perceived(threeASide(bystanders()), {stepIntoLane}, 2);
  REQUIRE(state.players()[3].position == Vec2{.x = 30.0, .y = 25.2});

  const auto candidates = candidatesOf(state, {}, SimTick(2));

  REQUIRE(candidateFor(candidates, 3).interceptionRisk < 0.1);
}

TEST_CASE("An unseen teammate is no option", "[passCandidates]") {
  // The carrier looks toward his own goal: both teammates are behind him.
  const auto candidates = candidatesOf(perceived(threeASide(bystanders(), {.x = -1.0, .y = 0.0})));

  REQUIRE(candidates.empty());
}

TEST_CASE("Candidates are ordered by validity, utility and id", "[passCandidates]") {
  // Players 2 and 3 stand symmetrically: equal scores, so id decides.
  const auto candidates = candidatesOf(perceived(threeASide(bystanders())));

  REQUIRE(candidates.at(0).utility == candidates.at(1).utility);
  REQUIRE(candidates.at(0).receiver == PlayerId(2));
  REQUIRE(candidates == candidatesOf(perceived(threeASide(bystanders()))));
}

TEST_CASE("Without a valid option every candidate says why", "[passCandidates]") {
  SECTION("too far and too close") {
    PassCandidateRules rules;
    rules.scoring.maxPassDistance = 10.0;
    const auto far = candidatesOf(perceived(threeASide(bystanders())), rules);
    REQUIRE(std::ranges::none_of(far, &PassCandidate::isValid));
    REQUIRE(far.front().rejection == PassRejection::kTooFar);

    rules.scoring.maxPassDistance = 35.0;
    rules.scoring.minPassDistance = 20.0;
    REQUIRE(candidatesOf(perceived(threeASide(bystanders())), rules).front().rejection ==
            PassRejection::kTooClose);
  }
  SECTION("out of reach") {
    PassCandidateRules rules;
    rules.passing.maxSpeed = 5.0;
    const auto candidates = candidatesOf(perceived(threeASide(bystanders())), rules);
    REQUIRE(candidates.front().rejection == PassRejection::kOutOfReach);
  }
  SECTION("both lanes blocked just before the receivers") {
    std::vector<PlayerMatchState> away = bystanders();
    away.at(0).position = {.x = 33.0, .y = 13.1};
    away.at(1).position = {.x = 33.0, .y = 26.9};
    const auto candidates = candidatesOf(perceived(threeASide(away)));
    REQUIRE(std::ranges::none_of(candidates, &PassCandidate::isValid));
    REQUIRE(candidates.front().rejection == PassRejection::kUnlikely);
  }
  REQUIRE(passRejectionName(PassRejection::kUnlikely) == "unlikely to arrive");
}

TEST_CASE("Progression points toward the opponent's goal", "[passCandidates]") {
  REQUIRE(attackingDirection(TeamSide::kHome) == 1.0);
  REQUIRE(attackingDirection(TeamSide::kAway) == -1.0);
}
