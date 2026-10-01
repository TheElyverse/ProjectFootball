#pragma once

#include <cstddef>
#include <optional>
#include <string_view>

#include "ballPhysics.hpp"
#include "goalFrame.hpp"
#include "matchSimulation.hpp"
#include "matchState.hpp"
#include "random.hpp"
#include "simTime.hpp"
#include "tacticalState.hpp"
#include "vec2.hpp"

namespace ElyverseFootball::SimMatch {

// How a goalkeeper stops shots; see docs/shot-stopping.md. Part of
// MatchConfig and therefore of every replay.
struct ShotStoppingConfig {
  // How long after the ball is struck or deflected he goes: at
  // keeperReflexes 0 and at 1, linear between.
  double slowestReaction = 0.3;   // s
  double quickestReaction = 0.1;  // s
  // How far a keeper of keeperAnticipation 0 may misread where the ball
  // passes him, across his plane and up. The spread shrinks with his skill
  // and is gone for a keeper of 1.
  double readError = 0.8;  // m
  // His hands as he stands set, this high above his feet.
  double readyHeight = 1.4;  // m
  // The farthest his hands get from where his feet are: an ellipse in his
  // plane, this far to either side along the ground and this high above it.
  double diveReach = 2.8;  // m
  double jumpReach = 2.5;  // m
  // How fast his hands go once he dives.
  double diveSpeed = 6.0;  // m/s
  // He touches a ball whose centre comes this close to the line from his
  // feet to his hands.
  double bodyReach = 0.5;  // m
  // A ball this fast he never holds, however it comes.
  double catchableSpeed = 40.0;  // m/s
  // A ball he does not hold comes off his hands with a share of its speed
  // into them between these two, the palm tilted toward where he stretched
  // to by parryTilt at full stretch and by up to parrySpread at random.
  double minParrySpeed = 0.2;
  double maxParrySpeed = 0.5;
  double parryTilt = 1.5;
  double parrySpread = 0.5;
  // He goes for a ball he reads no farther than this from where he can get
  // to in time: at keeperAnticipation 0 and at 1, linear between.
  double carelessTryMargin = 1.5;  // m
  double carefulTryMargin = 0.5;   // m
  // He leaves a ball he reads crossing his goal line farther than this
  // beside or above the frame.
  double wideMargin = 0.5;  // m
  // A ball on the grass slower than this is no shot to dive for: he goes and
  // picks it up.
  double diveBelowSpeed = 8.0;  // m/s
  // How long he is down once the ball has passed him: standing set and at
  // full stretch, linear between, shortened by up to reflexRecoveryShare of
  // it by his reflexes.
  double standingRecovery = 0.3;  // s
  double stretchRecovery = 1.0;   // s
  double reflexRecoveryShare = 1.0 / 3.0;

