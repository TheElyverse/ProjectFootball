#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <stdexcept>
#include <utility>
#include <variant>
#include <vector>

#include "ballMovement.hpp"
#include "duel.hpp"
#include "matchEvents.hpp"
#include "matchSimulation.hpp"
#include "matchState.hpp"
#include "playerMovement.hpp"
#include "referenceTactic.hpp"
#include "tactic.hpp"

using Catch::Matchers::WithinAbs;
using ElyverseFootball::SimCore::PlayerId;
using ElyverseFootball::SimCore::SimTick;
using ElyverseFootball::SimCore::Vec2;
using ElyverseFootball::SimMatch::ActionType;
using ElyverseFootball::SimMatch::BallPhysics;
using ElyverseFootball::SimMatch::BallTouch;
using ElyverseFootball::SimMatch::BallWon;
using ElyverseFootball::SimMatch::BallWonCause;
using ElyverseFootball::SimMatch::carrierUtilities;
using ElyverseFootball::SimMatch::defenderUtilities;
using ElyverseFootball::SimMatch::Duel;
using ElyverseFootball::SimMatch::DuelConfig;
using ElyverseFootball::SimMatch::DuelEnded;
using ElyverseFootball::SimMatch::DuelGeometry;
using ElyverseFootball::SimMatch::DuelMove;
using ElyverseFootball::SimMatch::DuelOutcome;
using ElyverseFootball::SimMatch::DuelStarted;
using ElyverseFootball::SimMatch::isStumbling;
using ElyverseFootball::SimMatch::LungeResult;
using ElyverseFootball::SimMatch::makeBallMovementSystem;
using ElyverseFootball::SimMatch::makeDuelSystem;
using ElyverseFootball::SimMatch::makePlayerMovementSystem;
using ElyverseFootball::SimMatch::MatchEvent;
using ElyverseFootball::SimMatch::MatchSimulation;
using ElyverseFootball::SimMatch::MatchState;
using ElyverseFootball::SimMatch::MatchStateWriter;
using ElyverseFootball::SimMatch::MatchStepContext;
using ElyverseFootball::SimMatch::MatchSystem;
using ElyverseFootball::SimMatch::moveDirection;
using ElyverseFootball::SimMatch::moveTarget;
using ElyverseFootball::SimMatch::PassAttempted;
using ElyverseFootball::SimMatch::PassIntent;
using ElyverseFootball::SimMatch::Pitch;
using ElyverseFootball::SimMatch::PlayerAction;
using ElyverseFootball::SimMatch::PlayerAttributes;
using ElyverseFootball::SimMatch::PlayerMatchState;
using ElyverseFootball::SimMatch::PossessionChanged;
using ElyverseFootball::SimMatch::resolveLunge;
using ElyverseFootball::SimMatch::TeamSide;
using ElyverseFootball::SimMatch::threat;
using ElyverseFootball::SimMatch::validate;
using ElyverseFootball::SimMatch::wayClearance;
using ElyverseFootball::SimTactics::referenceTacticSpec;
using ElyverseFootball::SimTactics::Tactic;

