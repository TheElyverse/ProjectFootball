#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "ballMovement.hpp"
#include "decisionTrace.hpp"
#include "duel.hpp"
#include "matchEvents.hpp"
#include "matchSetup.hpp"
#include "matchSimulation.hpp"
#include "matchState.hpp"
#include "observation.hpp"
#include "passCandidate.hpp"
#include "replay.hpp"
#include "scenarios.hpp"
#include "shotCandidate.hpp"

using ElyverseFootball::SimCore::PlayerId;
using ElyverseFootball::SimCore::SimTick;
using ElyverseFootball::SimCore::Vec2;
using ElyverseFootball::SimMatch::ActionType;
using ElyverseFootball::SimMatch::BallPhysics;
using ElyverseFootball::SimMatch::BallTouch;
using ElyverseFootball::SimMatch::DecisionDiagnostic;
using ElyverseFootball::SimMatch::DecisionOutcome;
using ElyverseFootball::SimMatch::DiagnosticsFilter;
using ElyverseFootball::SimMatch::DuelConfig;
using ElyverseFootball::SimMatch::makeBallMovementSystem;
using ElyverseFootball::SimMatch::makeDuelSystem;
using ElyverseFootball::SimMatch::MatchConfig;
using ElyverseFootball::SimMatch::MatchSetup;
using ElyverseFootball::SimMatch::MatchSimulation;
using ElyverseFootball::SimMatch::MatchState;
using ElyverseFootball::SimMatch::MatchStateWriter;
using ElyverseFootball::SimMatch::MatchStepContext;
using ElyverseFootball::SimMatch::MatchSystem;
using ElyverseFootball::SimMatch::Observation;
using ElyverseFootball::SimMatch::ObservedEntity;
using ElyverseFootball::SimMatch::PassCandidate;
using ElyverseFootball::SimMatch::PassIntent;
using ElyverseFootball::SimMatch::PassIntercepted;
using ElyverseFootball::SimMatch::Pitch;
using ElyverseFootball::SimMatch::PlayerAction;
using ElyverseFootball::SimMatch::PlayerMatchState;
using ElyverseFootball::SimMatch::ShotCandidate;
using ElyverseFootball::SimMatch::ShotRejection;
using ElyverseFootball::SimMatch::TeamSide;
using ElyverseFootball::SimReplay::attributeInterception;
using ElyverseFootball::SimReplay::DecisionTracer;
using ElyverseFootball::SimReplay::FailureCause;
using ElyverseFootball::SimReplay::failureCauseName;
using ElyverseFootball::SimReplay::formatTrace;
using ElyverseFootball::SimReplay::PassDecisionTrace;
using ElyverseFootball::SimReplay::PassResult;
using ElyverseFootball::SimReplay::PlaybackObserver;
using ElyverseFootball::SimReplay::playReplay;
using ElyverseFootball::SimReplay::recordMatch;
using ElyverseFootball::SimReplay::SweepDecisionTrace;
using ElyverseFootball::SimReplay::TraceConfig;
using ElyverseFootball::SimReplay::TraceEntry;
using ElyverseFootball::SimReplay::TruePositions;

