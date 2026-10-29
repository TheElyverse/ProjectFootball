// The foul rules (docs/fouls.md): how a foul is judged -- the free kick, the
// penalty, advantage and the cards --, what the foul system does with it, a
// sending-off, and the duels' clean tackles that are no fouls.

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <utility>
#include <variant>
#include <vector>

#include "fouls.hpp"
#include "ids.hpp"
#include "matchEvents.hpp"
#include "matchFixtures.hpp"
#include "matchSetup.hpp"
#include "matchSimulation.hpp"
#include "matchState.hpp"
#include "pitch.hpp"
#include "restartKind.hpp"
#include "simTime.hpp"
#include "vec2.hpp"

using ElyverseFootball::SimCore::PlayerId;
using ElyverseFootball::SimCore::SimTick;
using ElyverseFootball::SimCore::Vec2;
using ElyverseFootball::SimMatch::BallState;
using ElyverseFootball::SimMatch::benchPosition;
using ElyverseFootball::SimMatch::Card;
using ElyverseFootball::SimMatch::FoulCalled;
using ElyverseFootball::SimMatch::FoulConfig;
using ElyverseFootball::SimMatch::FoulRecord;
using ElyverseFootball::SimMatch::FoulRestart;
using ElyverseFootball::SimMatch::judgeFoul;
using ElyverseFootball::SimMatch::makeFoulSystem;
using ElyverseFootball::SimMatch::MatchEvent;
using ElyverseFootball::SimMatch::MatchSimulation;
using ElyverseFootball::SimMatch::MatchState;
using ElyverseFootball::SimMatch::MatchStateWriter;
using ElyverseFootball::SimMatch::MatchStepContext;
using ElyverseFootball::SimMatch::MatchSystem;
using ElyverseFootball::SimMatch::Pitch;
using ElyverseFootball::SimMatch::PlayerMatchState;
using ElyverseFootball::SimMatch::RestartKind;
using ElyverseFootball::SimMatch::RestartTaken;
using ElyverseFootball::SimMatch::TackleAttempted;
using ElyverseFootball::SimMatch::TeamSide;
using ElyverseFootball::TestSupport::playerAt;
using ElyverseFootball::TestSupport::setupOf;

namespace {

constexpr PlayerId kVictim{2};
constexpr PlayerId kOffender{9};

// Seven a side on the 60 x 40 m sandbox, home attacking +x: home's victim on
// the ball at `victim`, facing up the pitch, away's offender at `offender`,
// away's keeper on his line and its other outfield players at `others` deep.
struct Situation {
  Vec2 victim;
  Vec2 offender;
  // Where along the pitch away's other two outfield players stand.
  double others = 0.0;
  int yellowCards = 0;
};

[[nodiscard]] MatchState situation(const Situation& where) {
  const Vec2 victim = where.victim;
  const Vec2 offender = where.offender;
  const double others = where.others;
  using enum TeamSide;
  const Vec2 left{.x = -1.0, .y = 0.0};
  std::vector<PlayerMatchState> players{playerAt(1, kHome, {.x = 2.0, .y = 20.0}),
                                        playerAt(2, kHome, victim),
                                        playerAt(3, kHome, {.x = 20.0, .y = 5.0}),
                                        playerAt(4, kHome, {.x = 20.0, .y = 12.0}),
                                        playerAt(5, kHome, {.x = 20.0, .y = 20.0}),
                                        playerAt(6, kHome, {.x = 20.0, .y = 28.0}),
                                        playerAt(7, kHome, {.x = 20.0, .y = 35.0}),
                                        playerAt(8, kAway, {.x = 59.0, .y = 20.0}, left),
                                        playerAt(9, kAway, offender, left),
                                        playerAt(10, kAway, {.x = others, .y = 4.0}, left),
                                        playerAt(11, kAway, {.x = others, .y = 36.0}, left),
                                        playerAt(12, kAway, {.x = 15.0, .y = 10.0}, left),
                                        playerAt(13, kAway, {.x = 15.0, .y = 30.0}, left),
                                        playerAt(14, kAway, {.x = 10.0, .y = 20.0}, left)};
  players[8].yellowCards = where.yellowCards;
  BallState ball;
  ball.position = victim + Vec2{.x = 0.5, .y = 0.0};
  ball.owner = kVictim;
  auto state = MatchState::create(
      {.pitch = Pitch(60.0, 40.0), .players = players, .ball = ball, .playersPerSide = 7});
  REQUIRE(state.has_value());
  return *std::move(state);
}

[[nodiscard]] FoulRecord foulAt(const MatchState& state) {
  return {.offender = kOffender,
          .victim = kVictim,
          .tick = SimTick(0),
          .position = state.ball().position};
}

// The foul system over a step in which the duel recorded the foul.
struct Called {
  std::vector<FoulCalled> fouls;
  std::vector<RestartTaken> restarts;
  MatchState after;
};

[[nodiscard]] Called called(const MatchState& state) {
  const FoulRecord foul = foulAt(state);
  MatchSystem duel{
      .name = "duel",
      .update = [foul](const MatchStepContext& /*context*/, const MatchState& /*current*/,
                       MatchStateWriter& next) { next.setLastFoul(foul); },
      .intervalTicks = 1,
      .phaseTicks = 0};
  MatchSimulation simulation({.initialState = state,
                              .seed = 1,
                              .ticksPerSecond = 30,
                              .systems = {duel, makeFoulSystem({.fouls = {.enabled = true}})},
                              .commands = {}});
  REQUIRE(simulation.step().has_value());
  Called result{.fouls = {}, .restarts = {}, .after = simulation.state()};
  for (const MatchEvent& event : simulation.events()) {
    if (const auto* foulCalled = std::get_if<FoulCalled>(&event)) {
      result.fouls.push_back(*foulCalled);
    } else if (const auto* restart = std::get_if<RestartTaken>(&event)) {
      result.restarts.push_back(*restart);
    }
  }
  return result;
}

}  // namespace