namespace {

// Home's carrier at (30, 20), attacking the goal at x = 60, and away's
// defender at `defender`; the ball half a meter ahead of the carrier, here
// facing the defender, or behind him, shielded, or beside him.
[[nodiscard]] DuelGeometry geometry(const Vec2 ball, const Vec2 defender = {.x = 31.5, .y = 20.0}) {
  return {.carrier = {.x = 30.0, .y = 20.0},
          .defender = defender,
          .ball = ball,
          .goal = {.x = 60.0, .y = 20.0}};
}

constexpr Vec2 kInFront{.x = 30.5, .y = 20.0};
constexpr Vec2 kBehind{.x = 29.5, .y = 20.0};
constexpr Vec2 kBeside{.x = 30.0, .y = 20.5};

// One a side: home's carrier 1 at (30, 20) facing `facing`, the ball at his
// feet, touched at tick 0 -- in his hands if `held` --, and away's player 2
// at `defender`, as strong as `defenderStrength`. Only a goalkeeper holds the
// ball, so a held ball makes the carrier home's keeper in the reference
// tactic, with six idle players a side by the touchline to fill the squads.
[[nodiscard]] MatchState oneOnOne(const Vec2 defender, const Vec2 facing = {.x = 1.0, .y = 0.0},
                                  const bool held = false,
                                  const double defenderStrength = PlayerAttributes{}.strength) {
  const auto player = [](const PlayerId::ValueType number, const TeamSide side, const Vec2 position,
                         const Vec2 facingAlong) {
    return PlayerMatchState{.playerId = PlayerId(number),
                            .side = side,
                            .position = position,
                            .velocity = {},
                            .attributes = {},
                            .target = std::nullopt,
                            .facing = facingAlong};
  };
  std::vector<PlayerMatchState> players{
      player(1, TeamSide::kHome, {.x = 30.0, .y = 20.0}, facing),
      player(2, TeamSide::kAway, defender, {.x = -1.0, .y = 0.0})};
  players[1].attributes.strength = defenderStrength;
  std::optional<Tactic> tactic;
  if (held) {
    for (PlayerId::ValueType number = 3; number < 15; ++number) {
      const TeamSide side = number % 2 == 1 ? TeamSide::kHome : TeamSide::kAway;
      players.push_back(
          player(number, side, {.x = 4.0 * static_cast<double>(number), .y = 1.0}, facing));
    }
    tactic = Tactic::create(referenceTacticSpec()).value();
  }
  auto state = MatchState::create(
      {.pitch = Pitch(60.0, 40.0),
       .players = std::move(players),
       .ball = {.position = Vec2{.x = 30.0, .y = 20.0} + (facing * 0.5),
                .velocity = {},
                .owner = PlayerId(1),
                .lastTouch = BallTouch{.playerId = PlayerId(1), .tick = SimTick(0)},
                .held = held},
       .playersPerSide = held ? 7 : 1},
      {.home = std::move(tactic), .away = {}});
  REQUIRE(state.has_value());
  return *std::move(state);
}

// Player 2's decided action, every tick until `until`, then marking.
[[nodiscard]] MatchSystem decide(const ActionType type, const SimTick until = SimTick(1000000)) {
  return {.name = "decide",
          .update = [type, until](const MatchStepContext& context, const MatchState&,
                                  MatchStateWriter& next) {
            next.tactical(1).action =
                PlayerAction{.type = context.tick() < until ? type : ActionType::kMarkOpponent,
                             .target = {.x = 30.5, .y = 20.0},
                             .subject = PlayerId(1),
                             .decidedAt = context.tick(),
                             .withBall = false};
          }};
}

struct Played {
  std::vector<MatchEvent> events;
  std::optional<DuelStarted> started;
  std::optional<DuelEnded> ended;
  // The steps the duel was in progress, by its moves.
  std::size_t steps = 0;
};

// Up to ten seconds of the defender pressing the carrier, until the first
// duel ends.
[[nodiscard]] Played play(const MatchState& initial, const std::uint64_t seed,
                          const DuelConfig& config = {},
                          const ActionType type = ActionType::kPressCarrier,
                          const SimTick until = SimTick(1000000)) {
  MatchSimulation simulation(
      {.initialState = initial,
       .seed = seed,
       .ticksPerSecond = 30,
       .systems = {decide(type, until), makeDuelSystem(config), makePlayerMovementSystem(),
                   makeBallMovementSystem(BallPhysics{})},
       .commands = {}});
  Played played;
  while (simulation.tick() < SimTick(300) && !played.ended) {
    REQUIRE(simulation.step().has_value());
    if (simulation.state().tactical(1).duel) {
      ++played.steps;
    }
    for (const MatchEvent& event : simulation.events()) {
      played.events.push_back(event);
      if (const auto* started = std::get_if<DuelStarted>(&event)) {
        played.started = *started;
      }
      if (const auto* ended = std::get_if<DuelEnded>(&event)) {
        played.ended = *ended;
      }
    }
  }
  return played;
}

}  // namespace

TEST_CASE("The way to the ball is clear in front of the carrier and blocked behind him", "[duel]") {
  const DuelConfig config;
  const double inFront = wayClearance(geometry(kInFront), config);
  const double beside = wayClearance(geometry(kBeside), config);
  // In front the way passes him half a meter off, (0.5 - 0.35) / 0.35.
  REQUIRE_THAT(inFront, WithinAbs(0.15 / 0.35, 1e-12));
  REQUIRE(beside > 0.0);
  REQUIRE(beside < inFront);
  REQUIRE(wayClearance(geometry(kBehind), config) == 0.0);
}

