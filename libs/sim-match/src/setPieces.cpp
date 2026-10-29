#include "setPieces.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "choicePolicy.hpp"
#include "configChecks.hpp"
#include "matchEvents.hpp"
#include "teamFrame.hpp"
#include "zones.hpp"

namespace ElyverseFootball::SimMatch {
namespace {

using SimCore::Vec2;
using SimTactics::SetPieceDelivery;
using SimTactics::SetPieceKind;
using SimTactics::SetPiecePlan;

// How far in front of the goal line the box's targets stand.
inline constexpr double kBoxDepth = 2.0;  // m
// Where a throw-in's short options and a corner's short option stand.
inline constexpr double kAlongLine = 8.0;  // m
inline constexpr double kInfield = 10.0;   // m

// The index of the player in this slot of the side, if he is on the pitch.
[[nodiscard]] std::optional<std::size_t> playerInSlot(const MatchState& state, const TeamSide side,
                                                      const std::size_t slot) {
  std::size_t seen = 0;
  for (std::size_t index = 0; index < state.players().size(); ++index) {
    if (state.players()[index].side != side) {
      continue;
    }
    if (seen == slot) {
      return isOnPitch(state.players()[index]) ? std::optional(index) : std::nullopt;
    }
    ++seen;
  }
  return std::nullopt;
}

// How far up the pitch his slot stands, or he stands for a side without a
// tactic, as a share of the length.
[[nodiscard]] double slotDepth(const MatchState& state, const std::size_t index) {
  const PlayerMatchState& player = state.players()[index];
  if (const auto& tactic = state.tactics().of(player.side)) {
    return tactic->slots()[slotIndex(state, index)].position.depth;
  }
  return depthOf(player.side, player.position, state.pitch()) / state.pitch().lengthMeters();
}

// Where his slot stands on the pitch, or he stands for a side without a
// tactic: what choosing by nearness to the ball reads, so the choice holds
// while the players move.
[[nodiscard]] Vec2 slotSpot(const MatchState& state, const std::size_t index) {
  const PlayerMatchState& player = state.players()[index];
  if (const auto& tactic = state.tactics().of(player.side)) {
    const SimTactics::ShapePosition& shape = tactic->slots()[slotIndex(state, index)].position;
    return {.x = xAtDepth(player.side, shape.depth * state.pitch().lengthMeters(), state.pitch()),
            .y = shape.width * state.pitch().widthMeters()};
  }
  return player.position;
}

// The side's outfield players on the pitch but the taker.
[[nodiscard]] std::vector<std::size_t> outfield(const MatchState& state, const TeamSide side,
                                                const std::optional<std::size_t> taker) {
  std::vector<std::size_t> players;
  for (std::size_t index = 0; index < state.players().size(); ++index) {
    const PlayerMatchState& player = state.players()[index];
    if (player.side == side && isOnPitch(player) && index != taker && !keepsGoal(state, index)) {
      players.push_back(index);
    }
  }
  return players;
}

// The players a plan names in these slots, on the pitch and not the taker, or,
// without any such, `count` of `pool` in the order `before` sorts them.
// NOLINTBEGIN(bugprone-easily-swappable-parameters) -- planned slots, then the pool to choose from.
template <typename Before>
[[nodiscard]] std::vector<std::size_t> chosen(const MatchState& state, const TeamSide side,
                                              const std::vector<std::size_t>& slots,
                                              std::vector<std::size_t> pool, const int count,
                                              const std::optional<std::size_t> taker,
                                              Before before) {
  std::vector<std::size_t> players;
  for (const std::size_t slot : slots) {
    if (const auto index = playerInSlot(state, side, slot); index && index != taker) {
      players.push_back(*index);
    }
  }
  if (!players.empty()) {
    return players;
  }
  std::ranges::stable_sort(pool, before);
  pool.resize(std::min(pool.size(), static_cast<std::size_t>(std::max(count, 0))));
  return pool;
}
// NOLINTEND(bugprone-easily-swappable-parameters)

// The places a corner's attackers take, and where its delivery goes.
struct CornerPlaces {
  Vec2 nearPost;
  Vec2 farPost;
  Vec2 penaltySpot;
  Vec2 goalArea;
  Vec2 edge;
  Vec2 shortOption;
};

[[nodiscard]] CornerPlaces cornerPlaces(const MatchState& state, const SetPiece& setPiece) {
  const Pitch& pitch = state.pitch();
  const TeamSide side = setPiece.side;
  const Goal goal = attackedGoal(pitch, side);
  const double length = pitch.lengthMeters();
  const bool nearMin = std::abs(setPiece.spot.y - goal.postAtMinY().y) <
                       std::abs(setPiece.spot.y - goal.postAtMaxY().y);
  const double nearY = nearMin ? goal.postAtMinY().y : goal.postAtMaxY().y;
  const double farY = nearMin ? goal.postAtMaxY().y : goal.postAtMinY().y;
  const double box = xAtDepth(side, length - kBoxDepth, pitch);
  const PitchMarkings& marks = pitch.markings();
  const double toGoal = goal.center.y > setPiece.spot.y ? 1.0 : -1.0;
  return {.nearPost = {.x = box, .y = nearY},
          .farPost = {.x = box, .y = farY},
          .penaltySpot = pitch.penaltySpot(ownGoalEnd(opponentOf(side))),
          .goalArea = {.x = xAtDepth(side, length - marks.goalAreaDepthMeters, pitch),
                       .y = goal.center.y},
          .edge = {.x = xAtDepth(side, length - marks.penaltyAreaDepthMeters - 1.0, pitch),
                   .y = goal.center.y},
          .shortOption = pitch.clamp({.x = xAtDepth(side, length - (kBoxDepth + 1.0), pitch),
                                      .y = setPiece.spot.y + (toGoal * kAlongLine)})};
}

[[nodiscard]] Vec2 deliveryPlace(const CornerPlaces& places, const SetPieceDelivery delivery) {
  switch (delivery) {
    case SetPieceDelivery::kNearPost:
      return places.nearPost;
    case SetPieceDelivery::kPenaltySpot:
      return places.penaltySpot;
    case SetPieceDelivery::kShort:
      return places.shortOption;
    case SetPieceDelivery::kFarPost:
    case SetPieceDelivery::kLong:
      return places.farPost;
  }
  return places.farPost;
}

// The places a corner's runners take in their order: the delivery's first,
// then the box's others.
[[nodiscard]] std::vector<Vec2> cornerTargets(const CornerPlaces& places,
                                              const SetPieceDelivery delivery) {
  std::vector<Vec2> targets{deliveryPlace(places, delivery)};
  for (const Vec2 place :
       {places.nearPost, places.farPost, places.penaltySpot, places.goalArea, places.edge}) {
    if (!(place == targets.front())) {
      targets.push_back(place);
    }
  }
  return targets;
}

// The places a throw-in's or a goal kick's short options take.
[[nodiscard]] std::vector<Vec2> shortTargets(const MatchState& state, const SetPiece& setPiece) {
  const Pitch& pitch = state.pitch();
  const TeamSide side = setPiece.side;
  if (setPiece.kind == SetPieceKind::kGoalKick) {
    const PitchMarkings& marks = pitch.markings();
    const double x = xAtDepth(side, marks.penaltyAreaDepthMeters + 1.0, pitch);
    const double wide = (marks.penaltyAreaWidthMeters / 2.0) - 3.0;
    const double middle = pitch.widthMeters() / 2.0;
    return {{.x = x, .y = middle - wide}, {.x = x, .y = middle + wide}};
  }
  const Vec2 infield{.x = 0.0, .y = setPiece.spot.y < pitch.widthMeters() / 2.0 ? 1.0 : -1.0};
  const Vec2 forward{.x = attackingDirection(side), .y = 0.0};
  return {pitch.clamp(setPiece.spot + (forward * kAlongLine) + (infield * 2.0)),
          pitch.clamp(setPiece.spot + (infield * kInfield))};
}

// Goal side of `place`, toward the goal `side` defends.
[[nodiscard]] Vec2 goalSide(const MatchState& state, const TeamSide side, const Vec2 place,
                            const double distance) {
  const Vec2 goal = attackedGoal(state.pitch(), opponentOf(side)).center;
  return state.pitch().clamp(place + (SimCore::unitOr(goal - place, {}) * distance));
}

// The set piece's attackers: who goes where, in their order.
struct Runners {
  std::vector<std::size_t> players;
  std::vector<Vec2> places;
};

[[nodiscard]] Runners runnersOf(const MatchState& state, const SetPiece& setPiece,
                                const SetPieceConfig& config) {
  const auto taker = findPlayerIndex(state, setPiece.taker);
  const SetPiecePlan& plan = plansOf(state, setPiece.side).of(setPiece.kind);
  const auto pool = outfield(state, setPiece.side, taker);
  switch (setPiece.kind) {
    case SetPieceKind::kFreeKick: {
      // Within range, into the box as at a corner -- for the rebound and the
      // cross the taker may choose; beyond it, they play on in their shape.
      if (SimCore::distance(setPiece.spot, attackedGoal(state.pitch(), setPiece.side).center) >
          config.freeKickRange) {
        return {};
      }
      const auto places =
          cornerTargets(cornerPlaces(state, setPiece), SetPieceDelivery::kPenaltySpot);
      auto players = chosen(state, setPiece.side, plan.runners, pool, config.cornerRunners, taker,
                            [&state](const std::size_t left, const std::size_t right) {
                              return slotDepth(state, left) > slotDepth(state, right);
                            });
      players.resize(std::min(players.size(), places.size()));
      return {.players = players, .places = places};
    }
    case SetPieceKind::kPenalty:
      return {};
    case SetPieceKind::kCorner: {
      const auto places = cornerTargets(cornerPlaces(state, setPiece), plan.delivery);
      auto players = chosen(state, setPiece.side, plan.runners, pool, config.cornerRunners, taker,
                            [&state](const std::size_t left, const std::size_t right) {
                              return slotDepth(state, left) > slotDepth(state, right);
                            });
      players.resize(std::min(players.size(), places.size()));
      return {.players = players, .places = places};
    }
    case SetPieceKind::kThrowIn: {
      const Vec2 spot = setPiece.spot;
      auto players = chosen(state, setPiece.side, plan.runners, pool, config.shortOptions, taker,
                            [&state, spot](const std::size_t left, const std::size_t right) {
                              return SimCore::distance(slotSpot(state, left), spot) <
                                     SimCore::distance(slotSpot(state, right), spot);
                            });
      const auto places = shortTargets(state, setPiece);
      players.resize(std::min(players.size(), places.size()));
      return {.players = players, .places = places};
    }
    case SetPieceKind::kGoalKick: {
      auto players = chosen(state, setPiece.side, plan.runners, pool, config.shortOptions, taker,
                            [&state](const std::size_t left, const std::size_t right) {
                              return slotDepth(state, left) < slotDepth(state, right);
                            });
      const auto places = shortTargets(state, setPiece);
      players.resize(std::min(players.size(), places.size()));
      return {.players = players, .places = places};
    }
    case SetPieceKind::kKickoff:
      return {};
  }
  return {};
}

// At a goal kick everyone of the defending side in the penalty area leaves it.
void clearArea(const MatchState& state, const SetPiece& setPiece,
               std::vector<std::optional<Vec2>>& targets) {
  const TeamSide side = opponentOf(setPiece.side);
  const Pitch& pitch = state.pitch();
  const PitchRect area = pitch.penaltyArea(ownGoalEnd(setPiece.side));
  const double outside =
      xAtDepth(setPiece.side, pitch.markings().penaltyAreaDepthMeters + 1.0, pitch);
  for (std::size_t index = 0; index < state.players().size(); ++index) {
    const PlayerMatchState& player = state.players()[index];
    if (player.side == side && isOnPitch(player) && area.contains(player.position)) {
      targets[index] = Vec2{.x = outside, .y = player.position.y};
    }
  }
}

// At a corner the keeper on his line and the next two outfield players who
// mark nobody in zones at the near post and the goal area's front.
// NOLINTBEGIN(bugprone-easily-swappable-parameters) -- the pool, then who of it marks.
void cornerZones(const MatchState& state, const SetPiece& setPiece,
                 const std::vector<std::size_t>& pool, const std::vector<std::size_t>& markers,
                 std::vector<std::optional<Vec2>>& targets) {
  // NOLINTEND(bugprone-easily-swappable-parameters)
  const TeamSide side = opponentOf(setPiece.side);
  const CornerPlaces places = cornerPlaces(state, setPiece);
  const std::array<Vec2, 2> zones{goalSide(state, side, places.nearPost, kBoxDepth - 1.0),
                                  places.goalArea};
  std::size_t zone = 0;
  for (const std::size_t index : pool) {
    if (zone < zones.size() && std::ranges::find(markers, index) == markers.end()) {
      targets[index] = zones.at(zone);
      ++zone;
    }
  }
  const Goal goal = attackedGoal(state.pitch(), setPiece.side);
  for (std::size_t index = 0; index < state.players().size(); ++index) {
    if (state.players()[index].side == side && isOnPitch(state.players()[index]) &&
        keepsGoal(state, index)) {
      targets[index] = Vec2{.x = xAtDepth(side, 1.0, state.pitch()), .y = goal.center.y};
    }
  }
}

// At a penalty everyone but the taker and the defending keeper stands outside
// the penalty area and a centre circle's radius from the spot, by placeRadius
// and half a metre more, so that one near enough his place to count as in it
// is out; the keeper on his line between the posts.
void clearForPenalty(const MatchState& state, const SetPiece& setPiece,
                     const SetPieceConfig& config, std::vector<std::optional<Vec2>>& targets) {
  const Pitch& pitch = state.pitch();
  const TeamSide attacking = setPiece.side;
  const PitchRect area = pitch.penaltyArea(ownGoalEnd(opponentOf(attacking)));
  const double margin = config.placeRadius + 0.5;
  const double away = pitch.markings().centerCircleRadiusMeters + margin;
  const double line = pitch.lengthMeters() - pitch.markings().penaltyAreaDepthMeters - margin;
  const double spotDepth = depthOf(attacking, setPiece.spot, pitch);
  for (std::size_t index = 0; index < state.players().size(); ++index) {
    const PlayerMatchState& player = state.players()[index];
    if (!isOnPitch(player) || player.playerId == setPiece.taker) {
      continue;
    }
    if (player.side != attacking && keepsGoal(state, index)) {
      targets[index] = Vec2{.x = xAtDepth(player.side, 0.3, pitch),
                            .y = attackedGoal(pitch, attacking).center.y};
      continue;
    }
    if (!area.contains(player.position) && SimCore::distance(player.position, setPiece.spot) >=
                                               pitch.markings().centerCircleRadiusMeters) {
      continue;
    }
    // Behind the line and outside the arc, level with where he stands.
    const double across = std::abs(player.position.y - setPiece.spot.y);
    double depth = line;
    if (across < away) {
      depth = std::min(depth, spotDepth - std::sqrt((away * away) - (across * across)));
    }
    targets[index] = pitch.clamp({.x = xAtDepth(attacking, depth, pitch), .y = player.position.y});
  }
}

// At a free kick within range the defending side's most advanced outfield
// players form a wall across the way from the ball to the goal's centre, a
// centre circle's radius from the ball.
void buildWall(const MatchState& state, const SetPiece& setPiece, const SetPieceConfig& config,
               std::vector<std::optional<Vec2>>& targets) {
  const Pitch& pitch = state.pitch();
  const TeamSide side = opponentOf(setPiece.side);
  const Vec2 goal = attackedGoal(pitch, setPiece.side).center;
  const double distance = SimCore::distance(setPiece.spot, goal);
  if (distance > config.freeKickRange) {
    return;
  }
  int size = config.wallPlayers;
  if (distance > 27.0) {
    size -= 2;
  } else if (distance > 22.0) {
    size -= 1;
  }
  auto wall = chosen(state, side, {}, outfield(state, side, std::nullopt), size, std::nullopt,
                     [&state](const std::size_t left, const std::size_t right) {
                       return slotDepth(state, left) > slotDepth(state, right);
                     });
  const Vec2 toGoal = SimCore::unitOr(goal - setPiece.spot, {.x = 1.0, .y = 0.0});
  const Vec2 across{.x = -toGoal.y, .y = toGoal.x};
  const Vec2 centre = setPiece.spot + (toGoal * (pitch.markings().centerCircleRadiusMeters + 0.5));
  const double middle = (static_cast<double>(wall.size()) - 1.0) / 2.0;
  for (std::size_t place = 0; place < wall.size(); ++place) {
    targets[wall[place]] = pitch.clamp(
        centre + (across * ((static_cast<double>(place) - middle) * config.wallSpacing)));
  }
}

// The Laws keep the other side from the ball: throwInDistance at a throw-in, a
// centre circle's radius at a corner. Whoever would stand nearer -- at his
// place, or where he is without one -- goes out to that distance.
void keepAway(const MatchState& state, const SetPiece& setPiece, const SetPieceConfig& config,
              std::vector<std::optional<Vec2>>& targets) {
  const TeamSide side = opponentOf(setPiece.side);
  const Pitch& pitch = state.pitch();
  const Vec2 spot = setPiece.spot;
  const double away = setPiece.kind == SetPieceKind::kThrowIn
                          ? config.throwInDistance
                          : pitch.markings().centerCircleRadiusMeters;
  for (std::size_t index = 0; index < state.players().size(); ++index) {
    const PlayerMatchState& player = state.players()[index];
    const Vec2 target = targets[index].value_or(player.position);
    if (player.side == side && isOnPitch(player) && SimCore::distance(target, spot) < away) {
      targets[index] =
          pitch.clamp(spot + (SimCore::unitOr(target - spot, pitch.center() - spot) * away));
    }
  }
}

// The defenders' places: markers goal side of the runners, at a corner its
// keeper on his line and two more in zones; at a goal kick everyone out of
// the penalty area. Kept the distance the Laws ask from the ball.
void defend(const MatchState& state, const SetPiece& setPiece, const SetPieceConfig& config,
            const Runners& runners, std::vector<std::optional<Vec2>>& targets) {
  if (setPiece.kind == SetPieceKind::kGoalKick) {
    clearArea(state, setPiece, targets);
    return;
  }
  if (setPiece.kind == SetPieceKind::kKickoff) {
    return;
  }
  if (setPiece.kind == SetPieceKind::kPenalty) {
    clearForPenalty(state, setPiece, config, targets);
    return;
  }
  if (setPiece.kind == SetPieceKind::kFreeKick) {
    buildWall(state, setPiece, config, targets);
  }
  const TeamSide side = opponentOf(setPiece.side);
  const SetPiecePlan& plan = plansOf(state, side).of(setPiece.kind);
  const auto pool = outfield(state, side, std::nullopt);
  const Vec2 spot = setPiece.spot;
  // At a corner or a free kick the deepest slots mark, at a throw-in those whose
  // slots stand nearest the ball: the choice holds while they move.
  const bool corner =
      setPiece.kind == SetPieceKind::kCorner || setPiece.kind == SetPieceKind::kFreeKick;
  const auto markers =
      chosen(state, side, plan.markers, pool, static_cast<int>(runners.players.size()),
             std::nullopt, [&state, spot, corner](const std::size_t left, const std::size_t right) {
               if (corner) {
                 return slotDepth(state, left) < slotDepth(state, right);
               }
               return SimCore::distance(slotSpot(state, left), spot) <
                      SimCore::distance(slotSpot(state, right), spot);
             });
  for (std::size_t mark = 0; mark < markers.size() && mark < runners.players.size(); ++mark) {
    targets[markers[mark]] = goalSide(state, side, runners.places[mark], config.markDistance);
  }
  if (setPiece.kind == SetPieceKind::kCorner) {
    cornerZones(state, setPiece, pool, markers, targets);
  }
  keepAway(state, setPiece, config, targets);
}

// A pass of the delivery's kind to `target`.
[[nodiscard]] PassIntent passTo(const SetPiece& setPiece, const Vec2 from, const Vec2 target,
                                const std::optional<SimCore::PlayerId> receiver, const bool lofted,
                                const SetPieceConfig& config, const BallPhysics& physics,
                                const PassConfig& passing) {
  const double distance = SimCore::distance(from, target);
  const double speed = lofted ? std::min(flightSpeed(distance, config.flightSeconds, physics)
                                             .value_or(passing.maxLoftedSpeed),
                                         passing.maxLoftedSpeed)
                              : planPassSpeed(distance, physics, passing);
  return {.passer = setPiece.taker,
          .target = target,
          .speed = speed,
          .receiver = receiver,
          .lofted = lofted};
}

}  // namespace

void validate(const SetPieceConfig& config) {
  using ConfigChecks::isFiniteNonNegative;
  using ConfigChecks::isPositiveFinite;
  const bool valid =
      isFiniteNonNegative(config.throwInSeconds) && isFiniteNonNegative(config.cornerSeconds) &&
      isFiniteNonNegative(config.goalKickSeconds) && isFiniteNonNegative(config.kickoffSeconds) &&
      isPositiveFinite(config.readyRadius) && isPositiveFinite(config.placeRadius) &&
      isFiniteNonNegative(config.waitSeconds) && isPositiveFinite(config.flightSeconds) &&
      isFiniteNonNegative(config.markDistance) && config.cornerRunners >= 0 &&
      config.shortOptions >= 0 && isFiniteNonNegative(config.throwInDistance) &&
      isFiniteNonNegative(config.freeKickSeconds) && isFiniteNonNegative(config.penaltySeconds) &&
      isFiniteNonNegative(config.freeKickRange) && config.wallPlayers >= 0 &&
      isFiniteNonNegative(config.wallSpacing);
  if (!valid) {
    throw std::invalid_argument("set pieces: invalid configuration");
  }
}

double setupSeconds(const SetPieceKind kind, const SetPieceConfig& config) noexcept {
  switch (kind) {
    case SetPieceKind::kThrowIn:
      return config.throwInSeconds;
    case SetPieceKind::kCorner:
      return config.cornerSeconds;
    case SetPieceKind::kGoalKick:
      return config.goalKickSeconds;
    case SetPieceKind::kFreeKick:
      return config.freeKickSeconds;
    case SetPieceKind::kPenalty:
      return config.penaltySeconds;
    case SetPieceKind::kKickoff:
      return config.kickoffSeconds;
  }
  return 0.0;
}

const SimTactics::SetPiecePlans& plansOf(const MatchState& state, const TeamSide side) {
  static const SimTactics::SetPiecePlans kDefaultPlans;
  const auto& tactic = state.tactics().of(side);
  return tactic ? tactic->setPieces() : kDefaultPlans;
}

std::size_t plannedTaker(const MatchState& state, const TeamSide side, const SetPieceKind kind,
                         const std::size_t fallback) {
  const auto& slot = plansOf(state, side).of(kind).taker;
  if (!slot) {
    return fallback;
  }
  return playerInSlot(state, side, *slot).value_or(fallback);
}

Vec2 takerSpot(const MatchState& state, const SetPiece& setPiece, const BallPhysics& physics) {
  const Pitch& pitch = state.pitch();
  Vec2 facing{.x = attackingDirection(setPiece.side), .y = 0.0};
  if (setPiece.kind == SetPieceKind::kCorner || setPiece.kind == SetPieceKind::kThrowIn) {
    facing = SimCore::unitOr(pitch.center() - setPiece.spot, facing);
  }
  return pitch.clamp(setPiece.spot - (facing * physics.carryDistance));
}

std::vector<std::optional<Vec2>> setPieceTargets(const MatchState& state, const SetPiece& setPiece,
                                                 const SetPieceConfig& config,
                                                 const BallPhysics& physics) {
  std::vector<std::optional<Vec2>> targets(state.players().size());
  const Runners runners = runnersOf(state, setPiece, config);
  for (std::size_t runner = 0; runner < runners.players.size(); ++runner) {
    targets[runners.players[runner]] = runners.places[runner];
  }
  defend(state, setPiece, config, runners, targets);
  if (const auto taker = findPlayerIndex(state, setPiece.taker)) {
    targets[*taker] = takerSpot(state, setPiece, physics);
  }
  return targets;
}

std::optional<PassIntent> plannedDelivery(const MatchState& state, const SetPiece& setPiece,
                                          const SetPieceConfig& config, const BallPhysics& physics,
                                          const PassConfig& passing) {
  const auto taker = findPlayerIndex(state, setPiece.taker);
  if (!taker) {
    return std::nullopt;
  }
  // Their taker decides for himself (docs/set-pieces.md#free-kicks).
  if (setPiece.kind == SetPieceKind::kFreeKick || setPiece.kind == SetPieceKind::kPenalty) {
    return std::nullopt;
  }
  const SetPiecePlan& plan = plansOf(state, setPiece.side).of(setPiece.kind);
  const Runners runners = runnersOf(state, setPiece, config);
  const Vec2 from = setPiece.spot;
  const auto idOf = [&state](const std::size_t index) { return state.players()[index].playerId; };
  if (setPiece.kind == SetPieceKind::kCorner) {
    const Vec2 place = deliveryPlace(cornerPlaces(state, setPiece), plan.delivery);
    const auto receiver =
        runners.players.empty() ? std::nullopt : std::optional(idOf(runners.players.front()));
    return passTo(setPiece, from, place, receiver, plan.delivery != SetPieceDelivery::kShort,
                  config, physics, passing);
  }
  const auto pool = outfield(state, setPiece.side, taker);
  if (pool.empty()) {
    return std::nullopt;
  }
  if (plan.delivery == SetPieceDelivery::kLong) {
    const std::size_t front = *std::ranges::max_element(
        pool, [&state, &setPiece](const std::size_t left, const std::size_t right) {
          return depthOf(setPiece.side, state.players()[left].position, state.pitch()) <
                 depthOf(setPiece.side, state.players()[right].position, state.pitch());
        });
    return passTo(setPiece, from, state.players()[front].position, idOf(front), true, config,
                  physics, passing);
  }
  // Short: the first option, or for a kickoff the nearest teammate.
  const std::size_t near =
      !runners.players.empty()
          ? runners.players.front()
          : *std::ranges::min_element(
                pool, [&state, from](const std::size_t left, const std::size_t right) {
                  return SimCore::distance(state.players()[left].position, from) <
                         SimCore::distance(state.players()[right].position, from);
                });
  return passTo(setPiece, from, state.players()[near].position, idOf(near), false, config, physics,
                passing);
}

namespace {

// Whether the taker may play: the setup time is up, he stands at his place
// behind the ball, and everyone else with a place at his -- or the wait is up.
[[nodiscard]] bool isReady(const MatchStepContext& context, const MatchState& current,
                           const SetPiece& setPiece,
                           const std::vector<std::optional<Vec2>>& targets,
                           const SetPieceRules& rules) {
  const auto taker = findPlayerIndex(current, setPiece.taker);
  const double elapsed = context.time().secondsSince(setPiece.awarded);
  const double setup = setupSeconds(setPiece.kind, rules.setPieces);
  if (!taker || elapsed < setup ||
      SimCore::distance(current.players()[*taker].position,
                        takerSpot(current, setPiece, rules.physics)) >
          rules.setPieces.readyRadius) {
    return false;
  }
  for (std::size_t index = 0; index < targets.size(); ++index) {
    const auto& target = targets[index];
    if (target && index != taker &&
        SimCore::distance(current.players()[index].position, *target) >
            rules.setPieces.placeRadius) {
      return elapsed >= setup + rules.setPieces.waitSeconds;
    }
  }
  return true;
}

// A penalty taker's shot: the shot candidates from the spot, chosen by their
// utility as any shot decision is (docs/set-pieces.md#penalties).
[[nodiscard]] std::optional<ShotIntent> penaltyShot(const MatchStepContext& context,
                                                    const MatchState& current,
                                                    const std::size_t taker,
                                                    const SetPieceRules& rules) {
  const auto shots = generateShotCandidates(current, taker, context.time(), rules.shots);
  std::vector<double> utilities;
  std::vector<std::size_t> valid;
  for (std::size_t index = 0; index < shots.size(); ++index) {
    if (shots[index].isValid()) {
      utilities.push_back(shots[index].utility);
      valid.push_back(index);
    }
  }
  const auto chosen = chooseByUtility(utilities, rules.temperature,
                                      context.random(SimCore::RandomNumberGeneratorDomain::kAi));
  if (!chosen) {
    return std::nullopt;
  }
  const ShotCandidate& shot = shots[valid[*chosen]];
  return ShotIntent{.shooter = current.players()[taker].playerId,
                    .target = shot.target,
                    .height = shot.height,
                    .speed = shot.speed,
                    .type = shot.type,
                    .curvature = shot.curvature};
}

// The taker plays the planned delivery -- a penalty taker his shot, a free
// kick taker whatever he decides --: the ball at his feet, the pass or shot
// pending, the set piece over.
void take(const MatchStepContext& context, const MatchState& current, MatchStateWriter& next,
          const SetPiece& setPiece, const std::size_t taker, const SetPieceRules& rules) {
  const auto delivery =
      plannedDelivery(current, setPiece, rules.setPieces, rules.physics, rules.passing);
  const auto shot = setPiece.kind == SetPieceKind::kPenalty
                        ? penaltyShot(context, current, taker, rules)
                        : std::nullopt;
  // A kickoff's taker has had the ball since the line-up.
  if (next.ball().owner != setPiece.taker) {
    context.record(PossessionChanged{
        .tick = context.tick(), .previousOwner = next.ball().owner, .newOwner = setPiece.taker});
  }
  next.setBallOwner(setPiece.taker);
  next.setBallHeld(false);
  next.setBallTouch(std::nullopt);
  next.setBallLastTouch(BallTouch{.playerId = setPiece.taker, .tick = context.tick()});
  next.setBallPosition(setPiece.spot);
  next.setBallVelocity({});
  next.setBallHeight(0.0);
  next.setBallVerticalVelocity(0.0);
  next.setBallSpin(0.0);
  next.setBallSideSpin(0.0);
  Vec2 target = setPiece.spot;
  if (delivery) {
    next.setPendingAction(taker, *delivery);
    target = delivery->target;
  } else if (shot) {
    next.setPendingAction(taker, *shot);
    target = shot->target;
  }
  context.record(
      SetPieceTaken{.tick = context.tick(),
                    .kind = setPiece.kind,
                    .side = setPiece.side,
                    .taker = setPiece.taker,
                    .delivery = plansOf(current, setPiece.side).of(setPiece.kind).delivery,
                    .target = target,
                    .receiver = delivery ? delivery->receiver : std::nullopt});
  // No offside from a throw-in, a corner or a goal kick.
  if (setPiece.kind == SetPieceKind::kThrowIn || setPiece.kind == SetPieceKind::kCorner ||
      setPiece.kind == SetPieceKind::kGoalKick) {
    next.setOffsideExemption(BallTouch{.playerId = setPiece.taker, .tick = context.tick()});
  }
  // An indirect free kick scores only off another player (Law 13).
  if (setPiece.indirect) {
    next.setIndirectKick(BallTouch{.playerId = setPiece.taker, .tick = context.tick()});
  }
  next.setSetPiece(std::nullopt);
}

}  // namespace

MatchSystem makeSetPieceSystem(const SetPieceRules& rules) {
  validate(rules.setPieces);
  validate(rules.physics);
  validate(rules.passing);
  return {.name = std::string(kSetPieceSystemName),
          .update =
              [rules](const MatchStepContext& context, const MatchState& current,
                      MatchStateWriter& next) {
                // Another player's touch makes an indirect free kick count.
                if (const auto& kick = current.indirectKick();
                    kick && current.ball().lastTouch &&
                    current.ball().lastTouch->playerId != kick->playerId) {
                  next.setIndirectKick(std::nullopt);
                }
                const auto& setPiece = current.setPiece();
                if (!setPiece) {
                  return;
                }
                const auto targets =
                    setPieceTargets(current, *setPiece, rules.setPieces, rules.physics);
                for (std::size_t index = 0; index < targets.size(); ++index) {
                  if (targets[index] && isOnPitch(current.players()[index])) {
                    next.setPlayerTarget(index, targets[index]);
                  }
                }
                const auto taker = findPlayerIndex(current, setPiece->taker);
                if (!taker || !isReady(context, current, *setPiece, targets, rules)) {
                  return;
                }
                take(context, current, next, *setPiece, *taker, rules);
              },
          .intervalTicks = 1,
          .phaseTicks = 0};
}

}  // namespace ElyverseFootball::SimMatch
