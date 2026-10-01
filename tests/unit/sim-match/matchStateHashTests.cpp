#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "kickoffScenario.hpp"
#include "matchSimulation.hpp"
#include "matchState.hpp"
#include "matchStateHash.hpp"
#include "referenceTactic.hpp"
#include "tactic.hpp"
#include "vec2.hpp"

using ElyverseFootball::SimCore::PlayerId;
using ElyverseFootball::SimCore::SimTick;
using ElyverseFootball::SimCore::Vec2;
using ElyverseFootball::SimMatch::BallTouch;
using ElyverseFootball::SimMatch::GoalRecord;
using ElyverseFootball::SimMatch::hashMatchState;
using ElyverseFootball::SimMatch::makeSevenASideKickoff;
using ElyverseFootball::SimMatch::MatchSimulation;
using ElyverseFootball::SimMatch::MatchState;
using ElyverseFootball::SimMatch::MatchStateSpec;
using ElyverseFootball::SimMatch::MatchStateWriter;
using ElyverseFootball::SimMatch::MatchStepContext;
using ElyverseFootball::SimMatch::MatchSystem;
using ElyverseFootball::SimMatch::Observation;
using ElyverseFootball::SimMatch::ObservedEntity;
using ElyverseFootball::SimMatch::PassIntent;
using ElyverseFootball::SimMatch::Pitch;
using ElyverseFootball::SimMatch::PlayerMatchState;
using ElyverseFootball::SimMatch::ShotIntent;
using ElyverseFootball::SimMatch::ShotRecord;
using ElyverseFootball::SimMatch::TeamSide;

namespace {

[[nodiscard]] MatchStateSpec kickoffSpec() {
  const auto state = makeSevenASideKickoff(Pitch(60.0, 40.0));
  REQUIRE(state.has_value());
  return {.pitch = state->pitch(),
          .players = {state->players().begin(), state->players().end()},
          .ball = state->ball(),
          .playersPerSide = state->playersPerSide()};
}

[[nodiscard]] std::uint64_t hashOf(const MatchStateSpec& spec) {
  const auto state = MatchState::create(spec);
  REQUIRE(state.has_value());
  return hashMatchState(*state);
}

// A field's name and how to change it.
using Change = std::pair<std::string, std::function<void(MatchStateSpec&)>>;

}  // namespace

TEST_CASE("Equal states hash equally", "[matchStateHash]") {
  REQUIRE(hashOf(kickoffSpec()) == hashOf(kickoffSpec()));
}

TEST_CASE("The kickoff hash is pinned", "[matchStateHash]") {
  // Changes when the fixture, a state field or the hash encoding changes;
  // each of those invalidates recorded replays, so update it deliberately.
  // Re-pinned when the ball gained its height, vertical velocity and spin:
  // the kickoff ball lies still on the grass as it always has, but the hash
  // covers three numbers more. Re-pinned again when the pending pass became
  // a pending action per player, when players gained a shot accuracy and
  // when the state gained its last shot: the hash covers what they add. And
  // again with shot execution: players gained a technique and a strong and a
  // weak foot, the state its score and its last goal. And again when players
  // gained a keeper's positioning and anticipation and the tactics a
  // goalkeeper's sweeping dial.
  REQUIRE(hashOf(kickoffSpec()) == 0x120dce64d696017aULL);
}

// Guards against a field that is added to the state but forgotten here.
TEST_CASE("Every field of the state changes the hash", "[matchStateHash]") {
  const std::uint64_t original = hashOf(kickoffSpec());
  const std::vector<Change> changes{
      {"pitch length", [](auto& spec) { spec.pitch = Pitch(61.0, 40.0); }},
      {"pitch width", [](auto& spec) { spec.pitch = Pitch(60.0, 41.0); }},
      {"player id", [](auto& spec) { spec.players.at(3).playerId = PlayerId(99); }},
      {"player side",
       [](auto& spec) {
         spec.players.at(0).side = TeamSide::kAway;
         spec.players.at(13).side = TeamSide::kHome;
       }},
      {"player order", [](auto& spec) { std::swap(spec.players.at(0), spec.players.at(1)); }},
      {"player position", [](auto& spec) { spec.players.at(5).position.x += 0.001; }},
      {"player velocity", [](auto& spec) { spec.players.at(5).velocity.y = 1.0; }},
      {"max speed", [](auto& spec) { spec.players.at(2).attributes.maxSpeed = 8.0; }},
      {"acceleration", [](auto& spec) { spec.players.at(2).attributes.acceleration = 3.0; }},
      {"shot accuracy", [](auto& spec) { spec.players.at(2).attributes.shotAccuracy = 0.9; }},
      {"shot technique", [](auto& spec) { spec.players.at(2).attributes.shotTechnique = 0.9; }},
      {"strong foot",
       [](auto& spec) {
         spec.players.at(2).attributes.strongFoot = ElyverseFootball::SimMatch::Foot::kLeft;
       }},
      {"weak foot accuracy",
       [](auto& spec) { spec.players.at(2).attributes.weakFootAccuracy = 0.9; }},
      {"keeper positioning",
       [](auto& spec) { spec.players.at(0).attributes.keeperPositioning = 0.9; }},
      {"keeper anticipation",
       [](auto& spec) { spec.players.at(0).attributes.keeperAnticipation = 0.9; }},
      {"score home", [](auto& spec) { spec.score.home = 1; }},
      {"score away", [](auto& spec) { spec.score.away = 1; }},
      {"target", [](auto& spec) { spec.players.at(9).target = Vec2{}; }},
      {"facing", [](auto& spec) { spec.players.at(4).facing = Vec2{.x = 0.0, .y = 1.0}; }},
      {"ball position", [](auto& spec) { spec.ball.position.y = 1.0; }},
      {"ball velocity", [](auto& spec) { spec.ball.velocity.x = -1.0; }},
      {"ball height", [](auto& spec) { spec.ball.height = 2.0; }},
      {"ball vertical velocity", [](auto& spec) { spec.ball.verticalVelocity = 5.0; }},
      {"ball spin", [](auto& spec) { spec.ball.spin = 20.0; }},
      {"ball owner", [](auto& spec) { spec.ball.owner = PlayerId(7); }},
      {"last touch",
       [](auto& spec) {
         spec.ball.lastTouch = BallTouch{.playerId = PlayerId(7), .tick = SimTick(0)};
       }},
  };

  for (const auto& [field, change] : changes) {
    CAPTURE(field);
    MatchStateSpec spec = kickoffSpec();
    change(spec);
    REQUIRE(hashOf(spec) != original);
  }
}

