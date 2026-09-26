#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <utility>
#include <variant>
#include <vector>

#include "matchCommand.hpp"
#include "matchEvents.hpp"
#include "matchSetup.hpp"
#include "matchSimulation.hpp"
#include "matchState.hpp"
#include "referenceTactic.hpp"
#include "responsibility.hpp"
#include "tactic.hpp"
#include "tacticalPhase.hpp"
#include "teamPressing.hpp"

using ElyverseFootball::SimCore::PlayerId;
using ElyverseFootball::SimCore::SimTick;
using ElyverseFootball::SimCore::Vec2;
using ElyverseFootball::SimMatch::assignPressRoles;
using ElyverseFootball::SimMatch::GiveBallCommand;
using ElyverseFootball::SimMatch::MatchConfig;
using ElyverseFootball::SimMatch::MatchEvent;
using ElyverseFootball::SimMatch::MatchSetup;
using ElyverseFootball::SimMatch::MatchSimulation;
using ElyverseFootball::SimMatch::MatchState;
using ElyverseFootball::SimMatch::PassCommand;
using ElyverseFootball::SimMatch::Pitch;
using ElyverseFootball::SimMatch::PlayerMatchState;
using ElyverseFootball::SimMatch::PressAssignment;
using ElyverseFootball::SimMatch::PressingConfig;
using ElyverseFootball::SimMatch::PressingEnded;
using ElyverseFootball::SimMatch::PressingStarted;
using ElyverseFootball::SimMatch::pressJoiners;
using ElyverseFootball::SimMatch::PressOutcome;
using ElyverseFootball::SimMatch::PressRole;
using ElyverseFootball::SimMatch::ScheduledCommand;
using ElyverseFootball::SimMatch::spotTrigger;
using ElyverseFootball::SimMatch::startMatch;
using ElyverseFootball::SimMatch::TeamSide;
using ElyverseFootball::SimTactics::PressingTrigger;
using ElyverseFootball::SimTactics::referenceTacticSpec;
using ElyverseFootball::SimTactics::Tactic;
using ElyverseFootball::SimTactics::TacticalPhase;

