#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "crossArea.hpp"
#include "ids.hpp"
#include "observation.hpp"
#include "pitch.hpp"
#include "pitchControlGrid.hpp"
#include "shotType.hpp"
#include "simTime.hpp"
#include "tactic.hpp"
#include "tacticalPhase.hpp"
#include "tacticalState.hpp"
#include "teamPress.hpp"
#include "vec2.hpp"

namespace ElyverseFootball::SimMatch {

// Which squad a player belongs to; not itself an attacking direction. Home
// defends x = 0 in both halves -- when the teams change ends only a view
// turns the pitch round (docs/match-clock.md#halves) -- and systems that need
// the direction ask attackingDirection() (teamFrame.hpp).
enum class TeamSide : std::uint8_t {
  kHome,
  kAway,
};

// "home" / "away", for error messages and debug output. Any value outside the
// declared enumerators (e.g. a cast from persisted numeric input) yields
// "unknown" rather than being passed off as one of the two sides.
[[nodiscard]] std::string_view teamSideName(TeamSide side) noexcept;

// True only for the declared enumerators kHome and kAway.
[[nodiscard]] bool isValidTeamSide(TeamSide side) noexcept;

// Squad size per side used when a caller does not state one: seven-a-side,
// the sandbox of short scenarios. Eleven-a-side is the same type with another
// value (docs/match-engine.md#the-eleven-a-side-kickoff-fixture).
inline constexpr int kDefaultPlayersPerSide = 7;

// How far a facing vector's squared length may be from 1 and still count as a
// unit vector: generous for rounding in normalization, tight enough to catch
// an unnormalized vector.
inline constexpr double kFacingTolerance = 1e-9;

// Movement limits used when a caller does not state any: a fast amateur
// rather than an elite sprinter. They belong to the predefined test players of
// the sandbox; generated players will derive theirs from capabilities.
inline constexpr double kDefaultMaxSpeed = 7.5;      // m/s
inline constexpr double kDefaultAcceleration = 4.0;  // m/s²
// An average finisher's accuracy, on the scale of shotAccuracy, and the same
// for his technique and his weaker foot.
inline constexpr double kDefaultShotAccuracy = 0.5;
inline constexpr double kDefaultShotTechnique = 0.5;
inline constexpr double kDefaultWeakFootAccuracy = 0.5;
// An average keeper's positioning, anticipation, reflexes and handling, on the
// scale of keeperPositioning, keeperAnticipation, keeperReflexes and
// keeperHandling.
inline constexpr double kDefaultKeeperPositioning = 0.5;
inline constexpr double kDefaultKeeperAnticipation = 0.5;
inline constexpr double kDefaultKeeperReflexes = 0.5;
inline constexpr double kDefaultKeeperHandling = 0.5;
// An average player's jump, heading and strength, on the scale of jumping,
// heading and strength.
inline constexpr double kDefaultJumping = 0.5;
inline constexpr double kDefaultHeading = 0.5;
inline constexpr double kDefaultStrength = 0.5;
// An average player's ball control and dribbling, on the scale of
// ballControl and dribbling.
inline constexpr double kDefaultBallControl = 0.5;
inline constexpr double kDefaultDribbling = 0.5;
// An average decider, on the scale of decisionMaking.
inline constexpr double kDefaultDecisionMaking = 0.5;

// The foot a player strikes the ball with.
enum class Foot : std::uint8_t {
  kLeft,
  kRight,
};

// "left", "right"; "unknown" outside the enumerators.
[[nodiscard]] std::string_view footName(Foot foot) noexcept;

// What a player can do, fixed for a match. maxSpeed and acceleration are what
// his body allows and must be positive and finite; the same acceleration limits
// speeding up, slowing down and turning (see docs/players.md#player-movement).
// shotAccuracy, in [0, 1], is how closely his shots follow his aim, from wild
// to pinpoint (docs/on-ball.md#shot-decisions). shotTechnique, in [0, 1], is
// how cleanly he strikes the ball -- its pace, its lift and its spin -- and
// weakFootAccuracy, in [0, 1], how much of his shooting he keeps on the foot
// that is not his strongFoot (docs/on-ball.md#shooting). keeperPositioning, in
// [0, 1], is how exactly he takes up his place in goal, and keeperAnticipation,
// in [0, 1], how well he judges whether he reaches a ball played in behind
// first (docs/goalkeeper.md#positioning-and-sweeping) and where a shot will
// pass him; keeperReflexes, in [0, 1], how quickly he reacts to a shot, and
// keeperHandling, in [0, 1], how surely he holds one he reaches
// (docs/goalkeeper.md#shot-stopping). Only a goalkeeper uses them. jumping, in
// [0, 1], is how high he gets off the ground, heading how well he times his
// jump and directs a header, and strength how well he holds his own in a duel
// (docs/duels.md#aerial-duels-and-headers). ballControl, in [0, 1], is how
// closely his touches follow his intent and how fast he can run with the ball,
// dribbling, in [0, 1], how cleanly and quickly he takes the ball past an
// opponent (docs/on-ball.md#ball-carrying). decisionMaking, in [0, 1], is how
// reliably he picks his best option on the ball and, below the default, how far
// he misjudges his options (docs/on-ball.md#decision-making).
struct PlayerAttributes {
  double maxSpeed = kDefaultMaxSpeed;
  double acceleration = kDefaultAcceleration;
  double shotAccuracy = kDefaultShotAccuracy;
  double shotTechnique = kDefaultShotTechnique;
  Foot strongFoot = Foot::kRight;
  double weakFootAccuracy = kDefaultWeakFootAccuracy;
  double keeperPositioning = kDefaultKeeperPositioning;
  double keeperAnticipation = kDefaultKeeperAnticipation;
  double keeperReflexes = kDefaultKeeperReflexes;
  double keeperHandling = kDefaultKeeperHandling;
  double jumping = kDefaultJumping;
  double heading = kDefaultHeading;
  double strength = kDefaultStrength;
  double ballControl = kDefaultBallControl;
  double dribbling = kDefaultDribbling;
  double decisionMaking = kDefaultDecisionMaking;

