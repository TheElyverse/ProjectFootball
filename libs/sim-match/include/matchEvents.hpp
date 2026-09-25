#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <variant>
#include <vector>

#include "actionCandidate.hpp"
#include "ids.hpp"
#include "matchState.hpp"
#include "observation.hpp"
#include "passCandidate.hpp"
#include "simTime.hpp"
#include "stableHash.hpp"
#include "tacticalPhase.hpp"
#include "vec2.hpp"

namespace ElyverseFootball::SimMatch {

// Domain events: immutable facts about what happened in a step, recorded by
// the systems that made them happen (docs/match-events.md). Every event
// carries the tick of its step.

// A player kicked a pass: the ball left his foot toward target.
struct PassAttempted {
  SimCore::SimTick tick;
  SimCore::PlayerId passer;
  // Who the pass was meant for; empty for a pass into space.
  std::optional<SimCore::PlayerId> intendedReceiver;
  SimCore::Vec2 from;
  SimCore::Vec2 target;
  // The speed the ball left the foot with, execution error included.
  double speed = 0.0;

  friend bool operator==(const PassAttempted&, const PassAttempted&) = default;
};

// A teammate of the passer gained control of the pass.
struct PassReceived {
  SimCore::SimTick tick;
  SimCore::PlayerId receiver;
  SimCore::PlayerId passer;

  friend bool operator==(const PassReceived&, const PassReceived&) = default;
};

// An opponent of the passer gained control of the pass.
struct PassIntercepted {
  SimCore::SimTick tick;
  SimCore::PlayerId interceptor;
  SimCore::PlayerId passer;

  friend bool operator==(const PassIntercepted&, const PassIntercepted&) = default;
};

// A player gained control of a free ball nobody had played: at kickoff, for
// example.
struct LooseBallRecovered {
  SimCore::SimTick tick;
  SimCore::PlayerId player;

  friend bool operator==(const LooseBallRecovered&, const LooseBallRecovered&) = default;
};

// The ball's owner changed: from a player to a free ball, from a free ball to
// a player, or straight from one player to another.
struct PossessionChanged {
  SimCore::SimTick tick;
  std::optional<SimCore::PlayerId> previousOwner;
  std::optional<SimCore::PlayerId> newOwner;

  friend bool operator==(const PossessionChanged&, const PossessionChanged&) = default;
};

// A team with a tactic entered a new tactical phase (docs/match-phases.md).
// previous is empty for the team's first phase.
struct PhaseChanged {
  SimCore::SimTick tick;
  TeamSide side = TeamSide::kHome;
  std::optional<SimTactics::TacticalPhase> previous;
  SimTactics::TacticalPhase phase = SimTactics::TacticalPhase::kDefensiveBlock;

  friend bool operator==(const PhaseChanged&, const PhaseChanged&) = default;
};

using MatchEvent = std::variant<PassAttempted, PassReceived, PassIntercepted, LooseBallRecovered,
                                PossessionChanged, PhaseChanged>;

// "pass attempted", "pass received", ... for logs and diagnostics.
[[nodiscard]] std::string_view eventName(const MatchEvent& event);

// The tick an event happened in.
[[nodiscard]] SimCore::SimTick eventTick(const MatchEvent& event);

// Feeds every field of an event, its type first, into a stable hash, so a
// replay can check that playback produces the same event sequence.
void addEvent(SimCore::StableHasher& hasher, const MatchEvent& event);

// What the player on the ball did with a decision.
enum class DecisionOutcome : std::uint8_t {
  kPassed,
  kNoValidOption,
};

// Why a player on the ball decided as he did: what he remembered, every
// option with its scores, and his choice. Debug output only -- collected on
// request, never hashed, never read by a system, and building it draws no
// random numbers.
struct DecisionDiagnostic {
  SimCore::SimTick tick;
  SimCore::PlayerId player;
  std::vector<Observation> observations;
  std::vector<PassCandidate> candidates;
  DecisionOutcome outcome = DecisionOutcome::kNoValidOption;
  // Index into candidates of the chosen pass; empty without one.
  std::optional<std::size_t> chosen;

  friend bool operator==(const DecisionDiagnostic&, const DecisionDiagnostic&) = default;
};

// Why a player without the ball chose his action: every option with its
// weighted scores, and his choice. Like DecisionDiagnostic, debug output
// only: collected on request, never hashed, never read by a system.
struct ActionDiagnostic {
  SimCore::SimTick tick;
  SimCore::PlayerId player;
  std::vector<ActionCandidate> candidates;
  // Index into candidates of the chosen action.
  std::optional<std::size_t> chosen;

  friend bool operator==(const ActionDiagnostic&, const ActionDiagnostic&) = default;
};

}  // namespace ElyverseFootball::SimMatch