namespace {

constexpr double kSecondsPerTick = 1.0 / 30.0;

// The reference tactic reacting to the given triggers, pressing with this
// intensity in every defending phase. Its pressing line is so high that it
// never presses just because of the phase: presses in these tests come from
// triggers.
[[nodiscard]] Tactic pressingTactic(std::vector<PressingTrigger> triggers, const double intensity) {
  auto spec = referenceTacticSpec();
  spec.name = "pressing";
  spec.principles.pressingTriggers = std::move(triggers);
  spec.principles.pressingLine = 0.95;
  for (const TacticalPhase phase : {TacticalPhase::kDefensiveBlock, TacticalPhase::kPressing,
                                    TacticalPhase::kDefensiveTransition}) {
    spec.phases.at(ElyverseFootball::SimTactics::phaseIndex(phase)).pressingIntensity = intensity;
  }
  auto tactic = Tactic::create(spec);
  REQUIRE(tactic.has_value());
  return *std::move(tactic);
}

// Home (tactic) around midfield facing +x; away (scripted) with its players
// 8 and 9 where a test wants them and the rest deep in its half.
[[nodiscard]] MatchState scene(const Tactic& home, const Vec2 away8, const Vec2 away9) {
  const std::vector<Vec2> positions{{.x = 3.0, .y = 20.0},
                                    {.x = 15.0, .y = 12.0},
                                    {.x = 15.0, .y = 28.0},
                                    {.x = 22.0, .y = 20.0},
                                    {.x = 26.0, .y = 6.0},
                                    {.x = 26.0, .y = 34.0},
                                    {.x = 28.0, .y = 20.0},
                                    away8,
                                    away9,
                                    {.x = 57.0, .y = 20.0},
                                    {.x = 55.0, .y = 6.0},
                                    {.x = 55.0, .y = 34.0},
                                    {.x = 50.0, .y = 12.0},
                                    {.x = 50.0, .y = 28.0}};
  std::vector<PlayerMatchState> players;
  players.reserve(positions.size());
  for (std::size_t index = 0; index < positions.size(); ++index) {
    const TeamSide side = index < 7 ? TeamSide::kHome : TeamSide::kAway;
    players.push_back({.playerId = PlayerId(static_cast<PlayerId::ValueType>(index + 1)),
                       .side = side,
                       .position = positions.at(index),
                       .velocity = {},
                       .attributes = {},
                       .target = std::nullopt,
                       .facing = {.x = side == TeamSide::kHome ? 1.0 : -1.0, .y = 0.0}});
  }
  auto state = MatchState::create({.pitch = Pitch(60.0, 40.0),
                                   .players = std::move(players),
                                   .ball = {.position = away8,
                                            .velocity = {},
                                            .owner = std::nullopt,
                                            .lastTouch = std::nullopt},
                                   .playersPerSide = 7},
                                  {.home = home, .away = std::nullopt});
  REQUIRE(state.has_value());
  return *std::move(state);
}

// Away's player 8 gets the ball, keeps it and at tick 20 passes to 9 at the
// given speed. Home's pressing system is off (maxJoiners stays, intensity 0
// in the tactic) unless the test's tactic says otherwise.
[[nodiscard]] MatchSimulation passScene(const Tactic& home, const Vec2 away8, const Vec2 away9,
                                        const double speed) {
  MatchConfig config;
  config.decisions.minHoldSeconds = 1.0e6;
  return startMatch(MatchSetup{
      .initialState = scene(home, away8, away9),
      .config = config,
      .seed = 1,
      .commands = {{.tick = SimTick(0), .command = GiveBallCommand{.playerId = PlayerId(8)}},
                   {.tick = SimTick(20),
                    .command = PassCommand{.playerId = PlayerId(8),
                                           .target = away9,
                                           .speed = speed,
                                           .receiver = PlayerId(9)}}}});
}

// Steps until away's player 9 has the ball, and a few ticks more so the
// defenders see him with it.
void untilReceived(MatchSimulation& simulation) {
  for (int tick = 0; tick < 200 && simulation.state().ball().owner != PlayerId(9); ++tick) {
    REQUIRE(simulation.step().has_value());
  }
  REQUIRE(simulation.state().ball().owner == PlayerId(9));
  for (int tick = 0; tick < 4; ++tick) {
    REQUIRE(simulation.step().has_value());
  }
}

[[nodiscard]] std::optional<PressingTrigger> spotted(const MatchSimulation& simulation) {
  const auto trigger = spotTrigger(simulation.state(), TeamSide::kHome, simulation.tick(),
                                   kSecondsPerTick, PressingConfig{}, {});
  return trigger ? std::optional(trigger->trigger) : std::nullopt;
}

}  // namespace

TEST_CASE("Pressing intensity decides how many players join", "[teamPressing]") {
  const PressingConfig config;
  REQUIRE(pressJoiners(0.0, config) == 0);
  REQUIRE(pressJoiners(0.1, config) == 0);
  REQUIRE(pressJoiners(0.25, config) == 1);
  REQUIRE(pressJoiners(0.5, config) == 2);
  REQUIRE(pressJoiners(1.0, config) == 4);
}

TEST_CASE("Roles close the carrier's nearest options", "[teamPressing]") {
  // Away's 8 on the ball at (32, 20), options 9 at (38, 30), 13 at (50, 12),
  // 14 at (50, 28); its goalkeeper 10 is no option.
  const MatchState state =
      scene(pressingTactic({}, 0.0), {.x = 32.0, .y = 20.0}, {.x = 38.0, .y = 30.0});
  const auto roles = [&state](const int joiners) {
    return assignPressRoles(state, TeamSide::kHome,
                            {.carrier = PlayerId(8), .joiners = joiners, .coverDistance = 6.0});
  };
  // One joiner: only the presser, home's striker 7 at (28, 20).
  REQUIRE(roles(1) ==
          std::vector<PressAssignment>{
              {.player = PlayerId(7), .role = PressRole::kPress, .subject = PlayerId(8)}});
  // Two: the presser and a blocker in the lane to the nearest option, 9.
  const auto two = roles(2);
  REQUIRE(two.size() == 2);
  REQUIRE(two.at(1).role == PressRole::kBlockLane);
  REQUIRE(two.at(1).subject == PlayerId(9));
  // Four: presser, a cover reserved before lane blockers are allocated, and
  // two blockers for the two nearest options.
  const auto four = roles(4);
  REQUIRE(four.size() == 4);
  REQUIRE(four.at(1).role == PressRole::kCover);
  REQUIRE(four.at(1).subject == PlayerId(7));
  REQUIRE(four.at(2).role == PressRole::kBlockLane);
  REQUIRE(four.at(2).subject == PlayerId(9));
  REQUIRE(four.at(3).role == PressRole::kBlockLane);
  // Every player once, never the goalkeeper.
  for (std::size_t first = 0; first < four.size(); ++first) {
    REQUIRE(four.at(first).player != PlayerId(1));
    for (std::size_t second = 0; second < first; ++second) {
      REQUIRE(four.at(first).player != four.at(second).player);
    }
  }
  REQUIRE(roles(0).empty());
}