  friend bool operator==(const PlayerAttributes&, const PlayerAttributes&) = default;
};

// How fresh a player is (docs/players.md#fitness). sprintReserve is what is
// left of his sprint, from 1 when fresh to 0 when he has none, and runs in real
// time. playedTicks are the ticks he has been on the pitch; their share of the
// match duration is his fatigue over the match. topSpeedShare is the share of
// his maxSpeed both leave him, in (0, 1].
struct PlayerFitness {
  double sprintReserve = 1.0;
  std::int64_t playedTicks = 0;
  double topSpeedShare = 1.0;

  friend bool operator==(const PlayerFitness&, const PlayerFitness&) = default;
};

// One player as the match simulation sees it. Positions are in meters in pitch
// coordinates, velocities in meters per second. Deliberately minimal: the
// orientation, action and perception components of
// docs/implementation-plan.md section 6.2 arrive with the systems that fill
// them.
//
// target is where the player is moving to, assigned by a command and kept on
// the pitch; without one the player comes to a stop where he is.
//
// facing is the unit vector the player looks along, which decides what he can
// see (docs/players.md#perception). It is a vector rather than an angle so that
// no trigonometry, and none of its platform differences, enters the state.
struct PlayerMatchState {
  SimCore::PlayerId playerId;
  TeamSide side = TeamSide::kHome;
  SimCore::Vec2 position;
  SimCore::Vec2 velocity;
  PlayerAttributes attributes;
  std::optional<SimCore::Vec2> target;
  SimCore::Vec2 facing{.x = 1.0, .y = 0.0};
  PlayerFitness fitness = {};

  friend bool operator==(const PlayerMatchState&, const PlayerMatchState&) = default;
};

// Who last played the ball, and in which tick.
struct BallTouch {
  SimCore::PlayerId playerId;
  SimCore::SimTick tick;

  friend bool operator==(const BallTouch&, const BallTouch&) = default;
};

// A touch that plays the ball ahead of its carrier: in which tick, and how
// many seconds later at the earliest he plays it again -- until then it rolls
// on its own (docs/on-ball.md#ball-carrying).
struct CarryTouch {
  SimCore::SimTick tick;
  double seconds = 0.0;

  // Whether the touch's seconds have passed at `time`: from then on its owner
  // may get to the ball again.
  [[nodiscard]] bool hasPassed(const SimCore::StepTime time) const noexcept {
    return time.secondsSince(tick) >= seconds;
  }

