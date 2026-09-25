#pragma once

#include <optional>
#include <string_view>

#include "ballMovement.hpp"
#include "matchSimulation.hpp"
#include "matchState.hpp"
#include "pitch.hpp"
#include "vec2.hpp"

namespace ElyverseFootball::SimMatch {

// The most ball-path samples one interception search may check:
// horizonSeconds / sampleSeconds. Pursuit runs this search for every player,
// so the bound keeps a configuration -- read from a replay file, say -- from
// making a step arbitrarily slow. The defaults use 80.
inline constexpr double kMaxPursuitSamples = 1000.0;

// How players go after a free ball; see docs/reception.md. Part of
// MatchConfig and therefore of every replay.
struct PursuitConfig {
  // 3 ticks at 30 Hz: chasers re-plan ten times a second.
  int intervalTicks = 3;
  // The ball's predicted path is checked at these steps.
  double sampleSeconds = 0.1;
  // How far ahead the path is predicted.
  double horizonSeconds = 8.0;

  friend bool operator==(const PursuitConfig&, const PursuitConfig&) = default;
};

// Where and when a player can reach a free ball.
struct Interception {
  SimCore::Vec2 point;
  double seconds = 0.0;
};

// The earliest point on the free ball's predicted path -- rolled with
// stepFreeBall() in sampleSeconds steps -- the player reaches no later than
// the ball, by estimateArrivalSeconds(). If he reaches none before it stops
// or before horizonSeconds, where the ball ends up and when he gets there.
// Empty if he cannot reach that either.
[[nodiscard]] std::optional<Interception> findInterception(const PlayerMatchState& player,
                                                           const BallState& ball,
                                                           const BallPhysics& physics,
                                                           const Pitch& pitch,
                                                           const PursuitConfig& config);

inline constexpr std::string_view kPursuitSystemName = "ball pursuit";

// While the ball is free, sends one player per side after it: the one with
// the earliest interception, ties to the lower id. He becomes his side's
// chaser (MatchState::chaser()) and his movement target the interception
// point, overriding any assigned target; everyone else keeps his. The last
// player to touch the ball does not chase it while it still moves -- he just
// passed it.
//
// The chaser's target belongs to pursuit. A player who stops being the
// chaser -- someone else is closer, or anyone controls the ball -- has his
// target cleared and stops, rather than running on to where the ball was
// going to be; the tactical systems give a player of a side with a tactic his
// next target. Runs every config.intervalTicks ticks and writes movement
// targets and chasers only. The ball is read directly, not through
// perception. Throws std::invalid_argument for an interval below one tick, a
// sample or horizon that is not positive and finite, or a horizon of more
// than kMaxPursuitSamples samples.
[[nodiscard]] MatchSystem makePursuitSystem(const BallPhysics& physics,
                                            const PursuitConfig& config);

}  // namespace ElyverseFootball::SimMatch
