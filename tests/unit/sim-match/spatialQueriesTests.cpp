#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

#include "matchSimulation.hpp"
#include "matchState.hpp"
#include "playerMovement.hpp"
#include "spatialQueries.hpp"
#include "vec2.hpp"

using Catch::Matchers::WithinAbs;
using ElyverseFootball::SimCore::PlayerId;
using ElyverseFootball::SimCore::Vec2;
using ElyverseFootball::SimMatch::estimateArrivalSeconds;
using ElyverseFootball::SimMatch::findNearestPlayer;
using ElyverseFootball::SimMatch::findPlayersWithin;
using ElyverseFootball::SimMatch::kDefaultAcceleration;
using ElyverseFootball::SimMatch::kDefaultMaxSpeed;
using ElyverseFootball::SimMatch::kDefaultTicksPerSecond;
using ElyverseFootball::SimMatch::MatchState;
using ElyverseFootball::SimMatch::NearbyPlayer;
using ElyverseFootball::SimMatch::Pitch;
using ElyverseFootball::SimMatch::PlayerFilter;
using ElyverseFootball::SimMatch::PlayerKinematics;
using ElyverseFootball::SimMatch::PlayerMatchState;
using ElyverseFootball::SimMatch::stepPlayerMovement;
using ElyverseFootball::SimMatch::TeamSide;

namespace {

const Pitch kPitch(60.0, 40.0);

[[nodiscard]] PlayerMatchState playerAt(const std::uint32_t playerId, const TeamSide side,
                                        const Vec2 position, const Vec2 velocity = {}) {
  return {.playerId = PlayerId(playerId),
          .side = side,
          .position = position,
          .velocity = velocity,
          .attributes = {},
          .target = std::nullopt};
}

// Three a side around (30, 20). Player 5 and player 2 are both exactly 5 m
// from the center; player 2 is listed later in the state but has the lower id.
[[nodiscard]] MatchState threeASide() {
  auto state = MatchState::create({.pitch = kPitch,
                                   .players = {playerAt(5, TeamSide::kHome, {.x = 33.0, .y = 24.0}),
                                               playerAt(1, TeamSide::kHome, {.x = 30.0, .y = 21.0}),
                                               playerAt(3, TeamSide::kHome, {.x = 10.0, .y = 20.0}),
                                               playerAt(2, TeamSide::kAway, {.x = 25.0, .y = 20.0}),
                                               playerAt(4, TeamSide::kAway, {.x = 30.0, .y = 28.0}),
                                               playerAt(6, TeamSide::kAway, {.x = 55.0, .y = 5.0})},
                                   .ball = {},
                                   .playersPerSide = 3});
  REQUIRE(state.has_value());
  return *std::move(state);
}

// The id of the nearest player, 0 if there is none.
[[nodiscard]] std::uint32_t nearestId(const std::optional<NearbyPlayer>& nearest) {
  return nearest ? nearest->playerId.value() : 0U;
}

[[nodiscard]] std::vector<std::uint32_t> idsOf(const std::vector<NearbyPlayer>& players) {
  std::vector<std::uint32_t> ids;
  ids.reserve(players.size());
  for (const NearbyPlayer& player : players) {
    ids.push_back(player.playerId.value());
  }
  return ids;
}

constexpr Vec2 kCenter{.x = 30.0, .y = 20.0};

}  // namespace

TEST_CASE("Nearby players are found nearest first", "[spatial]") {
  const auto nearby = findPlayersWithin(threeASide(), kCenter, 10.0);

  REQUIRE(idsOf(nearby) == std::vector<std::uint32_t>{1, 2, 5, 4});
  REQUIRE(nearby.front().index == 1);
  REQUIRE(nearby.front().distance == 1.0);
  REQUIRE(nearby.at(1).distance == 5.0);
}

TEST_CASE("Players at equal distances are ordered by id", "[spatial]") {
  // Players 2 and 5 are both 5 m away; 2 comes first although 5 comes
  // first in the state.
  const auto nearby = findPlayersWithin(threeASide(), kCenter, 5.0);

  REQUIRE(idsOf(nearby) == std::vector<std::uint32_t>{1, 2, 5});
}

TEST_CASE("The radius is inclusive", "[spatial]") {
  const MatchState state = threeASide();

  REQUIRE(idsOf(findPlayersWithin(state, kCenter, 8.0)) == std::vector<std::uint32_t>{1, 2, 5, 4});
  REQUIRE(idsOf(findPlayersWithin(state, kCenter, std::nextafter(8.0, 0.0))) ==
          std::vector<std::uint32_t>{1, 2, 5});
  REQUIRE(findPlayersWithin(state, {.x = 0.0, .y = 40.0}, 0.5).empty());
}

TEST_CASE("A negative or NaN radius finds no one", "[spatial]") {
  const MatchState state = threeASide();

  REQUIRE(findPlayersWithin(state, kCenter, -10.0).empty());
  REQUIRE(findPlayersWithin(state, kCenter, -0.0).size() ==
          findPlayersWithin(state, kCenter, 0.0).size());
  REQUIRE(findPlayersWithin(state, kCenter, std::numeric_limits<double>::quiet_NaN()).empty());
}

TEST_CASE("Nearby queries filter by side and exclude a player", "[spatial]") {
  const MatchState state = threeASide();

  REQUIRE(idsOf(findPlayersWithin(state, kCenter, 30.0, PlayerFilter::onSide(TeamSide::kAway))) ==
          std::vector<std::uint32_t>{2, 4, 6});
  REQUIRE(idsOf(findPlayersWithin(state, kCenter, 30.0,
                                  PlayerFilter::onSide(TeamSide::kHome).except(PlayerId(1)))) ==
          std::vector<std::uint32_t>{5, 3});
}

