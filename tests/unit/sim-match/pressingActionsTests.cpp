#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

#include "actionCandidate.hpp"
#include "matchCommand.hpp"
#include "matchEvents.hpp"
#include "matchSetup.hpp"
#include "matchSimulation.hpp"
#include "matchState.hpp"
#include "offBallActions.hpp"
#include "passCandidates.hpp"
#include "perception.hpp"
#include "pressingActions.hpp"
#include "referenceTactic.hpp"
#include "tactic.hpp"

using Catch::Matchers::WithinAbs;
using ElyverseFootball::SimCore::PlayerId;
using ElyverseFootball::SimCore::SimTick;
using ElyverseFootball::SimCore::Vec2;
using ElyverseFootball::SimMatch::ActionCandidate;
using ElyverseFootball::SimMatch::ActionDiagnostic;
using ElyverseFootball::SimMatch::ActionType;
using ElyverseFootball::SimMatch::coverTarget;
using ElyverseFootball::SimMatch::generatePassCandidates;
using ElyverseFootball::SimMatch::GiveBallCommand;
using ElyverseFootball::SimMatch::laneBlockTarget;
using ElyverseFootball::SimMatch::laneOpenness;
using ElyverseFootball::SimMatch::makePerceptionSystem;
using ElyverseFootball::SimMatch::MatchConfig;
using ElyverseFootball::SimMatch::MatchSetup;
using ElyverseFootball::SimMatch::MatchSimulation;
using ElyverseFootball::SimMatch::MatchState;
using ElyverseFootball::SimMatch::PassCandidate;
using ElyverseFootball::SimMatch::Pitch;
using ElyverseFootball::SimMatch::PlayerMatchState;
using ElyverseFootball::SimMatch::pressTarget;
using ElyverseFootball::SimMatch::RememberedPlayer;
using ElyverseFootball::SimMatch::startMatch;
using ElyverseFootball::SimMatch::TeamSide;
using ElyverseFootball::SimMatch::TeamTactics;
using ElyverseFootball::SimTactics::referenceTacticSpec;
using ElyverseFootball::SimTactics::Tactic;

namespace {

constexpr double kSecondsPerTick = 1.0 / 30.0;

[[nodiscard]] double distanceBetween(const Vec2 first, const Vec2 second) {
  return std::sqrt((first - second).lengthSquared());
}

[[nodiscard]] double angleBetween(const Vec2 first, const Vec2 second) {
  const double cosine =
      first.dot(second) / std::sqrt(first.lengthSquared() * second.lengthSquared());
  return std::acos(std::clamp(cosine, -1.0, 1.0));
}

[[nodiscard]] RememberedPlayer opponentAt(const Vec2 position) {
  return {.index = 0,
          .playerId = PlayerId(99),
          .position = position,
          .velocity = {},
          .confidence = 1.0,
          .teammate = false};
}

// Seven a side from a list of positions, home first; ids 1-7 home, 8-14 away.
// Home faces +x, away -x.
[[nodiscard]] MatchState stateOf(const std::vector<Vec2>& positions,
                                 const std::optional<PlayerId> owner, TeamTactics tactics = {}) {
  std::vector<PlayerMatchState> players;
  players.reserve(positions.size());
  for (std::size_t index = 0; index < positions.size(); ++index) {
    const TeamSide side = index < 7 ? TeamSide::kHome : TeamSide::kAway;
    players.push_back({.playerId = PlayerId(static_cast<PlayerId::ValueType>(index + 1)),
                       .side = side,
                       .position = positions.at(index),
                       .velocity = {},
                       .attributes = {},
                       .target = std::nullopt,
                       .facing = {.x = side == TeamSide::kHome ? 1.0 : -1.0, .y = 0.0}});
  }
  auto state =
      MatchState::create({.pitch = Pitch(60.0, 40.0),
                          .players = std::move(players),
                          .ball = {.position = positions.front() + Vec2{.x = 0.5, .y = 0.0},
                                   .velocity = {},
                                   .owner = owner,
                                   .lastTouch = std::nullopt},
                          .playersPerSide = 7},
                         std::move(tactics));
  REQUIRE(state.has_value());
  return *std::move(state);
}

}  // namespace

