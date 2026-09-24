#include "reception.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <tuple>

#include "playerMovement.hpp"

namespace ElyverseFootball::SimMatch {
namespace {

using SimCore::Vec2;

[[nodiscard]] bool isFiniteNonNegative(const double value) noexcept {
  return value >= 0.0 && value <= std::numeric_limits<double>::max();
}

// The last touch cannot take the ball back until reclaimDelaySeconds after
// his touch.
[[nodiscard]] bool mayClaim(const PlayerMatchState& player, const BallState& ball,
                            const SimCore::SimTick now, const double secondsPerTick,
                            const ReceptionConfig& config) noexcept {
  if (!ball.lastTouch || ball.lastTouch->playerId != player.playerId) {
    return true;
  }
  const double sinceTouch =
      static_cast<double>(now.value() - ball.lastTouch->tick.value()) * secondsPerTick;
  return sinceTouch >= config.reclaimDelaySeconds;
}

}  // namespace

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
                                       const Vec2 ballTo, const SimCore::SimTick now,
                                       const double secondsPerTick, const ReceptionConfig& config) {
  std::optional<BallClaim> best;
  for (std::size_t index = 0; const PlayerMatchState& player : state.players()) {
    const std::size_t playerIndex = index++;
    if (!mayClaim(player, ball, now, secondsPerTick, config)) {
      continue;
    }
    const PlayerKinematics moved = stepPlayerMovement(player, secondsPerTick);
    const auto contact =
        findContact(player.position, moved.position, ball.position, ballTo, config.controlRadius);
    if (!contact) {
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

void validate(const ReceptionConfig& config) {
  if (!isFiniteNonNegative(config.controlRadius) ||
      !isFiniteNonNegative(config.reclaimDelaySeconds)) {
    throw std::invalid_argument("reception: invalid configuration");
  }
}

}  // namespace ElyverseFootball::SimMatch
