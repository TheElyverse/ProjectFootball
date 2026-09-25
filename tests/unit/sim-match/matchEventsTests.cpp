#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdint>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

#include "ballMovement.hpp"
#include "kickoffScenario.hpp"
#include "matchCommand.hpp"
#include "matchEvents.hpp"
#include "matchSetup.hpp"
#include "matchSimulation.hpp"
#include "matchState.hpp"
#include "matchStateHash.hpp"
#include "passing.hpp"
#include "stableHash.hpp"
#include "vec2.hpp"

using ElyverseFootball::SimCore::PlayerId;
using ElyverseFootball::SimCore::SimTick;
using ElyverseFootball::SimCore::StableHasher;
using ElyverseFootball::SimCore::Vec2;
using ElyverseFootball::SimMatch::addEvent;
using ElyverseFootball::SimMatch::BallPhysics;
using ElyverseFootball::SimMatch::DecisionDiagnostic;
using ElyverseFootball::SimMatch::DecisionOutcome;
using ElyverseFootball::SimMatch::eventName;
using ElyverseFootball::SimMatch::eventTick;
using ElyverseFootball::SimMatch::GiveBallCommand;
using ElyverseFootball::SimMatch::hashMatchState;
using ElyverseFootball::SimMatch::LooseBallRecovered;
using ElyverseFootball::SimMatch::makeSevenASideKickoff;
using ElyverseFootball::SimMatch::MatchConfig;
using ElyverseFootball::SimMatch::MatchEvent;
using ElyverseFootball::SimMatch::MatchSetup;
using ElyverseFootball::SimMatch::MatchSimulation;
using ElyverseFootball::SimMatch::MatchState;
using ElyverseFootball::SimMatch::PassAttempted;
using ElyverseFootball::SimMatch::PassCommand;
using ElyverseFootball::SimMatch::PassIntercepted;
using ElyverseFootball::SimMatch::PassReceived;
using ElyverseFootball::SimMatch::Pitch;
using ElyverseFootball::SimMatch::planPassSpeed;
using ElyverseFootball::SimMatch::PlayerMatchState;
using ElyverseFootball::SimMatch::PossessionChanged;
using ElyverseFootball::SimMatch::startMatch;
using ElyverseFootball::SimMatch::TeamSide;

namespace {

[[nodiscard]] PlayerMatchState playerAt(const std::uint32_t playerId, const TeamSide side,
                                        const Vec2 position) {
  return {.playerId = PlayerId(playerId),
          .side = side,
          .position = position,
          .velocity = {},
          .attributes = {},
          .target = std::nullopt,
          .facing = {.x = 1.0, .y = 0.0}};
}

// Two a side: player 1 passes to (30, 20) in tick 1; player 2 waits there,
// player 3 stands where the test puts him. Carriers never decide on their
// own, so the only pass is the commanded one.
[[nodiscard]] MatchSimulation passScene(const Vec2 thirdPlayer) {
  auto state = MatchState::create({.pitch = Pitch(60.0, 40.0),
                                   .players = {playerAt(1, TeamSide::kHome, {.x = 10.0, .y = 20.0}),
                                               playerAt(2, TeamSide::kHome, {.x = 30.0, .y = 20.0}),
                                               playerAt(3, TeamSide::kAway, thirdPlayer),
                                               playerAt(4, TeamSide::kAway, {.x = 50.0, .y = 5.0})},
                                   .ball = {.position = {.x = 10.5, .y = 20.0},
                                            .velocity = {},
                                            .owner = std::nullopt,
                                            .lastTouch = std::nullopt},
                                   .playersPerSide = 2});
  REQUIRE(state.has_value());
  MatchConfig config;
  config.decisions.minHoldSeconds = 1.0e6;
  config.passing.directionError = 0.0;
  config.passing.speedError = 0.0;
  const double speed = planPassSpeed(19.5, BallPhysics{}, config.passing);
  return startMatch(
      {.initialState = *std::move(state),
       .config = config,
       .seed = 3,
       .commands = {{.tick = SimTick(0), .command = GiveBallCommand{.playerId = PlayerId(1)}},
                    {.tick = SimTick(1),
                     .command = PassCommand{.playerId = PlayerId(1),
                                            .target = {.x = 30.0, .y = 20.0},
                                            .speed = speed,
                                            .receiver = PlayerId(2)}}}});
}

// Every event of the next `steps` steps, in order.
[[nodiscard]] std::vector<MatchEvent> eventsOf(MatchSimulation& simulation, const int steps) {
  std::vector<MatchEvent> events;
  for (int step = 0; step < steps; ++step) {
    REQUIRE(simulation.step().has_value());
    events.insert(events.end(), simulation.events().begin(), simulation.events().end());
  }
  return events;
}

template <typename Event>
[[nodiscard]] std::vector<Event> only(const std::vector<MatchEvent>& events) {
  std::vector<Event> matching;
  for (const MatchEvent& event : events) {
    if (const auto* typed = std::get_if<Event>(&event)) {
      matching.push_back(*typed);
    }
  }
  return matching;
}

}  // namespace