TEST_CASE("A defender threatens most goal-side, close and on his feet", "[duel]") {
  const DuelConfig config;
  // A meter and a half apart: five sixths of the way from breakRadius to
  // contactDistance.
  REQUIRE_THAT(threat(geometry(kInFront), 1.0, config), WithinAbs(1.5 / 1.8, 1e-12));
  REQUIRE_THAT(threat(geometry(kInFront), 0.5, config), WithinAbs(0.75 / 1.8, 1e-12));
  // Level with the carrier he threatens half as much, behind him not at all.
  REQUIRE_THAT(threat(geometry(kInFront, {.x = 30.0, .y = 21.5}), 1.0, config),
               WithinAbs(0.75 / 1.8, 1e-12));
  REQUIRE(threat(geometry(kInFront, {.x = 28.5, .y = 20.0}), 1.0, config) == 0.0);
  REQUIRE(threat(geometry(kInFront, {.x = 33.5, .y = 20.0}), 1.0, config) == 0.0);
}

TEST_CASE("The carrier shields against a threat and accelerates past a beaten defender", "[duel]") {
  const DuelConfig config;
  const Duel balanced{.carrier = PlayerId(1), .since = SimTick(0)};
  const auto threatened = carrierUtilities(geometry(kBehind), balanced, config);
  REQUIRE(threatened[0] > threatened[1]);
  REQUIRE(threatened[0] > threatened[2]);
  Duel defenderDown = balanced;
  defenderDown.defenderBalance = 0.0;
  const auto beaten = carrierUtilities(geometry(kBehind), defenderDown, config);
  REQUIRE(beaten[0] == 0.0);
  REQUIRE(beaten[2] > beaten[1]);
  // Off his own feet he turns no more.
  Duel carrierDown = balanced;
  carrierDown.carrierBalance = 0.0;
  REQUIRE(carrierUtilities(geometry(kBehind), carrierDown, config)[1] == 0.0);
}

TEST_CASE("The defender commits only to a ball within reach and clear of the body", "[duel]") {
  const DuelConfig config;
  const auto exposed = defenderUtilities(geometry(kInFront), 1.0, config);
  REQUIRE(exposed.size() == 2);
  REQUIRE(exposed[1] > exposed[0]);
  // Within reach behind the carrier, but only through him.
  REQUIRE(defenderUtilities(geometry(kBehind, {.x = 31.0, .y = 20.0}), 1.0, config).size() == 1);
  // Beyond tackleReach there is nothing to commit to.
  REQUIRE(defenderUtilities(geometry(kInFront, {.x = 32.5, .y = 20.0}), 1.0, config).size() == 1);
}

TEST_CASE("The moves take the ball away, aside and past, the defender goal-side", "[duel]") {
  const DuelConfig config;
  const DuelGeometry duel = geometry(kInFront);
  // Shielding, he stands with the ball on his far side.
  REQUIRE(moveDirection(DuelMove::kShield, duel) == Vec2{.x = -1.0, .y = 0.0});
  REQUIRE(moveTarget(DuelMove::kShield, duel, config) == duel.carrier);
  const Vec2 aside = moveDirection(DuelMove::kTurnAway, duel);
  REQUIRE(aside.x == 0.0);
  REQUIRE(std::abs(aside.y) == 1.0);
  const Vec2 turn = moveTarget(DuelMove::kTurnAway, duel, config);
  REQUIRE_THAT(turn.x, WithinAbs(30.0, 1e-12));
  REQUIRE_THAT(std::abs(turn.y - 20.0), WithinAbs(2.0, 1e-12));
  // Past the defender who stands in his way, half toward the goal and half
  // to the side he turns to.
  const Vec2 accelerate = moveTarget(DuelMove::kAccelerate, duel, config);
  REQUIRE(accelerate.x > 32.0);
  REQUIRE_THAT(accelerate.y - 20.0,
               WithinAbs(turn.y - 20.0 > 0.0 ? 2.0 * std::sqrt(2.0) : -2.0 * std::sqrt(2.0), 1e-9));
  const Vec2 jockey = moveTarget(DuelMove::kJockey, duel, config);
  REQUIRE_THAT(jockey.x, WithinAbs(31.0, 1e-12));
  REQUIRE_THAT(jockey.y, WithinAbs(20.0, 1e-12));
  REQUIRE(moveTarget(DuelMove::kCommit, duel, config) == kInFront);
  REQUIRE(moveDirection(DuelMove::kJockey, duel) == Vec2{});
}

