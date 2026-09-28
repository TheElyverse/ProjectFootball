#pragma once

#include <optional>

#include "matchState.hpp"
#include "pitch.hpp"
#include "vec2.hpp"

namespace ElyverseFootball::SimMatch {

// Rolling resistance of a ball on grass used when a caller does not state
// one. A ground pass leaving the foot at 10 m/s rolls 10² / (2 · 1.5) ≈ 33 m.
inline constexpr double kDefaultRollingDeceleration = 1.5;  // m/s²

// How far ahead of his feet a player carries a controlled ball.
inline constexpr double kDefaultCarryDistance = 0.5;  // m

// Standard gravity. The simulation is otherwise flat, so this is the only
// place it appears.
inline constexpr double kDefaultGravity = 9.81;  // m/s²

// Air resistance as a linear drag: the ball loses this fraction of its speed
// per second. A real football feels a drag that grows with the square of its
// speed, which no closed form solves; a linear drag does, and that is what
// keeps a flight tick-rate independent and bit-identical on every platform
// (docs/ball-movement.md). The default puts the terminal speed of a falling
// ball at gravity / airDrag ≈ 30 m/s, which is about right.
inline constexpr double kDefaultAirDrag = 0.33;  // 1/s

// The share of its vertical speed a ball keeps when it bounces on grass.
inline constexpr double kDefaultBounceRestitution = 0.6;

// The share of its speed along the ground a ball keeps when it bounces: the
// grass takes the rest.
inline constexpr double kDefaultBounceGrip = 0.8;

// The share of its spin a ball spends when it bounces, driving it on or
// checking it.
inline constexpr double kDefaultSpinTransfer = 0.2;

// How fast spin dies away in flight: this fraction per second.
inline constexpr double kDefaultSpinDecay = 0.4;  // 1/s

// A ball that leaves a bounce no faster than this upwards stays down and rolls
// on, rather than hopping ever smaller hops forever. At 0 that is still every
// ball a bounce leaves at a standstill.
inline constexpr double kDefaultRestingVerticalSpeed = 0.25;  // m/s

// A football's radius, which turns spin at the moment of a bounce into speed
// along the ground: the contact point of a ball spinning at ω moves at ω · r.
inline constexpr double kBallRadius = 0.11;  // m

// The physical constants of the ball model (docs/ball-movement.md). The
// rolling deceleration and gravity must be positive and finite, the carry
// distance, the drag, the spin decay and the resting speed finite and not
// negative, and the three shares between 0 and 1.
struct BallPhysics {
  double rollingDeceleration = kDefaultRollingDeceleration;
  double carryDistance = kDefaultCarryDistance;
  double gravity = kDefaultGravity;
  double airDrag = kDefaultAirDrag;
  double bounceRestitution = kDefaultBounceRestitution;
  double bounceGrip = kDefaultBounceGrip;
  double spinTransfer = kDefaultSpinTransfer;
  double spinDecay = kDefaultSpinDecay;
  double restingVerticalSpeed = kDefaultRestingVerticalSpeed;

  friend bool operator==(const BallPhysics&, const BallPhysics&) = default;
};

// Throws std::invalid_argument naming the offending value unless every
// constant keeps to the rules above.
void validate(const BallPhysics& physics);

// Whether the ball is flying rather than lying or rolling on the grass: it is
// off the ground, or it is on it and moving up or down. A ball that has just
// been lifted off the turf is in flight before it has gained any height.
[[nodiscard]] bool isInFlight(const BallState& ball) noexcept;

// Distance a ball rolling at this speed covers before it stops, in meters.
[[nodiscard]] double rollingDistance(double speed, const BallPhysics& physics) noexcept;

// The fraction t in (0, 1] of a move from start to end at which the ball
// crosses the pitch boundary, for a start on the pitch and an end off it, and
// 1 for a move that stays on. A free ball always moves along a straight line
// in the pitch plane -- drag acts along its direction of travel and a bounce
// only scales its speed -- so one fraction describes a whole tick, however
// often the ball bounces within it.
[[nodiscard]] double pitchExitFraction(SimCore::Vec2 start, SimCore::Vec2 end,
                                       const Pitch& pitch) noexcept;

// Where and when a ball in flight comes down, and how high it gets on the
// way; empty for a ball that is already on the ground. Solved from the
// closed-form flight rather than simulated, so a planner -- a chaser, later a
// goalkeeper -- can ask where a ball will land without stepping the match.
// The pitch boundary is not applied: this is the flight's own landing, and
// what happens to a ball that leaves the pitch is for the systems to decide.
struct BallLanding {
  SimCore::Vec2 position;
  double seconds = 0.0;
  double apexHeight = 0.0;
};

[[nodiscard]] std::optional<BallLanding> predictBallLanding(const BallState& ball,
                                                            const BallPhysics& physics) noexcept;

// How high a free ball is `seconds` into its step: the flight stepFreeBall()
// itself follows, gravity, drag and every bounce within the span, but without
// the pitch boundary.
//
// A ball that crosses a line is stopped on it and put down flat, which says
// nothing about how high it was as it crossed, so reception asks this rather
// than reading the height the step ended on (docs/reception.md). Seconds
// beyond the step are the flight's own continuation; a ball on the grass is
// at zero whatever it is asked.
[[nodiscard]] double ballHeightAfter(const BallState& ball, const BallPhysics& physics,
                                     double seconds) noexcept;

// One tick of a free ball as described in docs/ball-movement.md.
//
// A ball on the grass rolls: ground friction slows it at a constant rate along
// its direction of travel until it stops -- never reversing it. The
// integration is exact for constant deceleration, so a ball rolling from speed
// v stops after v² / (2 · rollingDeceleration) meters, independent of the tick
// rate.
//
// A ball off the ground, or one with a vertical velocity, flies: gravity pulls
// it down, a linear air drag takes speed from every component, and its spin
// dies away. Where it reaches the ground within the tick it bounces, losing
// part of its vertical speed and part of its speed along the ground, and its
// spin drives it on or checks it. The tick is split at that moment rather than
// at its end, which is what makes the same flight come out the same at 30 Hz
// and at 300 Hz. A ball whose bounce leaves it rising no faster than
// restingVerticalSpeed stays down and rolls the rest of the tick.
//
// A ball that leaves the pitch stops on the line where it crossed it, on the
// ground and at rest, in the air as on the grass: the documented stand-in
// until the rules decide on throw-ins, goal kicks, corners and goals.
[[nodiscard]] BallState stepFreeBall(const BallState& ball, const BallPhysics& physics,
                                     const Pitch& pitch, double secondsPerTick) noexcept;

// The same tick, with the part of it the ball travelled: the whole tick,
// or the part up to the moment the ball left the pitch and stopped on the
// line. stepFreeBall() leaves such a ball lying on the line, which says
// nothing about when it got there -- and reception has to know, because a
// contact somewhere along that shortened path happened earlier in the tick
// than the same fraction of a whole one (docs/reception.md).
struct BallStep {
  BallState ball;
  double seconds = 0.0;
};

[[nodiscard]] BallStep stepFreeBallTimed(const BallState& ball, const BallPhysics& physics,
                                         const Pitch& pitch, double secondsPerTick) noexcept;

}  // namespace ElyverseFootball::SimMatch
