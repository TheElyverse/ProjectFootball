#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include "ids.hpp"
#include "tacticalPhase.hpp"
#include "tacticalState.hpp"
#include "vec2.hpp"

namespace ElyverseFootball::SimMatch {

// How much one phase of play shifts the weights of a carry
// (docs/on-ball.md#ball-carrying): each weight is multiplied by its factor.
struct CarryPhaseFactors {
  double progression = 1.0;
  double space = 1.0;
  double shot = 1.0;
  double bypassed = 1.0;
  double risk = 1.0;

  friend bool operator==(const CarryPhaseFactors&, const CarryPhaseFactors&) = default;
};

// How a player on the ball weighs holding, turning, carrying and dribbling; see
// docs/on-ball.md#ball-carrying. Part of DecisionConfig and therefore of every
// replay.
struct CarryScoringConfig {
  // Whether players carry the ball at all: without, a player on the ball only
  // passes, shoots or keeps it, as before carrying existed. Scenarios that
  // isolate a pass or a shot switch it off
  // (docs/scenarios.md#scenario-catalogue).
  bool enabled = true;
  // Teammates and opponents remembered with less confidence are ignored.
  double minConfidence = 0.3;
  // The longest carry he plans, in seconds of running with the ball.
  double horizonSeconds = 2.0;
  // A shorter carry is no carry.
  double minCarryDistance = 2.0;  // m
  // Opponents this close shape the gaps he carries the ball through.
  double gapRadius = 20.0;  // m
  // Two directions whose unit vectors have at least this dot product are one.
  double sameDirection = 0.98;
  // A carry or dribble ends this far inside the touchlines and goal lines.
  double pitchMargin = 1.0;  // m
  // A carry ends where an opponent would get there less than this after him.
  double safetySeconds = 0.3;
  // An opponent's margin is the time he has to spare reaching the touched
  // ball, negative if he gets there first. The risk from him falls smoothly
  // from 1 at minus this many seconds through 1/2 at zero to 0 at plus this.
  double lossMarginSeconds = 0.4;
  // A touch is planned for him to meet the ball this long after it, at his
  // running speed with the ball, and plays it this far ahead at least and at
  // most.
  double touchSeconds = 0.5;
  double minTouchDistance = 1.0;  // m
  double maxTouchDistance = 5.0;  // m
  // He plays the ball again this share of the planned time after a touch at
  // the earliest.
  double touchShare = 0.5;
  // While the ball rolls ahead of him he looks around, and back at it once
  // he is this close to meeting it.
  double lookBackSeconds = 0.3;
  // A turn takes the ball this far at most.
  double turnDistance = 3.0;  // m
  // He dribbles past an opponent at most this far from him, standing in the
  // direction he attacks: the dot product of the unit vector toward him and
  // the attacking direction at least dribbleCone. The ball passes him this
  // far to his side and comes to rest dribbleBeyond behind him.
  double dribbleRadius = 6.0;  // m
  double dribbleCone = 0.5;
  double dribbleClearance = 2.5;  // m
  double dribbleBeyond = 1.5;     // m
  // A touch errs as a pass does, scaled by this factor: at the lowest ball
  // control and at the highest, linear in between; a dribble's again by the
  // dribble factor, at the lowest dribbling and at the highest.
  double maxTouchError = 3.0;
  double minTouchError = 0.5;
  double maxDribbleError = 1.5;
  double minDribbleError = 0.5;
  // A dribble's planned time to the ball, as a share of a carry's: at the
  // lowest dribbling and at the highest.
  double slowDribbleShare = 1.2;
  double quickDribbleShare = 0.8;
  // A shot from farther than this is worth nothing; one from where the goal
  // looks this wide (goalOpening()) or wider is worth its whole weight.
  double shotDistance = 25.0;  // m
  double fullOpening = 0.6;
  // Candidates less likely to keep the ball are not offered to the decision.
  double minKeepChance = 0.35;
  // An opponent this close to him puts no pressure on him holding the ball
  // at the edge and full pressure at zero distance.
  double pressureRadius = 6.0;  // m
  // Utility = keep·w_k + progression·w_p + space·w_s + shot·w_sh +
  // bypassed·w_b − risk·w_r, less turnCost for every second a turn takes;
  // for holding the ball holdWeight · (1 − pressure).
  double keepWeight = 0.8;
  double progressionWeight = 0.8;
  double spaceWeight = 0.2;
  double shotWeight = 0.6;
  double bypassWeight = 0.15;
  double riskWeight = 0.3;
  double holdWeight = 0.5;
  double turnCost = 0.6;
  // A turn takes this long standing, and as long again as stopping his run
  // along where he faces takes him at his acceleration.
  double turnSeconds = 0.5;
  // The phase factors of a side with a tactic, by the phase its team is in
  // with the ball; a side without one, or out of possession, scores with
  // factors of 1.
  CarryPhaseFactors buildUp{.risk = 1.5};
  CarryPhaseFactors progression{};
  CarryPhaseFactors finalThird{.shot = 2.0};
  CarryPhaseFactors attackingTransition{.progression = 2.0, .bypassed = 2.0, .risk = 0.8};