  friend bool operator==(const CarryTouch&, const CarryTouch&) = default;
};

// owner is the player in control of the ball; empty while the ball is free.
// One optional id rather than a flag per player, so the ball can never have
// two owners. A controlled ball follows its owner, a free ball rolls, flies
// and bounces on its own (docs/ball.md#possession, docs/ball.md#ball-movement).
// lastTouch is the last player to kick or take the ball; empty until someone
// has.
//
// position and velocity are the ball in the pitch plane, in meters and meters
// per second; height and verticalVelocity are the third dimension, meters
// above the ground and meters per second upwards. A ball with both of them at
// zero lies on the grass and rolls exactly as it always has.
//
// spin is top- or backspin in radians per second about the horizontal axis
// across its direction of travel: positive is topspin, which drives the ball
// on when it bounces, negative backspin, which checks it. sideSpin is spin in
// radians per second about the vertical axis: positive curls a flying ball to
// the left of its travel, negative to the right (docs/ball.md#curve).
//
// held is whether the owner holds the ball in his hands: a goalkeeper who
// took it with them (docs/goalkeeper.md#distribution). Only an owned ball can
// be held; nobody takes him on for it in a duel.
//
// touch is the owner's touch while the ball rolls ahead of him between two
// touches (docs/on-ball.md#ball-carrying): he keeps possession, but the ball
// moves on its own until he or an opponent reaches it. Only an owned ball at
// his feet can be touched on, and setting an owner leaves the ball at his feet.
struct BallState {
  SimCore::Vec2 position;
  SimCore::Vec2 velocity;
  std::optional<SimCore::PlayerId> owner;
  std::optional<BallTouch> lastTouch;
  double height = 0.0;
  double verticalVelocity = 0.0;
  double spin = 0.0;
  double sideSpin = 0.0;
  bool held = false;
  std::optional<CarryTouch> touch = std::nullopt;

  [[nodiscard]] bool isControlled() const noexcept { return owner.has_value(); }

  // Whether the owner has the ball at his feet or in his hands rather than
  // rolling ahead of him after a touch.
  [[nodiscard]] bool isAtFeet() const noexcept { return owner.has_value() && !touch; }

  // Whether the ball lies still on the grass: no velocity in any direction and
  // no height. A ball at the apex of its flight has no velocity in the pitch
  // plane either, so this is what tells the two apart.
  [[nodiscard]] bool isAtRest() const noexcept {
    return velocity == SimCore::Vec2{} && height == 0.0 && verticalVelocity == 0.0;
  }

  // Whether the ball is off the ground, and so out of reach of a foot.
  [[nodiscard]] bool isAirborne() const noexcept { return height > 0.0; }

  friend bool operator==(const BallState&, const BallState&) = default;
};

// No kicked football gets near this; the hardest shots are around 60 m/s. A
// faster ball is a broken fixture, and would overflow when its speed is
// squared. It bounds the speed in the pitch plane and the vertical speed
// separately.
inline constexpr double kMaxBallSpeed = 100.0;  // m/s

// A hard-struck ball spins at some 60 rad/s (ten revolutions a second); this
// leaves room for a fixture that exaggerates and still catches a broken one.
inline constexpr double kMaxBallSpin = 500.0;  // rad/s

// The part of a match being played (docs/match-clock.md).
enum class MatchPeriod : std::uint8_t {
  kFirstHalf,
  kSecondHalf,
  kFullTime,
};

// "firstHalf", "secondHalf", "fullTime"; "unknown" outside the enumerators.
[[nodiscard]] std::string_view matchPeriodName(MatchPeriod period) noexcept;

// Where the match clock stands (docs/match-clock.md): the period, the tick it
// started at, the real seconds of play lost in it so far, and the added time
// announced once its regulation time was up. At full time it keeps the second
// half's.
struct MatchClock {
  MatchPeriod period = MatchPeriod::kFirstHalf;
  SimCore::SimTick periodStart = SimCore::SimTick(0);
  double lostSeconds = 0.0;
  std::optional<int> addedMinutes = std::nullopt;

  friend bool operator==(const MatchClock&, const MatchClock&) = default;
};

// A play the offside rule watches (docs/match-engine.md#offside): who of
// `side` played the ball when, the offside line then as a depth from that
// side's goal line, and the players of the side who stood in an offside
// position, one bit per index in MatchState::players().
struct OffsideWatch {
  TeamSide side = TeamSide::kHome;
  SimCore::PlayerId passer;
  SimCore::SimTick tick;
  double line = 0.0;
  std::uint64_t offsidePlayers = 0;

  friend bool operator==(const OffsideWatch&, const OffsideWatch&) = default;
};

// The goals each side has scored.
struct Score {
  int home = 0;
  int away = 0;

  [[nodiscard]] int of(const TeamSide side) const noexcept {
    return side == TeamSide::kHome ? home : away;
  }

  friend bool operator==(const Score&, const Score&) = default;
};

enum class MatchStateErrorCode : std::uint8_t {
  kInvalidPlayersPerSide,
  kWrongPlayerCountPerSide,
  kInvalidTeamSide,
  kInvalidPlayerId,
  kDuplicatePlayerId,
  kInvalidPlayerAttributes,
  kNonFinitePlayerPosition,
  kPlayerOutsidePitch,
  kNonFinitePlayerVelocity,
  kPlayerTooFast,
  kNonFinitePlayerTarget,
  kInvalidPlayerFacing,
  kInvalidPlayerFitness,
  kNonFiniteBallPosition,
  kBallOutsidePitch,
  kNonFiniteBallVelocity,
  kBallTooFast,
  kNonFiniteBallHeight,
  kBallBelowGround,
  kNonFiniteBallVerticalVelocity,
  kBallVerticallyTooFast,
  kNonFiniteBallSpin,
  kBallSpinningTooFast,
  kUnknownBallOwner,
  kUnknownLastTouch,
  kHeldBallWithoutOwner,
  kHeldBallByOutfielder,
  kTouchedBallWithoutOwner,
  kInvalidCarryTouch,
  kTacticDoesNotFitSquad,
  kInvalidScore,
};

// The code is what tests and callers branch on; the message names the offending
// player index, id and value so a rejected fixture can be fixed without a
// debugger.
struct MatchStateError {
  MatchStateErrorCode code = MatchStateErrorCode::kInvalidPlayersPerSide;
  std::string message;

