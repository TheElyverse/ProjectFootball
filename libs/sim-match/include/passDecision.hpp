#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "carrying.hpp"
#include "crossCandidates.hpp"
#include "distribution.hpp"
#include "matchSimulation.hpp"
#include "passCandidates.hpp"
#include "random.hpp"
#include "shotCandidates.hpp"

namespace ElyverseFootball::SimMatch {

// How players on the ball decide; see docs/on-ball.md#pass-decisions. Part of
// MatchConfig and therefore of every replay.
struct DecisionConfig {
  // 6 ticks at 30 Hz: a player on the ball decides five times a second.
  int intervalTicks = 6;
  // A player keeps a ball he has just taken at least this long.
  double minHoldSeconds = 0.5;
  // Softmax temperature over utility for an average decider: lower picks the
  // best option more often, higher spreads the choice.
  double temperature = 0.15;
  // How a player's decisionMaking d shapes his choice
  // (docs/on-ball.md#decision-making): his temperature is temperature times
  // 1 + skillTemperatureSpread · (d0 - d) / d0, for the default d0, and below
  // d0 he misjudges every estimate by a triangular error of half-width
  // maxMisjudgement · (1 - d / d0).
  double skillTemperatureSpread = 0.75;
  double maxMisjudgement = 0.3;
  PassScoringConfig scoring;
  ShotScoringConfig shooting;
  // How the goalkeeper distributes the ball (docs/goalkeeper.md#distribution).
  DistributionConfig distribution;
  // A carrier without a valid option looks around for one this long, then
  // plays the best pass he rejected (docs/on-ball.md#pass-decisions).
  double stuckSeconds = 3.0;
  // Without any pass to play, he kicks the ball long this far inside a
  // touchline, and no closer than clearGoalLineMargin to the opponent's
  // goal line.
  double clearTouchlineMargin = 5.0;  // m
  double clearGoalLineMargin = 10.0;  // m
  // How he weighs holding, turning, carrying and dribbling
  // (docs/on-ball.md#ball-carrying).
  CarryScoringConfig carrying;
  // How a wide player weighs his crosses (docs/on-ball.md#crossing).
  CrossScoringConfig crossing;

  friend bool operator==(const DecisionConfig&, const DecisionConfig&) = default;
};

// What the player on the ball chose: to pass, to shoot, to hold, turn, carry,
// dribble or cross -- kPassed, kShot, kHeld, kTurned, kCarried, kDribbled or
// kCrossed -- and
// which, by its index in the list it comes from.
struct OnBallChoice {
  DecisionOutcome outcome = DecisionOutcome::kPassed;
  std::size_t index = 0;