TEST_CASE("A completed pass records attempt, reception and possession", "[matchEvents]") {
  MatchSimulation simulation = passScene({.x = 45.0, .y = 35.0});
  const std::vector<MatchEvent> events = eventsOf(simulation, 120);

  REQUIRE(events.size() == 5);
  REQUIRE(events.at(0) ==
          MatchEvent{PossessionChanged{
              .tick = SimTick(0), .previousOwner = std::nullopt, .newOwner = PlayerId(1)}});
  const auto attempts = only<PassAttempted>(events);
  REQUIRE(attempts.size() == 1);
  const PassAttempted& attempt = attempts.front();
  REQUIRE(attempt.tick == SimTick(1));
  REQUIRE(attempt.passer == PlayerId(1));
  REQUIRE(attempt.intendedReceiver == PlayerId(2));
  REQUIRE(attempt.target == Vec2{.x = 30.0, .y = 20.0});
  REQUIRE(attempt.speed > 0.0);
  REQUIRE(events.at(2) ==
          MatchEvent{PossessionChanged{
              .tick = SimTick(1), .previousOwner = PlayerId(1), .newOwner = std::nullopt}});

  const auto received = only<PassReceived>(events);
  REQUIRE(received.size() == 1);
  REQUIRE(received.front().receiver == PlayerId(2));
  REQUIRE(received.front().passer == PlayerId(1));
  REQUIRE(events.back() == MatchEvent{PossessionChanged{.tick = received.front().tick,
                                                        .previousOwner = std::nullopt,
                                                        .newOwner = PlayerId(2)}});
}

TEST_CASE("An intercepted pass records the interceptor", "[matchEvents]") {
  MatchSimulation simulation = passScene({.x = 20.0, .y = 20.5});
  const std::vector<MatchEvent> events = eventsOf(simulation, 120);

  const auto intercepted = only<PassIntercepted>(events);
  REQUIRE(intercepted.size() == 1);
  REQUIRE(intercepted.front().interceptor == PlayerId(3));
  REQUIRE(intercepted.front().passer == PlayerId(1));
  REQUIRE(only<PassReceived>(events).empty());
}

TEST_CASE("A ball nobody played is a loose ball when recovered", "[matchEvents]") {
  auto state = makeSevenASideKickoff(Pitch(60.0, 40.0));
  REQUIRE(state.has_value());
  MatchSimulation simulation =
      startMatch({.initialState = *std::move(state), .config = {}, .seed = 1, .commands = {}});

  const std::vector<MatchEvent> events = eventsOf(simulation, 60);

  const auto recovered = only<LooseBallRecovered>(events);
  REQUIRE(recovered.size() == 1);
  // The two forwards are equally close; the lower id wins.
  REQUIRE(recovered.front().player == PlayerId(7));
}

