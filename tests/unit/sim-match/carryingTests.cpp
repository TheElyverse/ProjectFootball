#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

#include "ballMovement.hpp"
#include "carrying.hpp"
#include "challenge.hpp"
#include "matchEvents.hpp"
#include "matchSimulation.hpp"
#include "matchState.hpp"
#include "perception.hpp"
#include "playerMovement.hpp"
#include "tacticalPhase.hpp"
#include "tacticalState.hpp"
#include "vec2.hpp"

using Catch::Matchers::WithinAbs;
using ElyverseFootball::SimCore::PlayerId;
using ElyverseFootball::SimCore::SimTick;
using ElyverseFootball::SimCore::Vec2;
using ElyverseFootball::SimMatch::BallPhysics;
using ElyverseFootball::SimMatch::BallWon;
using ElyverseFootball::SimMatch::BallWonCause;
using ElyverseFootball::SimMatch::carriedBallPosition;
using ElyverseFootball::SimMatch::CarryCandidate;
using ElyverseFootball::SimMatch::CarryCandidateRules;
using ElyverseFootball::SimMatch::carryContributions;
using ElyverseFootball::SimMatch::CarryIntent;
using ElyverseFootball::SimMatch::CarryKind;
using ElyverseFootball::SimMatch::CarryRejection;
using ElyverseFootball::SimMatch::CarryRun;
using ElyverseFootball::SimMatch::CarryScoringConfig;
using ElyverseFootball::SimMatch::carryScoringForPhase;
using ElyverseFootball::SimMatch::carryScoringForRisk;
using ElyverseFootball::SimMatch::DribbleAttempted;
using ElyverseFootball::SimMatch::DribbleEnded;
using ElyverseFootball::SimMatch::generateCarryCandidates;
using ElyverseFootball::SimMatch::GoalScored;
using ElyverseFootball::SimMatch::makeBallMovementSystem;
using ElyverseFootball::SimMatch::makeChallengeSystem;
using ElyverseFootball::SimMatch::makePerceptionSystem;
using ElyverseFootball::SimMatch::makePlayerMovementSystem;
using ElyverseFootball::SimMatch::MatchEvent;
using ElyverseFootball::SimMatch::MatchSimulation;
using ElyverseFootball::SimMatch::MatchState;
using ElyverseFootball::SimMatch::MatchStateWriter;
using ElyverseFootball::SimMatch::MatchStepContext;
using ElyverseFootball::SimMatch::MatchSystem;
using ElyverseFootball::SimMatch::PassAttempted;
using ElyverseFootball::SimMatch::PassIntent;
using ElyverseFootball::SimMatch::Pitch;
using ElyverseFootball::SimMatch::PlayerMatchState;
using ElyverseFootball::SimMatch::PossessionChanged;
using ElyverseFootball::SimMatch::runningSpeed;
using ElyverseFootball::SimMatch::TeamSide;
using ElyverseFootball::SimTactics::TacticalPhase;

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

// Two a side on a 60 x 40 pitch. Home 1 has the ball at his feet at (20, 20),
// facing `facing`; home 2 stands far back, away 3 and 4 where the test puts
// them.
[[nodiscard]] MatchState twoASide(const Vec2 away3, const Vec2 away4,
                                  const Vec2 facing = {.x = 1.0, .y = 0.0}) {
  auto state =
      MatchState::create({.pitch = Pitch(60.0, 40.0),
                          .players = {playerAt(1, TeamSide::kHome, {.x = 20.0, .y = 20.0}, facing),
                                      playerAt(2, TeamSide::kHome, {.x = 3.0, .y = 20.0}),
                                      playerAt(3, TeamSide::kAway, away3, {.x = -1.0, .y = 0.0}),
                                      playerAt(4, TeamSide::kAway, away4, {.x = -1.0, .y = 0.0})},
                          .ball = {.position = Vec2{.x = 20.0, .y = 20.0} + (facing * 0.5),
                                   .velocity = {},
                                   .owner = PlayerId(1),
                                   .lastTouch = std::nullopt},
                          .playersPerSide = 2});
  REQUIRE(state.has_value());
  return *std::move(state);
}

