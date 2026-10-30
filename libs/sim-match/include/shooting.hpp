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
// docs/on-ball.md#shooting. Part of MatchConfig and therefore of every replay.
struct ShotConfig {
  // How far from his aim a shot may go at the goal line, to either side and up
  // or down: the half-width of a triangular spread, at zero distance and per
  // meter to his target, for a player of average accuracy and technique in
  // open play -- wider than the shot decision judges it (ShotScoringConfig):
  // a shooter believes himself a little better than he is.
  double spreadAtZero = 0.2;     // m
  double spreadPerMeter = 0.11;  // m per m
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
  // The backspin of a clean chip by a player of perfect technique
  // (docs/on-ball.md#shot-types).
  double chipBackspin = 20.0;  // rad/s
  // What widens every error, each by this factor at its worst: 1 doubles it.
  // Pressure is that of the passes (passPressure()); balance how much of his
  // top speed the shooter moves at across or away from his shot; the weak
  // foot a foot without any accuracy; unsettled a ball he has only just
  // received at unsettledBallSpeed.
  double pressureErrorFactor = 3.5;
  double balanceErrorFactor = 1.0;
  double weakFootErrorFactor = 1.0;
  double unsettledErrorFactor = 1.0;
  // A placed shot -- a penalty, from a still ball and in his own time -- errs
  // this share of what the same strike in open play would: the spread above
  // is open play's, hurried even in the clear. A cross, struck into an area
  // rather than past a keeper, errs crossErrorShare of a shot
  // (docs/on-ball.md#crossing).
  double placedErrorShare = 0.5;
  double crossErrorShare = 0.4;
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
  // An opponent of the shooter on his feet lunges at such a shot that comes
  // within lungeRadius of him no higher than lungeHeight: the duel's lunge,
  // which meets it or misses and leaves him off his feet
  // (docs/duels.md#tackles-and-blocks).
  double lungeRadius = 1.0;  // m
  double lungeHeight = 0.9;  // m
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
// positive, and the deflected speeds, the blocked share and the placed and
// cross error shares lie in [0, 1] with the lowest speed not above the
// highest.
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
  bool placed = false;

  friend bool operator==(const ShotConditions&, const ShotConditions&) = default;
};

// The conditions of a shot by the player at this index, struck from `from`
// at the intent's target in the step at `time`. Physical, so from true
// positions: the pressure of the opponents close to him, the shooter's own motion,
// which foot the target asks for -- left is the side of growing pitch y for
// a player facing growing pitch x -- and the ball he last received.
[[nodiscard]] ShotConditions shotConditions(const MatchState& state, std::size_t shooterIndex,
                                            SimCore::Vec2 from, const ShotIntent& intent,
                                            SimCore::StepTime time, const ShotConfig& config,
                                            const PassConfig& passing);

// How much the conditions widen a player's errors: 1 in the clear, standing
// or running at his shot, on his strong foot, with a settled ball;
// placedErrorShare of that for a placed shot.
[[nodiscard]] double shotErrorFactor(const ShotConditions& conditions,
                                     const PlayerAttributes& attributes,
                                     const ShotConfig& config) noexcept;

// The ball as a strike sends it off, and the point on the goal line and the
// height it is really heading for: the aim, off by the shooter's error.
struct ShotStrike {
  SimCore::Vec2 velocity;
  double verticalVelocity = 0.0;
  double spin = 0.0;
  double sideSpin = 0.0;
  SimCore::Vec2 target;
  double height = 0.0;
};

// The how-well half of a shot; the what is the ShotIntent. The ball leaves
// the grass at `from` on the flight that brings it to the aimed height at
// the aimed point -- along the arc of the intent's curvature for a curled
// shot (aimAlongArc()), no steeper than maxLaunchSlope, the nearest it gets
// where that is not enough -- with the spin its type asks for: topspin for a
// driven shot, the sidespin of its curvature for a curled one, backspin for a
// chip (docs/on-ball.md#shot-types). The shooter's errors are always six
// draws from random: shotAccuracy scatters the point it heads for, across and
// up, shotTechnique its lift, its pace and its spin, and errorFactor
// (shotErrorFactor()) widens them all. A shot aimed at the grass or below it
// is struck along it and rolls. A target on the ball itself is struck along
// the shooter's facing.
// The aim strays along `strayAxis`, a unit vector: the goal line for a shot,
// across the ball's way for a cross (docs/on-ball.md#crossing).
[[nodiscard]] ShotStrike executeShot(const ShotIntent& intent, SimCore::Vec2 from,
                                     const PlayerMatchState& shooter, double errorFactor,
                                     const ShotConfig& config, const BallPhysics& physics,
                                     SimCore::RandomNumberGenerator& random,
                                     SimCore::Vec2 strayAxis = {.x = 0.0, .y = 1.0}) noexcept;

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
