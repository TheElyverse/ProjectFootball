#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "ballPhysics.hpp"
#include "ids.hpp"
#include "matchState.hpp"
#include "random.hpp"
#include "reception.hpp"
#include "simTime.hpp"
#include "vec2.hpp"

namespace ElyverseFootball::SimMatch {

// How players go up for a high ball, who wins it and what he does with it;
// see docs/aerial-duels.md. Part of MatchConfig and therefore of every
// replay.
struct AerialConfig {
  // The highest a standing player meets the ball with his head, as the
  // height of its centre, and how close to him along the ground it must come.
  // A goalkeeper in his own penalty area reaches it with his hands instead,
  // as far and as high as ReceptionConfig::handsRadius and handsHeight say.
  double headHeight = 1.9;  // m
  double headRadius = 0.7;  // m
  // How high a jump lifts him: at jumping 0 and at 1, linear between.
  double lowestJump = 0.3;   // m
  double highestJump = 0.7;  // m
  // How far from the ball's arrival an average header times the top of his
  // jump: the half-width of a triangular spread, scaled by
  // skillErrorFactor() of his heading -- a keeper's of his handling.
  double timingError = 0.25;  // s
  // He goes up for a ball up to this far above the best his jump can reach.
  double attemptMargin = 0.3;  // m
  // When the first player gets to a high ball, everyone else this close to
  // it goes up for it too.
  double contestRadius = 1.5;  // m
  // After going up he goes up for no other ball for this long.
  double landingSeconds = 0.6;  // s
  // What wins the duel among those who reach the ball: how far above it they
  // get, up to reachCap; how long after the ball they get there; their
  // strength; how square they stand to it; and for a keeper with his hands,
  // his handling. chooseByUtility() decides at duelTemperature.
  double reachWeight = 4.0;    // per m
  double reachCap = 0.3;       // m
  double arrivalWeight = 5.0;  // per s
  double strengthWeight = 1.0;
  double bodyWeight = 1.0;
  double keeperAdvantage = 1.0;
  double duelTemperature = 0.3;
  // A keeper of average handling drops this share of the balls he wins
  // against an opponent who went up with him, and punches them clear.
  double contestedDrop = 0.3;
  // He heads at goal from no farther than shotRange from its centre, at
  // shotSpeed, aiming shotHeight high and shotInside inside the post away
  // from the keeper.
  double shotRange = 14.0;  // m
  double shotSpeed = 14.0;  // m/s
  double shotHeight = 0.5;  // m
  double shotInside = 0.5;  // m
  // He heads to a teammate no farther than passRange -- down to one no
  // farther than knockDownRange --, so it gets there along the ground in
  // passSeconds. A teammate openDistance from his nearest opponent is open.
  double passRange = 20.0;      // m
  double knockDownRange = 6.0;  // m
  double passSeconds = 0.8;     // s
  double openDistance = 5.0;    // m
  double passWeight = 0.6;
  // He clears a ball the more readily the closer he is to his own goal,
  // within clearanceZone, heading it clearanceDistance up the pitch.
  double clearanceZone = 35.0;      // m
  double clearanceDistance = 30.0;  // m
  // No header leaves faster than this, along the ground and up together.
  double maxHeaderSpeed = 18.0;  // m/s
  double decisionTemperature = 0.15;
  // An average header goes up to directionError meters off his aim per
  // meter to it, across and up, and up to speedError harder or softer. A
  // fully mistimed one -- off by timingError -- widens that by
  // 1 + mistimedErrorFactor.
  double directionError = 0.06;
  double speedError = 0.1;
  double mistimedErrorFactor = 2.0;

  friend bool operator==(const AerialConfig&, const AerialConfig&) = default;
};

// Throws std::invalid_argument unless every value is finite and not
// negative, the head height and radius, the contest radius, the temperatures,
// the shot and pass ranges, the clearance zone, the pass time and the open
// distance are positive, the lowest jump does not
// exceed the highest, the knock-down range does not exceed the pass range,
// the dropped share lies in [0, 1] and the speed error below 1.
void validate(const AerialConfig& config);

// What the winner of an aerial contest did with the ball: headed it at
// goal, to a teammate or down to one close by, or clear; or, a goalkeeper
// with his hands, caught it or punched it clear.
enum class AerialPlay : std::uint8_t {
  kShot,
  kPass,
  kKnockDown,
  kClearance,
  kCaught,
  kPunched,
};

// "shot", "pass", "knockDown", "clearance", "caught", "punched"; "unknown"
// outside the enumerators.
[[nodiscard]] std::string_view aerialPlayName(AerialPlay play) noexcept;

// One player who went up for a high ball: the highest ball centre his jump
// met it with, and whether that reached it.
struct AerialContestant {
  SimCore::PlayerId player;
  double reach = 0.0;  // m
  bool reached = false;