TEST_CASE("A foul in midfield is a free kick without a card", "[fouls]") {
  const MatchState state = situation(
      {.victim = {.x = 30.0, .y = 20.0}, .offender = {.x = 31.5, .y = 20.0}, .others = 50.0});
  const auto judged = judgeFoul(state, foulAt(state), state.ball().owner, {});
  REQUIRE(judged.restart == FoulRestart::kFreeKick);
  REQUIRE(judged.card == Card::kNone);
  REQUIRE_FALSE(judged.fromBehind);
  REQUIRE_FALSE(judged.denyingGoal);

  const Called result = called(state);
  REQUIRE(result.fouls.size() == 1);
  REQUIRE(result.fouls.front().offender == kOffender);
  REQUIRE(result.fouls.front().victim == kVictim);
  REQUIRE(result.fouls.front().side == TeamSide::kAway);
  // A direct free kick for home where the foul was, the victim on the ball.
  REQUIRE(result.restarts.size() == 1);
  REQUIRE(result.restarts.front().kind == RestartKind::kDirectFreeKick);
  REQUIRE(result.restarts.front().position == state.ball().position);
  REQUIRE(result.after.ball().owner == kVictim);
}

TEST_CASE("A foul in the attacking third with the ball kept is advantage", "[fouls]") {
  const MatchState state = situation(
      {.victim = {.x = 45.0, .y = 20.0}, .offender = {.x = 46.5, .y = 20.0}, .others = 52.0});
  REQUIRE(judgeFoul(state, foulAt(state), state.ball().owner, {}).restart ==
          FoulRestart::kAdvantage);
  const Called result = called(state);
  REQUIRE(result.fouls.size() == 1);
  REQUIRE(result.restarts.empty());
}

TEST_CASE("A foul inside the penalty area is a penalty", "[fouls]") {
  // Inside the sandbox's area, 9.4 m deep, with two defenders deeper than the
  // victim: no goal denied.
  const MatchState state = situation(
      {.victim = {.x = 51.0, .y = 20.0}, .offender = {.x = 52.5, .y = 20.0}, .others = 51.8});
  const auto judged = judgeFoul(state, foulAt(state), state.ball().owner, {});
  REQUIRE(judged.restart == FoulRestart::kPenalty);
  REQUIRE(judged.card == Card::kNone);
  const Called result = called(state);
  REQUIRE(result.restarts.size() == 1);
  REQUIRE(result.restarts.front().kind == RestartKind::kPenalty);
  REQUIRE(result.restarts.front().position ==
          state.pitch().penaltySpot(ElyverseFootball::SimMatch::GoalEnd::kMaxX));
}

TEST_CASE("Cards follow the tackle from behind and the goal it denied", "[fouls]") {
  // From behind: a yellow.
  const MatchState behind = situation(
      {.victim = {.x = 30.0, .y = 20.0}, .offender = {.x = 28.5, .y = 20.0}, .others = 50.0});
  const auto booked = judgeFoul(behind, foulAt(behind), behind.ball().owner, {});
  REQUIRE(booked.fromBehind);
  REQUIRE(booked.card == Card::kYellow);
  // The last defender stops a run on goal outside the area: a straight red.
  const MatchState through = situation(
      {.victim = {.x = 38.0, .y = 20.0}, .offender = {.x = 39.5, .y = 20.0}, .others = 30.0});
  const auto denied = judgeFoul(through, foulAt(through), through.ball().owner, {});
  REQUIRE(denied.denyingGoal);
  REQUIRE(denied.card == Card::kRed);
  REQUIRE(denied.restart == FoulRestart::kFreeKick);
  // Inside the area it is a penalty and a yellow.
  const MatchState inside = situation(
      {.victim = {.x = 51.0, .y = 20.0}, .offender = {.x = 52.5, .y = 20.0}, .others = 30.0});
  const auto penalty = judgeFoul(inside, foulAt(inside), inside.ball().owner, {});
  REQUIRE(penalty.denyingGoal);
  REQUIRE(penalty.card == Card::kYellow);
  REQUIRE(penalty.restart == FoulRestart::kPenalty);
  // Farther from goal than denyingDistance nothing is denied.
  const MatchState far = situation(
      {.victim = {.x = 25.0, .y = 20.0}, .offender = {.x = 26.5, .y = 20.0}, .others = 20.0});
  REQUIRE_FALSE(judgeFoul(far, foulAt(far), far.ball().owner, {}).denyingGoal);
}