TEST_CASE("A presser approaches on the line to the option he cuts off", "[pressingActions]") {
  const Vec2 carrier{.x = 30.0, .y = 20.0};
  const Vec2 option{.x = 20.0, .y = 30.0};
  const Vec2 presser{.x = 36.0, .y = 12.0};
  const Vec2 ownGoal{.x = 60.0, .y = 20.0};
  const Vec2 target = pressTarget(carrier, option, ownGoal, 1.0);
  // One meter from the carrier, on his line to the option.
  REQUIRE_THAT(distanceBetween(target, carrier), WithinAbs(1.0, 1e-12));
  REQUIRE_THAT(angleBetween(target - carrier, option - carrier), WithinAbs(0.0, 1e-7));
  // Not the shortest path: the straight approach would stop a meter from
  // the carrier on the presser's own side of him...
  const Vec2 straight = carrier + ((presser - carrier) * (1.0 / distanceBetween(presser, carrier)));
  REQUIRE(distanceBetween(target, straight) > 1.5);
  // ...and leave the lane open that the angled approach closes.
  REQUIRE(laneOpenness(carrier, option, {opponentAt(target)}, 4.0) == 0.0);
  REQUIRE(laneOpenness(carrier, option, {opponentAt(straight)}, 4.0) > 0.2);
  // Without an option, goal-side.
  REQUIRE(pressTarget(carrier, std::nullopt, ownGoal, 1.0) == Vec2{.x = 31.0, .y = 20.0});
}

TEST_CASE("A lane blocker stands on the lane between its ends", "[pressingActions]") {
  const Vec2 carrier{.x = 20.0, .y = 20.0};
  const Vec2 receiver{.x = 35.0, .y = 20.0};
  REQUIRE(laneBlockTarget({.carrier = carrier, .receiver = receiver}, {.x = 27.0, .y = 26.0},
                          2.0) == Vec2{.x = 27.0, .y = 20.0});
  // Never on top of either end.
  REQUIRE(laneBlockTarget({.carrier = carrier, .receiver = receiver}, {.x = 10.0, .y = 26.0},
                          2.0) == Vec2{.x = 22.0, .y = 20.0});
  REQUIRE(laneBlockTarget({.carrier = carrier, .receiver = receiver}, {.x = 50.0, .y = 26.0},
                          2.0) == Vec2{.x = 33.0, .y = 20.0});
  // A lane shorter than twice the distance: its middle.
  REQUIRE(laneBlockTarget({.carrier = carrier, .receiver = {.x = 23.0, .y = 20.0}},
                          {.x = 0.0, .y = 0.0}, 2.0) == Vec2{.x = 21.5, .y = 20.0});
}

TEST_CASE("Blocking a lane raises the interception estimate of that pass", "[pressingActions]") {
  // Home's player 1 has the ball at (20, 20), his teammate 2 is at (35, 20).
  // Away's player 8 stands 6 m off the lane, then on it where
  // laneBlockTarget() puts him. The risk player 1 estimates for the pass,
  // from his own memory, rises measurably.
  const auto riskWith = [](const Vec2 blocker) {
    std::vector<Vec2> positions{{.x = 20.0, .y = 20.0}, {.x = 35.0, .y = 20.0},
                                {.x = 3.0, .y = 5.0},   {.x = 3.0, .y = 10.0},
                                {.x = 3.0, .y = 15.0},  {.x = 3.0, .y = 30.0},
                                {.x = 3.0, .y = 35.0},  blocker,
                                {.x = 57.0, .y = 5.0},  {.x = 57.0, .y = 10.0},
                                {.x = 57.0, .y = 15.0}, {.x = 57.0, .y = 30.0},
                                {.x = 57.0, .y = 35.0}, {.x = 58.0, .y = 20.0}};
    MatchSimulation simulation({.initialState = stateOf(positions, PlayerId(1)),
                                .seed = 1,
                                .ticksPerSecond = 30,
                                .systems = {makePerceptionSystem({})},
                                .commands = {}});
    REQUIRE(simulation.step().has_value());
    const std::vector<PassCandidate> candidates =
        generatePassCandidates(simulation.state(), 0, simulation.tick(), kSecondsPerTick, {});
    for (const PassCandidate& candidate : candidates) {
      if (candidate.receiver == PlayerId(2)) {
        return candidate.interceptionRisk;
      }
    }
    FAIL("no candidate for player 2");
    return 0.0;
  };
  const Vec2 blocker{.x = 27.0, .y = 26.0};
  const double before = riskWith(blocker);
  const double after = riskWith(laneBlockTarget(
      {.carrier = {.x = 20.0, .y = 20.0}, .receiver = {.x = 35.0, .y = 20.0}}, blocker, 2.0));
  REQUIRE(after - before > 0.3);
}