  friend bool operator==(const MatchStateError&, const MatchStateError&) = default;
};

// Unvalidated input for MatchState::create(). An aggregate rather than a
// parameter list, so every field is named at the call site and no two
// same-typed arguments can be swapped.
struct MatchStateSpec {
  Pitch pitch;
  std::vector<PlayerMatchState> players;
  BallState ball;
  int playersPerSide = kDefaultPlayersPerSide;
  // The score the match stands at; neither side's may be negative.
  Score score{};
};

// The tactic each side plays, if any (docs/tactics.md#tactic-model). A side
// without a tactic is scripted: its players move only on commands and after
// free balls, as in the P1 sandbox. The player in slot i of a tactic is the
// i-th player of that side in MatchState::players(), see slotIndex().
struct TeamTactics {
  std::optional<SimTactics::Tactic> home;
  std::optional<SimTactics::Tactic> away;

  [[nodiscard]] const std::optional<SimTactics::Tactic>& of(const TeamSide side) const noexcept {
    return side == TeamSide::kHome ? home : away;
  }

  friend bool operator==(const TeamTactics&, const TeamTactics&) = default;
};

// The last pass played in the match: who kicked it, from where, when, and
// for whom. Pressing triggers read it (docs/team-tactics.md#pressing).
struct PassRecord {
  SimCore::PlayerId passer;
  SimCore::Vec2 from;
  SimCore::SimTick tick;
  std::optional<SimCore::PlayerId> receiver;

  friend bool operator==(const PassRecord&, const PassRecord&) = default;
};

// The last shot taken in the match: who struck it, from where, and when. A free
// ball whose last touch was a shot, a deflection of one or the keeper's parry
// is loose, not a pass. deflection is the last outfield player the shot came
// off, and resolved whether its outcome has been recorded: a shot stays open
// until the ball crosses the goal line, a player controls it, it leaves the
// pitch or comes to rest (docs/on-ball.md#shooting). parry is the keeper's last
// touch of it, and saved whether he parried it on its way into his goal
// (docs/goalkeeper.md#shot-stopping).
struct ShotRecord {
  SimCore::PlayerId shooter;
  SimCore::Vec2 from;
  SimCore::SimTick tick;
  std::optional<BallTouch> deflection = std::nullopt;
  bool resolved = false;
  std::optional<BallTouch> parry = std::nullopt;
  bool saved = false;

  friend bool operator==(const ShotRecord&, const ShotRecord&) = default;
};

// The last time a player gained control of a free ball: who, when, and how
// fast the ball was coming -- a hard ball is hard to control. passer is the
// teammate whose pass it was, and nobody for a ball intercepted or loose.
struct ReceptionRecord {
  SimCore::PlayerId player;
  SimCore::SimTick tick;
  double ballSpeed = 0.0;
  std::optional<SimCore::PlayerId> passer = std::nullopt;

  friend bool operator==(const ReceptionRecord&, const ReceptionRecord&) = default;
};

// The last goal scored in the match: the side it counts for, who scored it --
// nobody, for a ball no player had touched -- and when. The restart after it
// is a kickoff (docs/match-engine.md#restarts).
struct GoalRecord {
  TeamSide side = TeamSide::kHome;
  std::optional<SimCore::PlayerId> scorer;
  SimCore::SimTick tick;

  friend bool operator==(const GoalRecord&, const GoalRecord&) = default;
};

// Which team has the ball, as the tactical phase system last saw it
// (docs/team-tactics.md#match-phases): the owner's side, or while the ball is
// free the side of its last touch -- a pass in flight still belongs to the
// passer's team. since is the tick the team won it; fromOpponent whether it
// took the ball from the other team rather than from nobody, as at kickoff.
// Empty team until anyone has touched the ball.
struct TeamPossession {
  std::optional<TeamSide> team;
  SimCore::SimTick since;
  bool fromOpponent = false;

  friend bool operator==(const TeamPossession&, const TeamPossession&) = default;
};

// The tactical phase a team is in and the tick it entered it.
struct TeamPhase {
  SimTactics::TacticalPhase phase = SimTactics::TacticalPhase::kDefensiveBlock;
  SimCore::SimTick since;

