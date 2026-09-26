#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "ballMovement.hpp"
#include "challenge.hpp"
#include "decisionTrace.hpp"
#include "matchEvents.hpp"
#include "matchSetup.hpp"
#include "matchSimulation.hpp"
#include "matchState.hpp"
#include "observation.hpp"
#include "passCandidate.hpp"
#include "replay.hpp"
#include "scenarios.hpp"

using ElyverseFootball::SimCore::PlayerId;
using ElyverseFootball::SimCore::SimTick;
using ElyverseFootball::SimCore::Vec2;
using ElyverseFootball::SimMatch::ActionType;
using ElyverseFootball::SimMatch::BallPhysics;
using ElyverseFootball::SimMatch::BallTouch;
using ElyverseFootball::SimMatch::ChallengeConfig;
using ElyverseFootball::SimMatch::DecisionDiagnostic;
using ElyverseFootball::SimMatch::DecisionOutcome;
using ElyverseFootball::SimMatch::DiagnosticsFilter;
using ElyverseFootball::SimMatch::makeBallMovementSystem;
using ElyverseFootball::SimMatch::makeChallengeSystem;
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
using ElyverseFootball::SimMatch::TeamSide;
using ElyverseFootball::SimReplay::attributeInterception;
using ElyverseFootball::SimReplay::DecisionTracer;
using ElyverseFootball::SimReplay::FailureCause;
using ElyverseFootball::SimReplay::formatTrace;
using ElyverseFootball::SimReplay::PassDecisionTrace;
using ElyverseFootball::SimReplay::PassResult;
using ElyverseFootball::SimReplay::PlaybackObserver;
using ElyverseFootball::SimReplay::playReplay;
using ElyverseFootball::SimReplay::recordMatch;
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

[[nodiscard]] ElyverseFootball::SimReplay::PassOutcome attribute(const Interception& setup) {
  PassCandidate pass;
  pass.receiver = PlayerId(2);
  pass.interceptionRisk = setup.estimatedRisk;
  DecisionDiagnostic decision{.tick = SimTick(30),
                              .player = PlayerId(1),
                              .observations = {},
                              .candidates = {pass},
                              .outcome = DecisionOutcome::kPassed,
                              .chosen = 0,
                              .scoring = {}};
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
  REQUIRE(text.find("because ") != std::string::npos);
  REQUIRE(text.find("-> intercepted by #8 at t=") != std::string::npos);
  REQUIRE(text.ends_with(": decision\n"));
}

TEST_CASE("A pass challenged away the step it is decided is traced as not played",
          "[decisionTrace]") {
  // Home's carrier (1) decides a pass at tick 0; away's presser (2), right
  // at the ball, wins a guaranteed challenge the very same step. The
  // pass-decision system runs before the challenge system in the standard
  // pipeline, so both a kPassed diagnostic and the challenge's
  // PossessionChanged fall in the same step's events and diagnostics.
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
        // The presser's action must already be in current by the step it
        // is meant to challenge on, since challenge reads current, not
        // this same step's writes; set it up one step ahead.
        if (context.tick() == SimTick(0)) {
          next.tactical(1).action = PlayerAction{.type = ActionType::kPressCarrier,
                                                 .target = {.x = 30.5, .y = 20.0},
                                                 .subject = PlayerId(1),
                                                 .decidedAt = SimTick(0),
                                                 .withBall = false};
          return;
        }
        if (context.tick() != SimTick(1)) {
          return;
        }
        next.setPendingPass(PassIntent{.passer = PlayerId(1),
                                       .target = {.x = 40.0, .y = 20.0},
                                       .speed = 10.0,
                                       .receiver = std::nullopt});
        if (context.collectsDiagnostics(PlayerId(1))) {
          context.diagnose(DecisionDiagnostic{.tick = SimTick(1),
                                              .player = PlayerId(1),
                                              .observations = {},
                                              .candidates = {},
                                              .outcome = DecisionOutcome::kPassed,
                                              .chosen = std::nullopt,
                                              .scoring = {}});
        }
      }};
  ChallengeConfig always;
  always.intervalTicks = 1;
  always.protectSeconds = 0.0;
  always.winChance = 1.0;
  MatchSimulation simulation(
      {.initialState = *std::move(initial),
       .seed = 1,
       .ticksPerSecond = 30,
       .systems = {fakeDecide, makeChallengeSystem(always), makeBallMovementSystem(BallPhysics{})},
       .commands = {}});
  simulation.setCollectDiagnostics(true);
  DecisionTracer tracer(MatchConfig{});
  REQUIRE(simulation.step().has_value());
  tracer.recordStep(simulation);
  REQUIRE(simulation.step().has_value());
  tracer.recordStep(simulation);

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
  REQUIRE(text.find(" #4 ") != std::string::npos);
}
