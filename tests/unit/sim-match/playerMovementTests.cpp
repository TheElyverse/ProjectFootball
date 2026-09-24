#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "kickoffScenario.hpp"
#include "matchCommand.hpp"
#include "matchSimulation.hpp"
#include "matchState.hpp"
#include "playerMovement.hpp"
#include "vec2.hpp"

using ElyverseFootball::SimCore::PlayerId;
using ElyverseFootball::SimCore::SimTick;
using ElyverseFootball::SimCore::Vec2;
using ElyverseFootball::SimMatch::kDefaultAcceleration;
using ElyverseFootball::SimMatch::kDefaultMaxSpeed;
using ElyverseFootball::SimMatch::kDefaultTicksPerSecond;
using ElyverseFootball::SimMatch::makePlayerMovementSystem;
using ElyverseFootball::SimMatch::makeSevenASideKickoff;
using ElyverseFootball::SimMatch::MatchSimulation;
using ElyverseFootball::SimMatch::MatchState;
using ElyverseFootball::SimMatch::MovePlayerCommand;
using ElyverseFootball::SimMatch::Pitch;
using ElyverseFootball::SimMatch::PlayerKinematics;
using ElyverseFootball::SimMatch::PlayerMatchState;
using ElyverseFootball::SimMatch::ScheduledCommand;
using ElyverseFootball::SimMatch::stepPlayerMovement;

namespace {

constexpr double kSecondsPerTick = 1.0 / kDefaultTicksPerSecond;
// Room for rounding in comparisons against a limit the model computes.
constexpr double kTolerance = 1e-9;

[[nodiscard]] PlayerMatchState playerAt(const Vec2 position,
                                        const std::optional<Vec2> target = std::nullopt,
                                        const Vec2 velocity = {}) {
  return {.playerId = PlayerId(1),
          .side = {},
          .position = position,
          .velocity = velocity,
          .attributes = {},
          .target = target,
          .facing = {.x = 1.0, .y = 0.0}};
}

// Moves the player tick by tick and records every state, including the first.
[[nodiscard]] std::vector<PlayerMatchState> trajectory(PlayerMatchState player, const int ticks) {
  std::vector<PlayerMatchState> states{player};
  for (int tick = 0; tick < ticks; ++tick) {
    const PlayerKinematics moved = stepPlayerMovement(player, kSecondsPerTick);
    player.position = moved.position;
    player.velocity = moved.velocity;
    states.push_back(player);
  }
  return states;
}

[[nodiscard]] double speedOf(const PlayerMatchState& player) {
  return std::sqrt(player.velocity.lengthSquared());
}

// The first tick at which the player stands on his target, at rest;
// states.size() if he never does.
[[nodiscard]] std::size_t arrivalTick(const std::vector<PlayerMatchState>& states) {
  for (std::size_t tick = 0; tick < states.size(); ++tick) {
    const PlayerMatchState& player = states[tick];
    if (player.target == player.position && player.velocity == Vec2{}) {
      return tick;
    }
  }
  return states.size();
}

// The velocity change between two ticks stays within a·Δt, except on arrival,
// where the player drops the last bit of speed, at most 2·a·Δt.
void requireAccelerationWithinLimits(const std::vector<PlayerMatchState>& states) {
  constexpr double kTickChange = kDefaultAcceleration * kSecondsPerTick;
  for (std::size_t tick = 1; tick < states.size(); ++tick) {
    CAPTURE(tick);
    const Vec2 change = states[tick].velocity - states[tick - 1].velocity;
    const bool arrived =
        states[tick].target == states[tick].position && states[tick].velocity == Vec2{};
    const double limit = arrived ? 2.0 * kTickChange : kTickChange;
    REQUIRE(std::sqrt(change.lengthSquared()) <= limit + kTolerance);
  }
}

[[nodiscard]] MatchState kickoff() {
  auto state = makeSevenASideKickoff(Pitch(60.0, 40.0));
  REQUIRE(state.has_value());
  return *std::move(state);
}

}  // namespace

TEST_CASE("A player without a target stays where he is", "[playerMovement]") {
  const auto states = trajectory(playerAt({.x = 10.0, .y = 10.0}), 90);

  for (const PlayerMatchState& player : states) {
    REQUIRE(player.position == Vec2{.x = 10.0, .y = 10.0});
    REQUIRE(player.velocity == Vec2{});
  }
}

TEST_CASE("A player on his target stays there", "[playerMovement]") {
  const Vec2 spot{.x = 25.0, .y = 5.0};
  const auto states = trajectory(playerAt(spot, spot), 60);

  for (const PlayerMatchState& player : states) {
    REQUIRE(player.position == spot);
    REQUIRE(player.velocity == Vec2{});
  }
}

TEST_CASE("A moving player without a target slows to a stop", "[playerMovement]") {
  const auto states =
      trajectory(playerAt({.x = 10.0, .y = 10.0}, std::nullopt, {.x = 6.0, .y = 0.0}), 90);

  for (std::size_t tick = 1; tick < states.size(); ++tick) {
    const double slowdown = speedOf(states[tick - 1]) - speedOf(states[tick]);
    REQUIRE(slowdown >= 0.0);
    REQUIRE(slowdown <= (kDefaultAcceleration * kSecondsPerTick) + kTolerance);
  }
  REQUIRE(states.back().velocity == Vec2{});
  // 6 m/s at 4 m/s² stops within 6² / (2 · 4) = 4.5 m, give or take a tick.
  REQUIRE(states.back().position.x > 10.0 + 4.0);
  REQUIRE(states.back().position.x < 10.0 + 4.5 + (6.0 * kSecondsPerTick));
}

