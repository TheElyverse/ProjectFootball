#include "reception.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <tuple>

#include "configChecks.hpp"
#include "playerMovement.hpp"
#include "shotStopping.hpp"
#include "teamFrame.hpp"
#include "zones.hpp"

namespace ElyverseFootball::SimMatch {
namespace {

using ConfigChecks::isFiniteNonNegative;
using SimCore::Vec2;

// How far and how high the player at this index reaches the ball: with his
// hands if hasHands(), at his feet otherwise.
[[nodiscard]] BallReach claimReach(const MatchState& state, const BallState& ball,
                                   const std::size_t playerIndex, const ReceptionConfig& config) {
  return hasHands(state, playerIndex, state.players()[playerIndex].position, ball.position)
             ? BallReach{.radius = config.handsRadius, .height = config.handsHeight}
             : BallReach{.radius = config.controlRadius, .height = config.controlHeight};
}

// Whether the ball's last touch let it go from his hands: he held it at the
// start of the step, or the release is recorded as that touch.
[[nodiscard]] bool releasedFromHands(const MatchState& state, const BallState& ball) {
  if (!ball.lastTouch) {
    return false;
  }
  if (state.ball().held && state.ball().owner == ball.lastTouch->playerId) {
    return true;
  }
  const auto index = findPlayerIndex(state, ball.lastTouch->playerId);
  return index && state.tactical(*index).handsReleased == ball.lastTouch->tick;
}

}  // namespace

bool mayCompete(const MatchState& state, const BallState& ball, const std::size_t playerIndex,
                const SimCore::StepTime time, const ReceptionConfig& config,
                const std::span<const std::size_t> excluded) {
  if (std::ranges::find(excluded, playerIndex) != excluded.end() ||
      !isOnPitch(state.players()[playerIndex]) || isDiving(state.tactical(playerIndex), time)) {
    return false;
  }
  // The last touch cannot take the ball back until reclaimDelaySeconds after
  // his touch, and nobody may stop a goalkeeper letting the ball go from his
  // hands (Law 12): his opponents cannot play it that long either.
  const PlayerMatchState& player = state.players()[playerIndex];
  if (!ball.lastTouch) {
    return true;
  }
  const double sinceTouch = time.secondsSince(ball.lastTouch->tick);
  if (ball.lastTouch->playerId == player.playerId) {
    return sinceTouch >= config.reclaimDelaySeconds;
  }
  const auto toucher = findPlayerIndex(state, ball.lastTouch->playerId);
  return !toucher || state.players()[*toucher].side == player.side ||
         sinceTouch >= config.reclaimDelaySeconds || !releasedFromHands(state, ball);
}

bool hasHands(const MatchState& state, const std::size_t playerIndex, const Vec2 playerPosition,
              const Vec2 ballPosition) {
  const GoalEnd end = ownGoalEnd(state.players()[playerIndex].side);
  return isGoalkeeper(state, playerIndex) && state.pitch().isInPenaltyArea(end, playerPosition) &&
         state.pitch().isInPenaltyArea(end, ballPosition) && !hasReleasedBall(state, playerIndex);
}

bool hasReleasedBall(const MatchState& state, const std::size_t playerIndex) {
  const auto& touch = state.ball().lastTouch;
  return state.tactical(playerIndex).handsReleased && touch &&
         touch->playerId == state.players()[playerIndex].playerId;
}

std::optional<BallClaim> findFirstReach(
    const MatchState& state, const BallState& ball, const BallStep& moved,
    const BallPhysics& physics, const SimCore::StepTime time, const ReceptionConfig& config,
    const std::span<const std::size_t> excluded,
    const std::function<std::optional<BallReach>(std::size_t)>& reachOf) {
  std::optional<BallClaim> best;
  for (std::size_t index = 0; const PlayerMatchState& player : state.players()) {
    const std::size_t playerIndex = index++;
    if (!mayCompete(state, ball, playerIndex, time, config, excluded)) {
      continue;
    }
    const auto reach = reachOf(playerIndex);
    if (!reach) {
      continue;
    }
    const PlayerKinematics stepped = stepPlayerMovement(player, ball, physics, time.secondsPerTick);
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
  const double linear = 2.0 * start.dot(change);
  if (startOutside <= 0.0) {
    if (linear > 0.0) {
      return std::nullopt;
    }
    return Contact{.contactFraction = 0.0, .closestDistance = closestDistance};
  }
  const double discriminant = (linear * linear) - (4.0 * changeSquared * startOutside);
  const double fraction =
      (-linear - std::sqrt(std::max(discriminant, 0.0))) / (2.0 * changeSquared);
  return Contact{.contactFraction = std::clamp(fraction, 0.0, 1.0),
                 .closestDistance = closestDistance};
}

std::optional<BallClaim> findBallClaim(const MatchState& state, const BallState& ball,
                                       const BallStep& moved, const BallPhysics& physics,
                                       const SimCore::StepTime time, const ReceptionConfig& config,
                                       const std::span<const std::size_t> excluded) {
  return findFirstReach(state, ball, moved, physics, time, config, excluded,
                        [&](const std::size_t playerIndex) {
                          return std::optional(claimReach(state, ball, playerIndex, config));
                        });
}

std::optional<BallClaim> findBallContact(const MatchState& state, const BallState& ball,
                                         const BallStep& moved, const BallPhysics& physics,
                                         const SimCore::StepTime time,
                                         const ReceptionConfig& config, const BallReach& outfield,
                                         const std::span<const std::size_t> excluded) {
  return findFirstReach(state, ball, moved, physics, time, config, excluded,
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
