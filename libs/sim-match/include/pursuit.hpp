#pragma once

#include <optional>
#include <string_view>

#include "ballMovement.hpp"
#include "goalkeeper.hpp"
#include "matchSimulation.hpp"
#include "matchState.hpp"
#include "pitch.hpp"
#include "reception.hpp"
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

// How high a goalkeeper reaches the ball with his hands, and the end whose
// penalty area he may use them in.
struct HandsReach {
  GoalEnd end = GoalEnd::kMinX;
  double height = 0.0;
};

// Where and when a player can reach a free ball.
struct Interception {
  SimCore::Vec2 point;
  double seconds = 0.0;
};

// The earliest point on the free ball's predicted path -- stepped with
// stepFreeBall() in sampleSeconds steps, so it rolls, flies and bounces as it
// will in the match -- the player reaches no later than
// the ball, by estimateArrivalSeconds(). A point where the ball is higher than
// reachHeight is no interception: he cannot play it there, and a ball over his
// head is chased to where it comes down. With hands, a point inside their
// penalty area counts up to their height instead. If he reaches none before
// the ball stops or before horizonSeconds, where the ball ends up and when he
// gets there. Empty if he cannot reach that either.
[[nodiscard]] std::optional<Interception> findInterception(
    const PlayerMatchState& player, const BallState& ball, const BallPhysics& physics,
    const Pitch& pitch, const PursuitConfig& config, double reachHeight,
    const std::optional<HandsReach>& hands = std::nullopt);

inline constexpr std::string_view kPursuitSystemName = "ball pursuit";

// While the ball is free, sends one player per side after it: the one with
// the earliest interception, ties to the lower id. He becomes his side's
// chaser (MatchState::chaser()) and his movement target the interception
// point, overriding any assigned target; everyone else keeps his. The last
// player to touch the ball does not chase it while it still moves -- he just
// passed it.
//
// The reach comes from the reception configuration: a chaser runs to where he
// can actually play the ball, not to a point it flies over -- a goalkeeper in
// his own penalty area up to his hands' height.
//
// A goalkeeper who would be his side's chaser of a ball the opponent played
// last, at a point behind his defensive line, decides first whether he comes
// (docs/goalkeeper.md): his
// interception against the opponents' earliest, with the misjudgement he
// draws from the kAi stream when he first judges that ball, by callSweep()
// and his tactic's sweeping dial. He keeps the judgement in his tactical
// state and reports it as a SweepDiagnostic when he first makes it or turns
// back. A ball he stays home for he leaves; while he comes for one he judges
// it again at every update, and turns back for good once a teammate is his
// side's chaser. If he stays, his side's next earliest player chases the ball.
// A keeper busy with a dive (isDiving(), docs/shot-stopping.md) chases
// nothing until he is up again.
//
// The chaser's target belongs to pursuit. A player who stops being the
// chaser -- someone else is closer, or anyone controls the ball -- has his
// target cleared and stops, rather than running on to where the ball was
// going to be; the tactical systems give a player of a side with a tactic his
// next target. Runs every config.intervalTicks ticks and writes movement
// targets, chasers and the goalkeepers' judgements only. The ball is read
// directly, not through perception. Throws std::invalid_argument for an
// interval below one tick, a sample or horizon that is not positive and
// finite, a horizon of more than kMaxPursuitSamples samples, or an invalid
// goalkeeper configuration.
[[nodiscard]] MatchSystem makePursuitSystem(const BallPhysics& physics, const PursuitConfig& config,
                                            const ReceptionConfig& reception,
                                            const GoalkeeperConfig& goalkeeper);
[[nodiscard]] MatchSystem makePursuitSystem(const BallPhysics& physics, const PursuitConfig& config,
                                            const ReceptionConfig& reception);
[[nodiscard]] MatchSystem makePursuitSystem(const BallPhysics& physics,
                                            const PursuitConfig& config);

}  // namespace ElyverseFootball::SimMatch