namespace {

// Player 1 passed to player 2 with the given estimated risk; player 3 was
// seen at `seen` -- or not at all -- and intercepted the pass.
struct Interception {
  double estimatedRisk = 0.1;
  std::optional<ElyverseFootball::SimCore::Vec2> seen;
  double confidence = 1.0;
};

// The passer judged the risk as `judgedRisk`, the estimate itself if empty.
[[nodiscard]] ElyverseFootball::SimReplay::PassOutcome attribute(
    const Interception& setup, const std::optional<double> judgedRisk = std::nullopt) {
  PassCandidate pass;
  pass.receiver = PlayerId(2);
  pass.interceptionRisk = setup.estimatedRisk;
  pass.judgedInterceptionRisk = judgedRisk.value_or(setup.estimatedRisk);
  DecisionDiagnostic decision{.tick = SimTick(30),
                              .player = PlayerId(1),
                              .observations = {},
                              .candidates = {pass},
                              .shots = {},
                              .outcome = DecisionOutcome::kPassed,
                              .chosen = 0,
                              .scoring = {},
                              .shotScoring = {},
                              .carries = {},
                              .carryScoring = {}};
  if (setup.seen) {
    decision.observations.push_back(Observation{.entity = ObservedEntity::player(PlayerId(3)),
                                                .position = *setup.seen,
                                                .velocity = {},
                                                .confidence = setup.confidence,
                                                .lastSeen = SimTick(30)});
  }
  const TruePositions truth{{PlayerId(3), {.x = 30.0, .y = 20.0}}};
  return attributeInterception(PassIntercepted{.tick = SimTick(45),
                                               .interceptor = PlayerId(3),
                                               .passer = PlayerId(1),
                                               .position = {.x = 28.0, .y = 20.0}},
                               decision, truth, MatchConfig{}, TraceConfig{});
}

[[nodiscard]] std::vector<PassDecisionTrace> passesOf(const std::vector<TraceEntry>& entries) {
  std::vector<PassDecisionTrace> passes;
  for (const TraceEntry& entry : entries) {
    if (const auto* pass = std::get_if<PassDecisionTrace>(&entry)) {
      passes.push_back(*pass);
    }
  }
  return passes;
}

// The trace of a scenario's first seconds, through a watched playback.
[[nodiscard]] std::vector<TraceEntry> traceOf(const std::string& scenario,
                                              const DiagnosticsFilter& filter,
                                              const std::int64_t ticks) {
  const auto* definition = ElyverseFootball::SimMatch::findScenario(scenario);
  REQUIRE(definition != nullptr);
  const auto setup = definition->make(7);
  REQUIRE(setup.has_value());
  const auto replay = recordMatch(*setup, SimTick(ticks), 30, "2026-09-25T12:00:00Z");
  REQUIRE(replay.has_value());
  DecisionTracer tracer(setup->config);
  const auto playback = playReplay(
      *replay,
      PlaybackObserver{.diagnostics = filter, .afterStep = [&tracer](const auto& simulation) {
                         tracer.recordStep(simulation);
                       }});
  // Tracing changes nothing: the playback still verifies.
  REQUIRE(playback.has_value());
  return {tracer.entries().begin(), tracer.entries().end()};
}

}  // namespace

TEST_CASE("An interceptor the passer had not seen is a perception failure", "[decisionTrace]") {
  const auto unseen = attribute({.estimatedRisk = 0.1, .seen = std::nullopt, .confidence = 1.0});
  REQUIRE(unseen.result == PassResult::kIntercepted);
  REQUIRE(unseen.by == PlayerId(3));
  REQUIRE(unseen.tick == SimTick(45));
  REQUIRE(unseen.cause == FailureCause::kPerception);
  REQUIRE_FALSE(unseen.believedMetersOff.has_value());

  // A memory too faint to be used counts as not seen.
  REQUIRE(attribute({.estimatedRisk = 0.1, .seen = {{.x = 30.0, .y = 20.0}}, .confidence = 0.1})
              .cause == FailureCause::kPerception);
}

TEST_CASE("An interceptor believed far from where he was is a perception failure",
          "[decisionTrace]") {
  const auto misjudged =
      attribute({.estimatedRisk = 0.9, .seen = {{.x = 30.0, .y = 25.0}}, .confidence = 1.0});
  REQUIRE(misjudged.cause == FailureCause::kPerception);
  REQUIRE(misjudged.believedMetersOff == 5.0);
}

TEST_CASE("A pass chosen despite a high estimated risk is a decision failure", "[decisionTrace]") {
  REQUIRE(attribute({.estimatedRisk = 0.6, .seen = {{.x = 31.0, .y = 20.0}}, .confidence = 1.0})
              .cause == FailureCause::kDecision);
}

