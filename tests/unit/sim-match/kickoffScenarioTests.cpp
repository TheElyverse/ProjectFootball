#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <limits>
#include <optional>
#include <set>
#include <vector>

#include "kickoffScenario.hpp"

using ElyverseFootball::SimCore::PlayerId;
using ElyverseFootball::SimCore::Vec2;
using ElyverseFootball::SimMatch::checkStartingPositions;
using ElyverseFootball::SimMatch::kDefaultPlayersPerSide;
using ElyverseFootball::SimMatch::makeSevenASideKickoff;
using ElyverseFootball::SimMatch::MatchState;
using ElyverseFootball::SimMatch::MatchStateErrorCode;
using ElyverseFootball::SimMatch::Pitch;
using ElyverseFootball::SimMatch::PlayerMatchState;
using ElyverseFootball::SimMatch::TeamSide;

namespace {

constexpr double kLengthMeters = 60.0;
constexpr double kWidthMeters = 40.0;

// The documented layout of docs/match-state.md, spelled out independently of
// the implementation: home in its own half, away mirrored through the halfway
// line, ids counted upwards from one, nobody moving.
constexpr std::array<Vec2, 7> kExpectedHomePositions{{
    {.x = 0.05 * kLengthMeters, .y = 0.50 * kWidthMeters},
    {.x = 0.22 * kLengthMeters, .y = 0.22 * kWidthMeters},
    {.x = 0.22 * kLengthMeters, .y = 0.78 * kWidthMeters},
    {.x = 0.35 * kLengthMeters, .y = 0.50 * kWidthMeters},
    {.x = 0.42 * kLengthMeters, .y = 0.25 * kWidthMeters},
    {.x = 0.42 * kLengthMeters, .y = 0.75 * kWidthMeters},
    {.x = 0.47 * kLengthMeters, .y = 0.50 * kWidthMeters},
}};

[[nodiscard]] PlayerMatchState expectedKickoffPlayer(const std::size_t index) {
  const bool isHome = index < kExpectedHomePositions.size();
  const std::size_t slot = isHome ? index : index - kExpectedHomePositions.size();
  const Vec2 homePosition = kExpectedHomePositions.at(slot);
  return {.playerId = PlayerId(static_cast<PlayerId::ValueType>(index + 1)),
          .side = isHome ? TeamSide::kHome : TeamSide::kAway,
          .position = isHome ? homePosition
                             : Vec2{.x = kLengthMeters - homePosition.x, .y = homePosition.y},
          .velocity = {},
          .attributes = {},
          .target = std::nullopt,
          // Each side faces the goal it attacks.
          .facing = {.x = isHome ? 1.0 : -1.0, .y = 0.0}};
}

[[nodiscard]] MatchState kickoffOn(const Pitch& pitch) {
  auto state = makeSevenASideKickoff(pitch);
  REQUIRE(state.has_value());
  return *state;
}

}  // namespace

TEST_CASE("The kickoff fixture is reproducible", "[kickoff]") {
  const Pitch pitch(kLengthMeters, kWidthMeters);

  REQUIRE(kickoffOn(pitch) == kickoffOn(pitch));
}

TEST_CASE("The kickoff fixture fields seven players per side with unique ids", "[kickoff]") {
  const MatchState state = kickoffOn(Pitch(kLengthMeters, kWidthMeters));

  REQUIRE(state.players().size() == 14);
  REQUIRE(state.playersPerSide() == kDefaultPlayersPerSide);

  std::set<PlayerId::ValueType> ids;
  std::size_t homeCount = 0;
  std::size_t awayCount = 0;
  for (const PlayerMatchState& player : state.players()) {
    REQUIRE(player.playerId.isValid());
    ids.insert(player.playerId.value());
    if (player.side == TeamSide::kHome) {
      ++homeCount;
    } else {
      ++awayCount;
    }
  }

  REQUIRE(ids.size() == 14);
  REQUIRE(homeCount == 7);
  REQUIRE(awayCount == 7);
}

// Guards the documented layout: a changed fraction is a changed fixture, and a
// changed fixture invalidates every replay recorded against it.
TEST_CASE("The kickoff fixture places players at the documented positions", "[kickoff]") {
  const MatchState state = kickoffOn(Pitch(kLengthMeters, kWidthMeters));

  REQUIRE(state.players().size() == 2 * kExpectedHomePositions.size());

  std::size_t index = 0;
  for (const PlayerMatchState& player : state.players()) {
    CAPTURE(index);
    REQUIRE(player == expectedKickoffPlayer(index));
    ++index;
  }
}

TEST_CASE("The kickoff fixture starts at rest with the ball on the center spot", "[kickoff]") {
  const MatchState state = kickoffOn(Pitch(kLengthMeters, kWidthMeters));

  for (const PlayerMatchState& player : state.players()) {
    CAPTURE(player.playerId.value());
    REQUIRE(player.velocity == Vec2{});
  }
  REQUIRE(state.ball().velocity == Vec2{});
  REQUIRE(state.ball().position == Vec2{.x = kLengthMeters / 2.0, .y = kWidthMeters / 2.0});
}

TEST_CASE("The kickoff fixture fits any valid pitch", "[kickoff]") {
  const std::vector<Pitch> pitches{Pitch(kLengthMeters, kWidthMeters), Pitch(105.0, 68.0),
                                   Pitch(0.001, 0.001), Pitch(1.0e6, 5.0e5)};

  for (const Pitch& pitch : pitches) {
    CAPTURE(pitch.lengthMeters(), pitch.widthMeters());
    const MatchState state = kickoffOn(pitch);
    REQUIRE(checkStartingPositions(state).empty());
    for (const PlayerMatchState& player : state.players()) {
      CAPTURE(player.position.x, player.position.y);
      REQUIRE(pitch.contains(player.position));
    }
    REQUIRE(pitch.contains(state.ball().position));
  }
}

TEST_CASE("Each side keeps to its own half at kickoff", "[kickoff]") {
  const Pitch pitch(kLengthMeters, kWidthMeters);
  const MatchState state = kickoffOn(pitch);

  for (const PlayerMatchState& player : state.players()) {
    CAPTURE(player.playerId.value(), player.position.x);
    if (player.side == TeamSide::kHome) {
      REQUIRE(player.position.x < pitch.lengthMeters() / 2.0);
    } else {
      REQUIRE(player.position.x > pitch.lengthMeters() / 2.0);
    }
  }
}

TEST_CASE("The kickoff fixture can start with a rolling ball", "[kickoff]") {
  const auto state =
      makeSevenASideKickoff(Pitch(kLengthMeters, kWidthMeters), {.x = 4.0, .y = -1.5});

  REQUIRE(state.has_value());
  REQUIRE(state->ball().velocity == Vec2{.x = 4.0, .y = -1.5});
  REQUIRE(state->ball().position == Vec2{.x = kLengthMeters / 2.0, .y = kWidthMeters / 2.0});
}

TEST_CASE("The kickoff fixture rejects a non-finite ball velocity", "[kickoff]") {
  const auto state = makeSevenASideKickoff(
      Pitch(kLengthMeters, kWidthMeters), {.x = std::numeric_limits<double>::infinity(), .y = 0.0});

  REQUIRE_FALSE(state.has_value());
  REQUIRE(state.error().size() == 1);
  REQUIRE(state.error().front().code == MatchStateErrorCode::kNonFiniteBallVelocity);
}