  friend bool operator==(const OnBallChoice&, const OnBallChoice&) = default;
};

// Picks one of the valid passes at the front of a list ordered like
// generatePassCandidates()'s or the first shot of a list ordered like
// generateShotCandidates()'s, if it is valid -- the best shot competes, not
// every zone of the goal --, with probability proportional to
// exp(utility / temperature): the seeded softmax policy. Draws exactly one
// number from random if there is a valid option and none otherwise; empty
// without a valid option. The exponential is stableExp(), so the choice is
// the same on every platform.
[[nodiscard]] std::optional<OnBallChoice> chooseOnBall(std::span<const PassCandidate> passes,
                                                       std::span<const ShotCandidate> shots,
                                                       double temperature,
                                                       SimCore::RandomNumberGenerator& random);

// chooseOnBall() with his options with the ball at his feet competing too:
// of the valid ones at the front of a list ordered like
// generateCarryCandidates()'s, the first of each kind -- the best hold, turn,
// carry and dribble compete, not every direction.
[[nodiscard]] std::optional<OnBallChoice> chooseOnBall(std::span<const PassCandidate> passes,
                                                       std::span<const ShotCandidate> shots,
                                                       std::span<const CarryCandidate> carries,
                                                       double temperature,
                                                       SimCore::RandomNumberGenerator& random);

// chooseOnBall() with his crosses competing too: the first of a list ordered
// like generateCrossCandidates()'s, if it is valid (docs/on-ball.md#crossing).
// kCrossed indexes it.
[[nodiscard]] std::optional<OnBallChoice> chooseOnBall(std::span<const PassCandidate> passes,
                                                       std::span<const ShotCandidate> shots,
                                                       std::span<const CarryCandidate> carries,
                                                       std::span<const CrossCandidate> crosses,
                                                       double temperature,
                                                       SimCore::RandomNumberGenerator& random);

// The softmax temperature of a player of this decisionMaking
// (docs/on-ball.md#decision-making): the configured temperature for the
// default, lower for a better decider, higher for a worse one.
[[nodiscard]] double choiceTemperature(const DecisionConfig& config,
                                       double decisionMaking) noexcept;

// The half-width of a player's misjudgement of his estimates: 0 at or above
// the default decisionMaking, maxMisjudgement for the worst decider.
[[nodiscard]] double misjudgementWidth(const DecisionConfig& config,
                                       double decisionMaking) noexcept;

// Adds a triangular error of half-width `width` to what he estimates -- the
// interception risk of every pass valid or rejected only as kUnlikely, one
// error each to the block, save and goal chances of all shots his position
// allows -- clamps it to [0, 1], scores the candidates again with it
// (judgePassCandidate(), judgeShotCandidate()) and orders them anew
// (docs/on-ball.md#decision-making). One triangular draw from random per such
// pass, in list order, then three for the shots if his position allows one;
// none at all for a width of 0.
void misjudgeOptions(std::vector<PassCandidate>& passes, std::vector<ShotCandidate>& shots,
                     double width, const PassScoringConfig& passScoring,
                     const ShotScoringConfig& shotScoring, SimCore::RandomNumberGenerator& random);

// Scoring adjusted to how much risk a carrier's tactic accepts, passingRisk in
// [0, 1] (docs/on-ball.md#pass-decisions): progression counts (0.5 + risk)
// times as much, interception risk (1.5 - risk) times, and the completion a
// pass needs to be offered (1.3 - 0.6 risk) times, at most 1. At 0.5 -- the
// reference tactic's -- nothing changes.
[[nodiscard]] PassScoringConfig scoringForRisk(const PassScoringConfig& scoring,
                                               double passingRisk) noexcept;

// Shot scoring adjusted to the same passingRisk
// (docs/on-ball.md#shot-decisions): a goal counts (0.5 + risk) times as much,
// losing the ball (1.5 - risk) times, and the goal chance a shot needs to be
// offered (1.3 - 0.6 risk) times, at most 1. At 0.5 nothing changes.
[[nodiscard]] ShotScoringConfig shotScoringForRisk(const ShotScoringConfig& scoring,
                                                   double passingRisk) noexcept;

inline constexpr std::string_view kPassDecisionSystemName = "pass decision";

// Every tick, while the ball rolls ahead of its owner after a touch, he runs
// after it, to where findInterception() (pursuit.hpp) says he meets it at his
// running speed with it, and looks about for his next decision
// (docs/on-ball.md#ball-carrying): at his run's focus until the touch's seconds
// have passed, then at the first of his lookTargets() (carrierScan.hpp), and
// back at the ball once he is lookBackSeconds from meeting it.
//
// With the ball at his feet or in his hands, every config.intervalTicks
// ticks -- and in the tick after he got his touched ball back -- the player
// on the ball decides:
//
//   1. Nothing to decide if the ball is free or its owner already has an
//      action pending.
//   2. He keeps a ball he took less than minHoldSeconds ago, and one he
//      holds in his hands less than the distribution's holdSeconds. His own
//      touches do not make him a new owner: not while his run is active
//      (activeCarry()).
//   3. He lists and scores his options with generatePassCandidates(),
//      generateShotCandidates() and generateCarryCandidates(), from his
//      perception only -- scored for the passing risk of his side's tactic in
//      its current phase (scoringForRisk(), shotScoringForRisk(),
//      carryScoringForRisk()) and his carries for the phase itself
//      (carryScoringForPhase()), if it has one. A goalkeeper adds his long
//      balls (generateLongBallCandidates()), biased by his tactic's
//      directness (directnessBias()), and from his hands throws no farther
//      than the distribution's throwRange (docs/goalkeeper.md#distribution).
//   4. A poor decider misjudges his passes and shots (misjudgeOptions(),
//      drawing from the kCognition random stream). He picks one of the valid
//      passes, the best valid shot and the valid carries with
//      chooseByUtility() at his choiceTemperature(), drawing from the kAi
//      random stream,
//      and writes it as his pending action -- the ball system plays it in
//      the next step. A turn, carry or dribble also starts or continues his
//      run (CarryRun) and sends him toward his touch; holding the ball stops
//      him. Without a valid option he keeps the ball -- unless he is a
//      goalkeeper who has held it in his hands for the distribution's
//      clearSeconds: he then plays the pass with the highest utility of
//      those rejected only as kUnlikely, drawing nothing more.
//   5. Any other carrier without a valid pass or shot who holds the ball
//      scans: each decision he looks at the next of his lookTargets()
//      (carrierScan.hpp) until the next. stuckSeconds after the first
//      decision without an option he plays the pass with the highest utility
//      of all he rejected, and without any a lofted kick to
//      clearanceTarget() at the speed that brings it down there after the
//      distribution's flightSeconds, no farther than the passing's
//      maxLoftedSpeed reaches. Neither draws a number.
//
// Writes the carrier's pending action, movement target, scan (CarrierScan)
// and run (CarryRun) only; a carrier who acts ends his scan. Throws
// std::invalid_argument for an interval below one tick, a negative or
// non-finite hold time, stuck time or margin, a temperature that is not
// positive and finite, a skillTemperatureSpread outside [0, 1) or a
// maxMisjudgement outside [0, 1], or invalid scoring or distribution
// (validate(PassScoringConfig), validate(ShotScoringConfig),
// validate(CarryScoringConfig), validate(DistributionConfig)).
[[nodiscard]] MatchSystem makePassDecisionSystem(const DecisionConfig& config,
                                                 const PassCandidateRules& rules);

}  // namespace ElyverseFootball::SimMatch