// The state after one perception update with the carrier looking all round:
// a perception config with a full field of view, so he remembers everyone.
[[nodiscard]] MatchState perceived(MatchState state) {
  ElyverseFootball::SimMatch::PerceptionConfig perception;
  perception.fieldOfViewDegrees = 360.0;
  MatchSimulation simulation({.initialState = std::move(state),
                              .seed = 1,
                              .ticksPerSecond = 30,
                              .systems = {makePerceptionSystem(perception)},
                              .commands = {}});
  REQUIRE(simulation.step().has_value());
  return simulation.state();
}

[[nodiscard]] std::vector<CarryCandidate> candidatesOf(const MatchState& state,
                                                       const CarryCandidateRules& rules = {}) {
  return generateCarryCandidates(state, 0, SimTick(1), kSecondsPerTick, rules);
}

[[nodiscard]] std::vector<CarryCandidate> ofKind(const std::vector<CarryCandidate>& candidates,
                                                 const CarryKind kind) {
  std::vector<CarryCandidate> found;
  std::ranges::copy_if(candidates, std::back_inserter(found),
                       [kind](const CarryCandidate& candidate) { return candidate.kind == kind; });
  return found;
}

// Has home 1 play a touch toward `target` in the step of tick 0, at this
// speed, and run after it toward `runTo`: the ball system plays it in the
// step of tick 1.
[[nodiscard]] MatchSystem touchAt(const Vec2 target, const double speed, const double seconds,
                                  const Vec2 runTo) {
  return {.name = "touch",
          .update = [=](const MatchStepContext& context, const MatchState& /*current*/,
                        MatchStateWriter& next) {
            if (context.tick() == SimTick(0)) {
              next.setPendingAction(0, CarryIntent{.carrier = PlayerId(1),
                                                   .target = target,
                                                   .speed = speed,
                                                   .seconds = seconds,
                                                   .errorFactor = 0.0});
              next.setPlayerTarget(0, runTo);
            }
          }};
}

// The state played on with the touch, movement and the ball, tick by tick;
// `each` sees every step.
void playTouch(MatchState state, MatchSystem touch, const int ticks,
               const std::function<void(const MatchSimulation&)>& each,
               std::vector<MatchSystem> extra = {}) {
  std::vector<MatchSystem> systems{std::move(touch)};
  for (MatchSystem& system : extra) {
    systems.push_back(std::move(system));
  }
  systems.push_back(makePlayerMovementSystem());
  systems.push_back(makeBallMovementSystem(BallPhysics{}));
  MatchSimulation simulation({.initialState = std::move(state),
                              .seed = 1,
                              .ticksPerSecond = 30,
                              .systems = std::move(systems),
                              .commands = {}});
  for (int tick = 0; tick < ticks; ++tick) {
    REQUIRE(simulation.step().has_value());
    each(simulation);
  }
}

[[nodiscard]] double cross(const Vec2 left, const Vec2 right) {
  return (left.x * right.y) - (left.y * right.x);
}

}  // namespace

TEST_CASE("Holding the ball is always an option", "[carrying]") {
  const auto candidates =
      candidatesOf(perceived(twoASide({.x = 55.0, .y = 5.0}, {.x = 55.0, .y = 35.0})));
  const auto holds = ofKind(candidates, CarryKind::kHold);
  REQUIRE(holds.size() == 1);
  REQUIRE(holds.front().isValid());
  REQUIRE(holds.front().target == Vec2{.x = 20.0, .y = 20.0});
  REQUIRE(holds.front().risk == 0.0);
  REQUIRE_THAT(holds.front().utility, WithinAbs(CarryScoringConfig{}.holdWeight, 1e-12));
}

TEST_CASE("In open space he carries the ball toward the goal as far as he plans", "[carrying]") {
  const auto candidates =
      candidatesOf(perceived(twoASide({.x = 55.0, .y = 5.0}, {.x = 55.0, .y = 35.0})));
  const auto carries = ofKind(candidates, CarryKind::kCarry);
  REQUIRE_FALSE(carries.empty());
  // The first direction is the goal's centre, straight ahead at y = 20.
  const auto toGoal = std::ranges::find_if(
      carries, [](const CarryCandidate& carry) { return std::abs(carry.target.y - 20.0) < 1e-9; });
  REQUIRE(toGoal != carries.end());
  REQUIRE(toGoal->isValid());
  // Two seconds at his running speed with the ball, in whole meters.
  const double speed = 7.5 * (0.8 + (0.15 * 0.5));
  REQUIRE(toGoal->distance == std::floor(speed * 2.0));
  REQUIRE(toGoal->risk < 0.01);
  REQUIRE(toGoal->progression > 0.0);
  REQUIRE(toGoal->touchSpeed > 0.0);
  REQUIRE(toGoal->touchSeconds > 0.0);
  REQUIRE(toGoal->utility > ofKind(candidates, CarryKind::kHold).front().utility);
}

