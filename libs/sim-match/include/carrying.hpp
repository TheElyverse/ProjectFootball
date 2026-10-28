#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include "ballPhysics.hpp"
#include "carryCandidate.hpp"
#include "ids.hpp"
#include "matchState.hpp"
#include "passing.hpp"
#include "perception.hpp"
#include "reception.hpp"
#include "simTime.hpp"
#include "tacticalState.hpp"
#include "vec2.hpp"

namespace ElyverseFootball::SimMatch {

// What carry candidate generation needs to know besides the state.
struct CarryCandidateRules {
  CarryScoringConfig scoring;
  BallPhysics ball;
  PassConfig passing;
  PerceptionConfig perception;
  ReceptionConfig reception;
};

// The options of the player at carrierIndex with the ball at his feet,
// scored only from his memory:
//
//   - holding the ball, always valid;
//   - carrying it -- or turning with it, for a direction away from where he
//     faces -- toward the opponent's goal, through the middle of each gap
//     between two opponents within gapRadius, away from the nearest
//     opponent, and toward the cell of the pitch-control grid his team
//     controls best within reach, one candidate per direction
//     (sameDirection); each as far as horizonSeconds take him, no farther
//     than pitchMargin inside the lines and short of where an opponent gets
//     there within safetySeconds of him;
//   - dribbling past each opponent within dribbleRadius in the direction he
//     attacks, on his left and on his right, to no closer than pitchMargin
//     to the lines -- not for a goalkeeper.
//
// Nothing with carrying switched off, and nothing for a goalkeeper with the
// ball in his hands. Ordered by
// orderCarryCandidates(). Deterministic: the same state gives the same list.
[[nodiscard]] std::vector<CarryCandidate> generateCarryCandidates(const MatchState& state,
                                                                  std::size_t carrierIndex,
                                                                  SimCore::StepTime time,
                                                                  const CarryCandidateRules& rules);

// Puts valid candidates first by descending utility, then the rest; ties by
// kind, then subject.
void orderCarryCandidates(std::vector<CarryCandidate>& candidates);

// The run of the player at this index while it is still his: he owns the
// ball, and his last touch of it is the run's. nullptr otherwise.
[[nodiscard]] const CarryRun* activeCarry(const MatchState& state, std::size_t playerIndex);

}  // namespace ElyverseFootball::SimMatch