  friend bool operator==(const TeamPhase&, const TeamPhase&) = default;
};

// A pass a player has decided on and not yet played: the what, decided apart
// from the how well (docs/on-ball.md#passing). The ball system executes it in
// the next step it runs, if the passer still owns the ball, and discards it
// otherwise.
struct PassIntent {
  SimCore::PlayerId passer;
  // Where the pass should go, in meters.
  SimCore::Vec2 target;
  // How fast the ball should leave the foot, in meters per second.
  double speed = 0.0;
  // Who the pass is meant for; empty for a pass into space.
  std::optional<SimCore::PlayerId> receiver;
  // Whether it goes through the air: speed is then the ball's speed along
  // the ground, and the flight brings it down on the target
  // (docs/goalkeeper.md#distribution). A ground pass otherwise.
  bool lofted = false;
  // For a cross, the area of the box it is aimed at (docs/on-ball.md#crossing):
  // in the air it is struck like a shot, arriving at `height` along the arc
  // of `curvature` (radians per meter, positive bending left); a cut-back is
  // a ground pass.
  std::optional<CrossArea> cross = std::nullopt;
  double height = 0.0;
  double curvature = 0.0;

  friend bool operator==(const PassIntent&, const PassIntent&) = default;
};

// A shot a player has decided on and not yet taken: where he aims, decided
// apart from how well he strikes it (docs/on-ball.md#shot-decisions). The ball
// system takes it in the next step it runs, if the shooter still owns the ball,
// and discards it otherwise.
struct ShotIntent {
  SimCore::PlayerId shooter;
  // Where he aims: a point on the goal line, and a height above the ground.
  SimCore::Vec2 target;
  double height = 0.0;
  // How fast the ball should leave the foot, in meters per second.
  double speed = 0.0;
  // How he strikes it, and for a curled shot the curvature of its path in
  // radians per meter, positive bending left (docs/on-ball.md#shot-types).
  ShotType type = ShotType::kDriven;
  double curvature = 0.0;

  friend bool operator==(const ShotIntent&, const ShotIntent&) = default;
};

// A touch a player on the ball has decided on and not yet played: to turn,
// carry the ball or dribble past an opponent (docs/on-ball.md#ball-carrying).
// The ball system plays it in the next step it runs, if he still has the ball
// at his feet, and discards it otherwise.
struct CarryIntent {
  SimCore::PlayerId carrier;
  // Where the touch should take the ball, in meters.
  SimCore::Vec2 target;
  // How fast the ball should leave the foot, in meters per second.
  double speed = 0.0;
  // When he plays it again at the earliest, in seconds after the touch.
  double seconds = 0.0;
  // The touch's error, as a factor on a pass's: his ball control and, for a
  // dribble, his dribbling.
  double errorFactor = 1.0;

  friend bool operator==(const CarryIntent&, const CarryIntent&) = default;
};

// What a player has decided and not yet done: an action waiting for the
// system that carries it out. Each player has his own; so far only the player
// on the ball decides one.
using PendingAction = std::variant<PassIntent, ShotIntent, CarryIntent>;

class MatchSimulation;
class MatchStateWriter;

// A validated match state: two teams, their players, and one ball on a pitch.
// Nothing here advances time -- MatchSimulation owns that.
//
// The invariants hold for every state, from kickoff to the final whistle:
// both squads have the stated size, every player has a unique valid id, a
// declared side, valid attributes and a unit facing vector, every
// position, velocity and target is finite, the ball's height is finite and not
// below the ground, and the ball belongs to no one or
// to a player in the state. Being on the
// pitch is deliberately not one of them: a ball that crossed the touchline or
// a player standing behind the goal line is football, not a broken state.
// checkStartingPositions() holds that rule for states a match starts from.
//
// Player order is part of the state. Two states holding the same players in a
// different order are not equal, because update order has to stay fixed for
// replays to reproduce.
class MatchState {
 public:
  // The only way to obtain a MatchState from outside the simulation, so every
  // instance that exists is valid. Reports every rule the spec breaks, not just
  // the first one, in a fixed order: squad size, then players by index, then
  // the ball, then the tactics, then the score. A tactic must have one slot per
  // player of its side.
  [[nodiscard]] static std::expected<MatchState, std::vector<MatchStateError>> create(
      MatchStateSpec spec, TeamTactics tactics = {});

  [[nodiscard]] const Pitch& pitch() const noexcept { return pitch_; }
  [[nodiscard]] std::span<const PlayerMatchState> players() const noexcept { return players_; }
  [[nodiscard]] const BallState& ball() const noexcept { return ball_; }
  [[nodiscard]] int playersPerSide() const noexcept { return playersPerSide_; }

