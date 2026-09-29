#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "vec2.hpp"

namespace ElyverseFootball::SimMatch {

// How a player on the ball weighs his shooting options; see
// docs/shot-decisions.md.
struct ShotScoringConfig {
  // Opponents remembered with less confidence are ignored.
  double minConfidence = 0.3;
  // Shots from farther from the goal's center are not considered.
  double maxShotDistance = 25.0;  // m
  // Shots at a goal that looks narrower than this are not considered: the
  // distance between the directions to the two posts as unit vectors,
  // 2·sin(angle / 2), about the angle in radians for a narrow one.
  double minOpening = 0.1;
  // The aiming grid over the goal: columns across, rows up.
  int zoneColumns = 7;
  int zoneRows = 5;
  // How fast a shot leaves the foot.
  double shotSpeed = 25.0;  // m/s
  // How far to either side the keeper reaches in a dive, at the height of
  // his feet, and how high he reaches straight up. His reach is the ellipse
  // with these half-axes.
  double keeperDiveReach = 2.0;  // m
  double keeperJumpReach = 2.6;  // m
  // How long the keeper takes to react and dive to the edge of his reach; a
  // ball closer to him takes proportionally less, one out of reach the run
  // until it is in reach and then the full dive.
  double keeperDiveSeconds = 0.5;
  // An outfield player blocks a shot below this height only.
  double blockReach = 1.8;  // m
  // A player's time to spare reaching the shot, negative if he gets there
  // first. The chance he gets to it falls smoothly from 1 at minus this many
  // seconds through 1/2 at zero to 0 at plus this: narrow for a blocker, who
  // only has to stand in the way, wide for the keeper, whose save is never
  // certain.
  double blockMarginSeconds = 0.15;
  double saveMarginSeconds = 0.4;
  // How far from his aim a shot may go, in meters to either side and up or
  // down: the half-width of a triangular spread, at zero distance and per
  // meter from the goal, for a player of average accuracy.
  double spreadAtZero = 0.2;     // m
  double spreadPerMeter = 0.04;  // m per m
  // An opponent this close to the shooter puts no pressure on him at the
  // edge and full pressure at zero distance; full pressure widens the spread
  // by this share.
  double pressureRadius = 3.0;  // m
  double pressureSpread = 1.0;
  // A shot is aimed at least this far inside a post or the crossbar, however
  // accurate the shooter.
  double frameMargin = 0.1;  // m
  // The share of blocked and saved shots his team gets the ball back from: a
  // rebound or a corner.
  double reboundShare = 0.3;
  // Shots more likely to be blocked, or less likely to go in, are not
  // offered to the decision.
  double maxBlockRisk = 0.7;
  double minGoalChance = 0.05;
  // Utility = goal·w_g + secondBall·w_s − loss·w_k, where loss is the chance
  // the team loses the ball: neither a goal nor a second ball. Keeping the
  // ball is worth 0, so a shot is only offered at a positive utility; w_k
  // matches a completed pass's worth (PassScoringConfig::completionWeight).
  double goalWeight = 2.5;
  double secondBallWeight = 0.5;
  double lossWeight = 1.0;

  friend bool operator==(const ShotScoringConfig&, const ShotScoringConfig&) = default;
};

// Why a shot cannot be taken; kValid if it can.
enum class ShotRejection : std::uint8_t {
  kValid,
  kTooFar,
  // He cannot see the goal's center: he is facing away from it.
  kGoalUnseen,
  kTooNarrow,
  kBlocked,
  kUnlikely,
  // Worth less than keeping the ball: a utility of 0 or less.
  kNotWorthIt,
};

[[nodiscard]] std::string_view shotRejectionName(ShotRejection rejection) noexcept;

// One shooting option as the shooter sees it: a zone of the goal, the point
// he aims at in it, and every score component, so that a decision can be
// explained. The probabilities follow the shot's spread over the goal's
// zones: where it may end up, and what becomes of it there.
struct ShotCandidate {
  // The zone of the aiming grid, counted from the post at the lower pitch y
  // and from the ground.
  std::size_t column = 0;
  std::size_t row = 0;
  // Where he aims: a point on the goal line, and a height above the ground.
  SimCore::Vec2 target;
  double height = 0.0;
  // From the ball to the goal's center.
  double distance = 0.0;
  // How wide the goal looks from the ball; see ShotScoringConfig::minOpening.
  double opening = 0.0;
  // The half-width of his spread around the aim, in meters.
  double spread = 0.0;
  // Chance the shot stays inside the frame, in [0, 1].
  double onTarget = 0.0;
  // Chance an outfield opponent blocks it, in [0, 1].
  double blockRisk = 0.0;
  // Chance the keeper saves it, in [0, 1].
  double saveRisk = 0.0;
  // Chance it goes in, in [0, 1].
  double goalChance = 0.0;
  // Chance his team gets the ball back from a block or a save, in [0, 1].
  double secondBallChance = 0.0;
  double utility = 0.0;
  ShotRejection rejection = ShotRejection::kValid;

  [[nodiscard]] bool isValid() const noexcept { return rejection == ShotRejection::kValid; }

  friend bool operator==(const ShotCandidate&, const ShotCandidate&) = default;
};

}  // namespace ElyverseFootball::SimMatch
