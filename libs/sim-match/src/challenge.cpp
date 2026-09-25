#include "challenge.hpp"

#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>

#include "matchEvents.hpp"

namespace ElyverseFootball::SimMatch {
namespace {

void validate(const ChallengeConfig& config) {
  constexpr double kMax = std::numeric_limits<double>::max();
  const auto positive = [](const double value) { return value > 0.0 && value <= kMax; };
  const bool valid = config.intervalTicks >= 1 && positive(config.radius) &&
                     config.winChance >= 0.0 && config.winChance <= 1.0 &&
                     positive(config.attemptSeconds) && config.protectSeconds >= 0.0 &&
                     config.protectSeconds <= kMax;
  if (!valid) {
    throw std::invalid_argument("ball challenge: invalid configuration");
  }
}

[[nodiscard]] double secondsSince(const SimCore::SimTick earlier, const SimCore::SimTick now,
                                  const double secondsPerTick) noexcept {
  return static_cast<double>(now.value() - earlier.value()) * secondsPerTick;
}

}  // namespace

MatchSystem makeChallengeSystem(const ChallengeConfig& config) {
  validate(config);
  return {
      .name = std::string(kChallengeSystemName),
      .update =
          [config](const MatchStepContext& context, const MatchState& current,
                   MatchStateWriter& next) {
            const BallState& ball = current.ball();
            if (!ball.owner) {
              return;
            }
            const auto carrierIndex = findPlayerIndex(current, *ball.owner);
            if (!carrierIndex) {
              return;
            }
            const PlayerMatchState& carrier = current.players()[*carrierIndex];
            const bool settled =
                !ball.lastTouch || secondsSince(ball.lastTouch->tick, context.tick(),
                                                context.secondsPerTick()) >= config.protectSeconds;
            if (!settled) {
              return;
            }
            for (std::size_t index = 0; index < current.players().size(); ++index) {
              const PlayerMatchState& player = current.players()[index];
              const PlayerTacticalState& tactical = current.tactical(index);
              const bool pressing = tactical.action &&
                                    tactical.action->type == ActionType::kPressCarrier &&
                                    player.side != carrier.side;
              const bool rested = !tactical.lastChallenge ||
                                  secondsSince(*tactical.lastChallenge, context.tick(),
                                               context.secondsPerTick()) >= config.attemptSeconds;
              const double distance =
                  std::sqrt((player.position - carrier.position).lengthSquared());
              if (!pressing || !rested || distance > config.radius) {
                continue;
              }
              next.tactical(index).lastChallenge = context.tick();
              const double roll =
                  context.random(SimCore::RandomNumberGeneratorDomain::kExecution).nextUniform();
              if (roll >= config.winChance) {
                continue;
              }
              next.setBallOwner(player.playerId);
              next.setBallLastTouch(BallTouch{.playerId = player.playerId, .tick = context.tick()});
              next.setPendingPass(std::nullopt);
              context.record(BallWon{.tick = context.tick(),
                                     .winner = player.playerId,
                                     .loser = carrier.playerId,
                                     .position = ball.position});
              context.record(PossessionChanged{.tick = context.tick(),
                                               .previousOwner = carrier.playerId,
                                               .newOwner = player.playerId});
              return;
            }
          },
      .intervalTicks = config.intervalTicks,
      .phaseTicks = 0};
}

}  // namespace ElyverseFootball::SimMatch