  // The goals both sides have scored, and the last of them; none in a state
  // created from a spec, whatever score it starts at.
  [[nodiscard]] const Score& score() const noexcept { return score_; }
  [[nodiscard]] const std::optional<GoalRecord>& lastGoal() const noexcept { return lastGoal_; }
  // A new state stands at the start of the first half.
  [[nodiscard]] const MatchClock& clock() const noexcept { return clock_; }
  // The play the offside rule watches, if any, and the touch -- a throw-in's,
  // a goal kick's or a corner's -- whose next play is free of it.
  [[nodiscard]] const std::optional<OffsideWatch>& offside() const noexcept { return offside_; }
  [[nodiscard]] const std::optional<BallTouch>& offsideExemption() const noexcept {
    return offsideExemption_;
  }

  // The last pass kicked, the last shot taken and the last reception of a
  // free ball; empty in every state created from a spec, until the ball
  // system records one.
  [[nodiscard]] const std::optional<PassRecord>& lastPass() const noexcept { return lastPass_; }
  [[nodiscard]] const std::optional<ShotRecord>& lastShot() const noexcept { return lastShot_; }
  [[nodiscard]] const std::optional<ReceptionRecord>& lastReception() const noexcept {
    return lastReception_;
  }

  // The tactics both sides play; a side without one is scripted.
  [[nodiscard]] const TeamTactics& tactics() const noexcept { return tactics_; }

  // Which team has the ball; no team in every state created from a spec.
  [[nodiscard]] const TeamPossession& possession() const noexcept { return possession_; }

  // The tactical runtime state of the player at this index in players():
  // his desired region and, with later systems, his chosen action. Empty for
  // every player of a state created from a spec. Throws std::out_of_range
  // past the end.
  [[nodiscard]] const PlayerTacticalState& tactical(std::size_t playerIndex) const {
    return tactical_.at(playerIndex);
  }

  // The side's press in progress, if any (docs/team-tactics.md#pressing).
  [[nodiscard]] const std::optional<TeamPress>& press(const TeamSide side) const noexcept {
    return side == TeamSide::kHome ? presses_[0] : presses_[1];
  }

  // The player each side has sent after the free ball, if any
  // (docs/ball.md#reception-interceptions-and-loose-balls). The ball pursuit
  // system owns his movement target while he chases; no other system writes it.
  [[nodiscard]] const std::optional<SimCore::PlayerId>& chaser(const TeamSide side) const noexcept {
    return side == TeamSide::kHome ? chasers_[0] : chasers_[1];
  }

  // The pitch-control grid as last refreshed
  // (docs/team-tactics.md#pitch-control); empty in every state created from a
  // spec, until the pitch-control system has run.
  [[nodiscard]] const std::optional<PitchControlGrid>& pitchControl() const noexcept {
    return pitchControl_;
  }

  // The phase of a side with a tactic; empty for a scripted side and in every
  // state created from a spec, until the phase system has run.
  [[nodiscard]] const std::optional<TeamPhase>& phase(const TeamSide side) const noexcept {
    return side == TeamSide::kHome ? phases_[0] : phases_[1];
  }

  // What the player at this index has decided and not yet done; empty almost
  // always, and for every player of a state created from a spec. Throws
  // std::out_of_range past the end.
  [[nodiscard]] const std::optional<PendingAction>& pendingAction(std::size_t playerIndex) const {
    return pendingActions_.at(playerIndex);
  }

  // The pass waiting to be played: the first pending pass in player order;
  // empty almost always.
  [[nodiscard]] std::optional<PassIntent> pendingPass() const noexcept;

  // The memory of the player at this index in players(); throws
  // std::out_of_range past the end. Every state created from a spec starts
  // with empty memories: perception is built up by the match, not given.
  [[nodiscard]] const PlayerPerception& perception(std::size_t playerIndex) const {
    return perceptions_.at(playerIndex);
  }

  friend bool operator==(const MatchState&, const MatchState&) = default;

 private:
  friend class MatchStateWriter;

  MatchState(MatchStateSpec spec, TeamTactics tactics);

