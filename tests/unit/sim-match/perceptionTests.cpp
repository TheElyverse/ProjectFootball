#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

#include "matchSimulation.hpp"
#include "matchState.hpp"
#include "observation.hpp"
#include "perception.hpp"
#include "vec2.hpp"

using Catch::Matchers::WithinAbs;
using ElyverseFootball::SimCore::PlayerId;
using ElyverseFootball::SimCore::SimTick;
using ElyverseFootball::SimCore::Vec2;
using ElyverseFootball::SimMatch::canSee;
using ElyverseFootball::SimMatch::estimatePosition;
using ElyverseFootball::SimMatch::makePerceptionSystem;
using ElyverseFootball::SimMatch::MatchSimulation;
using ElyverseFootball::SimMatch::MatchState;
using ElyverseFootball::SimMatch::MatchStateWriter;
using ElyverseFootball::SimMatch::MatchStepContext;
using ElyverseFootball::SimMatch::MatchSystem;
using ElyverseFootball::SimMatch::Observation;
using ElyverseFootball::SimMatch::ObservedEntity;
using ElyverseFootball::SimMatch::perceive;
using ElyverseFootball::SimMatch::PerceptionConfig;
using ElyverseFootball::SimMatch::Pitch;
using ElyverseFootball::SimMatch::PlayerMatchState;
using ElyverseFootball::SimMatch::PlayerPerception;
using ElyverseFootball::SimMatch::TeamSide;

namespace {

constexpr double kSecondsPerTick = 1.0 / 30.0;
const PerceptionConfig kConfig{};

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

// Observer 1 at (20, 20) looking along +x. Player 2 ahead, player 3 behind at
// 10 m, player 4 behind at 2 m; the ball ahead at 10 m.
[[nodiscard]] MatchState scene() {
  auto state = MatchState::create(
      {.pitch = Pitch(60.0, 40.0),
       .players = {playerAt(1, TeamSide::kHome, {.x = 20.0, .y = 20.0}),
                   playerAt(3, TeamSide::kHome, {.x = 10.0, .y = 20.0}),
                   playerAt(2, TeamSide::kAway, {.x = 35.0, .y = 25.0}),
                   playerAt(4, TeamSide::kAway, {.x = 18.0, .y = 20.0})},
       .ball = {.position = {.x = 30.0, .y = 20.0}, .velocity = {.x = 2.0, .y = 0.0}},
       .playersPerSide = 2});
  REQUIRE(state.has_value());
  return *std::move(state);
}

[[nodiscard]] std::vector<ObservedEntity> entitiesOf(const PlayerPerception& memory) {
  std::vector<ObservedEntity> entities;
  entities.reserve(memory.observations.size());
  for (const Observation& observation : memory.observations) {
    entities.push_back(observation.entity);
  }
  return entities;
}

// A system that moves one player to a position in the step of a given tick.
[[nodiscard]] MatchSystem teleport(const std::size_t index, const SimTick tick,
                                   const Vec2 position) {
  return {
      .name = "teleport " + std::to_string(index),
      .update = [=](const MatchStepContext& context, const MatchState&, MatchStateWriter& next) {
        if (context.tick() == SimTick(tick)) {
          next.setPlayerPosition(index, position);
        }
      }};
}

}  // namespace

TEST_CASE("A player sees what lies in his cone and range", "[perception]") {
  const PlayerMatchState observer = playerAt(1, TeamSide::kHome, {.x = 20.0, .y = 20.0});

  REQUIRE(canSee(observer, {.x = 40.0, .y = 20.0}, kConfig));
  REQUIRE(canSee(observer, {.x = 25.0, .y = 35.0}, kConfig));
  // Range is inclusive: 60 m straight ahead is seen, a hair more is not.
  REQUIRE(canSee(observer, {.x = 80.0, .y = 20.0}, kConfig));
  REQUIRE_FALSE(canSee(observer, {.x = std::nextafter(80.0, 100.0), .y = 20.0}, kConfig));
}

