#pragma once

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
#include "simTime.hpp"
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
  // the ball.
  [[nodiscard]] static std::expected<MatchState, std::vector<MatchStateError>> create(
      MatchStateSpec spec);

  [[nodiscard]] const Pitch& pitch() const noexcept { return pitch_; }
  [[nodiscard]] std::span<const PlayerMatchState> players() const noexcept { return players_; }
  [[nodiscard]] const BallState& ball() const noexcept { return ball_; }
  [[nodiscard]] int playersPerSide() const noexcept { return playersPerSide_; }

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

  explicit MatchState(MatchStateSpec spec);

  Pitch pitch_;
  std::vector<PlayerMatchState> players_;
  BallState ball_;
  int playersPerSide_;
  // Parallel to players_.
  std::vector<PlayerPerception> perceptions_;
  std::optional<PassIntent> pendingPass_;
};

// What a simulation system or command may change in a state: positions,
// velocities, movement targets, facings, perception memories, who owns and
// last touched the ball, and the pending pass, nothing else. Squad, ids, sides,
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