TEST_CASE("Perception memories are part of the hash", "[matchStateHash]") {
  const auto kickoff = makeSevenASideKickoff(Pitch(60.0, 40.0));
  REQUIRE(kickoff.has_value());
  const auto remember = [](const Observation observation) {
    return MatchSystem{.name = "remember",
                       .update = [observation](const MatchStepContext&, const MatchState&,
                                               MatchStateWriter& next) {
                         next.perception(2).observations = {observation};
                       }};
  };
  const Observation seenBall{.entity = ObservedEntity::ball(),
                             .position = {.x = 30.0, .y = 20.0},
                             .velocity = {},
                             .confidence = 1.0,
                             .lastSeen = SimTick(0)};
  Observation older = seenBall;
  older.confidence = 0.5;
  Observation seenPlayer = seenBall;
  seenPlayer.entity = ObservedEntity::player(PlayerId(9));

  const auto hashAfter = [&](const Observation observation) {
    MatchSimulation simulation({.initialState = *kickoff,
                                .seed = 1,
                                .ticksPerSecond = 30,
                                .systems = {remember(observation)},
                                .commands = {}});
    REQUIRE(simulation.step().has_value());
    return hashMatchState(simulation.state());
  };

  const std::uint64_t empty = hashMatchState(*kickoff);
  REQUIRE(hashAfter(seenBall) != empty);
  REQUIRE(hashAfter(seenBall) != hashAfter(older));
  REQUIRE(hashAfter(seenBall) != hashAfter(seenPlayer));
  REQUIRE(hashAfter(seenBall) == hashAfter(seenBall));
}