  friend bool operator==(const AerialContestant&, const AerialContestant&) = default;
};

// How high a jump lifts a player of this jumping.
[[nodiscard]] double jumpRise(double jumping, const AerialConfig& config) noexcept;

// The skill a player times his jump and aims his header with: a keeper with
// his hands his keeperHandling, everyone else his heading.
[[nodiscard]] double timingSkill(const PlayerAttributes& attributes, bool hands) noexcept;

// How far from the ball's arrival a player of this skill may time the top of
// his jump.
[[nodiscard]] double timingSpread(double skill, const AerialConfig& config) noexcept;

// How high a ball centre a player meets who stands this high and jumps this
// high, with the top of his jump `offset` seconds from the ball: the jump is
// a parabola under gravity, and off it he is back on his feet.
[[nodiscard]] double reachAt(double standing, double rise, double offset, double gravity) noexcept;

// Whether the player at this index has gone up for a ball within
// landingSeconds before the start of tick now, and so goes up for no other.
[[nodiscard]] bool isInTheAir(const MatchState& state, std::size_t playerIndex,
                              SimCore::SimTick now, double secondsPerTick,
                              const AerialConfig& config);

// How far and how high the player at this index gets to a high ball at best:
// with his hands if `hands`, with his head otherwise, his jump timed
// exactly, plus attemptMargin; only above controlHeight, where the feet do
// not take it.
[[nodiscard]] BallReach aerialReach(const MatchState& state, std::size_t playerIndex, bool hands,
                                    const ReceptionConfig& reception, const AerialConfig& config);

// Whether the ball may be higher than `height` at any moment of its flight
// from here on: its peak without drag, which only lowers it, is, and no
// bounce takes it higher than it came down from.
[[nodiscard]] bool mayRiseAbove(const BallState& ball, double height,
                                const BallPhysics& physics) noexcept;

// The first player to get to a high ball on its way this tick, as
// findFirstReach() finds him with aerialReach(): everyone who
// mayCompete() and is not in the air; a keeper with his hands if hasHands()
// where his hands first get to it. Empty if nobody gets near it, and without
// a search for a ball that never rises above controlHeight (mayRiseAbove()).
[[nodiscard]] std::optional<BallClaim> findAerialContact(
    const MatchState& state, const BallState& ball, const BallStep& moved,
    const BallPhysics& physics, SimCore::SimTick now, double secondsPerTick,
    const ReceptionConfig& reception, const AerialConfig& config,
    std::span<const std::size_t> excluded = {});

// A player going up for a high ball, as the duel sees him: how high he
// stands and jumps, with his hands or not, how long after the ball he gets
// to where it is, and how square he stands to it, from 0 with his back to
// it to 1 facing it.
struct AerialChallenger {
  std::size_t playerIndex = 0;
  double standing = 0.0;  // m
  double rise = 0.0;      // m
  bool hands = false;
  double lateSeconds = 0.0;
  double body = 1.0;
};

// Everyone who goes up for the ball the first contact found: he, and every
// other player who may compete for it, is not in the air, is within
// contestRadius of it by that moment and whose aerialReach() gets as high as
// it is there, in player order. Each has his hands if hasHands() where he
// and the ball are at that moment.
[[nodiscard]] std::vector<AerialChallenger> findChallengers(
    const MatchState& state, const BallState& ball, const BallStep& moved, const BallClaim& first,
    const BallPhysics& physics, SimCore::SimTick now, double secondsPerTick,
    const ReceptionConfig& reception, const AerialConfig& config,
    std::span<const std::size_t> excluded = {});

// One challenger's jump: how far off he timed it, the share of timingError
// that is (his mistiming, at most 1), how high that took him and whether it
// reached the ball.
struct AerialJump {
  double offset = 0.0;  // s
  double mistime = 0.0;
  double reach = 0.0;  // m
  bool reached = false;
};

// What a challenger who reached the ball brings to the duel.
[[nodiscard]] double duelUtility(const PlayerAttributes& attributes,
                                 const AerialChallenger& challenger, const AerialJump& jump,
                                 double ballHeight, const AerialConfig& config) noexcept;

// The jumps of every challenger, in his order, and the one who won the ball,
// as an index into the challengers; no winner if nobody reached it.
struct AerialDuel {
  std::vector<AerialJump> jumps;
  std::optional<std::size_t> winner;
};

// Every challenger times his jump -- two draws from random each, in order --
// and of those who reach a ball this high, chooseByUtility() picks the
// winner by duelUtility() -- one more draw if anyone reached it.
[[nodiscard]] AerialDuel resolveAerialDuel(const MatchState& state,
                                           std::span<const AerialChallenger> challengers,
                                           double ballHeight, const AerialConfig& config,
                                           double gravity, SimCore::RandomNumberGenerator& random);

// How likely the keeper who won the ball with his hands holds it: certainly
// if no opponent went up with him, otherwise all but contestedDrop scaled by
// skillErrorFactor() of his handling.
[[nodiscard]] double holdChance(const PlayerAttributes& keeper, bool contested,
                                const AerialConfig& config) noexcept;

// What a header is meant to do: the play, where to along the ground and how
// high the ball should be there, how fast it should leave the head, and for
// whom.
struct HeaderIntent {
  AerialPlay play = AerialPlay::kClearance;
  SimCore::Vec2 target;
  double height = 0.0;  // m
  double speed = 0.0;   // m/s
  std::optional<SimCore::PlayerId> receiver;