TEST_CASE("He carries the ball through the gap between two opponents", "[carrying]") {
  // Away 3 and 4 stand 10 m ahead, symmetric about his line: the gap's middle
  // is straight ahead, the gap behind them straight back.
  const auto candidates =
      candidatesOf(perceived(twoASide({.x = 30.0, .y = 14.0}, {.x = 30.0, .y = 26.0})));
  bool ahead = false;
  for (const CarryCandidate& candidate : candidates) {
    if (candidate.kind != CarryKind::kHold && candidate.target.x > 20.0 &&
        std::abs(candidate.target.y - 20.0) < 1e-9) {
      ahead = true;
    }
  }
  REQUIRE(ahead);
}

TEST_CASE("A carry stops short of where an opponent would get first", "[carrying]") {
  // Away 3 waits 8 m straight ahead.
  const auto candidates =
      candidatesOf(perceived(twoASide({.x = 28.0, .y = 20.0}, {.x = 55.0, .y = 35.0})));
  for (const CarryCandidate& candidate : ofKind(candidates, CarryKind::kCarry)) {
    if (std::abs(candidate.target.y - 20.0) < 1e-9 && candidate.target.x > 20.0) {
      CAPTURE(candidate.distance);
      REQUIRE(candidate.distance < 8.0);
    }
  }
}

TEST_CASE("A carry away from where he faces is a turn", "[carrying]") {
  // Facing his own goal, the way to the opponent's goal is behind him.
  const auto candidates = candidatesOf(
      perceived(twoASide({.x = 55.0, .y = 5.0}, {.x = 55.0, .y = 35.0}, {.x = -1.0, .y = 0.0})));
  const auto turns = ofKind(candidates, CarryKind::kTurn);
  REQUIRE_FALSE(turns.empty());
  for (const CarryCandidate& turn : turns) {
    REQUIRE(turn.distance <= CarryScoringConfig{}.turnDistance);
    REQUIRE(turn.target.x >= 20.0);
  }
}

TEST_CASE("He dribbles past an opponent ahead on either side", "[carrying]") {
  // Away 3 stands 4 m ahead.
  const auto candidates =
      candidatesOf(perceived(twoASide({.x = 24.0, .y = 20.0}, {.x = 55.0, .y = 35.0})));
  const auto dribbles = ofKind(candidates, CarryKind::kDribble);
  REQUIRE(dribbles.size() == 2);
  const Vec2 toOpponent{.x = 1.0, .y = 0.0};
  double sides = 0.0;
  for (const CarryCandidate& dribble : dribbles) {
    REQUIRE(dribble.subject == PlayerId(3));
    // Behind him, beside his line.
    REQUIRE(dribble.target.x > 24.0);
    sides += std::copysign(1.0, cross(toOpponent, dribble.target - Vec2{.x = 20.0, .y = 20.0}));
    REQUIRE(dribble.bypassed == 1);
    REQUIRE(dribble.errorFactor > 0.0);
  }
  REQUIRE(sides == 0.0);
}

TEST_CASE("No dribble past an opponent behind, too far or right on him", "[carrying]") {
  for (const Vec2 opponent :
       {Vec2{.x = 14.0, .y = 20.0}, Vec2{.x = 30.0, .y = 20.0}, Vec2{.x = 21.5, .y = 20.0}}) {
    CAPTURE(opponent.x);
    const auto candidates = candidatesOf(perceived(twoASide(opponent, {.x = 55.0, .y = 35.0})));
    REQUIRE(ofKind(candidates, CarryKind::kDribble).empty());
  }
}

TEST_CASE("Without a memory of opponents nothing threatens a carry", "[carrying]") {
  // Opponents right next to him he has not seen yet do not count.
  const auto candidates = candidatesOf(twoASide({.x = 21.0, .y = 21.0}, {.x = 21.0, .y = 19.0}));
  for (const CarryCandidate& candidate : candidates) {
    REQUIRE(candidate.risk == 0.0);
  }
  REQUIRE(ofKind(candidates, CarryKind::kDribble).empty());
}