TEST_CASE("Defenders spot each pressing trigger", "[teamPressing]") {
  SECTION("receiver facing his own goal") {
    // 9 receives from 8 behind him and turns to the ball: toward away's goal.
    MatchSimulation simulation =
        passScene(pressingTactic({PressingTrigger::kReceiverFacingOwnGoal}, 0.0),
                  {.x = 45.0, .y = 20.0}, {.x = 33.0, .y = 20.0}, 9.0);
    untilReceived(simulation);
    REQUIRE(spotted(simulation) == PressingTrigger::kReceiverFacingOwnGoal);
  }
  SECTION("back pass") {
    // 9 receives from 8 higher up the pitch: a pass back toward away's goal.
    MatchSimulation simulation = passScene(pressingTactic({PressingTrigger::kBackPass}, 0.0),
                                           {.x = 33.0, .y = 20.0}, {.x = 43.0, .y = 22.0}, 8.0);
    untilReceived(simulation);
    REQUIRE(spotted(simulation) == PressingTrigger::kBackPass);
  }
  SECTION("poor first touch") {
    MatchSimulation simulation = passScene(pressingTactic({PressingTrigger::kPoorFirstTouch}, 0.0),
                                           {.x = 45.0, .y = 30.0}, {.x = 33.0, .y = 12.0}, 20.0);
    untilReceived(simulation);
    REQUIRE(spotted(simulation) == PressingTrigger::kPoorFirstTouch);
  }
  SECTION("isolated receiver") {
    // 9 far from every teammate: 8 stays 18 m away, the rest deeper.
    MatchSimulation simulation =
        passScene(pressingTactic({PressingTrigger::kIsolatedReceiver}, 0.0), {.x = 45.0, .y = 35.0},
                  {.x = 33.0, .y = 20.0}, 11.0);
    untilReceived(simulation);
    REQUIRE(spotted(simulation) == PressingTrigger::kIsolatedReceiver);
  }
  SECTION("slow pass") {
    MatchSimulation simulation = passScene(pressingTactic({PressingTrigger::kSlowPass}, 0.0),
                                           {.x = 40.0, .y = 20.0}, {.x = 33.0, .y = 24.0}, 5.0);
    for (int tick = 0; tick < 24; ++tick) {
      REQUIRE(simulation.step().has_value());
    }
    REQUIRE_FALSE(simulation.state().ball().owner.has_value());
    const auto trigger = spotTrigger(simulation.state(), TeamSide::kHome, simulation.tick(),
                                     kSecondsPerTick, PressingConfig{}, {});
    REQUIRE(trigger.has_value());
    REQUIRE(trigger.value_or(ElyverseFootball::SimMatch::SpottedTrigger{}).carrier == PlayerId(9));
  }
  SECTION("a trigger the tactic does not react to is not spotted") {
    MatchSimulation simulation = passScene(pressingTactic({PressingTrigger::kSlowPass}, 0.0),
                                           {.x = 45.0, .y = 20.0}, {.x = 33.0, .y = 20.0}, 9.0);
    untilReceived(simulation);
    REQUIRE_FALSE(spotted(simulation).has_value());
  }
}

