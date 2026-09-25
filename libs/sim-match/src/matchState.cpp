#include "matchState.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <format>
#include <limits>
#include <optional>
#include <stdexcept>
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
  if (player.target && !player.target->isFinite()) {
    errors.push_back({.code = MatchStateErrorCode::kNonFinitePlayerTarget,
                      .message = describePlayer(index, player) +
                                 " has a non-finite target: " + formatPosition(*player.target)});
  }
}

// Positive and finite: a player who cannot move or speed up is not a slow
// player but a broken fixture, and would make every arrival time infinite.
[[nodiscard]] bool isPositiveFinite(const double value) noexcept {
  return value > 0.0 && value <= std::numeric_limits<double>::max();
}

void appendFacingErrors(const std::size_t index, const PlayerMatchState& player,
                        std::vector<MatchStateError>& errors) {
  const double lengthSquared = player.facing.lengthSquared();
  if (!player.facing.isFinite() || !(std::abs(lengthSquared - 1.0) <= kFacingTolerance)) {
    errors.push_back({.code = MatchStateErrorCode::kInvalidPlayerFacing,
                      .message = describePlayer(index, player) + " has facing " +
                                 formatVector(player.facing, "") +
                                 "which is not a finite unit vector"});
  }
}

void appendAttributeErrors(const std::size_t index, const PlayerMatchState& player,
                           std::vector<MatchStateError>& errors) {
  const PlayerAttributes& attributes = player.attributes;
  if (!isPositiveFinite(attributes.maxSpeed) || !isPositiveFinite(attributes.acceleration)) {
    errors.push_back({.code = MatchStateErrorCode::kInvalidPlayerAttributes,
                      .message = describePlayer(index, player) + " has max speed " +
                                 formatNumber(attributes.maxSpeed) + " m/s and acceleration " +
                                 formatNumber(attributes.acceleration) +
                                 " m/s^2, expected both positive and finite"});
    return;
  }
  // Movement assumes a player is never faster than his limit: it changes the
  // velocity by one tick's acceleration at a time and would leave a faster
  // player over it for many ticks. The margin absorbs rounding in a state
  // copied from a running match.
  constexpr double kSpeedTolerance = 1.0 + 1e-9;
  const double limit = attributes.maxSpeed * kSpeedTolerance;
  if (player.velocity.isFinite() && player.velocity.lengthSquared() > limit * limit) {
    errors.push_back({.code = MatchStateErrorCode::kPlayerTooFast,
                      .message = describePlayer(index, player) + " moves at " +
                                 formatVelocity(player.velocity) + ", faster than his max speed " +
                                 formatNumber(attributes.maxSpeed) + " m/s"});
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

    appendAttributeErrors(index, player, errors);
    appendNonFinitePlayerErrors(index, player, errors);
    appendFacingErrors(index, player, errors);
    ++index;
  }
}

[[nodiscard]] bool hasPlayer(const std::vector<PlayerMatchState>& players,
                             const SimCore::PlayerId playerId) noexcept {
  return std::ranges::any_of(
      players, [playerId](const PlayerMatchState& player) { return player.playerId == playerId; });
}

void validateBall(const MatchStateSpec& spec, std::vector<MatchStateError>& errors) {
  appendNonFiniteBallErrors(spec.ball, errors);
  const SimCore::Vec2 velocity = spec.ball.velocity;
  if (velocity.isFinite() && velocity.lengthSquared() > kMaxBallSpeed * kMaxBallSpeed) {
    errors.push_back({.code = MatchStateErrorCode::kBallTooFast,
                      .message = "the ball moves at " + formatVelocity(velocity) +
                                 ", faster than " + formatNumber(kMaxBallSpeed) + " m/s"});
  }
  if (spec.ball.lastTouch && !hasPlayer(spec.players, spec.ball.lastTouch->playerId)) {
    errors.push_back({.code = MatchStateErrorCode::kUnknownLastTouch,
                      .message = "the ball was last touched by player " +
                                 std::to_string(spec.ball.lastTouch->playerId.value()) +
                                 ", who is not in the state"});
  }
  if (spec.ball.owner && !hasPlayer(spec.players, *spec.ball.owner)) {
    errors.push_back({.code = MatchStateErrorCode::kUnknownBallOwner,
                      .message = "the ball belongs to player " +
                                 std::to_string(spec.ball.owner->value()) +
                                 ", who is not in the state"});
  }
}