TEST_CASE("A coverer stands behind the presser, toward the goal", "[pressingActions]") {
  const Vec2 presser{.x = 30.0, .y = 26.0};
  const Vec2 ownGoal{.x = 0.0, .y = 20.0};
  const Vec2 cover = coverTarget(presser, ownGoal, 6.0);
  REQUIRE_THAT(distanceBetween(cover, presser), WithinAbs(6.0, 1e-12));
  REQUIRE_THAT(distanceBetween(cover, ownGoal) + 6.0,
               WithinAbs(distanceBetween(presser, ownGoal), 1e-9));
}

TEST_CASE("Defenders are offered pressing, lane blocking and cover", "[pressingActions]") {
  // Home defends with the reference tactic; away's player 8, scripted, has
  // the ball at (40, 20) with a teammate, 9, at (46, 30).
  const auto tactic = Tactic::create(referenceTacticSpec());
  REQUIRE(tactic.has_value());
  const std::vector<Vec2> positions{
      {.x = 3.0, .y = 20.0},  {.x = 15.0, .y = 12.0}, {.x = 15.0, .y = 28.0},
      {.x = 25.0, .y = 20.0}, {.x = 30.0, .y = 5.0},  {.x = 30.0, .y = 35.0},
      {.x = 34.0, .y = 22.0}, {.x = 40.0, .y = 20.0}, {.x = 46.0, .y = 30.0},
      {.x = 57.0, .y = 20.0}, {.x = 55.0, .y = 8.0},  {.x = 55.0, .y = 32.0},
      {.x = 52.0, .y = 14.0}, {.x = 52.0, .y = 26.0}};
  MatchConfig config;
  config.decisions.minHoldSeconds = 1.0e6;
  MatchSimulation simulation = startMatch(MatchSetup{
      .initialState = stateOf(positions, std::nullopt, {.home = *tactic, .away = std::nullopt}),
      .config = config,
      .seed = 3,
      .commands = {{.tick = SimTick(0), .command = GiveBallCommand{.playerId = PlayerId(8)}}}});
  simulation.setCollectDiagnostics(true);
  std::vector<ActionDiagnostic> decisions;
  while (simulation.tick() < SimTick(40)) {
    REQUIRE(simulation.step().has_value());
    decisions.insert(decisions.end(), simulation.actionDiagnostics().begin(),
                     simulation.actionDiagnostics().end());
  }
  // The striker (7), 6 m from the carrier, may press him -- on the line to
  // away's player 9 -- or block the lane to 9.
  const ActionDiagnostic* striker = nullptr;
  for (const ActionDiagnostic& decision : decisions) {
    if (decision.player == PlayerId(7) && striker == nullptr) {
      striker = &decision;
    }
  }
  REQUIRE(striker != nullptr);
  const auto find = [&striker](const ActionType type) -> const ActionCandidate* {
    for (const ActionCandidate& candidate : striker->candidates) {
      if (candidate.type == type) {
        return &candidate;
      }
    }
    return nullptr;
  };
  const ActionCandidate* press = find(ActionType::kPressCarrier);
  REQUIRE(press != nullptr);
  REQUIRE(press->subject == PlayerId(8));
  REQUIRE(press->scores.responsibility == 1.0);  // the striker closes the pressing line
  REQUIRE(press->scores.urgency > 0.0);
  const ActionCandidate* block = find(ActionType::kBlockLane);
  REQUIRE(block != nullptr);
  REQUIRE(block->subject == PlayerId(9));

  // Once someone presses, a teammate is offered to cover him.
  bool coveredPresser = false;
  for (const ActionDiagnostic& decision : decisions) {
    for (const ActionCandidate& candidate : decision.candidates) {
      if (candidate.type == ActionType::kCover && candidate.subject &&
          candidate.scores.urgency > 0.79) {
        coveredPresser = true;
      }
    }
  }
  REQUIRE(coveredPresser);
}
