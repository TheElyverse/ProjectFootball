#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "actionCandidate.hpp"
#include "carryCandidate.hpp"
#include "crossCandidate.hpp"
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

// A part of a goal frame: the post at the lower or the higher pitch y, or
// the crossbar.
enum class WoodworkPart : std::uint8_t {
  kPostAtMinY,
  kPostAtMaxY,
  kCrossbar,
};

// "postAtMinY", "postAtMaxY", "crossbar"; "unknown" outside the enumerators.
[[nodiscard]] std::string_view woodworkPartName(WoodworkPart part) noexcept;

// What the keeper makes of a ball he could come for.
struct SweepCall {
  // The seconds he and the first attacker need to reach it; no attacker if
  // none of them can.
  double keeperSeconds = 0.0;
  std::optional<double> attackerSeconds;
  double misjudgement = 0.0;
  // The head start he wants, already lowered by the hysteresis if he is
  // coming.
  double threshold = 0.0;
  bool coming = false;

  friend bool operator==(const SweepCall&, const SweepCall&) = default;
};

// Domain events: immutable facts about what happened in a step, recorded by the
// systems that made them happen
// (docs/match-engine.md#match-events-and-decision-diagnostics). Every event
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

// A team with a tactic entered a new tactical phase
// (docs/team-tactics.md#match-phases). previous is empty for the team's first
// phase.
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

// The pitch control system updated its grid
// (docs/team-tactics.md#pitch-control): home's share of the pitch -- away's is
// the rest -- and where the ball was. A regular sample of the match, so
// analyses of territory and control need nothing but events.
struct PitchControlSampled {
  SimCore::SimTick tick;
  double homeShare = 0.5;
  SimCore::Vec2 ball;

  friend bool operator==(const PitchControlSampled&, const PitchControlSampled&) = default;
};

// Play restarted after the ball went out (docs/match-engine.md#restarts): how,
// who got the ball, and where it had left the pitch.
struct RestartTaken {
  SimCore::SimTick tick;
  RestartKind kind = RestartKind::kThrowIn;
  SimCore::PlayerId player;
  SimCore::Vec2 position;

  friend bool operator==(const RestartTaken&, const RestartTaken&) = default;
};

// How a player lost the ball he owned to an opponent: in a duel
// (docs/duels.md#duels-on-the-ball), or the opponent got to it while it rolled
// ahead of him after a touch (docs/on-ball.md#ball-carrying).
enum class BallWonCause : std::uint8_t {
  kDuel,
  kLooseTouch,
};

// "duel", "looseTouch"; "unknown" outside the enumerators.
[[nodiscard]] std::string_view ballWonCauseName(BallWonCause cause) noexcept;

// An opponent won the ball from its owner, at the ball's position, and how.
struct BallWon {
  SimCore::SimTick tick;
  SimCore::PlayerId winner;
  SimCore::PlayerId loser;
  SimCore::Vec2 position;
  BallWonCause cause = BallWonCause::kDuel;

  friend bool operator==(const BallWon&, const BallWon&) = default;
};

// A side started a coordinated press (docs/team-tactics.md#pressing): on whom,
// what triggered it -- empty for a press of the pressing phase -- and who plays
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
// tick of this event (docs/on-ball.md#shooting).
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
  // How he struck it (docs/on-ball.md#shot-types).
  ShotType type = ShotType::kDriven;

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

// An offside (docs/match-engine.md#offside): `player` of `side`, in an offside
// position when `passer` played the ball, became involved at `position`;
// lineX is the offside line then, in pitch x.
struct OffsideCalled {
  SimCore::SimTick tick;
  TeamSide side = TeamSide::kHome;
  SimCore::PlayerId player;
  SimCore::PlayerId passer;
  double lineX = 0.0;
  SimCore::Vec2 position;

  friend bool operator==(const OffsideCalled&, const OffsideCalled&) = default;
};

// What the referee made of a foul (docs/fouls.md): a direct free kick, a
// penalty, or advantage -- play goes on.
enum class FoulRestart : std::uint8_t {
  kFreeKick,
  kPenalty,
  kAdvantage,
};