TEST_CASE("Valid candidates come first, by descending utility", "[carrying]") {
  const auto candidates =
      candidatesOf(perceived(twoASide({.x = 24.0, .y = 20.0}, {.x = 30.0, .y = 26.0})));
  REQUIRE_FALSE(candidates.empty());
  bool invalidSeen = false;
  for (std::size_t index = 0; index < candidates.size(); ++index) {
    if (!candidates[index].isValid()) {
      invalidSeen = true;
      continue;
    }
    REQUIRE_FALSE(invalidSeen);
    if (index > 0) {
      REQUIRE(candidates[index - 1].utility >= candidates[index].utility);
    }
  }
}

TEST_CASE("A candidate's contributions add up to its utility", "[carrying]") {
  const CarryScoringConfig scoring;
  for (const CarryCandidate& candidate :
       candidatesOf(perceived(twoASide({.x = 24.0, .y = 20.0}, {.x = 30.0, .y = 26.0})))) {
    REQUIRE(carryContributions(candidate, scoring).total() == candidate.utility);
  }
}

TEST_CASE("Carry scoring shifts with the phase and the passing risk", "[carrying]") {
  const CarryScoringConfig scoring;
  const CarryScoringConfig transition =
      carryScoringForPhase(scoring, TacticalPhase::kAttackingTransition);
  REQUIRE(transition.progressionWeight == scoring.progressionWeight * 2.0);
  REQUIRE(transition.bypassWeight == scoring.bypassWeight * 2.0);
  REQUIRE(transition.riskWeight == scoring.riskWeight * 0.8);
  REQUIRE(carryScoringForPhase(scoring, TacticalPhase::kFinalThird).shotWeight ==
          scoring.shotWeight * 2.0);
  REQUIRE(carryScoringForPhase(scoring, TacticalPhase::kDefensiveBlock) == scoring);
  REQUIRE(carryScoringForRisk(scoring, 0.5) == scoring);
  const CarryScoringConfig bold = carryScoringForRisk(scoring, 1.0);
  REQUIRE(bold.progressionWeight > scoring.progressionWeight);
  REQUIRE(bold.riskWeight < scoring.riskWeight);
  REQUIRE(bold.minKeepChance < scoring.minKeepChance);
}

TEST_CASE("Invalid carry scoring is rejected", "[carrying]") {
  const auto rejects = [](const std::function<void(CarryScoringConfig&)>& change) {
    CarryScoringConfig scoring;
    change(scoring);
    REQUIRE_THROWS_AS(ElyverseFootball::SimMatch::validate(scoring), std::invalid_argument);
  };
  ElyverseFootball::SimMatch::validate(CarryScoringConfig{});
  rejects([](CarryScoringConfig& scoring) { scoring.horizonSeconds = 0.0; });
  rejects([](CarryScoringConfig& scoring) { scoring.minKeepChance = 1.5; });
  rejects([](CarryScoringConfig& scoring) { scoring.maxTouchDistance = 0.5; });
  rejects([](CarryScoringConfig& scoring) { scoring.riskWeight = -1.0; });
  rejects([](CarryScoringConfig& scoring) { scoring.finalThird.shot = std::nan(""); });
}

TEST_CASE("A player runs slower with the ball at his feet, by his ball control", "[carrying]") {
  const MatchState state = twoASide({.x = 55.0, .y = 5.0}, {.x = 55.0, .y = 35.0});
  const BallPhysics physics;
  PlayerMatchState carrier = state.players()[0];
  REQUIRE_THAT(runningSpeed(carrier, state.ball(), physics), WithinAbs(7.5 * 0.875, 1e-12));
  carrier.attributes.ballControl = 1.0;
  REQUIRE_THAT(runningSpeed(carrier, state.ball(), physics), WithinAbs(7.5 * 0.95, 1e-12));
  carrier.attributes.ballControl = 0.0;
  REQUIRE_THAT(runningSpeed(carrier, state.ball(), physics), WithinAbs(7.5 * 0.8, 1e-12));
  // Everyone else runs as fast as he can.
  REQUIRE(runningSpeed(state.players()[2], state.ball(), physics) == 7.5);
}

