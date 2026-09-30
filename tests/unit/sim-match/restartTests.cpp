#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <variant>
#include <vector>

#include "ballPhysics.hpp"
#include "kickoffScenario.hpp"
#include "matchCommand.hpp"
#include "matchEvents.hpp"
#include "matchSetup.hpp"
#include "matchSimulation.hpp"
#include "matchState.hpp"
#include "pitch.hpp"
#include "referenceTactic.hpp"
#include "restart.hpp"
#include "scenarios.hpp"
#include "tactic.hpp"
#include "vec2.hpp"

using ElyverseFootball::SimCore::PlayerId;
using ElyverseFootball::SimCore::SimTick;
using ElyverseFootball::SimCore::Vec2;
using ElyverseFootball::SimMatch::BallPhysics;
using ElyverseFootball::SimMatch::BallState;
using ElyverseFootball::SimMatch::BallTouch;
using ElyverseFootball::SimMatch::isOutOfPlay;
using ElyverseFootball::SimMatch::KickoffLineUp;
using ElyverseFootball::SimMatch::lineUpForKickoff;
using ElyverseFootball::SimMatch::MatchConfig;
using ElyverseFootball::SimMatch::MatchEvent;
using ElyverseFootball::SimMatch::MatchState;
using ElyverseFootball::SimMatch::Pitch;
using ElyverseFootball::SimMatch::PitchCircle;
using ElyverseFootball::SimMatch::planRestart;
using ElyverseFootball::SimMatch::PlayerMatchState;
using ElyverseFootball::SimMatch::RestartKind;
using ElyverseFootball::SimMatch::RestartPlan;
using ElyverseFootball::SimMatch::RestartTaken;
using ElyverseFootball::SimMatch::TeamSide;

namespace {

[[nodiscard]] PlayerMatchState playerAt(const std::uint32_t number, const TeamSide side,
                                        const Vec2 position) {
  return {.playerId = PlayerId(number),
          .side = side,
          .position = position,
          .velocity = {},
          .attributes = {},
          .target = std::nullopt,
          .facing = {.x = 1.0, .y = 0.0}};
}

// Two scripted players a side on a 60 x 40 pitch, home 1 and 2 and away 3
// and 4, standing at these positions.
[[nodiscard]] MatchState sceneOf(const std::array<Vec2, 4>& positions, const BallState& ball) {
  auto state = MatchState::create({.pitch = Pitch(60.0, 40.0),
                                   .players = {playerAt(1, TeamSide::kHome, positions[0]),
                                               playerAt(2, TeamSide::kHome, positions[1]),
                                               playerAt(3, TeamSide::kAway, positions[2]),
                                               playerAt(4, TeamSide::kAway, positions[3])},
                                   .ball = ball,
                                   .playersPerSide = 2});
  REQUIRE(state.has_value());
  return *std::move(state);
}

// Home 1 near its goal at x = 2, home 2 at (20, 30); away 3 at (40, 30),
// away 4 near its goal at x = 58.
[[nodiscard]] MatchState scene(const BallState& ball) {
  return sceneOf({{{.x = 2.0, .y = 20.0},
                   {.x = 20.0, .y = 30.0},
                   {.x = 40.0, .y = 30.0},
                   {.x = 58.0, .y = 20.0}}},
                 ball);
}

// The Laws' kickoff: everybody in his own half, the opponents of the side
// kicking off outside the centre circle -- on it at the nearest.
void requireLegalKickoff(const MatchState& state, const TeamSide kicking,
                         const std::span<const Vec2> positions) {
  const PitchCircle circle = state.pitch().centerCircle();
  REQUIRE(positions.size() == state.players().size());
  for (std::size_t index = 0; index < positions.size(); ++index) {
    CAPTURE(index);
    const TeamSide side = state.players()[index].side;
    REQUIRE(state.pitch().contains(positions[index]));
    if (side == TeamSide::kHome) {
      REQUIRE(positions[index].x <= circle.center.x);
    } else {
      REQUIRE(positions[index].x >= circle.center.x);
    }
    if (side != kicking) {
      REQUIRE(ElyverseFootball::SimCore::distance(positions[index], circle.center) >=
              circle.radiusMeters - 1.0e-9);
    }
  }
}

[[nodiscard]] BallState ballAt(const Vec2 position, const std::optional<std::uint32_t> lastTouch,
                               const Vec2 velocity = {}) {
  return {.position = position,
          .velocity = velocity,
          .owner = std::nullopt,
          .lastTouch = lastTouch ? std::optional(BallTouch{.playerId = PlayerId(*lastTouch),
                                                           .tick = SimTick(0)})
                                 : std::nullopt};
}

}  // namespace