// The card a foul earned: none, a yellow, a second yellow -- a sending-off --,
// or a straight red.
enum class Card : std::uint8_t {
  kNone,
  kYellow,
  kSecondYellow,
  kRed,
};

// "freeKick", "penalty", "advantage"; "none", "yellow", "secondYellow", "red";
// "unknown" outside the enumerators.
[[nodiscard]] std::string_view foulRestartName(FoulRestart restart) noexcept;
[[nodiscard]] std::string_view cardName(Card card) noexcept;

// A foul (docs/fouls.md): `offender` of `side` on `victim` where the ball was,
// what the referee gave, the card, and the criteria behind it -- the tackle
// came from behind, it denied an obvious goal-scoring opportunity.
struct FoulCalled {
  SimCore::SimTick tick;
  TeamSide side = TeamSide::kHome;
  SimCore::PlayerId offender;
  SimCore::PlayerId victim;
  SimCore::Vec2 position;
  FoulRestart restart = FoulRestart::kFreeKick;
  Card card = Card::kNone;
  bool fromBehind = false;
  bool denyingGoal = false;

  friend bool operator==(const FoulCalled&, const FoulCalled&) = default;
};

// A set piece was taken (docs/set-pieces.md): its kind, the side and taker,
// the delivery its plan asked for, where the pass goes and whom it is meant
// for. RestartTaken recorded when it was awarded.
struct SetPieceTaken {
  SimCore::SimTick tick;
  SimTactics::SetPieceKind kind = SimTactics::SetPieceKind::kThrowIn;
  TeamSide side = TeamSide::kHome;
  SimCore::PlayerId taker;
  SimTactics::SetPieceDelivery delivery = SimTactics::SetPieceDelivery::kShort;
  SimCore::Vec2 target;
  std::optional<SimCore::PlayerId> receiver = std::nullopt;

  friend bool operator==(const SetPieceTaken&, const SetPieceTaken&) = default;
};

// The referee ended a half (docs/match-clock.md): the period that ended, and
// the added time he had announced for it.
struct PeriodEnded {
  SimCore::SimTick tick;
  MatchPeriod period = MatchPeriod::kFirstHalf;
  int addedMinutes = 0;

  friend bool operator==(const PeriodEnded&, const PeriodEnded&) = default;
};

// What a goalkeeper made of a shot that passed him: he caught it; he parried
// it, back into play or -- by where it is heading -- behind his goal line;
// or it was out of his reach.
enum class SaveResult : std::uint8_t {
  kCaught,
  kParriedIntoPlay,
  kParriedBehind,
  kOutOfReach,
};

// "caught", "parriedIntoPlay", "parriedBehind", "outOfReach"; "unknown"
// outside the enumerators.
[[nodiscard]] std::string_view saveResultName(SaveResult result) noexcept;

// A shot passed the keeper of the goal it attacked, at the ball's position and
// height: recorded when he touched it, and when it was on its way into his
// goal and he did not (docs/goalkeeper.md#shot-stopping).
struct SaveAttempted {
  SimCore::SimTick tick;
  SimCore::PlayerId keeper;
  SimCore::PlayerId shooter;
  SimCore::SimTick shotTick;
  SaveResult result = SaveResult::kOutOfReach;
  SimCore::Vec2 position;
  double height = 0.0;

  friend bool operator==(const SaveAttempted&, const SaveAttempted&) = default;
};

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

// Players went up for a high ball, where it was and how high when the first of
// them got to it (docs/duels.md#aerial-duels-and-headers): everyone who did,
// how high each got and whether that reached it, the one who won it -- nobody
// if none reached it -- and what he did with it.
struct AerialContest {
  SimCore::SimTick tick;
  SimCore::Vec2 position;
  double height = 0.0;
  std::vector<AerialContestant> contestants;
  std::optional<SimCore::PlayerId> winner;
  std::optional<AerialPlay> play;

  friend bool operator==(const AerialContest&, const AerialContest&) = default;
};