TEST_CASE("A risky pass a poor decider misjudged as safe is a judgement failure",
          "[decisionTrace]") {
  const Interception seen{
      .estimatedRisk = 0.6, .seen = {{.x = 31.0, .y = 20.0}}, .confidence = 1.0};
  REQUIRE(attribute(seen, 0.3).cause == FailureCause::kJudgement);
  REQUIRE(failureCauseName(FailureCause::kJudgement) == "judgement");
  // Judged risky, whatever he estimated, it was his decision.
  REQUIRE(
      attribute({.estimatedRisk = 0.3, .seen = {{.x = 31.0, .y = 20.0}}, .confidence = 1.0}, 0.6)
          .cause == FailureCause::kDecision);
}

TEST_CASE("A pass judged safe with the interceptor in view is an execution failure",
          "[decisionTrace]") {
  const auto outcome =
      attribute({.estimatedRisk = 0.2, .seen = {{.x = 31.0, .y = 20.0}}, .confidence = 1.0});
  REQUIRE(outcome.cause == FailureCause::kExecution);
  REQUIRE_FALSE(outcome.believedMetersOff.has_value());
}

TEST_CASE("The tracer follows a completed pass to its receiver", "[decisionTrace]") {
  const auto passes = passesOf(traceOf(
      "pass-chain", {.players = {PlayerId(1)}, .from = std::nullopt, .to = std::nullopt}, 150));
  REQUIRE_FALSE(passes.empty());
  const PassDecisionTrace& first = passes.front();
  REQUIRE(first.decision.player == PlayerId(1));
  REQUIRE(first.decision.outcome == DecisionOutcome::kPassed);
  REQUIRE(first.outcome.result == PassResult::kReceived);
  REQUIRE(first.outcome.by.has_value());
}

TEST_CASE("The tracer attributes the risky pass of intercepted-pass to the decision",
          "[decisionTrace]") {
  const auto passes =
      passesOf(traceOf("intercepted-pass",
                       {.players = {PlayerId(1)}, .from = std::nullopt, .to = std::nullopt}, 150));
  REQUIRE_FALSE(passes.empty());
  const PassDecisionTrace& first = passes.front();
  REQUIRE(first.outcome.result == PassResult::kIntercepted);
  REQUIRE(first.outcome.by == PlayerId(8));
  REQUIRE(first.outcome.cause == FailureCause::kDecision);

  const std::string text = formatTrace(std::vector<TraceEntry>{first});
  CAPTURE(text);
  REQUIRE(text.starts_with("t=18 #1 passes to #2 (utility "));
  REQUIRE(text.contains("because "));
  REQUIRE(text.contains("-> intercepted by #8 at t="));
  REQUIRE(text.ends_with(": decision\n"));
}