  friend bool operator==(const HeaderIntent&, const HeaderIntent&) = default;
};

// A header the winner could play, and what it is worth to him.
struct HeaderOption {
  HeaderIntent intent;
  double utility = 0.0;
};

// Every header the player at this index could play off the ball where it
// is, from the true state -- a header is played without a second look:
// a clearance up the pitch, worth more the closer he is to his own goal; a
// shot, if the goal he attacks is within shotRange; and a pass or a
// knock-down to every teammate within passRange, worth more the closer and
// the more open he is.
[[nodiscard]] std::vector<HeaderOption> headerOptions(const MatchState& state,
                                                      std::size_t playerIndex,
                                                      const BallState& ball,
                                                      const AerialConfig& config);

// The clearance of headerOptions(), which a keeper's punch plays too.
[[nodiscard]] HeaderIntent clearanceIntent(const MatchState& state, std::size_t playerIndex,
                                           const BallState& ball, const AerialConfig& config);

// The decision half of a header: chooseByUtility() over headerOptions() at
// decisionTemperature, one draw from random.
[[nodiscard]] HeaderIntent decideHeader(const MatchState& state, std::size_t playerIndex,
                                        const BallState& ball, const AerialConfig& config,
                                        SimCore::RandomNumberGenerator& random);

// How much wider than an average header's this one's errors are: by
// skillErrorFactor() of the skill, and by up to 1 + mistimedErrorFactor for
// a jump fully mistimed.
[[nodiscard]] double headerErrorFactor(double skill, double mistime,
                                       const AerialConfig& config) noexcept;

// The ball as a header sends it off, and the point and height it is really
// heading for: the aim, off by the error.
struct HeaderStrike {
  SimCore::Vec2 velocity;
  double verticalVelocity = 0.0;
  SimCore::Vec2 target;
  double height = 0.0;
};

// The execution half of a header, from the ball where it meets the head:
// the ball leaves toward the target on the flight that brings it to the
// intended height there at the intended speed -- at most maxHeaderSpeed --,
// its aim strayed across and up by a triangular error and its pace by a
// uniform one, all widened by errorFactor -- always five draws from random.
// A flight the drag stops short of goes up at 45 degrees, and however its
// pace strays, it leaves no faster than maxHeaderSpeed in all. A target on
// the ball itself is headed along the player's facing.
[[nodiscard]] HeaderStrike executeHeader(const HeaderIntent& intent, const BallState& ball,
                                         const PlayerMatchState& player, double errorFactor,
                                         const AerialConfig& config, const BallPhysics& physics,
                                         SimCore::RandomNumberGenerator& random) noexcept;

}  // namespace ElyverseFootball::SimMatch