// A carrier knocked the ball past an opponent to go round him
// (docs/on-ball.md#ball-carrying): from where, and where the touch sends it.
struct DribbleAttempted {
  SimCore::SimTick tick;
  SimCore::PlayerId carrier;
  SimCore::PlayerId opponent;
  SimCore::Vec2 from;
  SimCore::Vec2 target;

  friend bool operator==(const DribbleAttempted&, const DribbleAttempted&) = default;
};

// What became of a dribble: the carrier got the ball back past the opponent,
// got it back without getting past him, or lost it -- to an opponent or out
// of play.
enum class DribbleOutcome : std::uint8_t {
  kBeaten,
  kKept,
  kLost,
};

// "beaten", "kept", "lost"; "unknown" outside the enumerators.
[[nodiscard]] std::string_view dribbleOutcomeName(DribbleOutcome outcome) noexcept;

// A dribble ended, where the ball was.
struct DribbleEnded {
  SimCore::SimTick tick;
  SimCore::PlayerId carrier;
  SimCore::PlayerId opponent;
  DribbleOutcome outcome = DribbleOutcome::kLost;
  SimCore::Vec2 position;

  friend bool operator==(const DribbleEnded&, const DribbleEnded&) = default;
};

// A defender took the carrier on for the ball at his feet
// (docs/duels.md#duels-on-the-ball), where the ball was.
struct DuelStarted {
  SimCore::SimTick tick;
  SimCore::PlayerId carrier;
  SimCore::PlayerId defender;
  SimCore::Vec2 position;

  friend bool operator==(const DuelStarted&, const DuelStarted&) = default;
};

// What became of a duel: the carrier kept the ball -- he played it on, got
// away or the defender let him go --, the defender won it cleanly, knocked it
// loose, or fouled the carrier.
enum class DuelOutcome : std::uint8_t {
  kKept,
  kWon,
  kLoose,
  kFoul,
};

// "kept", "won", "loose", "foul"; "unknown" outside the enumerators.
[[nodiscard]] std::string_view duelOutcomeName(DuelOutcome outcome) noexcept;

// A duel ended, where the ball was.
struct DuelEnded {
  SimCore::SimTick tick;
  SimCore::PlayerId carrier;
  SimCore::PlayerId defender;
  DuelOutcome outcome = DuelOutcome::kKept;
  SimCore::Vec2 position;

  friend bool operator==(const DuelEnded&, const DuelEnded&) = default;
};

// What a lunge did once it landed (docs/duels.md#the-lunge): a tackle missed,
// won the ball, knocked it loose or went through the man; a block met the
// shot and blocked it, or missed.
enum class LungeResult : std::uint8_t {
  kMissed,
  kWon,
  kLoose,
  kFoul,
  kBlocked,
};

// "missed", "won", "loose", "foul", "blocked"; "unknown" outside the
// enumerators.
[[nodiscard]] std::string_view lungeResultName(LungeResult result) noexcept;

// A defender lunged at the ball (docs/duels.md#tackles-and-blocks): a tackle
// at a carrier's ball in a duel, or a block of a shot in flight -- `carrier`
// is then the shooter. Where the ball was, and what the lunge did.
struct TackleAttempted {
  SimCore::SimTick tick;
  SimCore::PlayerId tackler;
  SimCore::PlayerId carrier;
  bool block = false;
  LungeResult result = LungeResult::kMissed;
  SimCore::Vec2 position;

  friend bool operator==(const TackleAttempted&, const TackleAttempted&) = default;
};

// A cross left the crosser's foot (docs/on-ball.md#crossing): the area it is
// aimed at, where it should arrive and how high, the teammate it is meant
// for, and the speed it left with. PassAttempted comes first, as for any pass.
struct CrossAttempted {
  SimCore::SimTick tick;
  SimCore::PlayerId crosser;
  CrossArea area = CrossArea::kPenaltySpot;
  SimCore::Vec2 from;
  SimCore::Vec2 target;
  double height = 0.0;
  std::optional<SimCore::PlayerId> runner;
  double speed = 0.0;

  friend bool operator==(const CrossAttempted&, const CrossAttempted&) = default;
};

