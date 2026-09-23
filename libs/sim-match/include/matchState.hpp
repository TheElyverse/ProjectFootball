#pragma once

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "ids.hpp"
#include "pitch.hpp"
#include "vec2.hpp"

namespace ElyverseFootball::SimMatch {

// Which end a player belongs to. The pitch coordinate system is fixed (see
// docs/match-geometry.md), so this says nothing about attacking direction --
// a scenario decides which side defends x = 0.
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

// One player as the match simulation sees it. Positions are in meters in pitch
// coordinates, velocities in meters per second. Deliberately minimal: the
// orientation, energy, action and perception components of
// docs/implementation-plan.md section 6.2 arrive with the systems that fill
// them.
struct PlayerMatchState {
  SimCore::PlayerId playerId;
  TeamSide side = TeamSide::kHome;
  SimCore::Vec2 position;
  SimCore::Vec2 velocity;

  friend bool operator==(const PlayerMatchState&, const PlayerMatchState&) = default;
};

// Position in meters, velocity in meters per second, both in the pitch plane.
// The third dimension and spin (section 6.7) arrive with the passing model.
struct BallState {
  SimCore::Vec2 position;
  SimCore::Vec2 velocity;

  friend bool operator==(const BallState&, const BallState&) = default;
};

enum class MatchStateErrorCode : std::uint8_t {
  kInvalidPlayersPerSide,
  kWrongPlayerCountPerSide,
  kInvalidTeamSide,
  kInvalidPlayerId,
  kDuplicatePlayerId,
  kNonFinitePlayerPosition,
  kPlayerOutsidePitch,
  kNonFinitePlayerVelocity,
  kNonFiniteBallPosition,
  kBallOutsidePitch,
  kNonFiniteBallVelocity,
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

// A validated match state: two teams, their players, and one ball on a pitch.
// Nothing here advances time -- MatchSimulation owns that.
//
// The invariants hold for every state, from kickoff to the final whistle:
// both squads have the stated size, every player has a unique valid id and a
// declared side, and every position and velocity is finite. Being on the
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

  friend bool operator==(const MatchState&, const MatchState&) = default;

 private:
  explicit MatchState(MatchStateSpec spec);

  Pitch pitch_;
  std::vector<PlayerMatchState> players_;
  BallState ball_;
  int playersPerSide_;
};

// The extra rule for a state a match starts from, such as a kickoff fixture:
// every player and the ball stand on the pitch, edges included. Returns one
// error per offender, players by index first and then the ball, and nothing
// for a state that complies.
[[nodiscard]] std::vector<MatchStateError> checkStartingPositions(const MatchState& state);

}  // namespace ElyverseFootball::SimMatch
