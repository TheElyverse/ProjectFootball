#include "aerialDuels.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

#include "choicePolicy.hpp"
#include "passCandidates.hpp"
#include "playerMovement.hpp"
#include "randomDraws.hpp"
#include "shooting.hpp"
#include "shotCandidates.hpp"
#include "teamFrame.hpp"
#include "zones.hpp"

namespace ElyverseFootball::SimMatch {
namespace {

using RandomDraws::symmetricTriangular;
using RandomDraws::symmetricUniform;
using SimCore::unitOr;
using SimCore::Vec2;

[[nodiscard]] bool isFiniteNonNegative(const double value) noexcept {
  return value >= 0.0 && value <= std::numeric_limits<double>::max();
}

[[nodiscard]] bool isPositiveFinite(const double value) noexcept {
  return value > 0.0 && value <= std::numeric_limits<double>::max();
}

// How high he stands to the ball, and with what: his hands or his head.
[[nodiscard]] double standingReach(const bool hands, const ReceptionConfig& reception,
                                   const AerialConfig& config) noexcept {
  return hands ? reception.handsHeight : config.headHeight;
}

// The opposing goalkeeper, if one keeps goal (keepsGoal()).
[[nodiscard]] std::optional<std::size_t> opposingKeeper(const MatchState& state,
                                                        const TeamSide side) {
  for (std::size_t index = 0; index < state.players().size(); ++index) {
    if (state.players()[index].side != side && keepsGoal(state, index)) {
      return index;
    }
  }
  return std::nullopt;
}

// How far the nearest opponent of the player at this index stands from him.
[[nodiscard]] double nearestOpponentDistance(const MatchState& state,
                                             const std::size_t playerIndex) {
  const PlayerMatchState& player = state.players()[playerIndex];
  double nearest = std::numeric_limits<double>::infinity();
  for (const PlayerMatchState& other : state.players()) {
    if (other.side != player.side) {
      nearest = std::min(nearest, SimCore::distance(other.position, player.position));
    }
  }
  return nearest;
}

// Where this many shares of the way from `start` to `end` is.
[[nodiscard]] Vec2 along(const Vec2 start, const Vec2 end, const double fraction) noexcept {
  return start + ((end - start) * fraction);
}

}  // namespace

void validate(const AerialConfig& config) {
  const bool finite =
      isFiniteNonNegative(config.lowestJump) && isFiniteNonNegative(config.highestJump) &&
      isFiniteNonNegative(config.timingError) && isFiniteNonNegative(config.attemptMargin) &&
      isFiniteNonNegative(config.landingSeconds) && isFiniteNonNegative(config.reachWeight) &&
      isFiniteNonNegative(config.reachCap) && isFiniteNonNegative(config.arrivalWeight) &&
      isFiniteNonNegative(config.strengthWeight) && isFiniteNonNegative(config.bodyWeight) &&
      isFiniteNonNegative(config.keeperAdvantage) && isFiniteNonNegative(config.shotRange) &&
      isFiniteNonNegative(config.shotSpeed) && isFiniteNonNegative(config.shotHeight) &&
      isFiniteNonNegative(config.shotInside) && isFiniteNonNegative(config.knockDownRange) &&
      isFiniteNonNegative(config.passWeight) && isFiniteNonNegative(config.clearanceDistance) &&
      isFiniteNonNegative(config.maxHeaderSpeed) && isFiniteNonNegative(config.directionError) &&
      isFiniteNonNegative(config.mistimedErrorFactor);
  const bool positive =
      isPositiveFinite(config.headHeight) && isPositiveFinite(config.headRadius) &&
      isPositiveFinite(config.contestRadius) && isPositiveFinite(config.duelTemperature) &&
      isPositiveFinite(config.decisionTemperature) && isPositiveFinite(config.passSeconds) &&
      isPositiveFinite(config.openDistance) && isPositiveFinite(config.shotRange) &&
      isPositiveFinite(config.passRange) && isPositiveFinite(config.clearanceZone);
  const bool ordered =
      config.lowestJump <= config.highestJump && config.knockDownRange <= config.passRange;
  const bool shares = config.contestedDrop >= 0.0 && config.contestedDrop <= 1.0 &&
                      config.speedError >= 0.0 && config.speedError < 1.0;
  if (!finite || !positive || !ordered || !shares) {
    throw std::invalid_argument("aerial duels: invalid configuration");
  }
}

std::string_view aerialPlayName(const AerialPlay play) noexcept {
  switch (play) {
    case AerialPlay::kShot:
      return "shot";
    case AerialPlay::kPass:
      return "pass";
    case AerialPlay::kKnockDown:
      return "knockDown";
    case AerialPlay::kClearance:
      return "clearance";
    case AerialPlay::kCaught:
      return "caught";
    case AerialPlay::kPunched:
      return "punched";
  }
  return "unknown";
}

double timingSkill(const PlayerAttributes& attributes, const bool hands) noexcept {
  return hands ? attributes.keeperHandling : attributes.heading;
}

double jumpRise(const double jumping, const AerialConfig& config) noexcept {
  return config.lowestJump + ((config.highestJump - config.lowestJump) * jumping);
}

double timingSpread(const double skill, const AerialConfig& config) noexcept {
  return config.timingError * skillErrorFactor(skill);
}

double reachAt(const double standing, const double rise, const double offset,
               const double gravity) noexcept {
  return standing + std::max(0.0, rise - (0.5 * gravity * offset * offset));
}

bool isInTheAir(const MatchState& state, const std::size_t playerIndex, const SimCore::SimTick now,
                const double secondsPerTick, const AerialConfig& config) {
  const auto& jumped = state.tactical(playerIndex).lastJump;
  return jumped && static_cast<double>(now.value() - jumped->value()) * secondsPerTick <
                       config.landingSeconds;
}

BallReach aerialReach(const MatchState& state, const std::size_t playerIndex, const bool hands,
                      const ReceptionConfig& reception, const AerialConfig& config) {
  const PlayerAttributes& attributes = state.players()[playerIndex].attributes;
  return {.radius = hands ? reception.handsRadius : config.headRadius,
          .height = standingReach(hands, reception, config) + jumpRise(attributes.jumping, config) +
                    config.attemptMargin,
          .above = reception.controlHeight};
}

bool mayRiseAbove(const BallState& ball, const double height, const BallPhysics& physics) noexcept {
  if (ball.height > height) {
    return true;
  }
  const double rising = ball.verticalVelocity;
  if (rising <= 0.0) {
    return false;
  }
  return physics.gravity <= 0.0 ||
         ball.height + ((rising * rising) / (2.0 * physics.gravity)) > height;
}

std::optional<BallClaim> findAerialContact(const MatchState& state, const BallState& ball,
                                           const BallStep& moved, const BallPhysics& physics,
                                           const SimCore::SimTick now, const double secondsPerTick,
                                           const ReceptionConfig& reception,
                                           const AerialConfig& config,
                                           const std::span<const std::size_t> excluded) {
  if (!mayRiseAbove(ball, reception.controlHeight, physics)) {
    return std::nullopt;
  }
  // A keeper's hands count where they first get to the ball: in his area,
  // or not at all.
  const auto handsOnContact = [&](const std::size_t playerIndex) {
    if (!isGoalkeeper(state, playerIndex)) {
      return false;
    }
    const PlayerMatchState& player = state.players()[playerIndex];
    const Vec2 stepped = stepPlayerMovement(player, secondsPerTick).position;
    const auto contact = findContact(player.position, stepped, ball.position, moved.ball.position,
                                     reception.handsRadius);
    const double fraction = contact ? contact->contactFraction : 0.0;
    return hasHands(state, playerIndex, along(player.position, stepped, fraction),
                    along(ball.position, moved.ball.position, fraction));
  };
  return findFirstReach(state, ball, moved, physics, now, secondsPerTick, reception, excluded,
                        [&](const std::size_t playerIndex) -> std::optional<BallReach> {
                          if (isInTheAir(state, playerIndex, now, secondsPerTick, config)) {
                            return std::nullopt;
                          }
                          return aerialReach(state, playerIndex, handsOnContact(playerIndex),
                                             reception, config);
                        });
}

std::vector<AerialChallenger> findChallengers(
    const MatchState& state, const BallState& ball, const BallStep& moved, const BallClaim& first,
    const BallPhysics& physics, const SimCore::SimTick now, const double secondsPerTick,
    const ReceptionConfig& reception, const AerialConfig& config,
    const std::span<const std::size_t> excluded) {
  const double fraction = first.contact.contactFraction;
  const Vec2 where = along(ball.position, moved.ball.position, fraction);
  const double height = ballHeightAfter(ball, physics, fraction * moved.seconds);
  const Vec2 way = unitOr(ball.velocity, Vec2{});
  std::vector<AerialChallenger> challengers;
  for (std::size_t index = 0; const PlayerMatchState& player : state.players()) {
    const std::size_t playerIndex = index++;
    const bool isFirst = playerIndex == first.playerIndex;
    if (!isFirst &&
        (!mayCompete(state, ball, playerIndex, now, secondsPerTick, reception, excluded) ||
         isInTheAir(state, playerIndex, now, secondsPerTick, config))) {
      continue;
    }
    const Vec2 position =
        along(player.position, stepPlayerMovement(player, secondsPerTick).position, fraction);
    const bool hands = hasHands(state, playerIndex, position, where);
    const BallReach reach = aerialReach(state, playerIndex, hands, reception, config);
    const double away = SimCore::distance(position, where);
    if (!isFirst && (away > config.contestRadius || height > reach.height)) {
      continue;
    }
    challengers.push_back(
        {.playerIndex = playerIndex,
         .standing = standingReach(hands, reception, config),
         .rise = jumpRise(player.attributes.jumping, config),
         .hands = hands,
         .lateSeconds =
             isFirst ? 0.0 : std::max(0.0, away - reach.radius) / player.attributes.maxSpeed,
         // Facing back along the ball's way is facing the ball.
         .body = (1.0 - player.facing.dot(way)) / 2.0});
  }
  return challengers;
}

double duelUtility(const PlayerAttributes& attributes, const AerialChallenger& challenger,
                   const AerialJump& jump, const double ballHeight,
                   const AerialConfig& config) noexcept {
  const double above = std::clamp(jump.reach - ballHeight, 0.0, config.reachCap);
  const double handling =
      challenger.hands ? config.keeperAdvantage * attributes.keeperHandling : 0.0;
  return (config.reachWeight * above) - (config.arrivalWeight * challenger.lateSeconds) +
         (config.strengthWeight * attributes.strength) + (config.bodyWeight * challenger.body) +
         handling;
}

AerialDuel resolveAerialDuel(const MatchState& state,
                             const std::span<const AerialChallenger> challengers,
                             const double ballHeight, const AerialConfig& config,
                             const double gravity, SimCore::RandomNumberGenerator& random) {
  AerialDuel duel;
  std::vector<double> utilities;
  std::vector<std::size_t> reached;
  for (std::size_t index = 0; index < challengers.size(); ++index) {
    const AerialChallenger& challenger = challengers[index];
    const PlayerAttributes& attributes = state.players()[challenger.playerIndex].attributes;
    AerialJump jump;
    jump.offset = symmetricTriangular(random) *
                  timingSpread(timingSkill(attributes, challenger.hands), config);
    jump.mistime =
        config.timingError > 0.0 ? std::min(1.0, std::abs(jump.offset) / config.timingError) : 0.0;
    jump.reach = reachAt(challenger.standing, challenger.rise, jump.offset, gravity);
    jump.reached = ballHeight <= jump.reach;
    if (jump.reached) {
      utilities.push_back(duelUtility(attributes, challenger, jump, ballHeight, config));
      reached.push_back(index);
    }
    duel.jumps.push_back(jump);
  }
  if (const auto chosen = chooseByUtility(utilities, config.duelTemperature, random)) {
    duel.winner = reached[*chosen];
  }
  return duel;
}

double holdChance(const PlayerAttributes& keeper, const bool contested,
                  const AerialConfig& config) noexcept {
  if (!contested) {
    return 1.0;
  }
  return std::clamp(1.0 - (config.contestedDrop * skillErrorFactor(keeper.keeperHandling)), 0.0,
                    1.0);
}

HeaderIntent clearanceIntent(const MatchState& state, const std::size_t playerIndex,
                             const BallState& ball, const AerialConfig& config) {
  const TeamSide side = state.players()[playerIndex].side;
  const Vec2 upfield{.x = attackingDirection(side) * config.clearanceDistance, .y = 0.0};
  return {.play = AerialPlay::kClearance,
          .target = state.pitch().clamp(ball.position + upfield),
          .height = 0.0,
          .speed = config.maxHeaderSpeed,
          .receiver = std::nullopt};
}

std::vector<HeaderOption> headerOptions(const MatchState& state, const std::size_t playerIndex,
                                        const BallState& ball, const AerialConfig& config) {
  const PlayerMatchState& player = state.players()[playerIndex];
  std::vector<HeaderOption> options;

  const Vec2 ownGoal = state.pitch().goal(ownGoalEnd(player.side)).center;
  options.push_back({.intent = clearanceIntent(state, playerIndex, ball, config),
                     .utility = std::max(0.0, 1.0 - (SimCore::distance(ball.position, ownGoal) /
                                                     config.clearanceZone))});

  const Goal goal = attackedGoal(state.pitch(), player.side);
  const double toGoal = SimCore::distance(ball.position, goal.center);
  if (toGoal <= config.shotRange) {
    // Inside the post away from the keeper; at the centre without one.
    double across = 0.0;
    if (const auto keeper = opposingKeeper(state, player.side)) {
      const double inside = std::max(0.0, (goal.widthMeters / 2.0) - config.shotInside);
      across = state.players()[*keeper].position.y > goal.center.y ? -inside : inside;
    }
    options.push_back({.intent = {.play = AerialPlay::kShot,
                                  .target = {.x = goal.center.x, .y = goal.center.y + across},
                                  .height = config.shotHeight,
                                  .speed = std::min(config.shotSpeed, config.maxHeaderSpeed),
                                  .receiver = std::nullopt},
                       .utility = 1.0 - (toGoal / config.shotRange)});
  }

  for (std::size_t index = 0; index < state.players().size(); ++index) {
    const PlayerMatchState& teammate = state.players()[index];
    if (index == playerIndex || teammate.side != player.side) {
      continue;
    }
    const double distance = SimCore::distance(ball.position, teammate.position);
    if (distance > config.passRange) {
      continue;
    }
    const double open = std::min(1.0, nearestOpponentDistance(state, index) / config.openDistance);
    options.push_back(
        {.intent = {.play = distance <= config.knockDownRange ? AerialPlay::kKnockDown
                                                              : AerialPlay::kPass,
                    .target = teammate.position,
                    .height = 0.0,
                    .speed = std::min(config.maxHeaderSpeed, distance / config.passSeconds),
                    .receiver = teammate.playerId},
         .utility = config.passWeight * open * (1.0 - (distance / config.passRange))});
  }
  return options;
}

HeaderIntent decideHeader(const MatchState& state, const std::size_t playerIndex,
                          const BallState& ball, const AerialConfig& config,
                          SimCore::RandomNumberGenerator& random) {
  const std::vector<HeaderOption> options = headerOptions(state, playerIndex, ball, config);
  std::vector<double> utilities;
  utilities.reserve(options.size());
  for (const HeaderOption& option : options) {
    utilities.push_back(option.utility);
  }
  // The clearance is always an option.
  return options[chooseByUtility(utilities, config.decisionTemperature, random).value_or(0)].intent;
}

double headerErrorFactor(const double skill, const double mistime,
                         const AerialConfig& config) noexcept {
  return skillErrorFactor(skill) * (1.0 + (config.mistimedErrorFactor * mistime));
}

HeaderStrike executeHeader(const HeaderIntent& intent, const BallState& ball,
                           const PlayerMatchState& player, const double errorFactor,
                           const AerialConfig& config, const BallPhysics& physics,
                           SimCore::RandomNumberGenerator& random) noexcept {
  const Vec2 aim = intent.target - ball.position;
  const double spread = config.directionError * aim.length() * errorFactor;
  // Drawn first and always, so the stream advances the same way for every
  // header, whatever its target.
  const double across = symmetricTriangular(random) * spread;
  const double upward = symmetricTriangular(random) * spread;
  const double strength =
      1.0 + (symmetricUniform(random) * std::min(config.speedError * errorFactor, 0.5));

  const Vec2 line = unitOr(aim, player.facing);
  HeaderStrike strike;
  strike.target = intent.target + (Vec2{.x = -line.y, .y = line.x} * across);
  strike.height = std::max(0.0, intent.height + upward);
  const Vec2 offset = strike.target - ball.position;
  const double fastest = std::min(config.maxHeaderSpeed, kMaxBallSpeed);
  const double speed = std::min(intent.speed, fastest);
  // Down to a point below the ball is a launch with a negative height; one
  // the drag stops short of goes up at 45 degrees, the farthest it can.
  const Launch launch{
      .speed = speed, .distance = offset.length(), .height = strike.height - ball.height};
  const double lift = launchVerticalVelocity(launch, physics).value_or(speed);
  // The pace strays along the ground and up alike, but never past fastest.
  const double pace = std::hypot(speed, lift) * strength;
  const double scale = pace > fastest ? strength * fastest / pace : strength;
  strike.velocity = unitOr(offset, player.facing) * (speed * scale);
  strike.verticalVelocity = lift * scale;
  return strike;
}

}  // namespace ElyverseFootball::SimMatch