TEST_CASE("Where the foot lands decides what a lunge does", "[duel]") {
  const DuelConfig config;
  const DuelGeometry exposed = geometry(kInFront);
  // On the ball: won cleanly against an average carrier's hold of 0.6.
  REQUIRE(resolveLunge(exposed, kInFront, 1.0, 0.5, config) == LungeResult::kWon);
  // A tenth off it meets it two thirds as well: loose -- but won from a
  // carrier off balance, and kept by one with a fine touch.
  const Vec2 glancing{.x = 30.5, .y = 20.1};
  REQUIRE(resolveLunge(exposed, glancing, 1.0, 0.5, config) == LungeResult::kWon);
  REQUIRE(resolveLunge(exposed, glancing, 1.0, 1.0, config) == LungeResult::kLoose);
  // Two tenths off, a third as well: loose against an average carrier, won
  // from one off balance, brushed off by one with a fine touch.
  const Vec2 brushing{.x = 30.5, .y = 20.2};
  REQUIRE(resolveLunge(exposed, brushing, 1.0, 0.5, config) == LungeResult::kLoose);
  REQUIRE(resolveLunge(exposed, brushing, 0.4, 0.5, config) == LungeResult::kWon);
  REQUIRE(resolveLunge(exposed, brushing, 1.0, 1.0, config) == LungeResult::kMissed);
  // Through the ball into the man after it is no foul.
  REQUIRE(resolveLunge(exposed, {.x = 30.3, .y = 20.0}, 1.0, 0.5, config) == LungeResult::kLoose);
  // Through the man to a shielded ball is one.
  REQUIRE(resolveLunge(geometry(kBehind), kBehind, 1.0, 0.5, config) == LungeResult::kFoul);
  REQUIRE(resolveLunge(exposed, {.x = 31.0, .y = 21.0}, 1.0, 0.5, config) == LungeResult::kMissed);
}

TEST_CASE("A presser within reach takes the settled carrier on and the duel lasts", "[duel]") {
  std::map<DuelOutcome, std::size_t> outcomes;
  std::size_t longest = 0;
  std::size_t ended = 0;
  for (std::uint64_t seed = 1; seed <= 100; ++seed) {
    CAPTURE(seed);
    const Played played = play(oneOnOne({.x = 31.5, .y = 20.0}), seed);
    // Protected for half a second after his touch at tick 0.
    REQUIRE(played.started == DuelStarted{.tick = SimTick(15),
                                          .carrier = PlayerId(1),
                                          .defender = PlayerId(2),
                                          .position = kInFront});
    longest = std::max(longest, played.steps);
    if (!played.ended) {
      continue;
    }
    ++ended;
    const DuelEnded end = played.ended.value_or(DuelEnded{});
    ++outcomes[end.outcome];
    // In its first step the defender only sizes the carrier up.
    REQUIRE(end.tick > SimTick(15));
    // The ball goes where the outcome says.
    const bool won = std::ranges::any_of(played.events, [](const MatchEvent& event) {
      return std::holds_alternative<BallWon>(event) &&
             std::get<BallWon>(event).cause == BallWonCause::kDuel;
    });
    REQUIRE(won == (end.outcome == DuelOutcome::kWon));
  }
  // Without a pass to play, a carrier who shields the ball can hold a duel
  // up for ten seconds, but rarely does.
  REQUIRE(ended >= 90);
  // Every outcome a lunge can have emerges from the steps of a duel, some of
  // them long. Kept needs a carrier who plays the ball on or gets away.
  REQUIRE(outcomes[DuelOutcome::kWon] > 0);
  REQUIRE(outcomes[DuelOutcome::kLoose] > 0);
  REQUIRE(outcomes[DuelOutcome::kFoul] > 0);
  REQUIRE(longest > 10);
}

TEST_CASE("Duels replay identically", "[duel]") {
  const auto run = [] {
    const Played played = play(oneOnOne({.x = 31.5, .y = 20.0}), 7);
    return std::pair{played.events, played.steps};
  };
  REQUIRE(run() == run());
}

TEST_CASE("Only a presser within reach takes the carrier on", "[duel]") {
  REQUIRE_FALSE(play(oneOnOne({.x = 31.6, .y = 20.0}), 1).started.has_value());
  REQUIRE_FALSE(
      play(oneOnOne({.x = 31.5, .y = 20.0}), 1, {}, ActionType::kMarkOpponent).started.has_value());
}

TEST_CASE("Nobody takes on a keeper who holds the ball in his hands", "[duel]") {
  REQUIRE_FALSE(
      play(oneOnOne({.x = 31.5, .y = 20.0}, {.x = 1.0, .y = 0.0}, true), 1).started.has_value());
}

