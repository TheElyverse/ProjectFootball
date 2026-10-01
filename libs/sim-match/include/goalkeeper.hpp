#pragma once

#include <cstddef>
#include <optional>

#include "matchState.hpp"
#include "random.hpp"
#include "tacticalPhase.hpp"
#include "tacticalState.hpp"
#include "vec2.hpp"

namespace ElyverseFootball::SimMatch {

// How the goalkeeper keeps his goal; see docs/goalkeeper.md. Part of
// MatchConfig and therefore of every replay.
struct GoalkeeperConfig {
  // Against a shot he stands this far off his goal line.
  double lineDepth = 1.5;  // m
  // Far from the ball he stands at this share of his defensive line's depth,
  // at most at the edge of his penalty area.
  double highDepthShare = 0.5;
  // With his own side on the ball he stands at most at this share of that
  // depth: behind his defenders to be played back to, never level with them.
  double possessionDepthShare = 0.5;
  // How far from his ideal place a keeper of keeperPositioning 0 may stand:
  // along the line from his goal to the ball, and across it. The spread
  // shrinks with his skill and is gone for a keeper of 1.
  double positionErrorAlong = 1.5;   // m
  double positionErrorAcross = 0.6;  // m
  // The head start on the first attacker he needs before he comes for a ball
  // the opponent played: at sweeping 0 and at sweeping 1 of his tactic,
  // linear between.
  double cautiousMargin = 0.5;  // s
  double boldMargin = -0.2;     // s
  // Once he comes, he only turns back when his margin falls this far below
  // the threshold: hysteresis against running out and back.
  double sweepHysteresis = 0.1;  // s
  // How far a keeper of keeperAnticipation 0 may misjudge his margin. The
  // spread shrinks with his skill and is gone for a keeper of 1.
  double misjudgement = 0.8;  // s

  friend bool operator==(const GoalkeeperConfig&, const GoalkeeperConfig&) = default;
};

// Throws std::invalid_argument unless the depth, the spreads, the hysteresis
// and the misjudgement are finite and not negative, the shares lie in [0, 1]
// and the margins are finite.
void validate(const GoalkeeperConfig& config);

// Where the goalkeeper at this index wants to stand in the phase: on the
// bisector of the angle the posts of his goal make as seen from the ball,
// this far along it from where it meets the goal line. Far from the ball he
// stands at highDepthShare of his defensive line's depth (capped at his
// penalty area), against a shot at lineDepth, and between the two by the
// ball's distance from his goal relative to his defensive line's. With his
// own side on the ball that high depth shrinks by possessionDepthShare; an
// opponent on the ball within shotRange of his goal puts him at lineDepth. He
// never stands beyond the ball, off the pitch, or wider than his posts by
// more than he stands off his goal line. No
// trigonometry. Throws std::invalid_argument for a player of a scripted side.
[[nodiscard]] SimCore::Vec2 goalkeeperTarget(const MatchState& state, std::size_t playerIndex,
                                             SimTactics::TacticalPhase phase,
                                             const GoalkeeperConfig& config, double shotRange);

// The goalkeeper's desired region: goalkeeperTarget() as its tactical target
// and, as its centre, where he takes up his place -- the target off by a
// triangular draw along the line from his goal to the ball and one across
// it, each spread by its positionError times (1 - keeperPositioning). Two
// draws from `random` per draw, four in all, whatever his skill.
[[nodiscard]] DesiredRegion goalkeeperRegion(const MatchState& state, std::size_t playerIndex,
                                             SimTactics::TacticalPhase phase,
                                             const GoalkeeperConfig& config, double shotRange,
                                             SimCore::RandomNumberGenerator& random);

// The head start the keeper needs to come for a ball: cautiousMargin at
// sweeping 0 to boldMargin at sweeping 1.
[[nodiscard]] double sweepThreshold(double sweeping, const GoalkeeperConfig& config) noexcept;

// How far a keeper of this anticipation misjudges his margin to one ball: a
// triangular draw spread by misjudgement times (1 - anticipation); positive
// is too bold. Two draws from `random`.
[[nodiscard]] double drawMisjudgement(double anticipation, const GoalkeeperConfig& config,
                                      SimCore::RandomNumberGenerator& random) noexcept;

// What the keeper makes of a ball he could come for.
struct SweepCall {
  // The seconds he and the first attacker need to reach it; no attacker if
  // none of them can.
  double keeperSeconds = 0.0;
  std::optional<double> attackerSeconds;
  double misjudgement = 0.0;
  // The head start he wants, already lowered by the hysteresis if he is
  // coming.
  double threshold = 0.0;
  bool coming = false;

  friend bool operator==(const SweepCall&, const SweepCall&) = default;
};

// Decides whether the keeper comes: he does if the attacker's seconds minus
// his own, plus his misjudgement, reach the threshold for his sweeping dial,
// lowered by sweepHysteresis while he is already coming. With no attacker
// able to reach the ball he always comes.
[[nodiscard]] SweepCall callSweep(double keeperSeconds, std::optional<double> attackerSeconds,
                                  double misjudgement, double sweeping, bool alreadyComing,
                                  const GoalkeeperConfig& config) noexcept;

}  // namespace ElyverseFootball::SimMatch