  Pitch pitch_;
  std::vector<PlayerMatchState> players_;
  BallState ball_;
  int playersPerSide_;
  TeamTactics tactics_;
  // Parallel to players_.
  std::vector<PlayerPerception> perceptions_;
  // Parallel to players_.
  std::vector<std::optional<PendingAction>> pendingActions_;
  std::optional<PassRecord> lastPass_;
  std::optional<ShotRecord> lastShot_;
  std::optional<ReceptionRecord> lastReception_;
  Score score_;
  std::optional<GoalRecord> lastGoal_;
  MatchClock clock_;
  std::optional<OffsideWatch> offside_;
  std::optional<BallTouch> offsideExemption_;
  TeamPossession possession_;
  // Home, away.
  std::array<std::optional<TeamPhase>, 2> phases_;
  std::optional<PitchControlGrid> pitchControl_;
  // Home, away.
  std::array<std::optional<SimCore::PlayerId>, 2> chasers_;
  // Parallel to players_.
  std::vector<PlayerTacticalState> tactical_;
  // Home, away.
  std::array<std::optional<TeamPress>, 2> presses_;
};

// What a simulation system or command may change in a state: positions,
// velocities, movement targets, facings, fitness, perception memories, the ball's
// height, vertical velocity and spin, who owns and
// last touched the ball, the players' pending actions, the last pass and shot and reception, the
// score, the match clock, the offside watch, team possession and phases, the
// pitch-control grid, the chasers, presses and players' tactical states,
// nothing else. Squad, ids, sides,
// attributes, player order and the pitch have no setter, so a system cannot break those invariants
// and nothing has to re-check them every tick. Players are addressed by their index in
// MatchState::players(); an index past the end throws std::out_of_range.
//
// Only MatchSimulation hands out writers, and a writer only lives for one step.
class MatchStateWriter {
 public:
  void setPlayerPosition(std::size_t playerIndex, SimCore::Vec2 position);
  void setPlayerVelocity(std::size_t playerIndex, SimCore::Vec2 velocity);
  void setPlayerTarget(std::size_t playerIndex, std::optional<SimCore::Vec2> target);
  // The facing must be a unit vector; the loop rejects any other.
  void setPlayerFacing(std::size_t playerIndex, SimCore::Vec2 facing);
  // The loop rejects a fitness outside its ranges (PlayerFitness).
  void setPlayerFitness(std::size_t playerIndex, const PlayerFitness& fitness);
  // The memory of the player at this index, to update in place.
  [[nodiscard]] PlayerPerception& perception(std::size_t playerIndex);
  // The ball as every system so far this step has left it, unlike
  // MatchState::ball(), which stays frozen at the step's start for every
  // system's current parameter; the ball-state writer reads this to see an
  // owner a duel already assigned this same step, and the movement system
  // where a duel rolled the ball.
  [[nodiscard]] const BallState& ball() const noexcept { return state_->ball_; }
  // The action the player at this index waits to do, as this step has left
  // it so far; see ball() for why this differs from
  // MatchState::pendingAction().
  [[nodiscard]] const std::optional<PendingAction>& pendingAction(std::size_t playerIndex) const {
    return state_->pendingActions_.at(playerIndex);
  }
  void setBallPosition(SimCore::Vec2 position) noexcept { state_->ball_.position = position; }
  void setBallVelocity(SimCore::Vec2 velocity) noexcept { state_->ball_.velocity = velocity; }
  // The third dimension and the spin. A negative height is below the ground
  // and is rejected by the loop's checks like a non-finite one.
  void setBallHeight(double height) noexcept { state_->ball_.height = height; }
  void setBallVerticalVelocity(double verticalVelocity) noexcept {
    state_->ball_.verticalVelocity = verticalVelocity;
  }
  void setBallSpin(double spin) noexcept { state_->ball_.spin = spin; }
  void setBallSideSpin(double sideSpin) noexcept { state_->ball_.sideSpin = sideSpin; }
  // Hands the ball to a player, or frees it with std::nullopt -- at his feet
  // either way, ending any touch: setBallHeld() puts it in his hands. Throws
  // std::invalid_argument for an id no player in the state has, so the ball
  // can only ever belong to a player on the pitch.
  void setBallOwner(std::optional<SimCore::PlayerId> owner);
  // Puts the owned ball in its owner's hands, or back at his feet; throws
  // std::invalid_argument for holding a ball nobody owns or an outfield
  // player owns, or one he has touched on.
  void setBallHeld(bool held);
  // Plays the owned ball ahead of its owner with a touch, or ends the touch
  // with std::nullopt once he has it at his feet again; throws
  // std::invalid_argument for a ball nobody owns or one held in the hands.
  void setBallTouch(std::optional<CarryTouch> touch);
  // Records who last played the ball; throws std::invalid_argument for a
  // player not in the state.
  void setBallLastTouch(std::optional<BallTouch> touch);
  // Sets or clears the action the player at this index waits to do; throws
  // std::invalid_argument for an action another player would do or one
  // naming a player not in the state.
  void setPendingAction(std::size_t playerIndex, std::optional<PendingAction> action);
  // Throw std::invalid_argument for a player not in the state.
  void setLastPass(std::optional<PassRecord> pass);
  // The last pass as this step has left it so far; see ball() for why this
  // differs from MatchState::lastPass().
  [[nodiscard]] const std::optional<PassRecord>& lastPass() const noexcept {
    return state_->lastPass_;
  }
  void setLastShot(std::optional<ShotRecord> shot);
  // The last shot as this step has left it so far; see ball() for why this
  // differs from MatchState::lastShot().
  [[nodiscard]] const std::optional<ShotRecord>& lastShot() const noexcept {
    return state_->lastShot_;
  }
  void setLastReception(std::optional<ReceptionRecord> reception);
  // The clock as this step has left it so far.
  [[nodiscard]] const MatchClock& clock() const noexcept { return state_->clock_; }
  void setClock(const MatchClock& clock) noexcept { state_->clock_ = clock; }
  // Adds real seconds of play lost to a stoppage to the period's lost time.
  void addLostSeconds(const double seconds) noexcept { state_->clock_.lostSeconds += seconds; }
  void setOffside(const std::optional<OffsideWatch>& watch) noexcept { state_->offside_ = watch; }
  void setOffsideExemption(const std::optional<BallTouch>& touch) noexcept {
    state_->offsideExemption_ = touch;
  }
  // Counts a goal for the record's side and keeps the record as the last
  // goal; throws std::invalid_argument for a scorer not in the state.
  void addGoal(const GoalRecord& goal);
  void setPossession(const TeamPossession& possession) noexcept {
    state_->possession_ = possession;
  }
  // Throws std::invalid_argument for a phase given to a side without a
  // tactic: only a tactic says what a phase means.
  void setPhase(TeamSide side, std::optional<TeamPhase> phase);
  // The tactical state of the player at this index, to update in place.
  [[nodiscard]] PlayerTacticalState& tactical(std::size_t playerIndex) {
    return state_->tactical_.at(playerIndex);
  }
  // Throws std::invalid_argument for a press by a side without a tactic or
  // naming a player not in the state.
  void setPress(TeamSide side, std::optional<TeamPress> press);
  // Throws std::invalid_argument for a player not on that side.
  void setChaser(TeamSide side, std::optional<SimCore::PlayerId> chaser);
  // Throws std::invalid_argument for a tactic that does not fit the squad.
  // A held ball drops to the feet of a holder the new tactic no longer makes
  // the goalkeeper.
  void setTactic(TeamSide side, SimTactics::Tactic tactic);
  void setPitchControl(std::optional<PitchControlGrid> grid) {
    state_->pitchControl_ = std::move(grid);
  }
  // Moves out the grid cached from the previous refresh, leaving the state
  // without one until setPitchControl() is called; lets a refresh recycle
  // its storage instead of allocating a fresh grid every time.
  [[nodiscard]] std::optional<PitchControlGrid> takePitchControl() noexcept {
    return std::move(state_->pitchControl_);
  }