TEST_CASE("A duel ends kept once the defender lets the carrier go", "[duel]") {
  // He would never commit; at tick 30 he stops pressing.
  DuelConfig patient;
  patient.commitWeight = 0.0;
  patient.jockeyWeight = 5.0;
  const Played played =
      play(oneOnOne({.x = 31.5, .y = 20.0}), 1, patient, ActionType::kPressCarrier, SimTick(30));
  REQUIRE(played.ended.has_value());
  const DuelEnded ended = played.ended.value_or(DuelEnded{});
  REQUIRE(ended.outcome == DuelOutcome::kKept);
  REQUIRE(ended.tick == SimTick(33));
}

TEST_CASE("Nobody takes a ball knocked loose while he is off his feet", "[duel]") {
  std::size_t loose = 0;
  for (std::uint64_t seed = 1; seed <= 100; ++seed) {
    CAPTURE(seed);
    MatchSimulation simulation(
        {.initialState = oneOnOne({.x = 31.5, .y = 20.0}),
         .seed = seed,
         .ticksPerSecond = 30,
         .systems = {decide(ActionType::kPressCarrier), makeDuelSystem({}),
                     makePlayerMovementSystem(), makeBallMovementSystem(BallPhysics{})},
         .commands = {}});
    std::optional<SimTick> knocked;
    while (simulation.tick() < SimTick(300) && !knocked) {
      REQUIRE(simulation.step().has_value());
      for (const MatchEvent& event : simulation.events()) {
        if (const auto* ended = std::get_if<DuelEnded>(&event);
            ended != nullptr && ended->outcome == DuelOutcome::kLoose) {
          knocked = ended->tick;
        }
      }
    }
    if (!knocked) {
      continue;
    }
    ++loose;
    // The carrier is always down, the defender too if his foot knocked it
    // loose; one still on his feet may take it in the very same step.
    REQUIRE(simulation.state().tactical(0).stumbledUntil ==
            SimTick(knocked.value_or(SimTick(0)).value() + 15));
    REQUIRE(simulation.state().ball().owner != PlayerId(1));
    // Whoever is down at the start of a step does not have the ball after it.
    while (isStumbling(simulation.state().tactical(0), simulation.tick())) {
      const SimTick tick = simulation.tick();
      const std::array down{isStumbling(simulation.state().tactical(0), tick),
                            isStumbling(simulation.state().tactical(1), tick)};
      REQUIRE(simulation.step().has_value());
      const auto& owner = simulation.state().ball().owner;
      REQUIRE_FALSE((down[0] && owner == PlayerId(1)));
      REQUIRE_FALSE((down[1] && owner == PlayerId(2)));
    }
  }
  REQUIRE(loose > 0);
}

TEST_CASE("A carrier who turns himself off his feet loses the ball loose at once", "[duel]") {
  // Shielding, he turns from the defender he faces to the far side: the whole
  // turnCost, here all his balance, in the duel's first step.
  DuelConfig dizzy;
  dizzy.turnCost = 1.0;
  dizzy.shieldWeight = 10.0;
  dizzy.turnWeight = 0.0;
  dizzy.accelerateWeight = 0.0;
  dizzy.decisionTemperature = 0.01;
  const Played played = play(oneOnOne({.x = 31.5, .y = 20.0}), 1, dizzy);
  REQUIRE(played.started.has_value());
  REQUIRE(played.ended.has_value());
  const DuelEnded ended = played.ended.value_or(DuelEnded{});
  REQUIRE(ended.outcome == DuelOutcome::kLoose);
  REQUIRE(ended.tick == SimTick(15));
  REQUIRE(std::ranges::any_of(played.events, [](const MatchEvent& event) {
    const auto* changed = std::get_if<PossessionChanged>(&event);
    return changed != nullptr && !changed->newOwner;
  }));
}