  friend bool operator==(const ShotStoppingConfig&, const ShotStoppingConfig&) = default;
};

// Throws std::invalid_argument unless every value is finite and not
// negative, the reaches, the dive speed, the body reach and the catchable
// speed are positive, no quickest, careful or standing value exceeds its
// counterpart, and the parry shares and the reflex share lie in [0, 1] with
// the lowest parry share not above the highest.
void validate(const ShotStoppingConfig& config);

inline constexpr std::string_view kShotStoppingSystemName = "shot stopping";

// How long a keeper of these reflexes takes to react to a ball.
[[nodiscard]] double reactionSeconds(double reflexes, const ShotStoppingConfig& config) noexcept;

// How far a keeper of this anticipation goes for a ball beyond his reach.
[[nodiscard]] double tryMargin(double anticipation, const ShotStoppingConfig& config) noexcept;

// How long a keeper is down after a ball passed him with his hands this far
// from ready, as a share of diveReach in [0, 1]: his keeperReflexes shorten
// it.
[[nodiscard]] double recoverySeconds(double stretch, const PlayerAttributes& keeper,
                                     const ShotStoppingConfig& config) noexcept;

// The goalkeeper who faces this free ball, by index: the ball is on a flight
// of the open shot -- struck or deflected, not parried --, on its way across
// the goal line of the keeper of the side the shot is against, and in the air
// or at least diveBelowSpeed fast. Empty for any other ball, and for a side
// without a tactic, which has no keeper.
[[nodiscard]] std::optional<std::size_t> facingKeeper(const MatchState& state,
                                                      const BallState& ball,
                                                      const std::optional<ShotRecord>& shot,
                                                      const BallPhysics& physics,
                                                      const ShotStoppingConfig& config);

// Whether the dive keeps him busy this long after he went: in the air or
// down. A keeper who left the ball never is.
[[nodiscard]] bool isBusy(const KeeperDive& dive, double seconds) noexcept;

// Whether the player at this index is busy with a dive at the start of tick
// `now`.
[[nodiscard]] bool isDiving(const MatchState& state, std::size_t playerIndex, SimCore::SimTick now,
                            double secondsPerTick);

// Where his hands are as he stands set: above his feet at readyHeight.
[[nodiscard]] PlanePoint readyHands(const ShotStoppingConfig& config) noexcept;

// The point itself if it lies within the ellipse his hands reach from his
// feet, otherwise the point on its edge toward it from his feet.
[[nodiscard]] PlanePoint clampToReach(PlanePoint point, const ShotStoppingConfig& config) noexcept;

// How far a player of these attributes runs this long after he set off from
// standing, at his acceleration up to his top speed, and how long he needs
// to get this far.
[[nodiscard]] double runDistance(double seconds, const PlayerAttributes& attributes) noexcept;
[[nodiscard]] double runSeconds(double distance, const PlayerAttributes& attributes) noexcept;

// Where across his plane his feet are this long after he went: running to
// `feet`, then there.
[[nodiscard]] double feetAt(const KeeperDive& dive, double seconds,
                            const PlayerAttributes& attributes) noexcept;

// Where his hands are this long after he went: ready above his feet while he
// runs, then on the straight line to his target at diveSpeed until they get
// there.
[[nodiscard]] PlanePoint handsAt(const KeeperDive& dive, double seconds,
                                 const PlayerAttributes& attributes,
                                 const ShotStoppingConfig& config) noexcept;

// Where on the ground he is this long after he went: under his hands.
[[nodiscard]] SimCore::Vec2 bodyAt(const KeeperDive& dive, double seconds,
                                   const PlayerAttributes& attributes,
                                   const ShotStoppingConfig& config) noexcept;

// The decision half of a save: the keeper at this index reads where the ball
// passes his plane -- through where he stands, square to the ball's way --
// off by a triangular draw across and one up, each spread by readError times
// (1 - keeperAnticipation), and plans to get there: how far across to run
// first and where to send his hands, so that they are as close to the point
// as he can get by the time the ball arrives, running as far as time allows.
// He leaves a ball he reads farther than tryMargin() from what he gets to,
// or crossing his goal line farther than wideMargin beside or above the
// frame. Always two draws from random. Empty, without a draw, for a ball
// that has passed his plane or stops short of it.
[[nodiscard]] std::optional<KeeperDive> decideDive(const MatchState& state, std::size_t keeperIndex,
                                                   SimCore::SimTick now,
                                                   const ShotStoppingConfig& config,
                                                   const BallPhysics& physics,
                                                   SimCore::RandomNumberGenerator& random);

// A keeper standing set where he is now against this ball's flight: his
// plane through his position, square to the ball's way, and his hands kept
// ready; once the ball has passed him, landSeconds from the start of `now`,
// he is down for standingRecovery, shortened by his reflexes. Empty for a
// ball that does not move along the ground or that nobody has touched.
[[nodiscard]] std::optional<KeeperDive> standingKeeper(const MatchState& state,
                                                       std::size_t keeperIndex,
                                                       const BallState& ball, SimCore::SimTick now,
                                                       const ShotStoppingConfig& config);

// Whether the dive answers the flight this ball is on.
[[nodiscard]] bool answers(const KeeperDive& dive, const BallState& ball) noexcept;

// When, within `seconds`, the ball passes the dive's plane, and the ball
// then; empty if it does not within them.
[[nodiscard]] std::optional<BallPassage> findPlanePassage(const BallState& ball,
                                                          const KeeperDive& dive,
                                                          const BallPhysics& physics,
                                                          double seconds) noexcept;

// The ball's centre as a point of the dive's plane.
[[nodiscard]] PlanePoint planePoint(const BallState& ball, const KeeperDive& dive) noexcept;

// Whether a ball whose centre is at `ball` touches the keeper with his feet
// at `feet` across and his hands at `hands`: it comes within bodyReach of the
// line between them.
[[nodiscard]] bool touchesKeeper(double feet, PlanePoint hands, PlanePoint ball,
                                 const ShotStoppingConfig& config) noexcept;

// How far his hands are from ready above his feet, as a share of diveReach
// in [0, 1].
[[nodiscard]] double stretchOf(double feet, PlanePoint hands,
                               const ShotStoppingConfig& config) noexcept;

// How likely a keeper is to hold this ball: its ease -- how far its speed is
// below catchableSpeed, times how far he is from full stretch -- with what it
// lacks widened by skillErrorFactor() of his keeperHandling. Never at
// catchableSpeed or above, whatever his handling.
[[nodiscard]] double catchChance(const BallState& ball, double stretch,
                                 const PlayerAttributes& keeper,
                                 const ShotStoppingConfig& config) noexcept;

// The execution half of a save the keeper touched with his feet at `feet`
// and his hands at `hands`: whether he holds the ball, and if not, the ball
// as it comes off him -- off his palm, square to the dive's plane and tilted
// toward where he stretched to, keeping a share of its speed into it and the
// rest along it, without spin. Always four draws from random.
struct SaveTouch {
  bool caught = false;
  BallState ball;
};

[[nodiscard]] SaveTouch executeSave(const BallState& ball, const KeeperDive& dive, double feet,
                                    PlanePoint hands, const PlayerAttributes& keeper,
                                    const ShotStoppingConfig& config,
                                    SimCore::RandomNumberGenerator& random) noexcept;

// Every tick: once the keeper facing a shot's flight (facingKeeper()) has
// had his reaction time since the ball was struck or deflected, he decides
// his dive against it (decideDive(), from the kAi stream) and keeps it in
// his tactical state. A flight he has answered he does not answer again,
// and a keeper busy with a dive answers nothing. The movement system moves
// him by it and the ball system executes it where the ball passes his plane.
// Writes players' tactical states only. Throws std::invalid_argument for an
// invalid configuration.
[[nodiscard]] MatchSystem makeShotStoppingSystem(const ShotStoppingConfig& config,
                                                 const BallPhysics& physics);

}  // namespace ElyverseFootball::SimMatch
