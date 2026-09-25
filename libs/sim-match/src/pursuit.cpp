#include "pursuit.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <string>
#include <tuple>

#include "spatialQueries.hpp"

namespace ElyverseFootball::SimMatch {
namespace {

void validate(const PursuitConfig& config) {
  constexpr double kMax = std::numeric_limits<double>::max();
  const bool valid = config.intervalTicks >= 1 && config.sampleSeconds > 0.0 &&
                     config.sampleSeconds <= kMax && config.horizonSeconds > 0.0 &&
                     config.horizonSeconds <= kMax &&
                     config.horizonSeconds / config.sampleSeconds <= kMaxPursuitSamples;
  if (!valid) {
    throw std::invalid_argument("pursuit: invalid configuration");
  }
}

// The player of one side who reaches the ball first.
struct Chaser {
  std::size_t index = 0;
  SimCore::PlayerId playerId;
  Interception interception;
};

// Home, away.
using Chasers = std::array<std::optional<Chaser>, 2>;

// Makes successor the side's chaser. A player who stops chasing stops where
// he is: his target was the ball's, and without the ball it leads nowhere.
void handOver(const MatchState& current, MatchStateWriter& next, const TeamSide side,
              const std::optional<SimCore::PlayerId> successor) {
  const auto& chaser = current.chaser(side);
  if (chaser && chaser != successor) {
    if (const auto index = findPlayerIndex(current, *chaser)) {
      next.setPlayerTarget(*index, std::nullopt);
    }
  }
  next.setChaser(side, successor);
}

}  // namespace

std::optional<Interception> findInterception(const PlayerMatchState& player, const BallState& ball,
                                             const BallPhysics& physics, const Pitch& pitch,
                                             const PursuitConfig& config) {
  BallState predicted = ball;
  double seconds = 0.0;
  while (true) {
    const auto arrival = estimateArrivalSeconds(player, predicted.position, pitch);
    if (arrival && *arrival <= seconds) {
      return Interception{.point = predicted.position, .seconds = seconds};
    }
    const bool resting = predicted.velocity == SimCore::Vec2{};
    if (resting || seconds >= config.horizonSeconds) {
      if (!arrival) {
        return std::nullopt;
      }
      return Interception{.point = predicted.position, .seconds = std::max(seconds, *arrival)};
    }
    // The last step ends on the horizon: a full sample could reach past it,
    // and repeated additions of 0.1 s fall just short of 8 s.
    const double step = std::min(config.sampleSeconds, config.horizonSeconds - seconds);
    predicted = stepFreeBall(predicted, physics, pitch, step);
    seconds = step == config.sampleSeconds ? seconds + step : config.horizonSeconds;
  }
}

namespace {

// Per side, the player who reaches the free ball first, ties to the lower id.
// The last player to touch the ball does not chase it while it still moves.
[[nodiscard]] Chasers findChasers(const MatchState& current, const BallPhysics& physics,
                                  const PursuitConfig& config) {
  const BallState& ball = current.ball();
  const bool moving = ball.velocity != SimCore::Vec2{};
  Chasers chasers;
  for (std::size_t index = 0; const PlayerMatchState& player : current.players()) {
    const std::size_t playerIndex = index++;
    const bool justPassed = moving && ball.lastTouch && ball.lastTouch->playerId == player.playerId;
    if (justPassed) {
      continue;
    }
    const auto interception = findInterception(player, ball, physics, current.pitch(), config);
    if (!interception) {
      continue;
    }
    auto& best = chasers.at(player.side == TeamSide::kHome ? 0 : 1);
    const Chaser candidate{
        .index = playerIndex, .playerId = player.playerId, .interception = *interception};
    const auto order = [](const Chaser& chaser) {
      return std::tuple(chaser.interception.seconds, chaser.playerId);
    };
    if (!best || order(candidate) < order(*best)) {
      best = candidate;
    }
  }
  return chasers;
}

}  // namespace

MatchSystem makePursuitSystem(const BallPhysics& physics, const PursuitConfig& config) {
  validate(config);
  return {.name = std::string(kPursuitSystemName),
          .update =
              [physics, config](const MatchStepContext& /*context*/, const MatchState& current,
                                MatchStateWriter& next) {
                if (current.ball().owner) {
                  for (const TeamSide side : {TeamSide::kHome, TeamSide::kAway}) {
                    handOver(current, next, side, std::nullopt);
                  }
                  return;
                }
                const Chasers chasers = findChasers(current, physics, config);
                for (const TeamSide side : {TeamSide::kHome, TeamSide::kAway}) {
                  const auto& chaser = chasers.at(side == TeamSide::kHome ? 0 : 1);
                  handOver(current, next, side,
                           chaser ? std::optional(chaser->playerId) : std::nullopt);
                  if (chaser) {
                    next.setPlayerTarget(chaser->index, chaser->interception.point);
                  }
                }
              },
          .intervalTicks = config.intervalTicks,
          .phaseTicks = 0};
}

}  // namespace ElyverseFootball::SimMatch