TEST_CASE("Events carry the tick of their step", "[matchEvents]") {
  MatchSimulation simulation = passScene({.x = 45.0, .y = 35.0});
  for (int step = 0; step < 120; ++step) {
    const SimTick tick = simulation.tick();
    REQUIRE(simulation.step().has_value());
    for (const MatchEvent& event : simulation.events()) {
      REQUIRE(eventTick(event) == tick);
    }
  }
}

TEST_CASE("Event hashes tell events apart", "[matchEvents]") {
  const auto hashOf = [](const MatchEvent& event) {
    StableHasher hasher;
    addEvent(hasher, event);
    return hasher.value();
  };
  const MatchEvent received{
      PassReceived{.tick = SimTick(5), .receiver = PlayerId(2), .passer = PlayerId(1)}};
  const MatchEvent intercepted{
      PassIntercepted{.tick = SimTick(5), .interceptor = PlayerId(2), .passer = PlayerId(1)}};
  const MatchEvent later{
      PassReceived{.tick = SimTick(6), .receiver = PlayerId(2), .passer = PlayerId(1)}};

  REQUIRE(hashOf(received) == hashOf(received));
  REQUIRE(hashOf(received) != hashOf(intercepted));
  REQUIRE(hashOf(received) != hashOf(later));
  REQUIRE(eventName(received) == "pass received");
  REQUIRE(eventName(intercepted) == "pass intercepted");
}

TEST_CASE("Decision diagnostics explain every decision", "[matchEvents]") {
  auto state = makeSevenASideKickoff(Pitch(60.0, 40.0));
  REQUIRE(state.has_value());
  MatchSimulation simulation = startMatch(
      {.initialState = *std::move(state),
       .config = {},
       .seed = 42,
       .commands = {{.tick = SimTick(0), .command = GiveBallCommand{.playerId = PlayerId(4)}}}});
  simulation.setCollectDiagnostics(true);

  int passes = 0;
  for (int step = 0; step < 300; ++step) {
    REQUIRE(simulation.step().has_value());
    for (const DecisionDiagnostic& diagnostic : simulation.diagnostics()) {
      REQUIRE(diagnostic.tick == SimTick(simulation.tick().value() - 1));
      REQUIRE_FALSE(diagnostic.observations.empty());
      if (diagnostic.outcome == DecisionOutcome::kPassed) {
        const std::size_t chosen = diagnostic.chosen.value_or(diagnostic.candidates.size());
        REQUIRE(chosen < diagnostic.candidates.size());
        REQUIRE(diagnostic.candidates.at(chosen).isValid());
        const auto pending = simulation.state().pendingPass();
        REQUIRE(pending.has_value());
        REQUIRE(pending.value_or(ElyverseFootball::SimMatch::PassIntent{}).receiver ==
                diagnostic.candidates.at(chosen).receiver);
        ++passes;
      } else {
        REQUIRE_FALSE(diagnostic.chosen.has_value());
      }
    }
  }
  REQUIRE(passes >= 2);
}

TEST_CASE("Collecting diagnostics changes nothing in the match", "[matchEvents]") {
  const auto run = [](const bool diagnostics) {
    auto state = makeSevenASideKickoff(Pitch(60.0, 40.0));
    REQUIRE(state.has_value());
    MatchSimulation simulation = startMatch(
        {.initialState = *std::move(state),
         .config = {},
         .seed = 42,
         .commands = {{.tick = SimTick(0), .command = GiveBallCommand{.playerId = PlayerId(4)}}}});
    simulation.setCollectDiagnostics(diagnostics);
    std::vector<std::uint64_t> hashes;
    std::vector<MatchEvent> events;
    for (int step = 0; step < 600; ++step) {
      REQUIRE(simulation.step().has_value());
      hashes.push_back(hashMatchState(simulation.state()));
      events.insert(events.end(), simulation.events().begin(), simulation.events().end());
      if (!diagnostics) {
        REQUIRE(simulation.diagnostics().empty());
      }
    }
    return std::pair(hashes, events);
  };

  REQUIRE(run(true) == run(false));
}