TEST_CASE("A free ball at rest on a line is out of play", "[restart]") {
  REQUIRE(isOutOfPlay(scene(ballAt({.x = 30.0, .y = 0.0}, 2))));
  REQUIRE(isOutOfPlay(scene(ballAt({.x = 60.0, .y = 12.0}, 2))));
  REQUIRE_FALSE(isOutOfPlay(scene(ballAt({.x = 30.0, .y = 0.5}, 2))));
  REQUIRE_FALSE(isOutOfPlay(scene(ballAt({.x = 30.0, .y = 0.0}, 2, {.x = 1.0, .y = 0.0}))));
  BallState owned = ballAt({.x = 30.0, .y = 0.0}, 2);
  owned.owner = PlayerId(2);
  REQUIRE_FALSE(isOutOfPlay(scene(owned)));

  // A ball over the line but still in the air is in play: it has not come to
  // rest anywhere yet.
  BallState flying = ballAt({.x = 30.0, .y = 0.0}, 2);
  flying.height = 1.5;
  REQUIRE_FALSE(isOutOfPlay(scene(flying)));
  BallState rising = ballAt({.x = 30.0, .y = 0.0}, 2);
  rising.verticalVelocity = 4.0;
  REQUIRE_FALSE(isOutOfPlay(scene(rising)));
}

TEST_CASE("Over a touchline the other side throws in, nearest player first", "[restart]") {
  // Home touched it last: away's nearest player, 3, throws in.
  REQUIRE(planRestart(scene(ballAt({.x = 30.0, .y = 40.0}, 2))) ==
          RestartPlan{.kind = RestartKind::kThrowIn, .playerIndex = 2});
  // Away touched it last: home's nearest, 2.
  REQUIRE(planRestart(scene(ballAt({.x = 30.0, .y = 40.0}, 3))) ==
          RestartPlan{.kind = RestartKind::kThrowIn, .playerIndex = 1});
  // Nobody touched it: the side whose half it lies in, away's nearest, 4.
  REQUIRE(planRestart(scene(ballAt({.x = 45.0, .y = 0.0}, std::nullopt))) ==
          RestartPlan{.kind = RestartKind::kThrowIn, .playerIndex = 3});
}

TEST_CASE("Over a goal line it is a corner or a goal kick", "[restart]") {
  // Away put it over home's goal line: goal kick for home.
  REQUIRE(planRestart(scene(ballAt({.x = 0.0, .y = 10.0}, 3))) ==
          RestartPlan{.kind = RestartKind::kGoalKick, .playerIndex = 0});
  // Home put it over its own goal line: corner for away, nearest player 3.
  REQUIRE(planRestart(scene(ballAt({.x = 0.0, .y = 10.0}, 1))) ==
          RestartPlan{.kind = RestartKind::kCorner, .playerIndex = 2});
  // Home put it over away's goal line: goal kick for away.
  REQUIRE(planRestart(scene(ballAt({.x = 60.0, .y = 30.0}, 2))) ==
          RestartPlan{.kind = RestartKind::kGoalKick, .playerIndex = 3});
}

TEST_CASE("A goal kick goes to the goalkeeper, not the nearest player", "[restart]") {
  const auto tactic = ElyverseFootball::SimTactics::Tactic::create(
      ElyverseFootball::SimTactics::referenceTacticSpec());
  REQUIRE(tactic.has_value());
  // Seven a side, from the tactic match fixture: home's goalkeeper is id 1.
  auto setup = ElyverseFootball::SimMatch::makeTacticMatch({.home = *tactic, .away = *tactic}, 1);
  REQUIRE(setup.has_value());
  const MatchState& kickoff = setup->initialState;
  std::vector<PlayerMatchState> players(kickoff.players().begin(), kickoff.players().end());
  // Home's centre back stands nearer to the ball than the goalkeeper.
  players.at(1).position = {.x = 1.0, .y = 35.0};
  auto state = MatchState::create({.pitch = kickoff.pitch(),
                                   .players = std::move(players),
                                   .ball = ballAt({.x = 0.0, .y = 38.0}, 9),
                                   .playersPerSide = 7},
                                  kickoff.tactics());
  REQUIRE(state.has_value());
  REQUIRE(planRestart(*state) == RestartPlan{.kind = RestartKind::kGoalKick, .playerIndex = 0});
}

