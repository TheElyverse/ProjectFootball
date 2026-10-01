#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "actionCandidate.hpp"
#include "goalFrame.hpp"
#include "goalkeeper.hpp"
#include "ids.hpp"
#include "matchState.hpp"
#include "observation.hpp"
#include "passCandidate.hpp"
#include "restartKind.hpp"
#include "shotCandidate.hpp"
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

// Play restarted after the ball went out (docs/restarts.md): how, who got
// the ball, and where it had left the pitch.
struct RestartTaken {
  SimCore::SimTick tick;
  RestartKind kind = RestartKind::kThrowIn;
  SimCore::PlayerId player;
  SimCore::Vec2 position;

  friend bool operator==(const RestartTaken&, const RestartTaken&) = default;
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

// A player took a shot: the ball left his foot from `from` toward the aimed
// point of the goal. Enough to reconstruct the chance; what became of it
// follows in the events below, which name the shot by its shooter and the
// tick of this event (docs/shooting.md).
struct ShotAttempted {
  SimCore::SimTick tick;
  SimCore::PlayerId shooter;
  SimCore::Vec2 from;
  SimCore::Vec2 target;
  double height = 0.0;
  // The point on the goal line and the height the ball really left for: the
  // aim, off by the shooter's execution error.
  SimCore::Vec2 struckAt;
  double struckHeight = 0.0;
  // The speed the ball left the foot with, execution error included.
  double speed = 0.0;
  // From `from` to the goal's center, and how wide the goal looked from
  // there (goalOpening()).
  double distance = 0.0;
  double opening = 0.0;

  friend bool operator==(const ShotAttempted&, const ShotAttempted&) = default;
};

// A shot came off an outfield player and flew on, at the ball's position
// and height. blocked if it lost most of its speed there.
struct ShotDeflected {
  SimCore::SimTick tick;
  SimCore::PlayerId shooter;
  SimCore::SimTick shotTick;
  SimCore::PlayerId player;
  SimCore::Vec2 position;
  double height = 0.0;
  bool blocked = false;

  friend bool operator==(const ShotDeflected&, const ShotDeflected&) = default;
};

// A shot hit a post or the crossbar and rebounded, at the ball's position and
// height.
struct ShotHitWoodwork {
  SimCore::SimTick tick;
  SimCore::PlayerId shooter;
  SimCore::SimTick shotTick;
  WoodworkPart part = WoodworkPart::kCrossbar;
  SimCore::Vec2 position;
  double height = 0.0;

  friend bool operator==(const ShotHitWoodwork&, const ShotHitWoodwork&) = default;
};

// What became of a shot: a goal; saved, taken by the keeper on its way into
// the goal; off target, past the goal, short of it or taken by the keeper on
// its way past; or blocked, touched by an outfield player without going in.
// A shot on target is a goal or saved.
enum class ShotOutcome : std::uint8_t {
  kGoal,
  kSaved,
  kOffTarget,
  kBlocked,
};

// "goal", "saved", "offTarget", "blocked"; "unknown" outside the enumerators.
[[nodiscard]] std::string_view shotOutcomeName(ShotOutcome outcome) noexcept;

// A shot's outcome, recorded exactly once per shot: when the ball crosses a
// goal line, a player controls it, it leaves the pitch or comes to rest.
struct ShotResolved {
  SimCore::SimTick tick;
  SimCore::PlayerId shooter;
  SimCore::SimTick shotTick;
  ShotOutcome outcome = ShotOutcome::kOffTarget;

  friend bool operator==(const ShotResolved&, const ShotResolved&) = default;
};

// The ball crossed a goal line between the posts and under the crossbar: a
// goal for `side`, and the score it makes. The scorer is the shooter of a
// shot by that side still on its way, otherwise whoever touched the ball
// last -- an own goal if he plays for the other side -- and nobody for a ball
// no player had touched. The assist is the teammate whose pass the scorer
// received last.
struct GoalScored {
  SimCore::SimTick tick;
  TeamSide side = TeamSide::kHome;
  std::optional<SimCore::PlayerId> scorer;
  std::optional<SimCore::PlayerId> assist;
  bool ownGoal = false;
  Score score;

  friend bool operator==(const GoalScored&, const GoalScored&) = default;
};

// New alternatives go last: an event's index is part of its hash.
using MatchEvent =
    std::variant<PassAttempted, PassReceived, PassIntercepted, LooseBallRecovered,
                 PossessionChanged, PhaseChanged, BallWon, PressingStarted, PressingEnded,
                 TacticChanged, PitchControlSampled, RestartTaken, ShotAttempted, ShotDeflected,
                 ShotHitWoodwork, ShotResolved, GoalScored>;

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
  kShot,
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
  // Every zone he could have aimed at; only the first, the best, competed
  // with the passes.
  std::vector<ShotCandidate> shots;
  DecisionOutcome outcome = DecisionOutcome::kNoValidOption;
  // Index of the chosen option: into candidates for a pass, into shots for a
  // shot; empty without one.
  std::optional<std::size_t> chosen;
  // The weights the candidates were scored with: the configured scoring,
  // adjusted to the passing risk of his tactic. passContributions() and
  // shotContributions() with them explain each utility.
  PassScoringConfig scoring;
  ShotScoringConfig shotScoring;

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

// Why a goalkeeper came for a free ball the opponent played or stayed home
// (docs/goalkeeper.md): reported when he first judges a ball and when he
// turns back from it. Like DecisionDiagnostic, debug output only.
struct SweepDiagnostic {
  SimCore::SimTick tick;
  SimCore::PlayerId player;
  SweepCall call;

  friend bool operator==(const SweepDiagnostic&, const SweepDiagnostic&) = default;
};

}  // namespace ElyverseFootball::SimMatch
