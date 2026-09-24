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

MatchSystem makePursuitSystem(const BallPhysics& physics, const PursuitConfig& config) {
  validate(config);
  return {.name = std::string(kPursuitSystemName),
          .update =
              [physics, config](const MatchStepContext& /*context*/, const MatchState& current,
                                MatchStateWriter& next) {
                const BallState& ball = current.ball();
                if (ball.owner) {
                  return;
                }
                const bool moving = ball.velocity != SimCore::Vec2{};
                std::array<std::optional<Chaser>, 2> chasers;  // home, away
                for (std::size_t index = 0; const PlayerMatchState& player : current.players()) {
                  const std::size_t playerIndex = index++;
                  const bool justPassed =
                      moving && ball.lastTouch && ball.lastTouch->playerId == player.playerId;
                  if (justPassed) {
                    continue;
                  }
                  const auto interception =
                      findInterception(player, ball, physics, current.pitch(), config);
                  if (!interception) {
                    continue;
                  }
                  auto& best = chasers.at(player.side == TeamSide::kHome ? 0 : 1);
                  const Chaser candidate{.index = playerIndex,
                                         .playerId = player.playerId,
                                         .interception = *interception};
                  const auto order = [](const Chaser& chaser) {
                    return std::tuple(chaser.interception.seconds, chaser.playerId);
                  };
                  if (!best || order(candidate) < order(*best)) {
                    best = candidate;
                  }
                }
                for (const auto& chaser : chasers) {
                  if (chaser) {
                    next.setPlayerTarget(chaser->index, chaser->interception.point);
                  }
                }
              },
          .intervalTicks = config.intervalTicks,
          .phaseTicks = 0};
}

}  // namespace ElyverseFootball::SimMatch
