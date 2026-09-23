#include "matchState.hpp"

#include <cstddef>
#include <format>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace ElyverseFootball::SimMatch {
namespace {

// Shortest representation that parses back to the same double -- what
// std::format prints for "{}" with no precision. A fixed precision would hide
// exactly the values validation exists to catch: a player one ulp past the
// touchline would print as sitting on it. The output is locale-independent,
// so messages match on every platform.
std::string formatNumber(const double value) {
  return std::format("{}", value);
}

std::string formatVector(const SimCore::Vec2 vector, const std::string_view unit) {
  return "(" + formatNumber(vector.x) + ", " + formatNumber(vector.y) + ") " + std::string(unit);
}

std::string formatPosition(const SimCore::Vec2 position) {
  return formatVector(position, "m");
}

std::string formatVelocity(const SimCore::Vec2 velocity) {
  return formatVector(velocity, "m/s");
}

std::string describePitch(const Pitch& pitch) {
  return formatNumber(pitch.lengthMeters()) + " x " + formatNumber(pitch.widthMeters()) +
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

void appendNonFinitePlayerErrors(const std::size_t index, const PlayerMatchState& player,
                                 std::vector<MatchStateError>& errors) {
  if (!player.position.isFinite()) {
    errors.push_back({.code = MatchStateErrorCode::kNonFinitePlayerPosition,
                      .message = describePlayer(index, player) +
                                 " has a non-finite position: " + formatPosition(player.position)});
  }
  if (!player.velocity.isFinite()) {
    errors.push_back({.code = MatchStateErrorCode::kNonFinitePlayerVelocity,
                      .message = describePlayer(index, player) +
                                 " has a non-finite velocity: " + formatVelocity(player.velocity)});
  }
}

void appendNonFiniteBallErrors(const BallState& ball, std::vector<MatchStateError>& errors) {
  if (!ball.position.isFinite()) {
    errors.push_back(
        {.code = MatchStateErrorCode::kNonFiniteBallPosition,
         .message = "the ball has a non-finite position: " + formatPosition(ball.position)});
  }
  if (!ball.velocity.isFinite()) {
    errors.push_back(
        {.code = MatchStateErrorCode::kNonFiniteBallVelocity,
         .message = "the ball has a non-finite velocity: " + formatVelocity(ball.velocity)});
  }
}

void validatePlayers(const MatchStateSpec& spec, std::vector<MatchStateError>& errors) {
  std::unordered_map<SimCore::PlayerId::ValueType, std::size_t> firstIndexById;

  // Range-for with a counter rather than spec.players[index]: no unchecked
  // subscript, and the index is still there for the messages. The body has no
  // continue, so the increment at the end runs for every player.
  for (std::size_t index = 0; const PlayerMatchState& player : spec.players) {
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

    appendNonFinitePlayerErrors(index, player, errors);
    ++index;
  }
}

void validateBall(const MatchStateSpec& spec, std::vector<MatchStateError>& errors) {
  appendNonFiniteBallErrors(spec.ball, errors);
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

void MatchStateWriter::setPlayerPosition(const std::size_t playerIndex,
                                         const SimCore::Vec2 position) {
  state_->players_.at(playerIndex).position = position;
}

void MatchStateWriter::setPlayerVelocity(const std::size_t playerIndex,
                                         const SimCore::Vec2 velocity) {
  state_->players_.at(playerIndex).velocity = velocity;
}

// Allocates nothing for a state without defects: the vector stays empty until
// the first error.
std::vector<MatchStateError> findNonFiniteValues(const MatchState& state) {
  std::vector<MatchStateError> errors;
  for (std::size_t index = 0; const PlayerMatchState& player : state.players()) {
    appendNonFinitePlayerErrors(index, player, errors);
    ++index;
  }
  appendNonFiniteBallErrors(state.ball(), errors);
  return errors;
}

// Every position in a MatchState is finite, so contains() fails here only for
// a point off the pitch -- the message never has to tell the two apart.
std::vector<MatchStateError> checkStartingPositions(const MatchState& state) {
  std::vector<MatchStateError> errors;
  const Pitch& pitch = state.pitch();

  for (std::size_t index = 0; const PlayerMatchState& player : state.players()) {
    if (!pitch.contains(player.position)) {
      errors.push_back({.code = MatchStateErrorCode::kPlayerOutsidePitch,
                        .message = describePlayer(index, player) + " is outside the " +
                                   describePitch(pitch) + " at " +
                                   formatPosition(player.position)});
    }
    ++index;
  }

  if (!pitch.contains(state.ball().position)) {
    errors.push_back({.code = MatchStateErrorCode::kBallOutsidePitch,
                      .message = "the ball is outside the " + describePitch(pitch) + " at " +
                                 formatPosition(state.ball().position)});
  }
  return errors;
}

}  // namespace ElyverseFootball::SimMatch
