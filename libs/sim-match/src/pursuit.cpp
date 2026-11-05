#include "pursuit.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

#include "configChecks.hpp"
#include "desiredRegion.hpp"
#include "playerMovement.hpp"
#include "shotStopping.hpp"
#include "spatialQueries.hpp"
#include "teamFrame.hpp"
#include "zones.hpp"

namespace ElyverseFootball::SimMatch {
namespace {

using ConfigChecks::isPositiveFinite;

void validate(const PursuitConfig& config) {
  const bool valid = config.intervalTicks >= 1 && isPositiveFinite(config.sampleSeconds) &&
                     isPositiveFinite(config.horizonSeconds) &&
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
                                             const PursuitConfig& config, const double reachHeight,
                                             const std::optional<HandsReach>& hands) {
  BallState predicted = ball;
  double seconds = 0.0;
  while (true) {
    const auto arrival = estimateArrivalSeconds(player, predicted.position, pitch);
    // A ball above his reach is no interception: he keeps going and takes it
    // where it has come down.
    const bool inHands = hands && pitch.isInPenaltyArea(hands->end, predicted.position);
    const bool reachable = predicted.height <= (inHands ? hands->height : reachHeight);
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

// A ball its owner touched on ahead of him is left to him by everyone who
// does not get to it before him, as a defender steps in only to a touch too
// heavy for its owner: their interceptions are emptied.
void leaveToOwner(const MatchState& current,
                  std::vector<std::optional<Interception>>& interceptions) {
  const BallState& ball = current.ball();
  const auto owner = ball.owner ? findPlayerIndex(current, *ball.owner) : std::nullopt;
  if (!owner || !ball.touch) {
    return;
  }
  const std::optional<Interception>& his = interceptions[*owner];
  const double ownerSeconds = his ? his->seconds : std::numeric_limits<double>::infinity();
  for (std::size_t index = 0; index < interceptions.size(); ++index) {
    std::optional<Interception>& interception = interceptions[index];
    if (index != *owner && (!interception || interception->seconds >= ownerSeconds)) {
      interception.reset();
    }
  }
}

// Every player's interception of the free ball, by index; empty for one who
// cannot reach it, for the last player to touch it while it still moves --
// unless he touched it on ahead of him and still owns it: he runs after it at
// his running speed with it --, for a keeper busy with a dive, and as
// leaveToOwner() leaves a touched ball.
// The goalkeeper reaches with his hands in his own penalty area.
[[nodiscard]] std::vector<std::optional<Interception>> findInterceptions(
    const MatchState& current, const BallPhysics& physics, const PursuitConfig& config,
    const ReceptionConfig& reception, const MatchStepContext& context) {
  const BallState& ball = current.ball();
  const bool moving = !ball.isAtRest();
  std::vector<std::optional<Interception>> interceptions;
  interceptions.reserve(current.players().size());
  for (std::size_t index = 0; index < current.players().size(); ++index) {
    const PlayerMatchState& player = current.players()[index];
    if (!isOnPitch(player)) {
      interceptions.emplace_back(std::nullopt);
      continue;
    }
    const bool owns = ball.owner == player.playerId;
    const bool justPassed =
        moving && ball.lastTouch && ball.lastTouch->playerId == player.playerId && !owns;
    const bool down = isOffHisFeet(current.tactical(index), context.time());
    // His hands only while he may still use them, as when he claims it.
    const auto hands = isGoalkeeper(current, index) && !hasReleasedBall(current, index)
                           ? std::optional(HandsReach{.end = ownGoalEnd(player.side),
                                                      .height = reception.handsHeight})
                           : std::nullopt;
    interceptions.push_back(
        justPassed || down
            ? std::nullopt
            : findInterception(owns ? atRunningSpeed(player, ball, physics) : player, ball, physics,
                               current.pitch(), config, reception.controlHeight, hands));
  }
  leaveToOwner(current, interceptions);
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

// The goalkeeper's judgement if it is of the ball with this last touch, null
// otherwise.
[[nodiscard]] const SweepJudgement* judgementOf(const std::optional<SweepJudgement>& judgement,
                                                const BallTouch& touch) noexcept {
  const bool sameBall =
      judgement && judgement->touchedBy == touch.playerId && judgement->touchedAt == touch.tick;
  return sameBall ? &*judgement : nullptr;
}

// Whether the point lies behind the side's defensive line, the space the
// goalkeeper rather than his defenders guards. Before the side has a phase
// there is no line, and everything is behind it.
[[nodiscard]] bool behindDefence(const MatchState& current, const TeamSide side,
                                 const SimCore::Vec2 point) {
  const auto& phase = current.phase(side);
  return !phase ||
         depthOf(side, point, current.pitch()) < defensiveLineDepth(current, side, phase->phase);
}

// A goalkeeper coming for the free ball turns back once someone else is his
// side's chaser, and leaves the ball like one he stayed home for.
void turnBack(const MatchState& current, MatchStateWriter& next, const TeamSide side,
              const std::optional<std::size_t> chaser) {
  const auto& touch = current.ball().lastTouch;
  if (!touch) {
    return;
  }
  for (std::size_t index = 0; index < current.players().size(); ++index) {
    if (current.players()[index].side != side || index == chaser || !isGoalkeeper(current, index)) {
      continue;
    }
    const SweepJudgement* judgement = judgementOf(current.tactical(index).sweep, *touch);
    if (judgement != nullptr && judgement->coming) {
      SweepJudgement left = *judgement;
      left.coming = false;
      next.tactical(index).sweep = left;
    }
  }
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
  const SweepJudgement* previous = judgementOf(current.tactical(keeper.index).sweep, *touch);
  if (previous != nullptr && !previous->coming) {
    return false;
  }
  const double misjudgement =
      previous != nullptr
          ? previous->misjudgement
          : drawMisjudgement(player.attributes.keeperAnticipation, config,
                             context.random(SimCore::RandomNumberGeneratorDomain::kAi));
  // A goalkeeper's side has a tactic: his slot is what makes him one.
  const auto& tactic = current.tactics().of(player.side);
  const double sweeping = tactic ? tactic->principles().goalkeeper.sweeping : 0.0;
  const SweepCall call =
      callSweep(keeper.interception.seconds,
                attacker ? std::optional(attacker->interception.seconds) : std::nullopt,
                misjudgement, sweeping, previous != nullptr && previous->coming, config);
  next.tactical(keeper.index).sweep = SweepJudgement{.touchedBy = touch->playerId,
                                                     .touchedAt = touch->tick,
                                                     .misjudgement = misjudgement,
                                                     .coming = call.coming};
  if (previous == nullptr || previous->coming != call.coming) {
    context.diagnose(
        SweepDiagnostic{.tick = context.tick(), .player = player.playerId, .call = call});
  }
  return call.coming;
}

// Whether the side leaves the ball alone: anyone's while a player controls
// it, his own side's while he has touched it on ahead of him.
[[nodiscard]] bool leavesBall(const MatchState& current, const TeamSide side) {
  const BallState& ball = current.ball();
  if (!ball.owner) {
    return false;
  }
  const auto owner = findPlayerIndex(current, *ball.owner);
  return !owner || !ball.touch || current.players()[*owner].side == side;
}

// Sends the side's player who reaches the ball first after it -- or, if he
// is a goalkeeper who stays home, the next one.
void chaseFor(const MatchStepContext& context, const MatchState& current, MatchStateWriter& next,
              const TeamSide side, const std::vector<std::optional<Interception>>& interceptions,
              const GoalkeeperConfig& goalkeeper) {
  auto chaser = findChaser(current, interceptions, side);
  if (chaser && isGoalkeeper(current, chaser->index) &&
      behindDefence(current, side, chaser->interception.point) &&
      !comesForBall(context, current, next, *chaser, interceptions, goalkeeper)) {
    chaser = findChaser(current, interceptions, side, chaser->index);
  }
  turnBack(current, next, side, chaser ? std::optional(chaser->index) : std::nullopt);
  handOver(current, next, side, chaser ? std::optional(chaser->playerId) : std::nullopt);
  if (chaser) {
    next.setPlayerTarget(chaser->index, chaser->interception.point);
  }
}

}  // namespace

MatchSystem makePursuitSystem(const PursuitRules& rules) {
  const BallPhysics& physics = rules.physics;
  const PursuitConfig& config = rules.pursuit;
  const ReceptionConfig& reception = rules.reception;
  const GoalkeeperConfig& goalkeeper = rules.goalkeeper;
  validate(config);
  validate(reception);
  validate(goalkeeper);
  return {
      .name = std::string(kPursuitSystemName),
      .update =
          [physics, config, goalkeeper, reception](
              const MatchStepContext& context, const MatchState& current, MatchStateWriter& next) {
            // Nobody chases a dead ball (docs/set-pieces.md).
            if (next.setPiece()) {
              return;
            }
            if (leavesBall(current, TeamSide::kHome) && leavesBall(current, TeamSide::kAway)) {
              for (const TeamSide side : {TeamSide::kHome, TeamSide::kAway}) {
                handOver(current, next, side, std::nullopt);
              }
              return;
            }
            const auto interceptions =
                findInterceptions(current, physics, config, reception, context);
            for (const TeamSide side : {TeamSide::kHome, TeamSide::kAway}) {
              if (leavesBall(current, side)) {
                handOver(current, next, side, std::nullopt);
              } else {
                chaseFor(context, current, next, side, interceptions, goalkeeper);
              }
            }
          },
      .intervalTicks = config.intervalTicks,
      .phaseTicks = 0};
}

}  // namespace ElyverseFootball::SimMatch
