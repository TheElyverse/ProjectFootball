#pragma once

#include <cstdint>
#include <string_view>

#include "ids.hpp"
#include "vec2.hpp"

namespace ElyverseFootball::SimMatch {

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