TEST_CASE("A second yellow sends the offender off", "[fouls]") {
  const MatchState state = situation({.victim = {.x = 44.0, .y = 20.0},
                                      .offender = {.x = 42.5, .y = 20.0},
                                      .others = 50.0,
                                      .yellowCards = 1});
  REQUIRE(judgeFoul(state, foulAt(state), state.ball().owner, {}).card == Card::kSecondYellow);
  const Called result = called(state);
  REQUIRE(result.fouls.front().card == Card::kSecondYellow);
  const PlayerMatchState& offender = result.after.players()[8];
  REQUIRE(offender.sentOff);
  REQUIRE(offender.yellowCards == 2);
  REQUIRE(offender.position == benchPosition(state.pitch()));
  // In the attacking third, but no advantage while a player goes off: a free
  // kick.
  REQUIRE(result.restarts.size() == 1);
}

TEST_CASE("A side plays on with a player sent off", "[fouls]") {
  using ElyverseFootball::SimMatch::makeTacticMatch;
  using ElyverseFootball::SimMatch::ObservedEntity;
  using ElyverseFootball::SimMatch::startMatch;
  auto setup = setupOf("tactic-match", 3);
  setup.config.fouls.enabled = true;
  MatchSystem sendOff{
      .name = "send off",
      .update =
          [](const MatchStepContext& context, const MatchState& current, MatchStateWriter& next) {
            if (context.tick() == SimTick(30)) {
              next.sendOff(9, benchPosition(current.pitch()));
            }
          },
      .intervalTicks = 1,
      .phaseTicks = 0};
  // Last, as the foul system comes after the systems that move players.
  std::vector<MatchSystem> systems = ElyverseFootball::SimMatch::makeMatchSystems(setup.config);
  systems.push_back(sendOff);
  MatchSimulation simulation({.initialState = setup.initialState,
                              .seed = setup.seed,
                              .ticksPerSecond = setup.config.ticksPerSecond,
                              .systems = std::move(systems),
                              .commands = setup.commands});
  for (int tick = 0; tick < 900; ++tick) {
    REQUIRE(simulation.step().has_value());
  }
  const MatchState& state = simulation.state();
  const PlayerMatchState& off = state.players()[9];
  CAPTURE(off.position.x, off.position.y);
  REQUIRE(off.sentOff);
  REQUIRE(off.position == benchPosition(state.pitch()));
  REQUIRE_FALSE(state.ball().owner == off.playerId);
  // Nobody presses him, blocks a lane to him or runs at him.
  for (const TeamSide side : {TeamSide::kHome, TeamSide::kAway}) {
    if (const auto& press = state.press(side)) {
      for (const auto& assignment : press->assignments) {
        REQUIRE_FALSE(assignment.subject == off.playerId);
      }
    }
  }
  for (std::size_t index = 0; index < state.players().size(); ++index) {
    if (const auto& action = state.tactical(index).action) {
      REQUIRE_FALSE(action->subject == off.playerId);
    }
  }
  for (std::size_t index = 0; index < state.players().size(); ++index) {
    REQUIRE(state.perception(index).find(ObservedEntity::player(off.playerId)) == nullptr);
  }
}

TEST_CASE("A clean tackle is no foul, a lunge through the man is", "[fouls]") {
  int clean = 0;
  int lungesThrough = 0;
  int called = 0;
  for (std::uint64_t seed = 1; seed <= 20; ++seed) {
    auto setup = setupOf("duel-ball-won", seed);
    setup.config.fouls.enabled = true;
    MatchSimulation simulation = ElyverseFootball::SimMatch::startMatch(setup);
    for (int tick = 0; tick < 150; ++tick) {
      REQUIRE(simulation.step().has_value());
      for (const MatchEvent& event : simulation.events()) {
        if (const auto* tackle = std::get_if<TackleAttempted>(&event)) {
          using ElyverseFootball::SimMatch::LungeResult;
          clean += tackle->result == LungeResult::kWon ? 1 : 0;
          lungesThrough += tackle->result == LungeResult::kFoul ? 1 : 0;
        }
        called += std::holds_alternative<FoulCalled>(event) ? 1 : 0;
      }
    }
  }
  CAPTURE(clean, lungesThrough, called);
  REQUIRE(clean > 0);
  REQUIRE(called == lungesThrough);
}
