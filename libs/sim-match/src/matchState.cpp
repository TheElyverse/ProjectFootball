#include "matchState.hpp"

#include <cstddef>
#include <iomanip>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>

namespace ElyverseFootball::SimMatch {
namespace {

std::string formatMeters(const double value) {
  std::ostringstream stream;
  stream << std::fixed << std::setprecision(2) << value;
  return stream.str();
}

std::string formatPoint(const SimCore::Vec2 point) {
  return "(" + formatMeters(point.x) + ", " + formatMeters(point.y) + ") m";
}

std::string describePitch(const Pitch& pitch) {
  return formatMeters(pitch.lengthMeters()) + " x " + formatMeters(pitch.widthMeters()) +
         " m pitch";
}

std::string describePlayer(const std::size_t index, const PlayerMatchState& player) {
  return "player at index " + std::to_string(index) + " (id " +
         std::to_string(player.playerId.value()) + ", " + std::string(teamSideName(player.side)) +
         ")";
}

// Squad size is checked per side rather than as a total: the total follows from
// the two side counts, and reporting both would turn one defect into two
// errors. Players whose side is not a declared enumerator count towards
// neither side; they get their own kInvalidTeamSide error in validatePlayers,
// so the count error is raised only when no assignment of those players to a
// side could make the squad sizes right -- one defect, one error.
void validateRoster(const MatchStateSpec& spec, std::vector<MatchStateError>& errors) {
  if (spec.playersPerSide < 1) {
    errors.push_back({.code = MatchStateErrorCode::kInvalidPlayersPerSide,
                      .message = "playersPerSide must be at least 1, got " +
                                 std::to_string(spec.playersPerSide)});
    return;
  }

  std::size_t homeCount = 0;
  std::size_t awayCount = 0;
  std::size_t unknownCount = 0;
  for (const PlayerMatchState& player : spec.players) {
    if (player.side == TeamSide::kHome) {
      ++homeCount;
    } else if (player.side == TeamSide::kAway) {
      ++awayCount;
    } else {
      ++unknownCount;
    }
  }

  const auto expectedPerSide = static_cast<std::size_t>(spec.playersPerSide);
  if (homeCount > expectedPerSide || awayCount > expectedPerSide ||
      homeCount + awayCount + unknownCount != 2 * expectedPerSide) {
    std::string message = "home has " + std::to_string(homeCount) + " players and away has " +
                          std::to_string(awayCount);
    if (unknownCount != 0) {
      message += " (" + std::to_string(unknownCount) + " more with an unknown side)";
    }
    message += ", expected " + std::to_string(spec.playersPerSide) + " per side";
    errors.push_back(
        {.code = MatchStateErrorCode::kWrongPlayerCountPerSide, .message = std::move(message)});
  }
}

void validatePlayers(const MatchStateSpec& spec, std::vector<MatchStateError>& errors) {
  std::unordered_map<SimCore::PlayerId::ValueType, std::size_t> firstIndexById;

  for (std::size_t index = 0; index < spec.players.size(); ++index) {
    const PlayerMatchState& player = spec.players[index];

    if (!isValidTeamSide(player.side)) {
      errors.push_back({.code = MatchStateErrorCode::kInvalidTeamSide,
                        .message = describePlayer(index, player) + " has team side value " +
                                   std::to_string(static_cast<unsigned>(player.side)) +
                                   ", expected home or away"});
    }

    // An invalid id is reported on its own: feeding it to the duplicate check
    // as well would report a second error for the same defect.
    if (!player.playerId.isValid()) {
      errors.push_back({.code = MatchStateErrorCode::kInvalidPlayerId,
                        .message = "player at index " + std::to_string(index) + " (" +
                                   std::string(teamSideName(player.side)) +
                                   ") has no valid player id"});
    } else {
      const auto [existing, inserted] = firstIndexById.try_emplace(player.playerId.value(), index);
      if (!inserted) {
        errors.push_back({.code = MatchStateErrorCode::kDuplicatePlayerId,
                          .message = "duplicate player id " +
                                     std::to_string(player.playerId.value()) + " at index " +
                                     std::to_string(index) + ", first seen at index " +
                                     std::to_string(existing->second)});
      }
    }

    // contains() already rejects non-finite positions, so the two position
    // checks are exclusive -- one defect, one error.
    if (!player.position.isFinite()) {
      errors.push_back({.code = MatchStateErrorCode::kNonFinitePlayerPosition,
                        .message = describePlayer(index, player) +
                                   " has a non-finite position: " + formatPoint(player.position)});
    } else if (!spec.pitch.contains(player.position)) {
      errors.push_back({.code = MatchStateErrorCode::kPlayerOutsidePitch,
                        .message = describePlayer(index, player) + " is outside the " +
                                   describePitch(spec.pitch) + " at " +
                                   formatPoint(player.position)});
    }

    if (!player.velocity.isFinite()) {
      errors.push_back({.code = MatchStateErrorCode::kNonFinitePlayerVelocity,
                        .message = describePlayer(index, player) +
                                   " has a non-finite velocity: " + formatPoint(player.velocity)});
    }
  }
}

void validateBall(const MatchStateSpec& spec, std::vector<MatchStateError>& errors) {
  if (!spec.ball.position.isFinite()) {
    errors.push_back(
        {.code = MatchStateErrorCode::kNonFiniteBallPosition,
         .message = "the ball has a non-finite position: " + formatPoint(spec.ball.position)});
  } else if (!spec.pitch.contains(spec.ball.position)) {
    errors.push_back({.code = MatchStateErrorCode::kBallOutsidePitch,
                      .message = "the ball is outside the " + describePitch(spec.pitch) + " at " +
                                 formatPoint(spec.ball.position)});
  }

  if (!spec.ball.velocity.isFinite()) {
    errors.push_back(
        {.code = MatchStateErrorCode::kNonFiniteBallVelocity,
         .message = "the ball has a non-finite velocity: " + formatPoint(spec.ball.velocity)});
  }
}

}  // namespace

std::string_view teamSideName(const TeamSide side) noexcept {
  switch (side) {
    case TeamSide::kHome:
      return "home";
    case TeamSide::kAway:
      return "away";
  }
  return "unknown";
}

bool isValidTeamSide(const TeamSide side) noexcept {
  return side == TeamSide::kHome || side == TeamSide::kAway;
}

MatchState::MatchState(MatchStateSpec spec)
    : pitch_(spec.pitch),
      players_(std::move(spec.players)),
      ball_(spec.ball),
      playersPerSide_(spec.playersPerSide) {}

std::expected<MatchState, std::vector<MatchStateError>> MatchState::create(MatchStateSpec spec) {
  std::vector<MatchStateError> errors;
  validateRoster(spec, errors);
  validatePlayers(spec, errors);
  validateBall(spec, errors);

  if (!errors.empty()) {
    return std::unexpected(std::move(errors));
  }
  return MatchState(std::move(spec));
}

}  // namespace ElyverseFootball::SimMatch