TEST_CASE("The restart system gives the ball to the taker once it is out", "[restart]") {
  const auto run = [](const bool enabled) {
    MatchConfig config;
    config.restarts.enabled = enabled;
    config.decisions.minHoldSeconds = 1.0e6;
    auto simulation = ElyverseFootball::SimMatch::startMatch(
        {.initialState = scene(ballAt({.x = 30.0, .y = 20.0}, std::nullopt)),
         .config = config,
         .seed = 1,
         .commands = {
             {.tick = SimTick(0),
              .command = ElyverseFootball::SimMatch::GiveBallCommand{.playerId = PlayerId(2)}},
             {.tick = SimTick(1),
              .command = ElyverseFootball::SimMatch::PassCommand{.playerId = PlayerId(2),
                                                                 .target = {.x = 30.0, .y = 45.0},
                                                                 .speed = 20.0,
                                                                 .receiver = std::nullopt}}}});
    std::vector<RestartTaken> restarts;
    for (int step = 0; step < 90; ++step) {
      REQUIRE(simulation.step().has_value());
      for (const MatchEvent& event : simulation.events()) {
        if (const auto* restart = std::get_if<RestartTaken>(&event)) {
          restarts.push_back(*restart);
          REQUIRE(simulation.state().ball().owner == restart->player);
        }
      }
    }
    return restarts;
  };
  const auto restarts = run(true);
  REQUIRE(restarts.size() == 1);
  REQUIRE(restarts.front().kind == RestartKind::kThrowIn);
  REQUIRE(restarts.front().player == PlayerId(3));
  REQUIRE(restarts.front().position.y == 40.0);
  // Disabled, the ball waits on the line.
  REQUIRE(run(false).empty());
}

TEST_CASE("A player on an out-of-play ball does not receive it before the restart", "[restart]") {
  using ElyverseFootball::SimMatch::LooseBallRecovered;
  using ElyverseFootball::SimMatch::PassIntercepted;
  using ElyverseFootball::SimMatch::PassReceived;
  using ElyverseFootball::SimMatch::PossessionChanged;
  // The ball rests on the touchline with away's 3 standing right on it, well
  // within reception's control radius: ball movement runs before the restart
  // system and must leave that ball alone.
  auto state =
      MatchState::create({.pitch = Pitch(60.0, 40.0),
                          .players = {playerAt(1, TeamSide::kHome, {.x = 2.0, .y = 20.0}),
                                      playerAt(2, TeamSide::kHome, {.x = 20.0, .y = 30.0}),
                                      playerAt(3, TeamSide::kAway, {.x = 30.0, .y = 40.0}),
                                      playerAt(4, TeamSide::kAway, {.x = 58.0, .y = 20.0})},
                          .ball = ballAt({.x = 30.0, .y = 40.0}, 2),
                          .playersPerSide = 2});
  REQUIRE(state.has_value());
  MatchConfig config;
  config.restarts.enabled = true;
  config.decisions.minHoldSeconds = 1.0e6;
  auto simulation = ElyverseFootball::SimMatch::startMatch(
      {.initialState = *std::move(state), .config = config, .seed = 1, .commands = {}});

  REQUIRE(simulation.step().has_value());

  int restarts = 0;
  int possessionChanges = 0;
  for (const MatchEvent& event : simulation.events()) {
    restarts += std::holds_alternative<RestartTaken>(event) ? 1 : 0;
    possessionChanges += std::holds_alternative<PossessionChanged>(event) ? 1 : 0;
    REQUIRE_FALSE(std::holds_alternative<PassIntercepted>(event));
    REQUIRE_FALSE(std::holds_alternative<PassReceived>(event));
    REQUIRE_FALSE(std::holds_alternative<LooseBallRecovered>(event));
  }
  REQUIRE(restarts == 1);
  REQUIRE(possessionChanges == 1);
  REQUIRE(simulation.state().ball().owner == PlayerId(3));
}