// Guards the pending actions and the last shot, which only a writer sets.
TEST_CASE("Pending actions and the last shot are part of the hash", "[matchStateHash]") {
  const auto kickoff = makeSevenASideKickoff(Pitch(60.0, 40.0));
  REQUIRE(kickoff.has_value());
  const auto idOf = [&kickoff](const std::size_t index) {
    return kickoff->players()[index].playerId;
  };
  const auto hashAfter = [&kickoff](std::function<void(MatchStateWriter&)> write) {
    MatchSimulation simulation(
        {.initialState = *kickoff,
         .seed = 1,
         .ticksPerSecond = 30,
         .systems = {MatchSystem{
             .name = "write",
             .update = [write = std::move(write)](const MatchStepContext&, const MatchState&,
                                                  MatchStateWriter& next) { write(next); }}},
         .commands = {}});
    REQUIRE(simulation.step().has_value());
    return hashMatchState(simulation.state());
  };
  const auto pass = [](const std::size_t index, const PassIntent& intent) {
    return [index, intent](MatchStateWriter& next) { next.setPendingAction(index, intent); };
  };
  const auto shot = [](const std::size_t index, const ShotIntent& intent) {
    return [index, intent](MatchStateWriter& next) { next.setPendingAction(index, intent); };
  };
  const auto lastShot = [](const ShotRecord& record) {
    return [record](MatchStateWriter& next) { next.setLastShot(record); };
  };
  const PassIntent aPass{
      .passer = idOf(2), .target = {.x = 30.0, .y = 20.0}, .speed = 10.0, .receiver = idOf(5)};
  const ShotIntent aShot{
      .shooter = idOf(2), .target = {.x = 60.0, .y = 20.0}, .height = 1.0, .speed = 25.0};
  const ShotRecord aRecord{.shooter = idOf(2), .from = {.x = 40.0, .y = 20.0}, .tick = SimTick(5)};
  const auto goal = [](const GoalRecord& record) {
    return [record](MatchStateWriter& next) { next.addGoal(record); };
  };
  const GoalRecord aGoal{.side = TeamSide::kHome, .scorer = idOf(2), .tick = SimTick(9)};
  const auto changed = [](auto value, auto change) {
    change(value);
    return value;
  };

  const std::vector<std::pair<std::string, std::uint64_t>> hashes{
      {"nothing", hashAfter([](MatchStateWriter&) {})},
      {"pass", hashAfter(pass(2, aPass))},
      {"pass in another slot",
       hashAfter(pass(3, changed(aPass, [&](PassIntent& intent) { intent.passer = idOf(3); })))},
      {"pass target",
       hashAfter(pass(2, changed(aPass, [](PassIntent& intent) { intent.target.y = 21.0; })))},
      {"pass speed",
       hashAfter(pass(2, changed(aPass, [](PassIntent& intent) { intent.speed = 11.0; })))},
      {"pass receiver",
       hashAfter(pass(2, changed(aPass, [&](PassIntent& intent) { intent.receiver = idOf(6); })))},
      {"pass into space",
       hashAfter(pass(2, changed(aPass, [](PassIntent& intent) { intent.receiver.reset(); })))},
      {"shot", hashAfter(shot(2, aShot))},
      {"shot in another slot",
       hashAfter(shot(3, changed(aShot, [&](ShotIntent& intent) { intent.shooter = idOf(3); })))},
      {"shot target",
       hashAfter(shot(2, changed(aShot, [](ShotIntent& intent) { intent.target.y = 21.0; })))},
      {"shot height",
       hashAfter(shot(2, changed(aShot, [](ShotIntent& intent) { intent.height = 2.0; })))},
      {"shot speed",
       hashAfter(shot(2, changed(aShot, [](ShotIntent& intent) { intent.speed = 24.0; })))},
      {"last shot", hashAfter(lastShot(aRecord))},
      {"last shot shooter", hashAfter(lastShot(changed(
                                aRecord, [&](ShotRecord& record) { record.shooter = idOf(3); })))},
      {"last shot from",
       hashAfter(lastShot(changed(aRecord, [](ShotRecord& record) { record.from.x = 41.0; })))},
      {"last shot tick",
       hashAfter(lastShot(changed(aRecord, [](ShotRecord& record) { record.tick = SimTick(6); })))},
      {"last shot deflection", hashAfter(lastShot(changed(aRecord,
                                                          [&](ShotRecord& record) {
                                                            record.deflection =
                                                                BallTouch{.playerId = idOf(9),
                                                                          .tick = SimTick(7)};
                                                          })))},
      {"last shot deflection by another",
       hashAfter(lastShot(changed(aRecord,
                                  [&](ShotRecord& record) {
                                    record.deflection =
                                        BallTouch{.playerId = idOf(10), .tick = SimTick(7)};
                                  })))},
      {"last shot resolved",
       hashAfter(lastShot(changed(aRecord, [](ShotRecord& record) { record.resolved = true; })))},
      {"goal", hashAfter(goal(aGoal))},
      {"goal for the other side",
       hashAfter(goal(changed(aGoal, [](GoalRecord& record) { record.side = TeamSide::kAway; })))},
      {"goal scorer",
       hashAfter(goal(changed(aGoal, [&](GoalRecord& record) { record.scorer = idOf(3); })))},
      {"goal without a scorer",
       hashAfter(goal(changed(aGoal, [](GoalRecord& record) { record.scorer.reset(); })))},
      {"goal tick",
       hashAfter(goal(changed(aGoal, [](GoalRecord& record) { record.tick = SimTick(10); })))},
  };
  for (std::size_t first = 0; first < hashes.size(); ++first) {
    for (std::size_t second = 0; second < first; ++second) {
      CAPTURE(hashes.at(first).first, hashes.at(second).first);
      REQUIRE(hashes.at(first).second != hashes.at(second).second);
    }
  }
}

TEST_CASE("Tactics are part of the hash", "[matchStateHash]") {
  using ElyverseFootball::SimMatch::TeamTactics;
  using ElyverseFootball::SimTactics::referenceTacticSpec;
  using ElyverseFootball::SimTactics::Tactic;
  const auto reference = Tactic::create(referenceTacticSpec());
  auto otherSpec = referenceTacticSpec();
  otherSpec.name = "other";
  const auto other = Tactic::create(otherSpec);
  REQUIRE(reference.has_value());
  REQUIRE(other.has_value());

  const auto hashWith = [](const TeamTactics& tactics) {
    auto state = MatchState::create(kickoffSpec(), tactics);
    REQUIRE(state.has_value());
    return hashMatchState(*state);
  };
  const std::uint64_t none = hashWith({});
  REQUIRE(none == hashOf(kickoffSpec()));
  REQUIRE(hashWith({.home = *reference, .away = {}}) != none);
  REQUIRE(hashWith({.home = *reference, .away = {}}) != hashWith({.home = {}, .away = *reference}));
  REQUIRE(hashWith({.home = *reference, .away = {}}) != hashWith({.home = *other, .away = {}}));
  REQUIRE(hashWith({.home = *reference, .away = *other}) ==
          hashWith({.home = *reference, .away = *other}));
}