TEST_CASE("The vision cone ends at half the field of view", "[perception]") {
  const PlayerMatchState observer = playerAt(1, TeamSide::kHome, {.x = 20.0, .y = 20.0});
  PerceptionConfig narrow;
  narrow.fieldOfViewDegrees = 90.0;

  // 45 degrees off the facing is the edge of a 90 degree cone.
  REQUIRE(canSee(observer, {.x = 30.0, .y = 29.9}, narrow));
  REQUIRE_FALSE(canSee(observer, {.x = 30.0, .y = 30.1}, narrow));
  // The default 180 degrees: sideways is seen, slightly behind is not.
  REQUIRE(canSee(observer, {.x = 20.001, .y = 30.0}, kConfig));
  REQUIRE_FALSE(canSee(observer, {.x = 19.999, .y = 30.0}, kConfig));
  // Exactly on the edge is inside: straight sideways, and the exact diagonal
  // of the 90 degree cone.
  REQUIRE(canSee(observer, {.x = 20.0, .y = 30.0}, kConfig));
  REQUIRE(canSee(observer, {.x = 20.0, .y = 10.0}, kConfig));
  REQUIRE(canSee(observer, {.x = 30.0, .y = 30.0}, narrow));
}

TEST_CASE("The cone edge is accurate for every field of view", "[perception]") {
  // The cone's cosine comes from a series, not std::cos: it must still put
  // the edge where the angle says, for narrow and wide cones alike.
  const double fieldOfView = GENERATE(10.0, 45.0, 90.0, 120.0, 179.0, 200.0, 270.0, 359.0);
  PerceptionConfig config;
  config.fieldOfViewDegrees = fieldOfView;
  config.awarenessRadius = 0.0;
  const PlayerMatchState observer = playerAt(1, TeamSide::kHome, {.x = 20.0, .y = 20.0});
  const double halfAngle = fieldOfView * std::numbers::pi / 360.0;
  const auto pointAt = [&observer](const double angle) {
    constexpr double kDistance = 10.0;
    return observer.position +
           Vec2{.x = kDistance * std::cos(angle), .y = kDistance * std::sin(angle)};
  };

  // A microradian inside the edge is seen, a microradian outside is not, on
  // both sides of the facing.
  REQUIRE(canSee(observer, pointAt(halfAngle - 1e-6), config));
  REQUIRE(canSee(observer, pointAt(-(halfAngle - 1e-6)), config));
  REQUIRE_FALSE(canSee(observer, pointAt(halfAngle + 1e-6), config));
  REQUIRE_FALSE(canSee(observer, pointAt(-(halfAngle + 1e-6)), config));
}

TEST_CASE("A player senses what is close behind him", "[perception]") {
  const PlayerMatchState observer = playerAt(1, TeamSide::kHome, {.x = 20.0, .y = 20.0});

  REQUIRE(canSee(observer, {.x = 17.0, .y = 20.0}, kConfig));
  REQUIRE_FALSE(canSee(observer, {.x = std::nextafter(17.0, 0.0), .y = 20.0}, kConfig));
}

TEST_CASE("Perceiving records what is seen, ordered, without the observer", "[perception]") {
  const MatchState state = scene();
  PlayerPerception memory;

  perceive(state, 0, SimTick(12), kSecondsPerTick, kConfig, memory);

  // Player 3 stands behind the observer beyond the awareness radius.
  REQUIRE(entitiesOf(memory) == std::vector{ObservedEntity::ball(),
                                            ObservedEntity::player(PlayerId(2)),
                                            ObservedEntity::player(PlayerId(4))});
  const Observation* ball = memory.find(ObservedEntity::ball());
  REQUIRE(ball != nullptr);
  REQUIRE(*ball == Observation{.entity = ObservedEntity::ball(),
                               .position = {.x = 30.0, .y = 20.0},
                               .velocity = {.x = 2.0, .y = 0.0},
                               .confidence = 1.0,
                               .lastSeen = SimTick(12)});
}