TEST_CASE("A defender off his feet pushes nobody off the ball", "[duel]") {
  // A defender far stronger than the carrier, in contact with him, pushes him
  // off his feet within two seconds -- but not while he is off his own feet,
  // as he is from the duel's first step on. The carrier stands, his turns
  // costing nothing.
  DuelConfig standing;
  standing.turnStep = 1e-6;
  standing.accelerateStep = 1e-6;
  standing.turnCost = 0.0;
  const MatchSystem down{.name = "down",
                         .update = [](const MatchStepContext& context, const MatchState& current,
                                      MatchStateWriter& next) {
                           if (current.tactical(1).duel) {
                             next.tactical(1).stumbledUntil = SimTick(context.tick().value() + 2);
                           }
                         }};
  for (std::uint64_t seed = 1; seed <= 20; ++seed) {
    CAPTURE(seed);
    MatchSimulation simulation(
        {.initialState = oneOnOne({.x = 31.0, .y = 20.0}, {.x = 1.0, .y = 0.0}, false, 1.0),
         .seed = seed,
         .ticksPerSecond = 30,
         .systems = {decide(ActionType::kPressCarrier), down, makeDuelSystem(standing),
                     makePlayerMovementSystem(), makeBallMovementSystem(BallPhysics{})},
         .commands = {}});
    while (simulation.tick() < SimTick(150)) {
      REQUIRE(simulation.step().has_value());
      for (const MatchEvent& event : simulation.events()) {
        REQUIRE_FALSE(std::holds_alternative<BallWon>(event));
      }
    }
    REQUIRE(simulation.state().tactical(1).duel.has_value());
    REQUIRE(simulation.state().ball().owner == PlayerId(1));
  }
}

TEST_CASE("A ball won in a duel survives ball movement in the same step", "[duel]") {
  // The carrier has a pass pending, decided a step earlier and still
  // waiting to be played, when a lunge that cannot miss lands. Ball movement
  // runs after the duel in the standard pipeline and must not play that
  // pass, or otherwise put the ball back with the loser.
  DuelConfig sure;
  sure.intervalTicks = 1;
  sure.protectSeconds = 0.0;
  sure.footError = 0.0;
  sure.jockeyWeight = 0.0;
  sure.commitWeight = 100.0;
  sure.decisionTemperature = 0.01;
  const MatchSystem passOnce{
      .name = "pass-once",
      .update = [](const MatchStepContext& context, const MatchState&, MatchStateWriter& next) {
        if (context.tick() == SimTick(1)) {
          next.setPendingAction(0, PassIntent{.passer = PlayerId(1),
                                              .target = {.x = 50.0, .y = 20.0},
                                              .speed = 10.0,
                                              .receiver = std::nullopt});
        }
      }};
  MatchSimulation simulation(
      {.initialState = oneOnOne({.x = 31.0, .y = 20.0}),
       .seed = 1,
       .ticksPerSecond = 30,
       .systems = {decide(ActionType::kPressCarrier), passOnce, makeDuelSystem(sure),
                   makeBallMovementSystem(BallPhysics{})},
       .commands = {}});
  // Tick 0: the presser decides. Tick 1: he takes the carrier on, who
  // decides his pass. Tick 2: he lunges.
  REQUIRE(simulation.step().has_value());
  REQUIRE(simulation.step().has_value());
  REQUIRE(simulation.state().pendingPass().has_value());
  REQUIRE(simulation.state().tactical(1).duel.has_value());
  REQUIRE(simulation.step().has_value());
  const auto events = simulation.events();
  REQUIRE(events.size() == 3);
  REQUIRE(std::get<DuelEnded>(events[0]).outcome == DuelOutcome::kWon);
  REQUIRE(std::get<BallWon>(events[1]).winner == PlayerId(2));
  REQUIRE(std::get<PossessionChanged>(events[2]).newOwner == PlayerId(2));
  REQUIRE(simulation.state().ball().owner == PlayerId(2));
  REQUIRE_FALSE(simulation.state().pendingPass().has_value());
  REQUIRE_FALSE(simulation.state().tactical(1).duel.has_value());
  for (const MatchEvent& event : events) {
    REQUIRE_FALSE(std::holds_alternative<PassAttempted>(event));
  }
}

TEST_CASE("The duel system rejects an invalid configuration", "[duel]") {
  REQUIRE_THROWS_AS(makeDuelSystem({.intervalTicks = 0}), std::invalid_argument);
  DuelConfig config;
  config.engageRadius = 4.0;
  REQUIRE_THROWS_AS(validate(config), std::invalid_argument);
  config = {};
  config.minHold = 0.95;
  REQUIRE_THROWS_AS(validate(config), std::invalid_argument);
  config = {};
  config.decisionTemperature = 0.0;
  REQUIRE_THROWS_AS(validate(config), std::invalid_argument);
  config = {};
  config.footRadius = -0.1;
  REQUIRE_THROWS_AS(validate(config), std::invalid_argument);
  REQUIRE_NOTHROW(makeDuelSystem({}));
}
