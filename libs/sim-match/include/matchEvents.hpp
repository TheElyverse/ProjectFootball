#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
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

// An opponent of the passer gained control of the pass, at the ball's
// position.
struct PassIntercepted {
  SimCore::SimTick tick;
  SimCore::PlayerId interceptor;
  SimCore::PlayerId passer;
  SimCore::Vec2 position;

  friend bool operator==(const PassIntercepted&, const PassIntercepted&) = default;
};

// A player gained control of a free ball nobody had played -- at kickoff, for
// example -- at the ball's position.
struct LooseBallRecovered {
  SimCore::SimTick tick;
  SimCore::PlayerId player;
  SimCore::Vec2 position;

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

// A side switched to another tactic by a ChangeTacticCommand: its name and
// content hash (tacticHash.hpp), so a log names the tactic and an analysis
// can tell two versions of a name apart.
struct TacticChanged {
  SimCore::SimTick tick;
  TeamSide side = TeamSide::kHome;
  std::string tactic;
  std::uint64_t contentHash = 0;

  friend bool operator==(const TacticChanged&, const TacticChanged&) = default;
};

// The pitch control system updated its grid (docs/pitch-control.md): home's
// share of the pitch -- away's is the rest -- and where the ball was. A
// regular sample of the match, so analyses of territory and control need
// nothing but events.
struct PitchControlSampled {
  SimCore::SimTick tick;
  double homeShare = 0.5;
  SimCore::Vec2 ball;

  friend bool operator==(const PitchControlSampled&, const PitchControlSampled&) = default;
};

// A presser won the ball from the carrier in a challenge (docs/pressing.md),
// at the ball's position.
struct BallWon {
  SimCore::SimTick tick;
  SimCore::PlayerId winner;
  SimCore::PlayerId loser;
  SimCore::Vec2 position;

  friend bool operator==(const BallWon&, const BallWon&) = default;
};

// A side started a coordinated press (docs/pressing.md): on whom, what
// triggered it -- empty for a press of the pressing phase -- and who plays
// which role.
struct PressingStarted {
  SimCore::SimTick tick;
  TeamSide side = TeamSide::kHome;
  SimCore::PlayerId carrier;
  std::optional<SimTactics::PressingTrigger> trigger;
  std::vector<PressAssignment> assignments;

  friend bool operator==(const PressingStarted&, const PressingStarted&) = default;
};

// A side's press ended, and how.
struct PressingEnded {
  SimCore::SimTick tick;
  TeamSide side = TeamSide::kHome;
  PressOutcome outcome = PressOutcome::kCarrierEscaped;

  friend bool operator==(const PressingEnded&, const PressingEnded&) = default;
};

using MatchEvent = std::variant<PassAttempted, PassReceived, PassIntercepted, LooseBallRecovered,
                                PossessionChanged, PhaseChanged, BallWon, PressingStarted,
                                PressingEnded, TacticChanged, PitchControlSampled>;

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
  // The weights the candidates were scored with: the configured scoring,
  // adjusted to the passing risk of his tactic. passContributions() with
  // them explains each utility.
  PassScoringConfig scoring;

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
  // Whether his team assigned the action -- a role in a press -- instead of
  // him choosing among the candidates; then the candidates hold that one
  // action.
  bool assigned = false;

  friend bool operator==(const ActionDiagnostic&, const ActionDiagnostic&) = default;
};

}  // namespace ElyverseFootball::SimMatch
