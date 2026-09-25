#include "kickoffScenario.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "ids.hpp"
#include "vec2.hpp"

namespace ElyverseFootball::SimMatch {
namespace {

// A position as a fraction of the pitch dimensions, so the fixture scales to
// any pitch size instead of hard-coding meters for one.
struct FormationSlot {
  double lengthFraction;
  double widthFraction;
};

// Home half, front to back: goalkeeper, two backs, a center midfielder, two
// wide midfielders, and the forward who takes the kickoff. Role names are
// documentation only -- responsibilities belong to sim-tactics.
constexpr std::array<FormationSlot, kDefaultPlayersPerSide> kHomeFormation{
    {
        {.lengthFraction = 0.05, .widthFraction = 0.50},
        {.lengthFraction = 0.22, .widthFraction = 0.22},
        {.lengthFraction = 0.22, .widthFraction = 0.78},
        {.lengthFraction = 0.35, .widthFraction = 0.50},
        {.lengthFraction = 0.42, .widthFraction = 0.25},
        {.lengthFraction = 0.42, .widthFraction = 0.75},
        {.lengthFraction = 0.47, .widthFraction = 0.50},
    },
};

constexpr double kCenterFraction = 0.5;

[[nodiscard]] SimCore::Vec2 homePosition(const Pitch& pitch, const FormationSlot slot) noexcept {
  return {
      .x = slot.lengthFraction * pitch.lengthMeters(),
      .y = slot.widthFraction * pitch.widthMeters(),
  };
}

// Mirrored through the halfway line: the same distance from the opposite goal
// line, on the same touchline side.
[[nodiscard]] SimCore::Vec2 awayPosition(const Pitch& pitch, const FormationSlot slot) noexcept {
  return {
      .x = pitch.lengthMeters() - (slot.lengthFraction * pitch.lengthMeters()),
      .y = slot.widthFraction * pitch.widthMeters(),
  };
}

}  // namespace

std::expected<MatchState, std::vector<MatchStateError>> makeSevenASideKickoff(
    const Pitch& pitch, const SimCore::Vec2 ballVelocity) {
  std::vector<PlayerMatchState> players;
  players.reserve(2U * kHomeFormation.size());

  SimCore::PlayerId::ValueType nextId = 1;
  for (const FormationSlot slot : kHomeFormation) {
    players.push_back({
        .playerId = SimCore::PlayerId(nextId++),
        .side = TeamSide::kHome,
        .position = homePosition(pitch, slot),
        .velocity = {},
        .attributes = {},
        .target = std::nullopt,
    });
  }
  for (const FormationSlot slot : kHomeFormation) {
    players.push_back({
        .playerId = SimCore::PlayerId(nextId++),
        .side = TeamSide::kAway,
        .position = awayPosition(pitch, slot),
        .velocity = {},
        .attributes = {},
        .target = std::nullopt,
    });
  }

  const BallState ball{
      .position =
          {
              .x = kCenterFraction * pitch.lengthMeters(),
              .y = kCenterFraction * pitch.widthMeters(),
          },
      .velocity = ballVelocity,
  };

  auto state = MatchState::create({
      .pitch = pitch,
      .players = std::move(players),
      .ball = ball,
      .playersPerSide = kDefaultPlayersPerSide,
  });
  if (!state) {
    return state;
  }
  if (std::vector<MatchStateError> errors = checkStartingPositions(*state); !errors.empty()) {
    return std::unexpected(std::move(errors));
  }
  return state;
}

}  // namespace ElyverseFootball::SimMatch