TEST_CASE("The nearest player honors side, exclusion and ties", "[spatial]") {
  const MatchState state = threeASide();

  REQUIRE(nearestId(findNearestPlayer(state, kCenter)) == 1);
  REQUIRE(nearestId(findNearestPlayer(state, kCenter, PlayerFilter{}.except(PlayerId(1)))) == 2);
  REQUIRE(nearestId(findNearestPlayer(state, {.x = 55.0, .y = 5.0},
                                      PlayerFilter::onSide(TeamSide::kHome))) == 5);
  const auto twoPlayers = MatchState::create(
      {.pitch = kPitch,
       .players = {playerAt(1, TeamSide::kHome, {}), playerAt(2, TeamSide::kAway, {})},
       .ball = {},
       .playersPerSide = 1});
  REQUIRE(twoPlayers.has_value());
  REQUIRE_FALSE(findNearestPlayer(*twoPlayers, kCenter,
                                  PlayerFilter::onSide(TeamSide::kAway).except(PlayerId(2)))
                    .has_value());
}

TEST_CASE("A stationary player's arrival time follows his limits", "[spatial]") {
  const PlayerMatchState player = playerAt(1, TeamSide::kHome, {.x = 10.0, .y = 20.0});
  // Reaching 7.5 m/s at 4 m/s² takes 1.875 s and 7.03125 m.
  constexpr double kAccelerationDistance =
      kDefaultMaxSpeed * kDefaultMaxSpeed / (2.0 * kDefaultAcceleration);

  REQUIRE(estimateArrivalSeconds(player, player.position, kPitch) == 0.0);
  // Within the acceleration phase: d = a·t²/2.
  REQUIRE_THAT(estimateArrivalSeconds(player, {.x = 12.0, .y = 20.0}, kPitch).value_or(-1.0),
               WithinAbs(1.0, 1e-12));
  REQUIRE_THAT(
      estimateArrivalSeconds(player, {.x = 10.0 + kAccelerationDistance, .y = 20.0}, kPitch)
          .value_or(-1.0),
      WithinAbs(1.875, 1e-12));
  // Beyond it, the rest at full speed: 1.875 s + (30 - 7.03125) / 7.5.
  REQUIRE_THAT(estimateArrivalSeconds(player, {.x = 40.0, .y = 20.0}, kPitch).value_or(-1.0),
               WithinAbs(1.875 + ((30.0 - kAccelerationDistance) / 7.5), 1e-12));
}

TEST_CASE("Running toward a position arrives sooner than running away", "[spatial]") {
  const Vec2 start{.x = 30.0, .y = 20.0};
  const Vec2 goal{.x = 40.0, .y = 20.0};
  const auto still = estimateArrivalSeconds(playerAt(1, TeamSide::kHome, start), goal, kPitch);
  const auto toward = estimateArrivalSeconds(
      playerAt(1, TeamSide::kHome, start, {.x = 6.0, .y = 0.0}), goal, kPitch);
  const auto away = estimateArrivalSeconds(
      playerAt(1, TeamSide::kHome, start, {.x = -6.0, .y = 0.0}), goal, kPitch);
  const auto sideways = estimateArrivalSeconds(
      playerAt(1, TeamSide::kHome, start, {.x = 0.0, .y = 6.0}), goal, kPitch);

  REQUIRE(toward.value_or(-1.0) < still.value_or(-1.0));
  REQUIRE(still.value_or(-1.0) < away.value_or(-1.0));
  // Only the component toward the position counts.
  REQUIRE(sideways == still);
  // From full speed toward it: 10 m at 7.5 m/s.
  REQUIRE_THAT(estimateArrivalSeconds(playerAt(1, TeamSide::kHome, start, {.x = 7.5, .y = 0.0}),
                                      goal, kPitch)
                   .value_or(-1.0),
               WithinAbs(10.0 / 7.5, 1e-12));
}

TEST_CASE("Positions off the pitch are unreachable", "[spatial]") {
  const PlayerMatchState player = playerAt(1, TeamSide::kHome, {.x = 10.0, .y = 20.0});

  REQUIRE_FALSE(estimateArrivalSeconds(player, {.x = -0.5, .y = 20.0}, kPitch).has_value());
  REQUIRE_FALSE(estimateArrivalSeconds(player, {.x = 30.0, .y = 41.0}, kPitch).has_value());
  REQUIRE_FALSE(estimateArrivalSeconds(
                    player, {.x = std::numeric_limits<double>::quiet_NaN(), .y = 1.0}, kPitch)
                    .has_value());
  REQUIRE(estimateArrivalSeconds(player, {.x = 0.0, .y = 20.0}, kPitch).has_value());
}

TEST_CASE("Arrival estimates agree with the movement system", "[spatial]") {
  // A player sent past the position passes it at full effort; the estimate
  // must match the tick he gets there to within a tick.
  const double distance = GENERATE(3.0, 12.0, 35.0);
  CAPTURE(distance);
  PlayerMatchState player = playerAt(1, TeamSide::kHome, {.x = 5.0, .y = 20.0});
  player.target = Vec2{.x = 59.0, .y = 20.0};
  const Vec2 position{.x = 5.0 + distance, .y = 20.0};
  const double estimate = estimateArrivalSeconds(player, position, kPitch).value_or(-1.0);

  constexpr double kSecondsPerTick = 1.0 / kDefaultTicksPerSecond;
  int ticks = 0;
  while (player.position.x < position.x) {
    const PlayerKinematics moved = stepPlayerMovement(player, kSecondsPerTick);
    player.position = moved.position;
    player.velocity = moved.velocity;
    ++ticks;
  }

  REQUIRE_THAT(static_cast<double>(ticks) * kSecondsPerTick, WithinAbs(estimate, kSecondsPerTick));
}
