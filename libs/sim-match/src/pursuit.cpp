#include "pursuit.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

#include "spatialQueries.hpp"
#include "teamFrame.hpp"
#include "zones.hpp"

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
                                             const PursuitConfig& config,
                                             const double reachHeight) {
  BallState predicted = ball;
  double seconds = 0.0;
  while (true) {
    const auto arrival = estimateArrivalSeconds(player, predicted.position, pitch);
    // A ball above his reach is no interception: he keeps going and takes it
    // where it has come down.
    const bool reachable = predicted.height <= reachHeight;
    if (reachable && arrival && *arrival <= seconds) {
      return Interception{.point = predicted.position, .seconds = seconds};
    }
    if (predicted.isAtRest() || seconds >= config.horizonSeconds) {
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

// Every player's interception of the free ball, by index; empty for one who
// cannot reach it, and for the last player to touch it while it still moves.
[[nodiscard]] std::vector<std::optional<Interception>> findInterceptions(
    const MatchState& current, const BallPhysics& physics, const PursuitConfig& config,
    const double reachHeight) {
  const BallState& ball = current.ball();
  const bool moving = !ball.isAtRest();
  std::vector<std::optional<Interception>> interceptions;
  interceptions.reserve(current.players().size());
  for (const PlayerMatchState& player : current.players()) {
    const bool justPassed = moving && ball.lastTouch && ball.lastTouch->playerId == player.playerId;
    interceptions.push_back(
        justPassed ? std::nullopt
                   : findInterception(player, ball, physics, current.pitch(), config, reachHeight));
  }
  return interceptions;
}

// The side's player who reaches the free ball first, ties to the lower id,
// leaving out the one at `excluded`.
[[nodiscard]] std::optional<Chaser> findChaser(
    const MatchState& current, const std::vector<std::optional<Interception>>& interceptions,
    const TeamSide side, const std::optional<std::size_t> excluded = std::nullopt) {
  std::optional<Chaser> best;
  for (std::size_t index = 0; index < interceptions.size(); ++index) {
    const PlayerMatchState& player = current.players()[index];
    const std::optional<Interception>& interception = interceptions[index];
    if (!interception || player.side != side || index == excluded) {
      continue;
    }
    const Chaser candidate{
        .index = index, .playerId = player.playerId, .interception = *interception};
    const auto order = [](const Chaser& chaser) {
      return std::tuple(chaser.interception.seconds, chaser.playerId);
    };
    if (!best || order(candidate) < order(*best)) {
      best = candidate;
    }
  }
  return best;
}

// Whether the goalkeeper who would chase the free ball comes for it. A ball
// his own side played last he comes for like anyone; one the opponent played
// he judges against the opponents' earliest player, keeping his judgement of
// that ball in his tactical state. Once he stays home for a ball he leaves it;
// once he comes he checks again at every update, so he can abandon a run that
// has become hopeless.
[[nodiscard]] bool comesForBall(const MatchStepContext& context, const MatchState& current,
                                MatchStateWriter& next, const Chaser& keeper,
                                const std::vector<std::optional<Interception>>& interceptions,
                                const GoalkeeperConfig& config) {
  const PlayerMatchState& player = current.players()[keeper.index];
  const auto& touch = current.ball().lastTouch;
  if (!touch) {
    return true;
  }
  const auto toucher = findPlayerIndex(current, touch->playerId);
  if (!toucher || current.players()[*toucher].side == player.side) {
    return true;
  }
  const auto attacker = findChaser(current, interceptions, opponentOf(player.side));
  const auto& previous = current.tactical(keeper.index).sweep;
  const bool sameBall =
      previous && previous->touchedBy == touch->playerId && previous->touchedAt == touch->tick;
  if (sameBall && !previous->coming) {
    return false;
  }
  const double misjudgement =
      sameBall ? previous->misjudgement
               : drawMisjudgement(player.attributes.keeperAnticipation, config,
                                  context.random(SimCore::RandomNumberGeneratorDomain::kAi));
  // A goalkeeper's side has a tactic: his slot is what makes him one.
  const auto& tactic = current.tactics().of(player.side);
  const double sweeping = tactic ? tactic->principles().goalkeeper.sweeping : 0.0;
  const SweepCall call =
      callSweep(keeper.interception.seconds,
                attacker ? std::optional(attacker->interception.seconds) : std::nullopt,
                misjudgement, sweeping, sameBall && previous->coming, config);
  next.tactical(keeper.index).sweep = SweepJudgement{.touchedBy = touch->playerId,
                                                     .touchedAt = touch->tick,
                                                     .misjudgement = misjudgement,
                                                     .coming = call.coming};
  if (!sameBall || previous->coming != call.coming) {
    context.diagnose(
        SweepDiagnostic{.tick = context.tick(), .player = player.playerId, .call = call});
  }
  return call.coming;
}

}  // namespace

MatchSystem makePursuitSystem(const BallPhysics& physics, const PursuitConfig& config,
                              const ReceptionConfig& reception,
                              const GoalkeeperConfig& goalkeeper) {
  validate(config);
  validate(reception);
  validate(goalkeeper);
  return {
      .name = std::string(kPursuitSystemName),
      .update =
          [physics, config, goalkeeper, reachHeight = reception.controlHeight](
              const MatchStepContext& context, const MatchState& current, MatchStateWriter& next) {
            if (current.ball().owner) {
              for (const TeamSide side : {TeamSide::kHome, TeamSide::kAway}) {
                handOver(current, next, side, std::nullopt);
              }
              return;
            }
            const auto interceptions = findInterceptions(current, physics, config, reachHeight);
            for (const TeamSide side : {TeamSide::kHome, TeamSide::kAway}) {
              auto chaser = findChaser(current, interceptions, side);
              if (chaser && isGoalkeeper(current, chaser->index) &&
                  !comesForBall(context, current, next, *chaser, interceptions, goalkeeper)) {
                chaser = findChaser(current, interceptions, side, chaser->index);
              }
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

MatchSystem makePursuitSystem(const BallPhysics& physics, const PursuitConfig& config,
                              const ReceptionConfig& reception) {
  return makePursuitSystem(physics, config, reception, GoalkeeperConfig{});
}

MatchSystem makePursuitSystem(const BallPhysics& physics, const PursuitConfig& config) {
  return makePursuitSystem(physics, config, ReceptionConfig{});
}

}  // namespace ElyverseFootball::SimMatch