TEST_CASE("Unseen observations fade and are forgotten", "[perception]") {
  // Player 2 walks out of sight behind the observer at tick 3 and stays
  // there; the observer keeps looking along +x.
  MatchSimulation simulation(
      {.initialState = scene(),
       .seed = 1,
       .ticksPerSecond = 30,
       .systems = {teleport(2, SimTick(3), {.x = 5.0, .y = 5.0}), makePerceptionSystem(kConfig)},
       .commands = {}});
  std::vector<double> confidences;
  for (int tick = 0; tick < 120; ++tick) {
    REQUIRE(simulation.step().has_value());
    const Observation* observation =
        simulation.state().perception(0).find(ObservedEntity::player(PlayerId(2)));
    confidences.push_back(observation == nullptr ? -1.0 : observation->confidence);
    if (observation != nullptr && tick >= 3) {
      // Last seen at tick 3 where it was before the teleport took effect.
      REQUIRE(observation->lastSeen == SimTick(3));
      REQUIRE(observation->position == Vec2{.x = 35.0, .y = 25.0});
    }
  }

  // Seen at 0 and 3; the teleport lands after tick 3, so from tick 6 on the
  // confidence falls linearly: 1 - age / 3 s.
  REQUIRE(confidences.at(3) == 1.0);
  REQUIRE_THAT(confidences.at(6), WithinAbs(1.0 - (0.1 / 3.0), 1e-12));
  REQUIRE_THAT(confidences.at(48), WithinAbs(1.0 - (1.5 / 3.0), 1e-12));
  // 3 s after tick 3 it is forgotten.
  REQUIRE(confidences.at(92) > 0.0);
  REQUIRE(confidences.at(93) == -1.0);
  REQUIRE(confidences.back() == -1.0);
}

TEST_CASE("Perception updates only on its own ticks", "[perception]") {
  MatchSimulation simulation({.initialState = scene(),
                              .seed = 1,
                              .ticksPerSecond = 30,
                              .systems = {makePerceptionSystem(kConfig)},
                              .commands = {}});
  std::vector<std::int64_t> lastSeen;
  for (int tick = 0; tick < 8; ++tick) {
    REQUIRE(simulation.step().has_value());
    lastSeen.push_back(
        simulation.state().perception(0).find(ObservedEntity::ball())->lastSeen.value());
  }

  REQUIRE(lastSeen == std::vector<std::int64_t>{0, 0, 0, 3, 3, 3, 6, 6});
}

TEST_CASE("An unseen entity's position is extrapolated for a limited time", "[perception]") {
  const Observation observation{.entity = ObservedEntity::ball(),
                                .position = {.x = 10.0, .y = 10.0},
                                .velocity = {.x = 4.0, .y = -2.0},
                                .confidence = 0.5,
                                .lastSeen = SimTick(30)};

  REQUIRE(estimatePosition(observation, SimTick(30), kSecondsPerTick, kConfig) ==
          observation.position);
  const Vec2 halfSecond = estimatePosition(observation, SimTick(45), kSecondsPerTick, kConfig);
  REQUIRE_THAT(halfSecond.x, WithinAbs(12.0, 1e-12));
  REQUIRE_THAT(halfSecond.y, WithinAbs(9.0, 1e-12));
  // Capped at one second.
  REQUIRE(estimatePosition(observation, SimTick(300), kSecondsPerTick, kConfig) ==
          Vec2{.x = 14.0, .y = 8.0});
}

TEST_CASE("The perception system rejects an invalid configuration", "[perception]") {
  PerceptionConfig config;
  SECTION("interval") {
    config.intervalTicks = 0;
  }
  SECTION("field of view") {
    config.fieldOfViewDegrees = GENERATE(0.0, -10.0, 361.0);
  }
  SECTION("distance") {
    config.viewDistance = -1.0;
  }
  SECTION("awareness") {
    config.awarenessRadius = std::numeric_limits<double>::infinity();
  }
  SECTION("memory") {
    config.memorySeconds = 0.0;
  }
  SECTION("extrapolation") {
    config.extrapolationSeconds = -0.5;
  }

  REQUIRE_THROWS_AS(makePerceptionSystem(config), std::invalid_argument);
}
