#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include "crossArea.hpp"
#include "ids.hpp"
#include "vec2.hpp"

namespace ElyverseFootball::SimMatch {

// How a wide player on the ball weighs his crosses; see
// docs/on-ball.md#crossing. Part of DecisionConfig and so of every replay.
struct CrossScoringConfig {
  // Whether he crosses at all; scenarios that set up another decision turn
  // it off.
  bool enabled = true;
  // Teammates and opponents remembered with less confidence are ignored.
  double minConfidence = 0.3;
  // He crosses from within this share of the pitch length of the goal line
  // he attacks, and from outside the penalty area's width.
  double maxDepthShare = 0.3;
  // A cross in the air is floated or driven: floated it leaves at crossSpeed
  // along the ground and arrives at headerHeight, driven at drivenSpeed and
  // drivenHeight, below the head. A cut-back rolls at cutBackSpeed.
  double crossSpeed = 16.0;    // m/s
  double headerHeight = 1.6;   // m
  double drivenSpeed = 24.0;   // m/s
  double drivenHeight = 0.9;   // m
  double cutBackSpeed = 12.0;  // m/s
  // The share of his shot's curl (ShotScoringConfig::curlSpin) a cross in
  // the air bends with, toward the goal.
  double curlShare = 0.5;
  // An opponent's margin is how much later than the first teammate he gets
  // to where the cross arrives, each from no earlier than the ball -- along
  // a cut-back's way too; the risk from him falls smoothly from 1 at minus
  // this through 1/2 at 0 to 0 at plus this. The keeper reaches a cross in his
  // penalty area with his hands. A runner who arrives after the ball misses
  // it by the same step: half at this late, certainly at twice.
  double contestMarginSeconds = 0.5;  // s
  // Crosses less likely than this to reach a teammate first are not offered.
  double minWinChance = 0.2;
  // Utility = win·w_w + win·threat·w_t − (1 − win)·w_l.
  double winWeight = 1.0;
  double threatWeight = 1.0;
  double lossWeight = 0.3;

  friend bool operator==(const CrossScoringConfig&, const CrossScoringConfig&) = default;
};

// Why a cross cannot be played; kValid if it can.
enum class CrossRejection : std::uint8_t {
  kValid,
  // No teammate he remembers gets there.
  kNoRunner,
  // No kick of his reaches it.
  kOutOfReach,
  kUnlikely,
};

// "valid", "noRunner", "outOfReach", "unlikely"; "unknown"
// outside the enumerators.
[[nodiscard]] std::string_view crossRejectionName(CrossRejection rejection) noexcept;

// One crossing option as the crosser sees it, with every score component so
// that a decision can be explained.
struct CrossCandidate {
  CrossArea area = CrossArea::kPenaltySpot;
  // Where it arrives, at what height, how fast it leaves along the ground,
  // and the curvature of its path in radians per meter, positive bending
  // left.
  SimCore::Vec2 target;
  double height = 0.0;
  double speed = 0.0;
  double curvature = 0.0;
  // The teammate he believes gets there first, if any.
  std::optional<SimCore::PlayerId> runner = std::nullopt;
  // Seconds until the ball arrives, and until the runner does.
  double ballSeconds = 0.0;
  double runnerSeconds = 0.0;
  // Chance a teammate gets to it first, in [0, 1].
  double winChance = 0.0;
  // Chance the keeper claims it, in [0, 1]; part of what winChance loses.
  double keeperClaim = 0.0;
  // How dangerous a header or a shot from there is, in [0, 1].
  double threat = 0.0;
  double utility = 0.0;
  CrossRejection rejection = CrossRejection::kValid;

  [[nodiscard]] bool isValid() const noexcept { return rejection == CrossRejection::kValid; }

  friend bool operator==(const CrossCandidate&, const CrossCandidate&) = default;
};

}  // namespace ElyverseFootball::SimMatch