// New alternatives go last: an event's index is part of its hash.
using MatchEvent =
    std::variant<PassAttempted, PassReceived, PassIntercepted, LooseBallRecovered,
                 PossessionChanged, PhaseChanged, BallWon, PressingStarted, PressingEnded,
                 TacticChanged, PitchControlSampled, RestartTaken, ShotAttempted, ShotDeflected,
                 ShotHitWoodwork, ShotResolved, GoalScored, SaveAttempted, AerialContest,
                 DribbleAttempted, DribbleEnded, DuelStarted, DuelEnded, TackleAttempted,
                 CrossAttempted, PeriodEnded, OffsideCalled, FoulCalled, SetPieceTaken>;

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
  kHeld,
  kTurned,
  kCarried,
  kDribbled,
  kCrossed,
};

// "passed", "noValidOption", "shot", "held", "turned", "carried",
// "dribbled", "crossed"; "unknown" outside the enumerators.
[[nodiscard]] std::string_view decisionOutcomeName(DecisionOutcome outcome) noexcept;

// Whether the outcome plays the ball on with a touch
// (docs/on-ball.md#ball-carrying): turned, carried or dribbled.
[[nodiscard]] bool isTouch(DecisionOutcome outcome) noexcept;

// How a carrier came to his decision (docs/on-ball.md#pass-decisions): by
// choosing among his valid options, the usual way, or -- without one -- by
// looking for one on the first or the other side, or by playing the best
// rejected pass or clearing the ball once he has looked for long enough.
enum class OnBallReason : std::uint8_t {
  kChoice,
  kScanFirstSide,
  kScanOtherSide,
  kForcedRejected,
  kForcedClear,
};

// "choice", "scanFirstSide", ...; "unknown" outside the enumerators.
[[nodiscard]] std::string_view onBallReasonName(OnBallReason reason) noexcept;

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
  // shot, into carries for holding, turning, carrying or dribbling; empty
  // without one.
  std::optional<std::size_t> chosen;
  // The weights the candidates were scored with: the configured scoring,
  // adjusted to the passing risk of his tactic and, for a goalkeeper, its
  // directness. passContributions() and
  // shotContributions() with them explain each utility.
  PassScoringConfig scoring;
  ShotScoringConfig shotScoring;
  // His options with the ball at his feet (docs/on-ball.md#ball-carrying), and
  // the weights they were scored with: carryContributions() with them explains
  // each utility.
  std::vector<CarryCandidate> carries;
  CarryScoringConfig carryScoring;
  // Whether he held the ball in his hands: his passes are then throws, his
  // lofted passes punts (docs/goalkeeper.md#distribution).
  bool held = false;
  // How he came to it, and where it points him: the point he looks at while
  // he scans, the point he clears the ball to; empty otherwise.
  OnBallReason reason = OnBallReason::kChoice;
  std::optional<SimCore::Vec2> target = std::nullopt;
  // His crosses (docs/on-ball.md#crossing), the best first -- only that one
  // competed --, and the weights they were scored with; chosen indexes them
  // for a cross.
  // NOLINTNEXTLINE(readability-redundant-member-init) -- designated initializers leave it out.
  std::vector<CrossCandidate> crosses = {};
  CrossScoringConfig crossScoring = {};

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
  // Whether he was sent at a carrier standing on the ball
  // (docs/team-tactics.md#pressing); then the candidates hold that one action.
  bool idlePress = false;

  friend bool operator==(const ActionDiagnostic&, const ActionDiagnostic&) = default;
};

// Why a goalkeeper came for a free ball the opponent played or stayed home
// (docs/goalkeeper.md#positioning-and-sweeping): reported when he first judges
// a ball and when he turns back from it. Like DecisionDiagnostic, debug output
// only.
struct SweepDiagnostic {
  SimCore::SimTick tick;
  SimCore::PlayerId player;
  SweepCall call;

  friend bool operator==(const SweepDiagnostic&, const SweepDiagnostic&) = default;
};

}  // namespace ElyverseFootball::SimMatch