TEST_CASE("A short movement ends exactly on the target without passing it", "[playerMovement]") {
  const Vec2 target{.x = 12.0, .y = 10.0};
  const auto states = trajectory(playerAt({.x = 10.0, .y = 10.0}, target), 90);
  requireAccelerationWithinLimits(states);

  const std::size_t arrived = arrivalTick(states);
  REQUIRE(arrived < states.size());
  for (std::size_t tick = 1; tick < states.size(); ++tick) {
    CAPTURE(tick);
    REQUIRE(states[tick].position.x >= states[tick - 1].position.x);
    REQUIRE(states[tick].position.x <= target.x);
    REQUIRE(states[tick].position.y == target.y);
  }
  for (std::size_t tick = arrived; tick < states.size(); ++tick) {
    REQUIRE(states[tick].position == target);
    REQUIRE(states[tick].velocity == Vec2{});
  }
}

TEST_CASE("Speed and acceleration stay within the player's limits", "[playerMovement]") {
  const auto states = trajectory(playerAt({.x = 2.0, .y = 3.0}, Vec2{.x = 55.0, .y = 37.0}), 400);

  double topSpeed = 0.0;
  for (const PlayerMatchState& player : states) {
    REQUIRE(speedOf(player) <= kDefaultMaxSpeed + kTolerance);
    topSpeed = std::max(topSpeed, speedOf(player));
  }
  requireAccelerationWithinLimits(states);
  // A long run reaches full speed before braking.
  REQUIRE(topSpeed > kDefaultMaxSpeed - kTolerance);
  REQUIRE(arrivalTick(states) < states.size());
}

TEST_CASE("A 20 m run from rest takes as long as the model predicts", "[playerMovement]") {
  // Accelerate to 7.5 m/s over 7.03 m, cruise, brake over another 7.03 m:
  // 1.875 s + 0.79 s + 1.875 s = 4.54 s.
  const auto states = trajectory(playerAt({.x = 10.0, .y = 20.0}, Vec2{.x = 30.0, .y = 20.0}), 300);

  const std::size_t arrived = arrivalTick(states);
  REQUIRE(arrived < states.size());
  const double seconds = static_cast<double>(arrived) * kSecondsPerTick;
  CAPTURE(seconds);
  REQUIRE(seconds > 4.4);
  REQUIRE(seconds < 4.7);
}

TEST_CASE("A player turns around when his target changes", "[playerMovement]") {
  PlayerMatchState player = playerAt({.x = 10.0, .y = 20.0}, Vec2{.x = 40.0, .y = 20.0});
  auto run = trajectory(player, 45);
  player = run.back();
  REQUIRE(speedOf(player) > 5.0);

  const Vec2 behind{.x = 12.0, .y = 25.0};
  player.target = behind;
  const auto back = trajectory(player, 300);

  const std::size_t arrived = arrivalTick(back);
  REQUIRE(arrived < back.size());
  for (std::size_t tick = arrived; tick < back.size(); ++tick) {
    REQUIRE(back[tick].position == behind);
  }
  requireAccelerationWithinLimits(back);
}

TEST_CASE("A slow player does not jump back onto a target just behind him", "[playerMovement]") {
  // Moving away at 0.2 m/s -- slow enough to stop -- with a target 1 mm
  // behind: the step moves him further away, so it must not end on the
  // target.
  PlayerMatchState player = playerAt({.x = 10.0, .y = 20.0}, Vec2{.x = 9.999, .y = 20.0});
  player.velocity = {.x = 0.2, .y = 0.0};

  const PlayerKinematics moved = stepPlayerMovement(player, kSecondsPerTick);

  REQUIRE(moved.position.x > player.position.x);
  REQUIRE(moved.velocity.x > 0.0);
  const auto back = trajectory(player, 60);
  REQUIRE(back.back().position == Vec2{.x = 9.999, .y = 20.0});
}

TEST_CASE("Players follow commands through the movement system", "[playerMovement]") {
  const Vec2 wing{.x = 50.0, .y = 38.0};
  MatchSimulation simulation(
      {.initialState = kickoff(),
       .seed = 1,
       .ticksPerSecond = kDefaultTicksPerSecond,
       .systems = {makePlayerMovementSystem()},
       .commands = {
           {.tick = SimTick(0),
            .command = MovePlayerCommand{.playerId = PlayerId(7), .target = wing}},
           {.tick = SimTick(0),
            .command =
                MovePlayerCommand{.playerId = PlayerId(9), .target = {.x = 70.0, .y = -5.0}}},
       }});
  const MatchState start = simulation.state();

  for (int tick = 0; tick < 300; ++tick) {
    REQUIRE(simulation.step().has_value());
    for (const PlayerMatchState& player : simulation.state().players()) {
      REQUIRE(simulation.state().pitch().contains(player.position));
    }
  }

  const auto players = simulation.state().players();
  REQUIRE(players[6].position == wing);
  // The off-pitch target was moved to the nearest corner.
  REQUIRE(players[8].position == Vec2{.x = 60.0, .y = 0.0});
  // Everyone else had no target and never moved.
  for (std::size_t index = 0; index < players.size(); ++index) {
    if (index != 6 && index != 8) {
      REQUIRE(players[index].position == start.players()[index].position);
    }
  }
}