TEST_CASE("A shot is traced with its aim, its chances and its reason", "[decisionTrace]") {
  ShotCandidate shot;
  shot.target = {.x = 60.0, .y = 21.25};
  shot.height = 0.43;
  shot.goalChance = 0.5;
  shot.saveRisk = 0.3;
  shot.blockRisk = 0.1;
  shot.judgedGoalChance = 0.5;
  shot.judgedSaveRisk = 0.3;
  shot.judgedBlockRisk = 0.1;
  shot.secondBallChance = 0.12;
  const DecisionDiagnostic decision{.tick = SimTick(40),
                                    .player = PlayerId(7),
                                    .observations = {},
                                    .candidates = {},
                                    .shots = {shot},
                                    .outcome = DecisionOutcome::kShot,
                                    .chosen = 0,
                                    .scoring = {},
                                    .shotScoring = {},
                                    .carries = {},
                                    .carryScoring = {}};

  const std::string text =
      formatTrace(std::vector<TraceEntry>{PassDecisionTrace{.decision = decision, .outcome = {}}});
  CAPTURE(text);
  REQUIRE(text.starts_with("t=40 #7 shoots at (60.0, 21.2) 0.43 m high (utility "));
  REQUIRE(text.contains("goal chance 0.50, save 0.30, block 0.10"));
  REQUIRE(text.ends_with("because goal\n"));

  // A poor decider's misjudged chances follow the estimates they differ from.
  DecisionDiagnostic misjudged = decision;
  misjudged.shots.front().judgedGoalChance = 0.7;
  misjudged.shots.front().judgedBlockRisk = 0.05;
  const std::string misjudgedText =
      formatTrace(std::vector<TraceEntry>{PassDecisionTrace{.decision = misjudged, .outcome = {}}});
  CAPTURE(misjudgedText);
  REQUIRE(
      misjudgedText.contains("goal chance 0.50, judged 0.70, save 0.30, block 0.10, judged 0.05"));

  // A curled shot and a chip say how he struck them.
  DecisionDiagnostic struck = decision;
  struck.shots.front().type = ElyverseFootball::SimMatch::ShotType::kCurled;
  REQUIRE(formatTrace(std::vector<TraceEntry>{PassDecisionTrace{.decision = struck, .outcome = {}}})
              .starts_with("t=40 #7 curls a shot at (60.0, 21.2)"));
  struck.shots.front().type = ElyverseFootball::SimMatch::ShotType::kChipped;
  REQUIRE(formatTrace(std::vector<TraceEntry>{PassDecisionTrace{.decision = struck, .outcome = {}}})
              .starts_with("t=40 #7 chips at (60.0, 21.2)"));
}

TEST_CASE("A decision without a shot says why the goal was not on", "[decisionTrace]") {
  ShotCandidate shot;
  shot.goalChance = 0.12;
  shot.judgedGoalChance = 0.12;
  shot.rejection = ShotRejection::kNotWorthIt;
  DecisionDiagnostic decision{.tick = SimTick(40),
                              .player = PlayerId(7),
                              .observations = {},
                              .candidates = {},
                              .shots = {shot},
                              .outcome = DecisionOutcome::kNoValidOption,
                              .chosen = std::nullopt,
                              .scoring = {},
                              .shotScoring = {},
                              .carries = {},
                              .carryScoring = {}};
  const auto traced = [&decision] {
    return formatTrace(
        std::vector<TraceEntry>{PassDecisionTrace{.decision = decision, .outcome = {}}});
  };

  REQUIRE(traced().contains("; no shot: not worth it (goal chance 0.12))"));
  decision.shots.front().rejection = ShotRejection::kGoalUnseen;
  REQUIRE(traced().contains("; no shot: goal unseen)"));
}

TEST_CASE("Holding the ball to look around says whether he had a valid option", "[decisionTrace]") {
  ShotCandidate shot;
  shot.rejection = ShotRejection::kGoalUnseen;
  DecisionDiagnostic decision{.tick = SimTick(18),
                              .player = PlayerId(1),
                              .observations = {},
                              .candidates = {PassCandidate{}},
                              .shots = {shot},
                              .outcome = DecisionOutcome::kHeld,
                              .chosen = 0,
                              .scoring = {},
                              .shotScoring = {},
                              .carries = {ElyverseFootball::SimMatch::CarryCandidate{}},
                              .carryScoring = {},
                              .reason = ElyverseFootball::SimMatch::OnBallReason::kScanFirstSide,
                              .target = Vec2{.x = 12.0, .y = 26.2}};
  const auto traced = [&decision] {
    return formatTrace(
        std::vector<TraceEntry>{PassDecisionTrace{.decision = decision, .outcome = {}}});
  };

  REQUIRE(traced().contains("keeps the ball: looks for a better option (1 options"));
  decision.candidates.front().rejection = ElyverseFootball::SimMatch::PassRejection::kUnlikely;
  REQUIRE(traced().contains("keeps the ball: no valid option (1 options"));
}