TEST_CASE("A touch plays the ball ahead and the carrier keeps it", "[carrying]") {
  // Away 3 and 4 far off; home 1 touches the ball 4 m ahead and runs after it.
  const MatchState state = twoASide({.x = 55.0, .y = 5.0}, {.x = 55.0, .y = 35.0});
  std::optional<SimTick> touched;
  std::optional<SimTick> retaken;
  playTouch(state, touchAt({.x = 24.5, .y = 20.0}, 4.0, 0.4, {.x = 30.0, .y = 20.0}), 60,
            [&](const MatchSimulation& simulation) {
              const auto& ball = simulation.state().ball();
              REQUIRE(ball.owner == PlayerId(1));
              if (ball.touch && !touched) {
                touched = ball.touch->tick;
                REQUIRE(ball.touch->seconds == 0.4);
                REQUIRE(ball.lastTouch == ElyverseFootball::SimMatch::BallTouch{
                                              .playerId = PlayerId(1), .tick = SimTick(1)});
                REQUIRE(ball.velocity.x > 0.0);
                REQUIRE(ball.velocity.y == 0.0);
              }
              if (touched && !ball.touch && !retaken) {
                retaken = simulation.tick();
                // At his feet again, moving with him.
                const PlayerMatchState& carrier = simulation.state().players()[0];
                REQUIRE(ball.position ==
                        carriedBallPosition(carrier, BallPhysics{}, simulation.state().pitch()));
                REQUIRE(ball.velocity == carrier.velocity);
              }
              for (const MatchEvent& event : simulation.events()) {
                REQUIRE_FALSE(std::holds_alternative<PossessionChanged>(event));
              }
            });
  REQUIRE(touched == SimTick(1));
  REQUIRE(retaken.has_value());
  // Not before the touch's seconds have passed.
  REQUIRE(retaken.value_or(SimTick(0)).value() - 1 >= 12);
}

TEST_CASE("An opponent who gets to the touched ball first wins it", "[carrying]") {
  // Away 3 stands on the ball's way.
  const MatchState state = twoASide({.x = 23.0, .y = 20.0}, {.x = 55.0, .y = 35.0});
  std::optional<BallWon> won;
  playTouch(state, touchAt({.x = 26.0, .y = 20.0}, 6.0, 0.4, {.x = 26.0, .y = 20.0}), 30,
            [&](const MatchSimulation& simulation) {
              for (const MatchEvent& event : simulation.events()) {
                if (const auto* ballWon = std::get_if<BallWon>(&event)) {
                  won = *ballWon;
                  REQUIRE(simulation.state().ball().owner == PlayerId(3));
                  REQUIRE_FALSE(simulation.state().ball().touch.has_value());
                }
              }
            });
  REQUIRE(won.has_value());
  REQUIRE(won.value_or(BallWon{}).winner == PlayerId(3));
  REQUIRE(won.value_or(BallWon{}).loser == PlayerId(1));
  REQUIRE(won.value_or(BallWon{}).cause == BallWonCause::kLooseTouch);
}

TEST_CASE("A teammate leaves the touched ball to the carrier", "[carrying]") {
  // Home 2 stands on the ball's way; the carrier stays where he is.
  auto spec = twoASide({.x = 55.0, .y = 5.0}, {.x = 55.0, .y = 35.0});
  std::vector<PlayerMatchState> players(spec.players().begin(), spec.players().end());
  players[1].position = {.x = 23.0, .y = 20.0};
  auto state = MatchState::create({.pitch = spec.pitch(),
                                   .players = std::move(players),
                                   .ball = spec.ball(),
                                   .playersPerSide = 2});
  REQUIRE(state.has_value());
  playTouch(*state, touchAt({.x = 26.0, .y = 20.0}, 4.0, 5.0, {.x = 20.0, .y = 20.0}), 45,
            [](const MatchSimulation& simulation) {
              REQUIRE(simulation.state().ball().owner == PlayerId(1));
            });
}

TEST_CASE("A touched ball that leaves the pitch is nobody's", "[carrying]") {
  // Toward the touchline, far too hard, and he does not follow.
  const MatchState state = twoASide({.x = 55.0, .y = 5.0}, {.x = 55.0, .y = 35.0});
  bool freed = false;
  playTouch(state, touchAt({.x = 20.0, .y = 40.0}, 15.0, 5.0, {.x = 20.0, .y = 20.0}), 60,
            [&](const MatchSimulation& simulation) {
              const auto& ball = simulation.state().ball();
              if (!freed && !ball.owner) {
                freed = true;
                REQUIRE(ball.position.y == 40.0);
              }
            });
  REQUIRE(freed);
}

