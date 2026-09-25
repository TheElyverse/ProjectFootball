#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "ids.hpp"
#include "observation.hpp"
#include "pitch.hpp"
#include "pitchControlGrid.hpp"
#include "simTime.hpp"
#include "tactic.hpp"
#include "tacticalPhase.hpp"
#include "tacticalState.hpp"
#include "teamPress.hpp"
#include "vec2.hpp"

namespace ElyverseFootball::SimMatch {

// Which squad a player belongs to. The pitch coordinate system is fixed (see
// docs/match-geometry.md), so this is not itself an attacking direction. The
// sandbox has no halves yet: home defends x = 0 in every match, and systems
// that need the direction ask attackingDirection() (passCandidates.hpp), the
// one place to change once rules let teams switch ends.
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

// Squad size per side used when a caller does not state one. Seven-a-side is
// the M0 iteration stage (docs/implementation-plan.md section 6.1); the value
// is a default, not a rule built into the types.
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

// What a player's body allows, fixed for a match. Both values must be positive
// and finite. The same acceleration limits speeding up, slowing down and
// turning (see docs/player-movement.md).
struct PlayerAttributes {
  double maxSpeed = kDefaultMaxSpeed;
  double acceleration = kDefaultAcceleration;

  friend bool operator==(const PlayerAttributes&, const PlayerAttributes&) = default;
};

// One player as the match simulation sees it. Positions are in meters in pitch
// coordinates, velocities in meters per second. Deliberately minimal: the
// orientation, energy, action and perception components of
// docs/implementation-plan.md section 6.2 arrive with the systems that fill
// them.
//
// target is where the player is moving to, assigned by a command and kept on
// the pitch; without one the player comes to a stop where he is.
//
// facing is the unit vector the player looks along, which decides what he
// can see (docs/perception.md). It is a vector rather than an angle so that
// no trigonometry, and none of its platform differences, enters the state.
struct PlayerMatchState {
  SimCore::PlayerId playerId;
  TeamSide side = TeamSide::kHome;
  SimCore::Vec2 position;
  SimCore::Vec2 velocity;
  PlayerAttributes attributes;
  std::optional<SimCore::Vec2> target;
  SimCore::Vec2 facing{.x = 1.0, .y = 0.0};

  friend bool operator==(const PlayerMatchState&, const PlayerMatchState&) = default;
};

// Position in meters, velocity in meters per second, both in the pitch plane.
// The third dimension and spin (section 6.7) arrive with the passing model.
//
// Who last played the ball, and in which tick.
struct BallTouch {
  SimCore::PlayerId playerId;
  SimCore::SimTick tick;

  friend bool operator==(const BallTouch&, const BallTouch&) = default;
};

// owner is the player in control of the ball; empty while the ball is free.
// One optional id rather than a flag per player, so the ball can never have
// two owners. A controlled ball follows its owner, a free ball rolls on its
// own (docs/possession.md). lastTouch is the last player to kick or take the
// ball; empty until someone has.
struct BallState {
  SimCore::Vec2 position;
  SimCore::Vec2 velocity;
  std::optional<SimCore::PlayerId> owner;
  std::optional<BallTouch> lastTouch;

  [[nodiscard]] bool isControlled() const noexcept { return owner.has_value(); }

  friend bool operator==(const BallState&, const BallState&) = default;
};

// No kicked football gets near this; the hardest shots are around 60 m/s. A
// faster ball is a broken fixture, and would overflow when its speed is
// squared.
inline constexpr double kMaxBallSpeed = 100.0;  // m/s

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
  kNonFiniteBallPosition,
  kBallOutsidePitch,
  kNonFiniteBallVelocity,
  kBallTooFast,
  kUnknownBallOwner,
  kUnknownLastTouch,
  kTacticDoesNotFitSquad,
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
};

// The tactic each side plays, if any (docs/tactics.md). A side without a
// tactic is scripted: its players move only on commands and after free
// balls, as in the P1 sandbox. The player in slot i of a tactic is the i-th
// player of that side in MatchState::players(), see slotIndex().
struct TeamTactics {
  std::optional<SimTactics::Tactic> home;
  std::optional<SimTactics::Tactic> away;

  [[nodiscard]] const std::optional<SimTactics::Tactic>& of(const TeamSide side) const noexcept {
    return side == TeamSide::kHome ? home : away;
  }

  friend bool operator==(const TeamTactics&, const TeamTactics&) = default;
};

// The last pass played in the match: who kicked it, from where, when, and
// for whom. Pressing triggers read it (docs/pressing.md).
struct PassRecord {
  SimCore::PlayerId passer;
  SimCore::Vec2 from;
  SimCore::SimTick tick;
  std::optional<SimCore::PlayerId> receiver;

  friend bool operator==(const PassRecord&, const PassRecord&) = default;
};

// The last time a player gained control of a free ball: who, when, and how
// fast the ball was coming -- a hard ball is hard to control.
struct ReceptionRecord {
  SimCore::PlayerId player;
  SimCore::SimTick tick;
  double ballSpeed = 0.0;

  friend bool operator==(const ReceptionRecord&, const ReceptionRecord&) = default;
};

// Which team has the ball, as the tactical phase system last saw it
// (docs/match-phases.md): the owner's side, or while the ball is free the side
// of its last touch -- a pass in flight still belongs to the passer's team.
// since is the tick the team won it; fromOpponent whether it took the ball
// from the other team rather than from nobody, as at kickoff. Empty team
// until anyone has touched the ball.
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
// from the how well (docs/passing.md). The ball system executes it in the
// next step it runs, if the passer still owns the ball, and discards it
// otherwise.
struct PassIntent {
  SimCore::PlayerId passer;
  // Where the pass should go, in meters.
  SimCore::Vec2 target;
  // How fast the ball should leave the foot, in meters per second.
  double speed = 0.0;
  // Who the pass is meant for; empty for a pass into space.
  std::optional<SimCore::PlayerId> receiver;