TEST_CASE("A failed step publishes no events", "[matchEvents]") {
  auto state = makeSevenASideKickoff(Pitch(60.0, 40.0));
  REQUIRE(state.has_value());
  MatchSimulation simulation(
      {.initialState = *std::move(state),
       .seed = 1,
       .ticksPerSecond = 30,
       .systems = {{.name = "record then break",
                    .update =
                        [](const ElyverseFootball::SimMatch::MatchStepContext& context,
                           const MatchState&, ElyverseFootball::SimMatch::MatchStateWriter& next) {
                          context.record(
                              LooseBallRecovered{.tick = context.tick(), .player = PlayerId(1)});
                          if (context.tick() == SimTick(1)) {
                            next.setBallVelocity({.x = std::nan(""), .y = 0.0});
                          }
                        }}},
       .commands = {}});
  REQUIRE(simulation.step().has_value());
  REQUIRE(simulation.events().size() == 1);

  REQUIRE_FALSE(simulation.step().has_value());

  // Still the events of the last good step.
  REQUIRE(simulation.events().size() == 1);
  REQUIRE(eventTick(simulation.events().front()) == SimTick(0));
}

TEST_CASE("Action diagnostics are kept only when collected", "[matchEvents]") {
  using ElyverseFootball::SimMatch::ActionCandidate;
  using ElyverseFootball::SimMatch::ActionDiagnostic;
  using ElyverseFootball::SimMatch::ActionType;
  using ElyverseFootball::SimMatch::MatchStateWriter;
  using ElyverseFootball::SimMatch::MatchStepContext;
  using ElyverseFootball::SimMatch::MatchSystem;
  const ActionDiagnostic diagnostic{
      .tick = SimTick(0),
      .player = PlayerId(3),
      .candidates = {ActionCandidate{.type = ActionType::kRunInBehind,
                                     .target = {.x = 50.0, .y = 20.0},
                                     .subject = std::nullopt,
                                     .scores = {.responsibility = 1.0},
                                     .utility = 1.0}},
      .chosen = 0};
  const MatchSystem explain{
      .name = "explain",
      .update = [&diagnostic](const MatchStepContext& context, const MatchState&,
                              MatchStateWriter&) { context.diagnose(diagnostic); }};
  const auto kickoff = makeSevenASideKickoff(Pitch(60.0, 40.0));
  REQUIRE(kickoff.has_value());
  MatchSimulation simulation({.initialState = *kickoff,
                              .seed = 1,
                              .ticksPerSecond = 30,
                              .systems = {explain},
                              .commands = {}});
  REQUIRE(simulation.step().has_value());
  REQUIRE(simulation.actionDiagnostics().empty());
  simulation.setCollectDiagnostics(true);
  REQUIRE(simulation.step().has_value());
  REQUIRE(simulation.actionDiagnostics().size() == 1);
  REQUIRE(simulation.actionDiagnostics().front() == diagnostic);
  REQUIRE(simulation.diagnostics().empty());
}

TEST_CASE("An action's dominant score is its largest contribution", "[matchEvents]") {
  using ElyverseFootball::SimMatch::ActionScores;
  using ElyverseFootball::SimMatch::dominantScore;
  REQUIRE(dominantScore({.responsibility = 0.4, .region = -0.9, .lane = 0.8}) == "region");
  REQUIRE(dominantScore({.space = 0.5, .lane = 0.5}) == "space");
  REQUIRE(dominantScore(ActionScores{}) == "responsibility");
  REQUIRE(ActionScores{.responsibility = 1.0, .effort = -0.25}.total() == 0.75);
}
