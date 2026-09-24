#pragma once

#include <cstdint>
#include <string>
#include <variant>

#include "ids.hpp"
#include "matchState.hpp"
#include "simTime.hpp"
#include "vec2.hpp"

namespace ElyverseFootball::SimMatch {

// Sends a player toward a position. A target off the pitch is replaced by the
// nearest point on it (Pitch::clamp()), so a movement target never leads a
// player off the field. A new target replaces the previous one.
struct MovePlayerCommand {
  SimCore::PlayerId playerId;
  SimCore::Vec2 target;

  friend bool operator==(const MovePlayerCommand&, const MovePlayerCommand&) = default;
};

// An intent from outside the systems -- a scenario script, a coach, a test --
// that changes the match state at a tick. Every command type is a
// std::variant alternative, so a replay can record and a reader can dispatch
// on them without a class hierarchy.
using MatchCommand = std::variant<MovePlayerCommand>;

// A command and the tick whose step applies it. Commands of the same tick are
// applied in the order they were scheduled.
struct ScheduledCommand {
  SimCore::SimTick tick;
  MatchCommand command;

  friend bool operator==(const ScheduledCommand&, const ScheduledCommand&) = default;
};

enum class MatchCommandErrorCode : std::uint8_t {
  kTickInPast,
  kUnknownPlayer,
  kNonFiniteTarget,
};

struct MatchCommandError {
  MatchCommandErrorCode code = MatchCommandErrorCode::kTickInPast;
  std::string message;

  friend bool operator==(const MatchCommandError&, const MatchCommandError&) = default;
};

}  // namespace ElyverseFootball::SimMatch