  friend bool operator==(const PassIntent&, const PassIntent&) = default;
};

class MatchSimulation;
class MatchStateWriter;

// A validated match state: two teams, their players, and one ball on a pitch.
// Nothing here advances time -- MatchSimulation owns that.
//
// The invariants hold for every state, from kickoff to the final whistle:
// both squads have the stated size, every player has a unique valid id, a
// declared side, positive finite attributes and a unit facing vector, every
// position, velocity and target is finite, and the ball belongs to no one or
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
  // the ball, then the tactics. A tactic must have one slot per player of its
  // side.
  [[nodiscard]] static std::expected<MatchState, std::vector<MatchStateError>> create(
      MatchStateSpec spec, TeamTactics tactics = {});

  [[nodiscard]] const Pitch& pitch() const noexcept { return pitch_; }
  [[nodiscard]] std::span<const PlayerMatchState> players() const noexcept { return players_; }
  [[nodiscard]] const BallState& ball() const noexcept { return ball_; }
  [[nodiscard]] int playersPerSide() const noexcept { return playersPerSide_; }

  // The last pass kicked and the last reception of a free ball; empty in
  // every state created from a spec, until the ball system records one.
  [[nodiscard]] const std::optional<PassRecord>& lastPass() const noexcept { return lastPass_; }
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

  // The side's press in progress, if any (docs/pressing.md).
  [[nodiscard]] const std::optional<TeamPress>& press(const TeamSide side) const noexcept {
    return side == TeamSide::kHome ? presses_[0] : presses_[1];
  }

  // The player each side has sent after the free ball, if any
  // (docs/reception.md). The ball pursuit system owns his movement target
  // while he chases; no other system writes it.
  [[nodiscard]] const std::optional<SimCore::PlayerId>& chaser(const TeamSide side) const noexcept {
    return side == TeamSide::kHome ? chasers_[0] : chasers_[1];
  }

  // The pitch-control grid as last refreshed (docs/pitch-control.md); empty
  // in every state created from a spec, until the pitch-control system has
  // run.
  [[nodiscard]] const std::optional<PitchControlGrid>& pitchControl() const noexcept {
    return pitchControl_;
  }

  // The phase of a side with a tactic; empty for a scripted side and in every
  // state created from a spec, until the phase system has run.
  [[nodiscard]] const std::optional<TeamPhase>& phase(const TeamSide side) const noexcept {
    return side == TeamSide::kHome ? phases_[0] : phases_[1];
  }

  // The pass decided and waiting to be played; empty almost always. Every
  // state created from a spec starts without one.
  [[nodiscard]] const std::optional<PassIntent>& pendingPass() const noexcept {
    return pendingPass_;
  }

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
  std::optional<PassIntent> pendingPass_;
  std::optional<PassRecord> lastPass_;
  std::optional<ReceptionRecord> lastReception_;
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
// velocities, movement targets, facings, perception memories, who owns and
// last touched the ball, the pending pass, the last pass and reception, team
// possession and phases, the
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
  // The memory of the player at this index, to update in place.
  [[nodiscard]] PlayerPerception& perception(std::size_t playerIndex);
  void setBallPosition(SimCore::Vec2 position) noexcept { state_->ball_.position = position; }
  void setBallVelocity(SimCore::Vec2 velocity) noexcept { state_->ball_.velocity = velocity; }
  // Hands the ball to a player, or frees it with std::nullopt. Throws
  // std::invalid_argument for an id no player in the state has, so the ball
  // can only ever belong to a player on the pitch.
  void setBallOwner(std::optional<SimCore::PlayerId> owner);
  // Records who last played the ball; throws std::invalid_argument for a
  // player not in the state.
  void setBallLastTouch(std::optional<BallTouch> touch);
  // Sets or clears the pass waiting to be played; throws
  // std::invalid_argument for a passer or receiver not in the state.
  void setPendingPass(std::optional<PassIntent> pass);
  // Throw std::invalid_argument for a player not in the state.
  void setLastPass(std::optional<PassRecord> pass);
  void setLastReception(std::optional<ReceptionRecord> reception);
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
  void setTactic(TeamSide side, SimTactics::Tactic tactic);
  void setPitchControl(std::optional<PitchControlGrid> grid) {
    state_->pitchControl_ = std::move(grid);
  }

 private:
  friend class MatchSimulation;

  explicit MatchStateWriter(MatchState& state) noexcept : state_(&state) {}

  // Throws std::invalid_argument naming the role if no player has the id.
  void requirePlayer(SimCore::PlayerId playerId, std::string_view role) const;

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

// The finiteness rules of MatchState::create(), for a state the simulation
// has just written: every non-finite position, velocity and target and every
// facing that is not a finite unit vector, players by index
// first and then the ball, with the same codes and messages create() uses.
// Empty for a state without defects.
[[nodiscard]] std::vector<MatchStateError> findNonFiniteValues(const MatchState& state);

// The extra rule for a state a match starts from, such as a kickoff fixture:
// every player and the ball stand on the pitch, edges included. Returns one
// error per offender, players by index first and then the ball, and nothing
// for a state that complies.
[[nodiscard]] std::vector<MatchStateError> checkStartingPositions(const MatchState& state);

}  // namespace ElyverseFootball::SimMatch