TEST_CASE("A trigger is spotted only by a defender near the ball who sees it", "[teamPressing]") {
  // The same back-to-goal reception as above.
  const auto scenario = [](MatchConfig config) {
    config.decisions.minHoldSeconds = 1.0e6;
    MatchSimulation simulation = startMatch(MatchSetup{
        .initialState = scene(pressingTactic({PressingTrigger::kReceiverFacingOwnGoal}, 0.0),
                              {.x = 45.0, .y = 20.0}, {.x = 33.0, .y = 20.0}),
        .config = config,
        .seed = 1,
        .commands = {{.tick = SimTick(0), .command = GiveBallCommand{.playerId = PlayerId(8)}},
                     {.tick = SimTick(20),
                      .command = PassCommand{.playerId = PlayerId(8),
                                             .target = {.x = 33.0, .y = 20.0},
                                             .speed = 9.0,
                                             .receiver = PlayerId(9)}}}});
    untilReceived(simulation);
    return simulation;
  };
  // Perception updated only at kickoff: nobody has seen the receiver since,
  // so nobody knows he faces his goal.
  MatchConfig blind;
  blind.perception.intervalTicks = 1000;
  REQUIRE_FALSE(spotted(scenario(blind)).has_value());
  // With the usual perception, but nobody within a meter of the ball.
  const MatchSimulation seeing = scenario({});
  PressingConfig near;
  near.triggerRadius = 1.0;
  REQUIRE_FALSE(
      spotTrigger(seeing.state(), TeamSide::kHome, seeing.tick(), kSecondsPerTick, near, {})
          .has_value());
  REQUIRE(spotted(seeing) == PressingTrigger::kReceiverFacingOwnGoal);
}

TEST_CASE("A press starts with its roles and ends with an outcome", "[teamPressing]") {
  MatchConfig config;
  config.decisions.minHoldSeconds = 1.0e6;
  MatchSimulation simulation = startMatch(MatchSetup{
      .initialState = scene(pressingTactic({PressingTrigger::kReceiverFacingOwnGoal}, 1.0),
                            {.x = 45.0, .y = 20.0}, {.x = 33.0, .y = 20.0}),
      .config = config,
      .seed = 2,
      .commands = {{.tick = SimTick(0), .command = GiveBallCommand{.playerId = PlayerId(8)}},
                   {.tick = SimTick(20),
                    .command = PassCommand{.playerId = PlayerId(8),
                                           .target = {.x = 33.0, .y = 20.0},
                                           .speed = 9.0,
                                           .receiver = PlayerId(9)}}}});
  std::optional<PressingStarted> started;
  std::optional<PressingEnded> ended;
  while (simulation.tick() < SimTick(300) && !ended) {
    REQUIRE(simulation.step().has_value());
    for (const MatchEvent& event : simulation.events()) {
      if (const auto* start = std::get_if<PressingStarted>(&event); start != nullptr && !started) {
        started = *start;
        REQUIRE(simulation.state().press(TeamSide::kHome).has_value());
      }
      if (const auto* end = std::get_if<PressingEnded>(&event)) {
        ended = *end;
      }
    }
  }
  REQUIRE(started.has_value());
  const PressingStarted& press = started.value_or(PressingStarted{});
  REQUIRE(press.side == TeamSide::kHome);
  REQUIRE(press.carrier == PlayerId(9));
  REQUIRE(press.trigger == PressingTrigger::kReceiverFacingOwnGoal);
  REQUIRE(press.assignments.size() == 4);
  REQUIRE(press.assignments.front().role == PressRole::kPress);
  REQUIRE(ended.has_value());
  REQUIRE_FALSE(simulation.state().press(TeamSide::kHome).has_value());
}

TEST_CASE("The pressing system rejects an invalid configuration", "[teamPressing]") {
  using ElyverseFootball::SimMatch::validate;
  PressingConfig config;
  config.maxJoiners = 0;
  REQUIRE_THROWS_AS(validate(config), std::invalid_argument);
  config = {};
  config.facingOwnGoal = -2.0;
  REQUIRE_THROWS_AS(validate(config), std::invalid_argument);
  REQUIRE_NOTHROW(validate(PressingConfig{}));
}