TEST_CASE("A kickoff lines both sides up in their formations, in their own halves", "[restart]") {
  const auto tactic = ElyverseFootball::SimTactics::Tactic::create(
      ElyverseFootball::SimTactics::referenceTacticSpec());
  REQUIRE(tactic.has_value());
  const auto state = ElyverseFootball::SimMatch::makeSevenASideKickoff(
      Pitch(60.0, 40.0), {}, {.home = *tactic, .away = *tactic});
  REQUIRE(state.has_value());

  const auto lineUp = lineUpForKickoff(*state, TeamSide::kAway, BallPhysics{});
  REQUIRE(lineUp.has_value());
  const KickoffLineUp& kickoff = lineUp.value_or(KickoffLineUp{});
  requireLegalKickoff(*state, TeamSide::kAway, kickoff.positions);

  // Each slot of the base shape at half its depth: the goalkeeper at 0.04 of
  // the pitch length stands at 0.02 of it, the winger at 0.5 at a quarter.
  for (std::size_t slot = 0; slot < 6; ++slot) {
    CAPTURE(slot);
    const auto& shape = tactic->slots()[slot].position;
    REQUIRE(kickoff.positions.at(slot) == Vec2{.x = shape.depth * 30.0, .y = shape.width * 40.0});
    REQUIRE(kickoff.positions.at(7 + slot) ==
            Vec2{.x = 60.0 - (shape.depth * 30.0), .y = shape.width * 40.0});
  }
  REQUIRE(kickoff.positions.at(4) == Vec2{.x = 15.0, .y = 0.12 * 40.0});
  // Home's striker waits in his place; away's, the nearest to the centre
  // spot, stands behind the ball that lies on it.
  REQUIRE(kickoff.positions.at(6) == Vec2{.x = 0.65 * 30.0, .y = 20.0});
  REQUIRE(kickoff.takerIndex == 13);
  REQUIRE(kickoff.positions.at(13) == Vec2{.x = 30.0 + BallPhysics{}.carryDistance, .y = 20.0});
}

TEST_CASE("A scripted side goes back to its own half for a kickoff", "[restart]") {
  // Home 1 is in away's half and away 4 in home's; away 3 stands in the
  // centre circle.
  const MatchState state = sceneOf({{{.x = 45.0, .y = 10.0},
                                     {.x = 20.0, .y = 30.0},
                                     {.x = 31.0, .y = 21.0},
                                     {.x = 10.0, .y = 5.0}}},
                                   ballAt({.x = 60.0, .y = 20.0}, std::nullopt));

  const auto lineUp = lineUpForKickoff(state, TeamSide::kHome, BallPhysics{});
  REQUIRE(lineUp.has_value());
  const KickoffLineUp& kickoff = lineUp.value_or(KickoffLineUp{});
  requireLegalKickoff(state, TeamSide::kHome, kickoff.positions);

  // Home 1 is the nearest to the centre spot once he is back on the halfway
  // line, and takes the kickoff; home 2 was in his half already.
  REQUIRE(kickoff.takerIndex == 0);
  REQUIRE(kickoff.positions.at(0) == Vec2{.x = 30.0 - BallPhysics{}.carryDistance, .y = 20.0});
  REQUIRE(kickoff.positions.at(1) == Vec2{.x = 20.0, .y = 30.0});
  // Away 3 steps straight back onto the circle, away 4 to the halfway line.
  const PitchCircle circle = state.pitch().centerCircle();
  REQUIRE(kickoff.positions.at(2).y == 21.0);
  REQUIRE(kickoff.positions.at(2).x > 31.0);
  REQUIRE(std::abs(ElyverseFootball::SimCore::distance(kickoff.positions.at(2), circle.center) -
                   circle.radiusMeters) < 1.0e-9);
  REQUIRE(kickoff.positions.at(3) == Vec2{.x = 30.0, .y = 5.0});
}

TEST_CASE("A match starts from the kickoff a goal restarts it with", "[restart]") {
  const auto tactic = ElyverseFootball::SimTactics::Tactic::create(
      ElyverseFootball::SimTactics::referenceTacticSpec());
  REQUIRE(tactic.has_value());
  const auto setup =
      ElyverseFootball::SimMatch::makeTacticMatch({.home = *tactic, .away = *tactic}, 1);
  REQUIRE(setup.has_value());
  const MatchState& start = setup->initialState;

  const auto lineUp = lineUpForKickoff(start, TeamSide::kHome, setup->config.ball);
  REQUIRE(lineUp.has_value());
  const KickoffLineUp& kickoff = lineUp.value_or(KickoffLineUp{});
  requireLegalKickoff(start, TeamSide::kHome, kickoff.positions);
  for (std::size_t index = 0; index < start.players().size(); ++index) {
    CAPTURE(index);
    REQUIRE(start.players()[index].position == kickoff.positions.at(index));
  }
  REQUIRE(start.ball().position == start.pitch().center());

  // Home's striker, player 7, kicks off from the centre spot.
  REQUIRE(start.players()[kickoff.takerIndex].playerId == PlayerId(7));
  auto simulation = ElyverseFootball::SimMatch::startMatch(*setup);
  REQUIRE(simulation.step().has_value());
  REQUIRE(simulation.state().ball().owner == PlayerId(7));
  REQUIRE(simulation.state().ball().position == start.pitch().center());
}
