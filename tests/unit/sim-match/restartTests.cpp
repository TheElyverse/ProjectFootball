#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

#include "matchCommand.hpp"
#include "matchEvents.hpp"
#include "matchSetup.hpp"
#include "matchSimulation.hpp"
#include "matchState.hpp"
#include "referenceTactic.hpp"
#include "restart.hpp"
#include "scenarios.hpp"
#include "tactic.hpp"
#include "vec2.hpp"

using ElyverseFootball::SimCore::PlayerId;
using ElyverseFootball::SimCore::SimTick;
using ElyverseFootball::SimCore::Vec2;
using ElyverseFootball::SimMatch::BallState;
using ElyverseFootball::SimMatch::BallTouch;
using ElyverseFootball::SimMatch::isOutOfPlay;
using ElyverseFootball::SimMatch::MatchConfig;
using ElyverseFootball::SimMatch::MatchEvent;
using ElyverseFootball::SimMatch::MatchState;
using ElyverseFootball::SimMatch::Pitch;
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

// Two scripted players a side on a 60 x 40 pitch: home 1 near its goal at
// x = 2, home 2 at (20, 30); away 3 at (40, 30), away 4 near its goal at
// x = 58.
[[nodiscard]] MatchState scene(const BallState& ball) {
  auto state =
      MatchState::create({.pitch = Pitch(60.0, 40.0),
                          .players = {playerAt(1, TeamSide::kHome, {.x = 2.0, .y = 20.0}),
                                      playerAt(2, TeamSide::kHome, {.x = 20.0, .y = 30.0}),
                                      playerAt(3, TeamSide::kAway, {.x = 40.0, .y = 30.0}),
                                      playerAt(4, TeamSide::kAway, {.x = 58.0, .y = 20.0})},
                          .ball = ball,
                          .playersPerSide = 2});
  REQUIRE(state.has_value());
  return *std::move(state);
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