  friend bool operator==(const CarryScoringConfig&, const CarryScoringConfig&) = default;
};

// Throws std::invalid_argument unless the confidence, keep chance, cone,
// same-direction and touch share lie in [0, 1], every distance, time and
// error factor is finite and positive -- the clearance, margin, safety,
// look-back and turn seconds may be 0 -- the touch distances are ordered, and
// every weight, cost and factor is finite, not negative and at most
// kMaxScoringWeight (passCandidates.hpp).
void validate(const CarryScoringConfig& config);

// The scoring with the weights of this phase's factors; the scoring itself
// for a phase out of possession.
[[nodiscard]] CarryScoringConfig carryScoringForPhase(const CarryScoringConfig& scoring,
                                                      SimTactics::TacticalPhase phase) noexcept;

// The scoring adjusted to how much risk a carrier's tactic accepts,
// passingRisk in [0, 1], as for passes (scoringForRisk()): progression
// counts (0.5 + risk) times as much, the risk of losing the ball (1.5 -
// risk) times, and the keep chance a carry needs to be offered (1.3 - 0.6
// risk) times, at most 1.
[[nodiscard]] CarryScoringConfig carryScoringForRisk(const CarryScoringConfig& scoring,
                                                     double passingRisk) noexcept;

// Why a candidate cannot be chosen; kValid if it can.
enum class CarryRejection : std::uint8_t {
  kValid,
  kTooShort,
  kUnlikely,
};

[[nodiscard]] std::string_view carryRejectionName(CarryRejection rejection) noexcept;

// One option of a player on the ball besides passing and shooting, as he
// sees it, with every score so a decision can be explained.
struct CarryCandidate {
  CarryKind kind = CarryKind::kHold;
  // Where the carry takes him, his own position for holding the ball.
  SimCore::Vec2 target{};
  double distance = 0.0;
  // His first touch: where it sends the ball, how fast, and when he plays
  // it again at the earliest. Nothing for holding the ball.
  SimCore::Vec2 touchTarget{};
  double touchSpeed = 0.0;
  double touchSeconds = 0.0;
  // The touch's error as a factor on a pass's.
  double errorFactor = 1.0;
  // The opponent he dribbles past.
  std::optional<SimCore::PlayerId> subject = std::nullopt;
  // The chance that no remembered opponent gets to the touched ball first,
  // and its complement; for holding the ball 1 − pressure and pressure.
  double keep = 1.0;
  double risk = 0.0;
  // How much nearer the centre of the goal he attacks the target lies than
  // the ball, over the pitch length, in [-1, 1]: ground gained toward the
  // goal, not toward its line, so a carry into the corner gains nothing.
  double progression = 0.0;
  // How much more of the target than of the ball's spot his team controls,
  // in [-1, 1]; 0 without a pitch-control grid.
  double space = 0.0;
  // How much better a shot from the target is than one from the ball, in
  // [0, 1].
  double shot = 0.0;
  // Remembered opponents between him and the goal he leaves behind.
  int bypassed = 0;
  // How long a turn takes him, 0 for any other candidate.
  double turnSeconds = 0.0;
  double utility = 0.0;
  CarryRejection rejection = CarryRejection::kValid;

  [[nodiscard]] bool isValid() const noexcept { return rejection == CarryRejection::kValid; }

  friend bool operator==(const CarryCandidate&, const CarryCandidate&) = default;
};

// The weighted parts of a candidate's utility: keep, progression, space,
// shot and bypassed add, risk and a turn's cost subtract; holding the ball
// has its hold part only. Their sum is the utility, bit for bit.
struct CarryContributions {
  double keep = 0.0;
  double progression = 0.0;
  double space = 0.0;
  double shot = 0.0;
  double bypassed = 0.0;
  double risk = 0.0;
  double turn = 0.0;
  double hold = 0.0;

  [[nodiscard]] double total() const noexcept {
    return keep + progression + space + shot + bypassed + risk + turn + hold;
  }

  friend bool operator==(const CarryContributions&, const CarryContributions&) = default;
};

[[nodiscard]] CarryContributions carryContributions(const CarryCandidate& candidate,
                                                    const CarryScoringConfig& scoring) noexcept;

// The part that contributed most to a utility, by absolute value: "keep",
// "progression", "space", "shot", "bypassed", "risk", "turn" or "hold". Ties
// go to the one listed first.
[[nodiscard]] std::string_view dominantContribution(const CarryContributions& parts) noexcept;

}  // namespace ElyverseFootball::SimMatch