 private:
  friend class MatchSimulation;

  explicit MatchStateWriter(MatchState& state) noexcept : state_(&state) {}

  // Throws std::invalid_argument naming the role if no player has the id.
  void requirePlayer(SimCore::PlayerId playerId, std::string_view role) const;
  // Whether the ball has an owner and he is his side's goalkeeper.
  [[nodiscard]] bool ownerGuardsGoal() const;

  MatchState* state_;
};

// The index of the player with this id in MatchState::players(), or nothing
// if no player has it.
[[nodiscard]] std::optional<std::size_t> findPlayerIndex(const MatchState& state,
                                                         SimCore::PlayerId playerId) noexcept;

// The player's slot in his side's tactic: how many players of his side come
// before him in MatchState::players(). Throws std::out_of_range for an index
// past the end.
[[nodiscard]] std::size_t slotIndex(const MatchState& state, std::size_t playerIndex);

// Whether the player at this index guards his side's goal: his slot holds
// guardGoal in his side's tactic. Lets MatchState::create() and the writer
// tell the goalkeeper, the only player who may hold the ball, like
// isGoalkeeper() does.
[[nodiscard]] bool guardsGoal(std::span<const PlayerMatchState> players, const TeamTactics& tactics,
                              std::size_t playerIndex);

// The finiteness rules of MatchState::create(), for a state the simulation
// has just written: every non-finite position, velocity and target, every
// facing that is not a finite unit vector, and a ball height, vertical
// velocity or spin that is not finite or a ball below the ground, players by
// index
// first and then the ball, with the same codes and messages create() uses.
// Empty for a state without defects.
[[nodiscard]] std::vector<MatchStateError> findNonFiniteValues(const MatchState& state);

// The extra rule for a state a match starts from, such as a kickoff fixture:
// every player and the ball stand on the pitch, edges included. Returns one
// error per offender, players by index first and then the ball, and nothing
// for a state that complies.
[[nodiscard]] std::vector<MatchStateError> checkStartingPositions(const MatchState& state);

}  // namespace ElyverseFootball::SimMatch
