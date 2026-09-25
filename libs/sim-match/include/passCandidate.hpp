#pragma once

#include <cstdint>
#include <string_view>

#include "ids.hpp"
#include "vec2.hpp"

namespace ElyverseFootball::SimMatch {

// How a player on the ball weighs his passing options; see
// docs/pass-candidates.md.
struct PassScoringConfig {
  // Teammates and opponents remembered with less confidence are ignored.
  double minConfidence = 0.3;
  double minPassDistance = 2.0;   // m
  double maxPassDistance = 35.0;  // m
  // An opponent's margin is the time he has to spare reaching the pass,
  // negative if he gets there first. The risk from him falls smoothly from
  // 1 at minus this many seconds through 1/2 at zero to 0 at plus this.
  double interceptionMarginSeconds = 0.6;
  // An opponent this close to the receiver puts no pressure on him at the
  // edge and full pressure at zero distance.
  double pressureRadius = 6.0;  // m
  // Candidates less likely to arrive are not offered to the decision.
  double minCompletion = 0.35;
  // Utility = completion·w_c + progression·w_p − pressure·w_r − risk·w_i.
  double completionWeight = 1.0;
  double progressionWeight = 0.8;
  double pressureWeight = 0.3;
  double riskWeight = 0.3;

  friend bool operator==(const PassScoringConfig&, const PassScoringConfig&) = default;
};

// Why a candidate cannot be played; kValid if it can.
enum class PassRejection : std::uint8_t {
  kValid,
  kTooClose,
  kTooFar,
  kOutOfReach,
  kUnlikely,
};

[[nodiscard]] std::string_view passRejectionName(PassRejection rejection) noexcept;

// One passing option as the carrier sees it, with every score component so
// that a decision can be explained.
struct PassCandidate {
  SimCore::PlayerId receiver;
  // Where the carrier believes the receiver is: estimatePosition() of his
  // observation.
  SimCore::Vec2 target;
  double distance = 0.0;
  // planPassSpeed() for the distance.
  double speed = 0.0;
  // How sure the carrier is of the receiver's position: his observation's
  // confidence.
  double receiverConfidence = 0.0;
  // Chance that some remembered opponent reaches the pass first, in [0, 1].
  double interceptionRisk = 0.0;
  // (1 - interceptionRisk) · receiverConfidence, in [0, 1].
  double completion = 0.0;
  // Meters gained toward the opponent's goal line over the pitch length, in
  // [-1, 1].
  double progression = 0.0;
  // How close the nearest remembered opponent is to the receiver, in [0, 1].
  double receiverPressure = 0.0;
  double utility = 0.0;
  PassRejection rejection = PassRejection::kValid;

  [[nodiscard]] bool isValid() const noexcept { return rejection == PassRejection::kValid; }

  friend bool operator==(const PassCandidate&, const PassCandidate&) = default;
};

}  // namespace ElyverseFootball::SimMatch