TEST_CASE("A goalkeeper's call to come for a ball is traced with both arrivals",
          "[decisionTrace]") {
  const auto entries =
      traceOf("keeper-sweep-claim",
              {.players = {PlayerId(8)}, .from = std::nullopt, .to = std::nullopt}, 60);
  std::vector<TraceEntry> sweeps;
  for (const TraceEntry& entry : entries) {
    if (std::holds_alternative<SweepDecisionTrace>(entry)) {
      sweeps.push_back(entry);
    }
  }
  REQUIRE(sweeps.size() == 1);
  const std::string text = formatTrace(sweeps);
  CAPTURE(text);
  REQUIRE(text.starts_with("t=3 #8 sweeps (reaches the ball in "));
  REQUIRE(text.contains(" s, the first attacker in "));
  REQUIRE(text.ends_with("; misjudged by +0.00 s, needs +0.15 s)\n"));
}

TEST_CASE("A pass lost in a duel the step it is decided is traced as not played",
          "[decisionTrace]") {
  // Away's presser (2), right at the ball, takes home's carrier (1) on at
  // tick 1, while the carrier waits to play a pass; at tick 2 the carrier
  // decides another, and the presser lunges with a foot that cannot miss the
  // very same step. The
  // pass-decision system runs before the duel system in the standard
  // pipeline, so both a kPassed diagnostic and the duel's PossessionChanged
  // fall in the same step's events and diagnostics.
  const auto player = [](const PlayerId::ValueType number, const TeamSide side,
                         const Vec2 position) {
    return PlayerMatchState{.playerId = PlayerId(number),
                            .side = side,
                            .position = position,
                            .velocity = {},
                            .attributes = {},
                            .target = std::nullopt,
                            .facing = {.x = 1.0, .y = 0.0}};
  };
  auto initial = MatchState::create(
      {.pitch = Pitch(60.0, 40.0),
       .players = {player(1, TeamSide::kHome, {.x = 30.0, .y = 20.0}),
                   player(2, TeamSide::kAway, {.x = 30.5, .y = 20.0})},
       .ball = {.position = {.x = 30.5, .y = 20.0},
                .velocity = {},
                .owner = PlayerId(1),
                .lastTouch = BallTouch{.playerId = PlayerId(1), .tick = SimTick(0)}},
       .playersPerSide = 1});
  REQUIRE(initial.has_value());

  const MatchSystem fakeDecide{
      .name = "fake-decide",
      .update = [](const MatchStepContext& context, const MatchState&, MatchStateWriter& next) {
        // The presser's action must already be in current by the step he
        // is meant to take the carrier on, since the duel reads current, not
        // this same step's writes; set it up one step ahead.
        if (context.tick() == SimTick(0)) {
          next.tactical(1).action = PlayerAction{.type = ActionType::kPressCarrier,
                                                 .target = {.x = 30.5, .y = 20.0},
                                                 .subject = PlayerId(1),
                                                 .decidedAt = SimTick(0),
                                                 .withBall = false};
          return;
        }
        // Waiting to play a pass, he does not roll the ball away in the
        // duel's first step.
        next.setPendingAction(0, PassIntent{.passer = PlayerId(1),
                                            .target = {.x = 40.0, .y = 20.0},
                                            .speed = 10.0,
                                            .receiver = std::nullopt});
        if (context.tick() == SimTick(2) && context.collectsDiagnostics(PlayerId(1))) {
          context.diagnose(DecisionDiagnostic{.tick = SimTick(2),
                                              .player = PlayerId(1),
                                              .observations = {},
                                              .candidates = {},
                                              .shots = {},
                                              .outcome = DecisionOutcome::kPassed,
                                              .chosen = std::nullopt,
                                              .scoring = {},
                                              .shotScoring = {},
                                              .carries = {},
                                              .carryScoring = {}});
        }
      }};
  // He always commits, and his foot lands on the ball.
  DuelConfig sure;
  sure.intervalTicks = 1;
  sure.protectSeconds = 0.0;
  sure.footError = 0.0;
  sure.jockeyWeight = 0.0;
  sure.commitWeight = 100.0;
  sure.decisionTemperature = 0.01;
  MatchSimulation simulation(
      {.initialState = *std::move(initial),
       .seed = 1,
       .ticksPerSecond = 30,
       .systems = {fakeDecide, makeDuelSystem({.duels = sure}), makeBallMovementSystem()},
       .commands = {}});
  simulation.setCollectDiagnostics(true);
  DecisionTracer tracer(MatchConfig{});
  for (int step = 0; step < 3; ++step) {
    REQUIRE(simulation.step().has_value());
    tracer.recordStep(simulation);
  }
  REQUIRE(simulation.state().ball().owner == PlayerId(2));

  const auto passes = passesOf({tracer.entries().begin(), tracer.entries().end()});
  REQUIRE(passes.size() == 1);
  REQUIRE(passes.front().decision.outcome == DecisionOutcome::kPassed);
  REQUIRE(passes.front().outcome.result == PassResult::kNotPlayed);
}