void validateTactics(const MatchStateSpec& spec, const TeamTactics& tactics,
                     std::vector<MatchStateError>& errors) {
  for (const TeamSide side : {TeamSide::kHome, TeamSide::kAway}) {
    const auto& tactic = tactics.of(side);
    if (tactic && std::cmp_not_equal(tactic->slots().size(), spec.playersPerSide)) {
      errors.push_back({.code = MatchStateErrorCode::kTacticDoesNotFitSquad,
                        .message = std::format("{}'s tactic '{}' has {} slots for {} players",
                                               teamSideName(side), tactic->name(),
                                               tactic->slots().size(), spec.playersPerSide)});
    }
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

MatchState::MatchState(MatchStateSpec spec, TeamTactics tactics)
    : pitch_(spec.pitch),
      players_(std::move(spec.players)),
      ball_(spec.ball),
      playersPerSide_(spec.playersPerSide),
      tactics_(std::move(tactics)),
      perceptions_(players_.size()) {}

std::expected<MatchState, std::vector<MatchStateError>> MatchState::create(MatchStateSpec spec,
                                                                           TeamTactics tactics) {
  std::vector<MatchStateError> errors;
  validateRoster(spec, errors);
  validatePlayers(spec, errors);
  validateBall(spec, errors);
  validateTactics(spec, tactics, errors);

  if (!errors.empty()) {
    return std::unexpected(std::move(errors));
  }
  return MatchState(std::move(spec), std::move(tactics));
}

void MatchStateWriter::setPlayerPosition(const std::size_t playerIndex,
                                         const SimCore::Vec2 position) {
  state_->players_.at(playerIndex).position = position;
}

void MatchStateWriter::setPlayerVelocity(const std::size_t playerIndex,
                                         const SimCore::Vec2 velocity) {
  state_->players_.at(playerIndex).velocity = velocity;
}

void MatchStateWriter::setPlayerFacing(const std::size_t playerIndex, const SimCore::Vec2 facing) {
  state_->players_.at(playerIndex).facing = facing;
}

void MatchStateWriter::setBallOwner(const std::optional<SimCore::PlayerId> owner) {
  if (owner) {
    requirePlayer(*owner, "the ball's owner");
  }
  state_->ball_.owner = owner;
}

void MatchStateWriter::requirePlayer(const SimCore::PlayerId playerId,
                                     const std::string_view role) const {
  if (!hasPlayer(state_->players_, playerId)) {
    throw std::invalid_argument("MatchStateWriter: no player with id " +
                                std::to_string(playerId.value()) + " can be " + std::string(role));
  }
}

void MatchStateWriter::setBallLastTouch(const std::optional<BallTouch> touch) {
  if (touch) {
    requirePlayer(touch->playerId, "the last to touch the ball");
  }
  state_->ball_.lastTouch = touch;
}

void MatchStateWriter::setPendingPass(const std::optional<PassIntent> pass) {
  if (pass) {
    requirePlayer(pass->passer, "a passer");
    if (pass->receiver) {
      requirePlayer(*pass->receiver, "a receiver");
    }
  }
  state_->pendingPass_ = pass;
}

PlayerPerception& MatchStateWriter::perception(const std::size_t playerIndex) {
  return state_->perceptions_.at(playerIndex);
}

void MatchStateWriter::setPlayerTarget(const std::size_t playerIndex,
                                       const std::optional<SimCore::Vec2> target) {
  state_->players_.at(playerIndex).target = target;
}

std::optional<std::size_t> findPlayerIndex(const MatchState& state,
                                           const SimCore::PlayerId playerId) noexcept {
  for (std::size_t index = 0; const PlayerMatchState& player : state.players()) {
    if (player.playerId == playerId) {
      return index;
    }
    ++index;
  }
  return std::nullopt;
}

std::size_t slotIndex(const MatchState& state, const std::size_t playerIndex) {
  const auto players = state.players();
  if (playerIndex >= players.size()) {
    throw std::out_of_range("slotIndex: no player at index " + std::to_string(playerIndex));
  }
  const TeamSide side = players[playerIndex].side;
  return static_cast<std::size_t>(std::ranges::count_if(
      players.first(playerIndex),
      [side](const PlayerMatchState& player) { return player.side == side; }));
}

// Allocates nothing for a state without defects: the vector stays empty until
// the first error.
std::vector<MatchStateError> findNonFiniteValues(const MatchState& state) {
  std::vector<MatchStateError> errors;
  for (std::size_t index = 0; const PlayerMatchState& player : state.players()) {
    appendNonFinitePlayerErrors(index, player, errors);
    appendFacingErrors(index, player, errors);
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
