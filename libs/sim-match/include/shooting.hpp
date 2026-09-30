#pragma once

#include <cstddef>

#include "ballPhysics.hpp"
#include "matchState.hpp"
#include "passing.hpp"
#include "random.hpp"
#include "simTime.hpp"
#include "vec2.hpp"

namespace ElyverseFootball::SimMatch {

// How players strike shots and what a body in the way does to one; see
// docs/shooting.md. Part of MatchConfig and therefore of every replay.
struct ShotConfig {
  // How far from his aim a shot may go at the goal line, to either side and up
  // or down: the half-width of a triangular spread, at zero distance and per
  // meter to his target, for a player of average accuracy and technique --
  // what the shot decision assumes of him (ShotScoringConfig).
  double spreadAtZero = 0.2;     // m
  double spreadPerMeter = 0.04;  // m per m
  // The strike is up to this fraction harder or softer than intended, for a
  // player of average technique -- and never by more than half, however badly
  // he strikes it.
  double speedError = 0.05;
  // A ball leaves the foot rising no steeper than this: its vertical speed
  // over its speed along the ground. 1 is 45 degrees.
  double maxLaunchSlope = 1.0;
  // The topspin of a clean strike by a player of perfect technique, and how
  // far from his a strike's spin may be for a player of average technique.
  double topspin = 30.0;    // rad/s
  double spinError = 15.0;  // rad/s
  // What widens every error, each by this factor at its worst: 1 doubles it.
  // Pressure is that of the passes (passPressure()); balance how much of his
  // top speed the shooter moves at across or away from his shot; the weak
  // foot a foot without any accuracy; unsettled a ball he has only just
  // received at unsettledBallSpeed.
  double pressureErrorFactor = 1.0;
  double balanceErrorFactor = 1.0;
  double weakFootErrorFactor = 1.0;
  double unsettledErrorFactor = 1.0;
  // A target this far to the side of his weaker foot is struck with it: the
  // cross product of his facing and the direction of the shot, 0.3 being
  // about 17 degrees.
  double weakFootSide = 0.3;
  // A received ball takes this long to settle, the faster it came the more.
  double unsettledSeconds = 1.0;
  double unsettledBallSpeed = 20.0;  // m/s
  // A shot at least this fast is not taken at the feet by an outfield player:
  // it comes off a body within blockRadius of its way, where it is no higher
  // than blockReach.
  double deflectionSpeed = 10.0;  // m/s
  double blockRadius = 0.5;       // m
  double blockReach = 1.8;        // m
  // A deflected ball keeps a share of its speed between these two, turns up
  // to deflectionSpread off its line -- meters to the side per meter along
  // it -- and is lifted by up to deflectionLift. A ball that keeps less than
  // blockedBelow of its speed was blocked.
  double minDeflectedSpeed = 0.1;
  double maxDeflectedSpeed = 0.9;
  double blockedBelow = 0.5;
  double deflectionSpread = 1.0;
  double deflectionLift = 3.0;  // m/s

  friend bool operator==(const ShotConfig&, const ShotConfig&) = default;
};

// Throws std::invalid_argument unless every value is finite and not negative,
// the launch slope, the unsettled time and speed and the block radius are
// positive, and the deflected speeds and the blocked share lie in [0, 1] with
// the lowest speed not above the highest.
void validate(const ShotConfig& config);

// How much wider than an average player's an error is for a skill in [0, 1]:
// a tenth as wide at 1, exactly as wide at 0.5, almost twice at 0.
[[nodiscard]] double skillErrorFactor(double skill) noexcept;

// What the shooter strikes the ball under, each in [0, 1] but the foot.
struct ShotConditions {
  double pressure = 0.0;
  double imbalance = 0.0;
  bool weakFoot = false;
  double unsettled = 0.0;

  friend bool operator==(const ShotConditions&, const ShotConditions&) = default;
};

// The conditions of a shot by the player at this index, struck from `from`
// at the intent's target in the step of tick now. Physical, so from true
// positions: the pressure of the nearest opponent, the shooter's own motion,
// which foot the target asks for -- left is the side of growing pitch y for
// a player facing growing pitch x -- and the ball he last received.
[[nodiscard]] ShotConditions shotConditions(const MatchState& state, std::size_t shooterIndex,
                                            SimCore::Vec2 from, const ShotIntent& intent,
                                            SimCore::SimTick now, double secondsPerTick,
                                            const ShotConfig& config, const PassConfig& passing);

// How much the conditions widen a player's errors: 1 in the clear, standing
// or running at his shot, on his strong foot, with a settled ball.
[[nodiscard]] double shotErrorFactor(const ShotConditions& conditions,
                                     const PlayerAttributes& attributes,
                                     const ShotConfig& config) noexcept;

// The ball as a strike sends it off, and the point on the goal line and the
// height it is really heading for: the aim, off by the shooter's error.
struct ShotStrike {
  SimCore::Vec2 velocity;
  double verticalVelocity = 0.0;
  double spin = 0.0;
  SimCore::Vec2 target;
  double height = 0.0;
};

// The how-well half of a shot; the what is the ShotIntent. The ball leaves
// the grass at `from` on the flight that brings it to the aimed height at
// the aimed point -- no steeper than maxLaunchSlope, the nearest it gets
// where that is not enough -- with the shooter's errors, always six draws
// from random: shotAccuracy scatters the point it heads for, across and up,
// shotTechnique its lift, its pace and its spin, and errorFactor
// (shotErrorFactor()) widens them all. A shot aimed at the grass or below it
// is struck along it and rolls. A target on the ball itself is struck along
// the shooter's facing.
[[nodiscard]] ShotStrike executeShot(const ShotIntent& intent, SimCore::Vec2 from,
                                     const PlayerMatchState& shooter, double errorFactor,
                                     const ShotConfig& config, const BallPhysics& physics,
                                     SimCore::RandomNumberGenerator& random) noexcept;

// A shot coming off a body: the ball where it is, leaving with a share of its
// speed, off its line and lifted, without spin, and whether that blocked it.
// Always three draws from random.
struct Deflection {
  BallState ball;
  bool blocked = false;
};

[[nodiscard]] Deflection deflectShot(const BallState& ball, const ShotConfig& config,
                                     SimCore::RandomNumberGenerator& random) noexcept;

}  // namespace ElyverseFootball::SimMatch