TEST_CASE("A dribble whose touch goes in is not lost", "[carrying]") {
  // Home 1 dribbles past away 3 and touches the ball into the empty goal; he
  // does not follow.
  const MatchState state = twoASide({.x = 55.0, .y = 5.0}, {.x = 55.0, .y = 35.0});
  MatchSystem dribble{.name = "dribble",
                      .update = [](const MatchStepContext& context, const MatchState& /*current*/,
                                   MatchStateWriter& next) {
                        if (context.tick() == SimTick(0)) {
                          next.setPendingAction(0, CarryIntent{.carrier = PlayerId(1),
                                                               .target = {.x = 60.0, .y = 20.0},
                                                               .speed = 15.0,
                                                               .seconds = 5.0,
                                                               .errorFactor = 0.0});
                          next.tactical(0).carry = CarryRun{.kind = CarryKind::kDribble,
                                                            .target = {.x = 60.0, .y = 20.0},
                                                            .subject = PlayerId(3),
                                                            .since = context.tick(),
                                                            .focus = {.x = 60.0, .y = 20.0},
                                                            .gaze = {.x = 1.0, .y = 0.0},
                                                            .touchedAt = std::nullopt,
                                                            .retakenAt = std::nullopt,
                                                            .dribbling = true};
                        }
                      }};
  bool attempted = false;
  bool scored = false;
  playTouch(state, std::move(dribble), 150, [&](const MatchSimulation& simulation) {
    for (const MatchEvent& event : simulation.events()) {
      attempted = attempted || std::holds_alternative<DribbleAttempted>(event);
      scored = scored || std::holds_alternative<GoalScored>(event);
      REQUIRE_FALSE(std::holds_alternative<DribbleEnded>(event));
    }
  });
  REQUIRE(attempted);
  REQUIRE(scored);
}

TEST_CASE("A pass for the ball rolling ahead of its owner is dropped", "[carrying]") {
  // Home 1 touches the ball ahead; while it rolls a pass to home 2 comes in.
  const MatchState state = twoASide({.x = 55.0, .y = 5.0}, {.x = 55.0, .y = 35.0});
  MatchSystem pass{.name = "pass",
                   .update = [](const MatchStepContext& context, const MatchState& current,
                                MatchStateWriter& next) {
                     if (context.tick() == SimTick(4)) {
                       REQUIRE(current.ball().touch.has_value());
                       next.setPendingAction(0, PassIntent{.passer = PlayerId(1),
                                                           .target = {.x = 3.0, .y = 20.0},
                                                           .speed = 10.0,
                                                           .receiver = PlayerId(2)});
                     }
                   }};
  std::vector<MatchSystem> extra;
  extra.push_back(std::move(pass));
  playTouch(
      state, touchAt({.x = 24.5, .y = 20.0}, 4.0, 0.4, {.x = 30.0, .y = 20.0}), 30,
      [](const MatchSimulation& simulation) {
        REQUIRE(simulation.state().ball().owner == PlayerId(1));
        for (const MatchEvent& event : simulation.events()) {
          REQUIRE_FALSE(std::holds_alternative<PassAttempted>(event));
        }
      },
      std::move(extra));
}

TEST_CASE("A challenge cannot take a ball rolling ahead of its owner", "[carrying]") {
  // Away 3, pressing, right next to the carrier while his ball rolls.
  const MatchState state = twoASide({.x = 21.0, .y = 20.5}, {.x = 55.0, .y = 35.0});
  const MatchSystem pressing{.name = "press",
                             .update = [](const MatchStepContext& context,
                                          const MatchState& /*current*/, MatchStateWriter& next) {
                               next.tactical(2).action = ElyverseFootball::SimMatch::PlayerAction{
                                   .type = ElyverseFootball::SimMatch::ActionType::kPressCarrier,
                                   .target = {.x = 21.0, .y = 20.5},
                                   .subject = PlayerId(1),
                                   .decidedAt = context.tick(),
                                   .withBall = false};
                             }};
  ElyverseFootball::SimMatch::ChallengeConfig challenge;
  challenge.winChance = 1.0;
  challenge.protectSeconds = 0.0;
  playTouch(state, touchAt({.x = 30.0, .y = 20.0}, 1.5, 5.0, {.x = 20.0, .y = 20.0}), 30,
            [](const MatchSimulation& simulation) {
              if (simulation.previousState().ball().touch) {
                for (const MatchEvent& event : simulation.events()) {
                  REQUIRE_FALSE(std::holds_alternative<BallWon>(event));
                }
              }
            },
            {pressing, makeChallengeSystem(challenge)});
}
