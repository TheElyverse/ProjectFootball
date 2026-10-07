#include "reception.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <tuple>

#include "playerMovement.hpp"
#include "shotStopping.hpp"
#include "teamFrame.hpp"
#include "zones.hpp"

namespace ElyverseFootball::SimMatch {
namespace {

using SimCore::Vec2;

[[nodiscard]] bool isFiniteNonNegative(const double value) noexcept {
  return value >= 0.0 && value <= std::numeric_limits<double>::max();
}

// How far and how high the player at this index reaches the ball: with his
// hands if hasHands(), at his feet otherwise.
[[nodiscard]] BallReach claimReach(const MatchState& state, const BallState& ball,
                                   const std::size_t playerIndex, const ReceptionConfig& config) {
  return hasHands(state, ball, playerIndex)
             ? BallReach{.radius = config.handsRadius, .height = config.handsHeight}
             : BallReach{.radius = config.controlRadius, .height = config.controlHeight};
}

}  // namespace

bool mayCompete(const MatchState& state, const BallState& ball, const std::size_t playerIndex,
                const SimCore::SimTick now, const double secondsPerTick,
                const ReceptionConfig& config, const std::optional<std::size_t> excluded) {
  if (playerIndex == excluded || isDiving(state, playerIndex, now, secondsPerTick)) {
    return false;
  }
  // The last touch cannot take the ball back until reclaimDelaySeconds after
  // his touch.
  const PlayerMatchState& player = state.players()[playerIndex];
  if (!ball.lastTouch || ball.lastTouch->playerId != player.playerId) {
    return true;
  }
  const double sinceTouch =
      static_cast<double>(now.value() - ball.lastTouch->tick.value()) * secondsPerTick;
  return sinceTouch >= config.reclaimDelaySeconds;
}

bool hasHands(const MatchState& state, const BallState& ball, const std::size_t playerIndex) {
  const PlayerMatchState& player = state.players()[playerIndex];
  const GoalEnd end = ownGoalEnd(player.side);
  return isGoalkeeper(state, playerIndex) && state.pitch().isInPenaltyArea(end, player.position) &&
         state.pitch().isInPenaltyArea(end, ball.position);
}

std::optional<BallClaim> findFirstReach(
    const MatchState& state, const BallState& ball, const BallStep& moved,
    const BallPhysics& physics, const SimCore::SimTick now, const double secondsPerTick,
    const ReceptionConfig& config, const std::optional<std::size_t> excluded,
    const std::function<std::optional<BallReach>(std::size_t)>& reachOf) {
  std::optional<BallClaim> best;
  for (std::size_t index = 0; const PlayerMatchState& player : state.players()) {
    const std::size_t playerIndex = index++;
    if (!mayCompete(state, ball, playerIndex, now, secondsPerTick, config, excluded)) {
      continue;
    }
    const auto reach = reachOf(playerIndex);
    if (!reach) {
      continue;
    }
    const PlayerKinematics stepped = stepPlayerMovement(player, secondsPerTick);
    const auto contact = findContact(player.position, stepped.position, ball.position,
                                     moved.ball.position, reach->radius);
    if (!contact) {
      continue;
    }
    // Out of reach: the ball passes over his head rather than to his feet.
    // Asked of the flight at the moment of contact rather than interpolated
    // between the ends of the tick, which describe neither a ball that bounces
    // on the way nor one the line stops and puts down flat. The contact is a
    // fraction of the path the ball really travelled, so it is that span --
    // not the whole tick -- that turns it into a moment.
    const double height = ballHeightAfter(ball, physics, contact->contactFraction * moved.seconds);
    if (height > reach->height || height <= reach->above) {
      continue;
    }
    const BallClaim claim{
        .playerIndex = playerIndex, .playerId = player.playerId, .contact = *contact};
    const auto order = [](const BallClaim& candidate) {
      return std::tuple(candidate.contact.contactFraction, candidate.contact.closestDistance,
                        candidate.playerId);
    };
    if (!best || order(claim) < order(*best)) {
      best = claim;
    }
  }
  return best;
}

std::optional<Contact> findContact(const Vec2 playerFrom, const Vec2 playerTo, const Vec2 ballFrom,
                                   const Vec2 ballTo, const double radius) noexcept {
  // The ball relative to the player: d(t) = start + t · change for t in
  // [0, 1]. Contact is the first t with |d(t)| <= radius.
  const Vec2 start = ballFrom - playerFrom;
  const Vec2 change = (ballTo - ballFrom) - (playerTo - playerFrom);
  const double changeSquared = change.lengthSquared();

  const double closestFraction =
      changeSquared > 0.0 ? std::clamp(-start.dot(change) / changeSquared, 0.0, 1.0) : 0.0;
  const double closestDistance = std::sqrt((start + (change * closestFraction)).lengthSquared());
  if (closestDistance > radius) {
    return std::nullopt;
  }

  // |start + t·change|² = radius², the smaller root: a·t² + b·t + c = 0.
  const double startOutside = start.lengthSquared() - (radius * radius);
  if (startOutside <= 0.0) {
    return Contact{.contactFraction = 0.0, .closestDistance = closestDistance};
  }
  const double linear = 2.0 * start.dot(change);
  const double discriminant = (linear * linear) - (4.0 * changeSquared * startOutside);
  const double fraction =
      (-linear - std::sqrt(std::max(discriminant, 0.0))) / (2.0 * changeSquared);
  return Contact{.contactFraction = std::clamp(fraction, 0.0, 1.0),
                 .closestDistance = closestDistance};
}

std::optional<BallClaim> findBallClaim(const MatchState& state, const BallState& ball,
                                       const BallStep& moved, const BallPhysics& physics,
                                       const SimCore::SimTick now, const double secondsPerTick,
                                       const ReceptionConfig& config,
                                       const std::optional<std::size_t> excluded) {
  return findFirstReach(state, ball, moved, physics, now, secondsPerTick, config, excluded,
                        [&](const std::size_t playerIndex) {
                          return std::optional(claimReach(state, ball, playerIndex, config));
                        });
}

std::optional<BallClaim> findBallContact(const MatchState& state, const BallState& ball,
                                         const BallStep& moved, const BallPhysics& physics,
                                         const SimCore::SimTick now, const double secondsPerTick,
                                         const ReceptionConfig& config, const BallReach& outfield,
                                         const std::optional<std::size_t> excluded) {
  return findFirstReach(state, ball, moved, physics, now, secondsPerTick, config, excluded,
                        [&](const std::size_t playerIndex) {
                          return std::optional(isGoalkeeper(state, playerIndex)
                                                   ? claimReach(state, ball, playerIndex, config)
                                                   : outfield);
                        });
}

void validate(const ReceptionConfig& config) {
  if (!isFiniteNonNegative(config.controlRadius) ||
      !isFiniteNonNegative(config.reclaimDelaySeconds) ||
      !isFiniteNonNegative(config.controlHeight) || !isFiniteNonNegative(config.handsRadius) ||
      !isFiniteNonNegative(config.handsHeight)) {
    throw std::invalid_argument("reception: invalid configuration");
  }
}

}  // namespace ElyverseFootball::SimMatch