TEST_CASE("The tracer keeps to the filtered players and ticks", "[decisionTrace]") {
  const auto entries = traceOf(
      "tactic-match", {.players = {PlayerId(4)}, .from = SimTick(30), .to = SimTick(90)}, 150);
  REQUIRE_FALSE(entries.empty());
  for (const TraceEntry& entry : entries) {
    std::visit(
        [](const auto& trace) {
          REQUIRE(trace.decision.player == PlayerId(4));
          REQUIRE(trace.decision.tick >= SimTick(30));
          REQUIRE(trace.decision.tick <= SimTick(90));
        },
        entry);
  }
  const std::string text = formatTrace(entries);
  REQUIRE(text.starts_with("t="));
  REQUIRE(text.contains(" #4 "));
}

TEST_CASE("A cross is traced with its area, its runner and its reason", "[decisionTrace]") {
  ElyverseFootball::SimMatch::CrossCandidate cross;
  cross.area = ElyverseFootball::SimMatch::CrossArea::kFarPost;
  cross.target = {.x = 56.9, .y = 22.2};
  cross.height = 1.6;
  cross.runner = PlayerId(9);
  cross.winChance = 0.5;
  cross.keeperClaim = 0.3;
  cross.threat = 1.0;
  DecisionDiagnostic decision{.tick = SimTick(12),
                              .player = PlayerId(6),
                              .observations = {},
                              .candidates = {},
                              .shots = {},
                              .outcome = DecisionOutcome::kCrossed,
                              .chosen = 0,
                              .scoring = {},
                              .shotScoring = {},
                              .carries = {},
                              .carryScoring = {}};
  decision.crosses = {cross};
  const std::string text =
      formatTrace(std::vector<TraceEntry>{PassDecisionTrace{.decision = decision, .outcome = {}}});
  CAPTURE(text);
  REQUIRE(text.starts_with("t=12 #6 crosses to the farPost (56.9, 22.2) 1.60 m high for #9"));
  REQUIRE(text.contains("win chance 0.50, keeper 0.30"));
  REQUIRE(text.contains("because win -> pending"));
}

TEST_CASE("The tracer follows a cross to whoever wins it in the air", "[decisionTrace]") {
  const auto passes = passesOf(traceOf(
      "early-cross", {.players = {PlayerId(1)}, .from = std::nullopt, .to = std::nullopt}, 90));
  REQUIRE_FALSE(passes.empty());
  const PassDecisionTrace& first = passes.front();
  REQUIRE(first.decision.outcome == DecisionOutcome::kCrossed);
  // Seed 7: the keeper claims it in the air -- an interception by #8.
  REQUIRE(first.outcome.result == PassResult::kIntercepted);
  REQUIRE(first.outcome.by == PlayerId(8));
}
