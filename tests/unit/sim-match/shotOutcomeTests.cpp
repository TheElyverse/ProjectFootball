// What becomes of a shot in the ball system (docs/shooting.md): a goal, the
// woodwork, a block, a deflection, a save or a miss, each with its events.

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

#include "ballMovement.hpp"
#include "goalFrame.hpp"
#include "ids.hpp"
#include "matchEvents.hpp"
#include "matchSimulation.hpp"
#include "matchState.hpp"
#include "passing.hpp"
#include "pitch.hpp"
#include "playerMovement.hpp"
#include "reception.hpp"
#include "referenceTactic.hpp"
#include "restart.hpp"
#include "shooting.hpp"
#include "simTime.hpp"
#include "tactic.hpp"
#include "vec2.hpp"

using ElyverseFootball::SimCore::PlayerId;
using ElyverseFootball::SimCore::SimTick;
using ElyverseFootball::SimCore::Vec2;
using ElyverseFootball::SimMatch::BallPhysics;
using ElyverseFootball::SimMatch::BallState;
using ElyverseFootball::SimMatch::BallTouch;
using ElyverseFootball::SimMatch::Goal;
using ElyverseFootball::SimMatch::GoalEnd;
using ElyverseFootball::SimMatch::GoalScored;
using ElyverseFootball::SimMatch::isOutOfPlay;
using ElyverseFootball::SimMatch::kBallRadius;
using ElyverseFootball::SimMatch::LooseBallRecovered;
using ElyverseFootball::SimMatch::makeBallMovementSystem;
using ElyverseFootball::SimMatch::makePlayerMovementSystem;
using ElyverseFootball::SimMatch::makeRestartSystem;
using ElyverseFootball::SimMatch::MatchEvent;
using ElyverseFootball::SimMatch::MatchSimulation;
using ElyverseFootball::SimMatch::MatchState;
using ElyverseFootball::SimMatch::MatchStateWriter;
using ElyverseFootball::SimMatch::MatchStepContext;
using ElyverseFootball::SimMatch::MatchSystem;
using ElyverseFootball::SimMatch::PassConfig;
using ElyverseFootball::SimMatch::PassIntercepted;
using ElyverseFootball::SimMatch::PassReceived;
using ElyverseFootball::SimMatch::Pitch;
using ElyverseFootball::SimMatch::PlayerMatchState;
using ElyverseFootball::SimMatch::PossessionChanged;
using ElyverseFootball::SimMatch::ReceptionConfig;
using ElyverseFootball::SimMatch::RestartConfig;
using ElyverseFootball::SimMatch::RestartKind;
using ElyverseFootball::SimMatch::RestartTaken;
using ElyverseFootball::SimMatch::Score;
using ElyverseFootball::SimMatch::ShotAttempted;
using ElyverseFootball::SimMatch::ShotConfig;
using ElyverseFootball::SimMatch::ShotDeflected;
using ElyverseFootball::SimMatch::ShotHitWoodwork;
using ElyverseFootball::SimMatch::ShotIntent;
using ElyverseFootball::SimMatch::ShotOutcome;
using ElyverseFootball::SimMatch::ShotRecord;
using ElyverseFootball::SimMatch::ShotResolved;
using ElyverseFootball::SimMatch::TeamSide;
using ElyverseFootball::SimMatch::TeamTactics;
using ElyverseFootball::SimMatch::WoodworkConfig;
using ElyverseFootball::SimMatch::WoodworkPart;
using ElyverseFootball::SimTactics::referenceTacticSpec;
using ElyverseFootball::SimTactics::Tactic;

namespace {

constexpr Vec2 kShooter{.x = 50.0, .y = 20.0};
// Out of everybody's way.
constexpr Vec2 kFarAway{.x = 3.0, .y = 3.0};

[[nodiscard]] Goal goal() {
  return Pitch(60.0, 40.0).goal(GoalEnd::kMaxX);
}

// Execution without error, so a test can predict the ball exactly.
[[nodiscard]] ShotConfig exact() {
  ShotConfig config;
  config.spreadAtZero = 0.0;
  config.spreadPerMeter = 0.0;
  config.speedError = 0.0;
  config.spinError = 0.0;
  return config;
}

[[nodiscard]] PlayerMatchState playerAt(const std::uint32_t playerId, const TeamSide side,
                                        const Vec2 position) {
  return {.playerId = PlayerId(playerId),
          .side = side,
          .position = position,
          .velocity = {},
          .attributes = {},
          .target = std::nullopt,
          .facing = {.x = side == TeamSide::kHome ? 1.0 : -1.0, .y = 0.0}};
}

// What the shot is up against, and how the ball system is set up.
struct Chance {
  Vec2 shooter = kShooter;
  Vec2 keeper = kFarAway;
  Vec2 defender = kFarAway;
  Vec2 teammate = kFarAway;
  ShotConfig shooting = exact();
  bool restarts = false;
  std::uint64_t seed = 1;
};

// Seven a side on the sandbox pitch, attacking the goal at x = 60. Home 1 has
// the ball; home 2 is the teammate. Away plays the reference tactic, whose
// first slot, away 8, keeps goal; away 9 is the defender. Everyone else
// stands in a far corner, and nobody moves.
[[nodiscard]] MatchState chanceState(const Chance& chance, const BallState& ball) {
  std::vector<PlayerMatchState> players{playerAt(1, TeamSide::kHome, chance.shooter),
                                        playerAt(2, TeamSide::kHome, chance.teammate)};
  for (std::uint32_t id = 3; id <= 7; ++id) {
    players.push_back(playerAt(id, TeamSide::kHome, kFarAway));
  }
  players.push_back(playerAt(8, TeamSide::kAway, chance.keeper));
  players.push_back(playerAt(9, TeamSide::kAway, chance.defender));
  for (std::uint32_t id = 10; id <= 14; ++id) {
    players.push_back(playerAt(id, TeamSide::kAway, kFarAway));
  }
  auto tactic = Tactic::create(referenceTacticSpec());
  REQUIRE(tactic.has_value());
  auto state = MatchState::create({.pitch = Pitch(60.0, 40.0),
                                   .players = std::move(players),
                                   .ball = ball,
                                   .playersPerSide = 7},
                                  TeamTactics{.home = std::nullopt, .away = *std::move(tactic)});
  REQUIRE(state.has_value());
  return *std::move(state);
}

[[nodiscard]] std::vector<MatchSystem> ballSystems(const Chance& chance) {
  const RestartConfig restarts{.enabled = chance.restarts};
  std::vector<MatchSystem> systems{
      makePlayerMovementSystem(),
      makeBallMovementSystem(BallPhysics{}, PassConfig{}, ReceptionConfig{}, restarts,
                             chance.shooting, WoodworkConfig{})};
  if (chance.restarts) {
    systems.push_back(makeRestartSystem(restarts, BallPhysics{}));
  }
  return systems;
}

// A match in which home 1 decides on this shot in the first step, so the ball
// system strikes it in the second.
[[nodiscard]] MatchSimulation shotMatch(const Chance& chance, const ShotIntent& intent) {
  std::vector<MatchSystem> systems{
      {.name = "shot decision",
       .update = [intent](const MatchStepContext& context, const MatchState& /*state*/,
                          MatchStateWriter& next) {
         if (context.tick() == SimTick(0)) {
           next.setPendingAction(0, intent);
         }
       }}};
  std::ranges::move(ballSystems(chance), std::back_inserter(systems));
  return MatchSimulation(
      {.initialState = chanceState(chance, {.position = chance.shooter + Vec2{.x = 0.5, .y = 0.0},
                                            .velocity = {},
                                            .owner = PlayerId(1),
                                            .lastTouch = std::nullopt}),
       .seed = chance.seed,
       .ticksPerSecond = 30,
       .systems = std::move(systems),
       .commands = {}});
}

[[nodiscard]] ShotIntent shotAt(const Vec2 target, const double height, const double speed = 25.0) {
  return {.shooter = PlayerId(1), .target = target, .height = height, .speed = speed};
}

// Every event of this many steps, in order.
[[nodiscard]] std::vector<MatchEvent> play(MatchSimulation& simulation, const int ticks) {
  std::vector<MatchEvent> events;
  for (int tick = 0; tick < ticks; ++tick) {
    REQUIRE(simulation.step().has_value());
    events.insert(events.end(), simulation.events().begin(), simulation.events().end());
  }
  return events;
}

template <typename Event>
[[nodiscard]] std::vector<Event> eventsOf(const std::vector<MatchEvent>& events) {
  std::vector<Event> found;
  for (const MatchEvent& event : events) {
    if (const auto* typed = std::get_if<Event>(&event)) {
      found.push_back(*typed);
    }
  }
  return found;
}

// The one outcome of the one shot the events hold.
[[nodiscard]] ShotOutcome outcomeOf(const std::vector<MatchEvent>& events) {
  const auto attempts = eventsOf<ShotAttempted>(events);
  const auto resolved = eventsOf<ShotResolved>(events);
  REQUIRE(attempts.size() == 1);
  REQUIRE(resolved.size() == 1);
  REQUIRE(resolved.front().shooter == PlayerId(1));
  REQUIRE(resolved.front().shotTick == attempts.front().tick);
  return resolved.front().outcome;
}

}  // namespace

TEST_CASE("A shot inside the frame is a goal", "[shotOutcome]") {
  MatchSimulation simulation = shotMatch({}, shotAt(goal().center, 1.0));
  const auto events = play(simulation, 30);

  REQUIRE(outcomeOf(events) == ShotOutcome::kGoal);
  const auto goals = eventsOf<GoalScored>(events);
  REQUIRE(goals.size() == 1);
  REQUIRE(goals.front().side == TeamSide::kHome);
  REQUIRE(goals.front().scorer == PlayerId(1));
  REQUIRE_FALSE(goals.front().assist.has_value());
  REQUIRE_FALSE(goals.front().ownGoal);
  REQUIRE(goals.front().score == Score{.home = 1, .away = 0});
  REQUIRE(eventsOf<ShotHitWoodwork>(events).empty());
  REQUIRE(eventsOf<ShotDeflected>(events).empty());

  const MatchState& state = simulation.state();
  REQUIRE(state.score() == Score{.home = 1, .away = 0});
  REQUIRE(state.lastGoal().has_value());
  REQUIRE(state.lastShot().has_value());
  REQUIRE(state.lastShot().value_or(ShotRecord{}).resolved);
  // The ball lies on the goal line where it went in: a state play restarts
  // from.
  REQUIRE(isOutOfPlay(state));
  REQUIRE(state.ball().position == goal().center);
}

TEST_CASE("The attempt records the aim and where the ball really left for", "[shotOutcome]") {
  Chance chance;
  chance.shooting = ShotConfig{};
  MatchSimulation simulation = shotMatch(chance, shotAt(goal().center, 1.0));
  const auto attempts = eventsOf<ShotAttempted>(play(simulation, 2));

  REQUIRE(attempts.size() == 1);
  const ShotAttempted& attempt = attempts.front();
  REQUIRE(attempt.tick == SimTick(1));
  REQUIRE(attempt.from == kShooter + Vec2{.x = 0.5, .y = 0.0});
  REQUIRE(attempt.target == goal().center);
  REQUIRE(attempt.height == 1.0);
  REQUIRE(attempt.struckAt.x == 60.0);
  REQUIRE(attempt.struckAt != attempt.target);
  REQUIRE(attempt.struckHeight != attempt.height);
  REQUIRE(attempt.distance == 9.5);
}

TEST_CASE("A goal restarts with a kickoff by the side that conceded", "[shotOutcome]") {
  Chance chance;
  chance.restarts = true;
  // Away 9 is the nearest to the centre spot.
  chance.defender = {.x = 33.0, .y = 20.0};
  MatchSimulation simulation = shotMatch(chance, shotAt(goal().center, 1.0));
  const auto events = play(simulation, 30);

  REQUIRE(outcomeOf(events) == ShotOutcome::kGoal);
  const auto restarts = eventsOf<RestartTaken>(events);
  REQUIRE(restarts.size() == 1);
  REQUIRE(restarts.front().kind == RestartKind::kKickoff);
  REQUIRE(restarts.front().player == PlayerId(9));
  REQUIRE(restarts.front().position == Vec2{.x = 30.0, .y = 20.0});
  REQUIRE(simulation.state().ball().owner == PlayerId(9));
  REQUIRE(simulation.state().score() == Score{.home = 1, .away = 0});
}

TEST_CASE("A shot against the post comes back into play", "[shotOutcome]") {
  Chance chance;
  // Straight in front of the post, so the ball comes straight back.
  chance.shooter = {.x = 50.0, .y = goal().postAtMaxY().y};
  MatchSimulation simulation = shotMatch(chance, shotAt(goal().postAtMaxY(), 0.5));
  const auto events = play(simulation, 90);

  const auto hits = eventsOf<ShotHitWoodwork>(events);
  REQUIRE(hits.size() == 1);
  REQUIRE(hits.front().part == WoodworkPart::kPostAtMaxY);
  REQUIRE(hits.front().shooter == PlayerId(1));
  REQUIRE(hits.front().position.x < 60.0);
  REQUIRE(eventsOf<GoalScored>(events).empty());
  // It rebounds to the shooter, and his own side taking it is no block.
  REQUIRE(outcomeOf(events) == ShotOutcome::kOffTarget);
  REQUIRE(simulation.state().ball().owner == PlayerId(1));
  REQUIRE(simulation.state().score() == Score{});
  // A ball won back from a shot is loose, not a pass.
  REQUIRE(eventsOf<LooseBallRecovered>(events).size() == 1);
  REQUIRE(eventsOf<PassReceived>(events).empty());
}

TEST_CASE("A shot against the crossbar comes back into play", "[shotOutcome]") {
  // Level with the bar's axis.
  const double level = goal().heightMeters + WoodworkConfig{}.radius - kBallRadius;
  MatchSimulation simulation = shotMatch({}, shotAt(goal().center, level));
  const auto events = play(simulation, 20);

  const auto hits = eventsOf<ShotHitWoodwork>(events);
  REQUIRE(hits.size() == 1);
  REQUIRE(hits.front().part == WoodworkPart::kCrossbar);
  REQUIRE(hits.front().height > 1.0);
  REQUIRE(eventsOf<GoalScored>(events).empty());
  REQUIRE(simulation.state().ball().position.x < 60.0);
}

TEST_CASE("A shot wide of the post is off target", "[shotOutcome]") {
  Chance chance;
  chance.restarts = true;
  MatchSimulation simulation =
      shotMatch(chance, shotAt(goal().postAtMaxY() + Vec2{.x = 0.0, .y = 1.0}, 0.5));
  const auto events = play(simulation, 30);

  REQUIRE(outcomeOf(events) == ShotOutcome::kOffTarget);
  REQUIRE(eventsOf<GoalScored>(events).empty());
  REQUIRE(eventsOf<ShotHitWoodwork>(events).empty());
  REQUIRE(simulation.state().score() == Score{});
  // The defending side's goal kick, by its keeper.
  const auto restarts = eventsOf<RestartTaken>(events);
  REQUIRE(restarts.size() == 1);
  REQUIRE(restarts.front().kind == RestartKind::kGoalKick);
  REQUIRE(restarts.front().player == PlayerId(8));
}

TEST_CASE("A shot over the crossbar is off target", "[shotOutcome]") {
  MatchSimulation simulation = shotMatch({}, shotAt(goal().center, goal().heightMeters + 1.0));
  const auto events = play(simulation, 30);

  REQUIRE(outcomeOf(events) == ShotOutcome::kOffTarget);
  REQUIRE(eventsOf<GoalScored>(events).empty());
}

TEST_CASE("A defender in the way blocks a shot instead of taking it", "[shotOutcome]") {
  Chance chance;
  chance.defender = {.x = 54.0, .y = 20.0};
  // The ball dies on him.
  chance.shooting.minDeflectedSpeed = 0.1;
  chance.shooting.maxDeflectedSpeed = 0.1;
  MatchSimulation simulation = shotMatch(chance, shotAt(goal().center, 0.5));
  const auto events = play(simulation, 8);

  const auto deflections = eventsOf<ShotDeflected>(events);
  REQUIRE(deflections.size() == 1);
  REQUIRE(deflections.front().player == PlayerId(9));
  REQUIRE(deflections.front().shooter == PlayerId(1));
  REQUIRE(deflections.front().blocked);
  REQUIRE(deflections.front().position.x < 54.0);
  // The ball changed its flight; it did not change hands.
  REQUIRE_FALSE(simulation.state().ball().owner.has_value());
  REQUIRE(simulation.state().ball().lastTouch.value_or(BallTouch{}).playerId == PlayerId(9));
  REQUIRE(simulation.state().ball().velocity.length() < 3.0);
  REQUIRE(eventsOf<ShotResolved>(events).empty());

  // Whoever gets to the dead ball, the shot was blocked.
  const auto later = play(simulation, 120);
  REQUIRE(eventsOf<ShotResolved>(later).size() == 1);
  REQUIRE(eventsOf<ShotResolved>(later).front().outcome == ShotOutcome::kBlocked);
  REQUIRE(eventsOf<PassIntercepted>(later).empty());
  REQUIRE(eventsOf<PassReceived>(later).empty());
}

TEST_CASE("A deflected shot that goes in is the shooter's goal", "[shotOutcome]") {
  Chance chance;
  chance.defender = {.x = 54.0, .y = 20.0};
  // It glances off him and flies on as it came.
  chance.shooting.minDeflectedSpeed = 0.9;
  chance.shooting.maxDeflectedSpeed = 0.9;
  chance.shooting.deflectionSpread = 0.0;
  chance.shooting.deflectionLift = 0.0;
  MatchSimulation simulation = shotMatch(chance, shotAt(goal().center, 0.5));
  const auto events = play(simulation, 30);

  const auto deflections = eventsOf<ShotDeflected>(events);
  REQUIRE(deflections.size() == 1);
  REQUIRE_FALSE(deflections.front().blocked);
  REQUIRE(outcomeOf(events) == ShotOutcome::kGoal);
  const auto goals = eventsOf<GoalScored>(events);
  REQUIRE(goals.size() == 1);
  REQUIRE(goals.front().scorer == PlayerId(1));
  REQUIRE_FALSE(goals.front().ownGoal);
}

TEST_CASE("A deflected shot that misses was blocked", "[shotOutcome]") {
  int blocked = 0;
  for (std::uint64_t seed = 1; seed <= 40; ++seed) {
    Chance chance;
    chance.defender = {.x = 54.0, .y = 20.0};
    chance.seed = seed;
    MatchSimulation simulation = shotMatch(chance, shotAt(goal().center, 0.5));
    const auto events = play(simulation, 300);

    REQUIRE_FALSE(eventsOf<ShotDeflected>(events).empty());
    const ShotOutcome outcome = outcomeOf(events);
    REQUIRE((outcome == ShotOutcome::kBlocked || outcome == ShotOutcome::kGoal));
    REQUIRE((outcome == ShotOutcome::kGoal) == !eventsOf<GoalScored>(events).empty());
    blocked += outcome == ShotOutcome::kBlocked ? 1 : 0;
  }
  REQUIRE(blocked > 20);
}

TEST_CASE("A shot over a defender's head is not blocked", "[shotOutcome]") {
  Chance chance;
  // Just in front of the goal, where the ball is at its aimed height.
  chance.defender = {.x = 59.0, .y = 20.0};
  chance.shooting.blockReach = 0.8;
  MatchSimulation simulation = shotMatch(chance, shotAt(goal().center, 1.2));
  const auto events = play(simulation, 30);

  REQUIRE(eventsOf<ShotDeflected>(events).empty());
  REQUIRE(outcomeOf(events) == ShotOutcome::kGoal);
}

TEST_CASE("A slow shot is taken at the feet like any ball", "[shotOutcome]") {
  Chance chance;
  chance.defender = {.x = 54.0, .y = 20.0};
  MatchSimulation simulation = shotMatch(chance, shotAt(goal().center, 0.0, 6.0));
  const auto events = play(simulation, 60);

  REQUIRE(eventsOf<ShotDeflected>(events).empty());
  REQUIRE(outcomeOf(events) == ShotOutcome::kBlocked);
  REQUIRE(simulation.state().ball().owner == PlayerId(9));
  REQUIRE(eventsOf<LooseBallRecovered>(events).size() == 1);
  REQUIRE(eventsOf<PassIntercepted>(events).empty());
}

TEST_CASE("The keeper takes a shot at him and saves it", "[shotOutcome]") {
  Chance chance;
  chance.keeper = {.x = 59.0, .y = 20.0};
  MatchSimulation simulation = shotMatch(chance, shotAt(goal().center, 0.3));
  const auto events = play(simulation, 30);

  REQUIRE(outcomeOf(events) == ShotOutcome::kSaved);
  REQUIRE(eventsOf<ShotDeflected>(events).empty());
  REQUIRE(eventsOf<GoalScored>(events).empty());
  REQUIRE(simulation.state().ball().owner == PlayerId(8));
  REQUIRE(simulation.state().score() == Score{});
}

TEST_CASE("A shot the keeper takes on its way past the goal was off target", "[shotOutcome]") {
  Chance chance;
  const Vec2 wide = goal().postAtMaxY() + Vec2{.x = 0.0, .y = 1.0};
  chance.keeper = wide - Vec2{.x = 1.0, .y = 0.0};
  MatchSimulation simulation = shotMatch(chance, shotAt(wide, 0.3));
  const auto events = play(simulation, 30);

  REQUIRE(outcomeOf(events) == ShotOutcome::kOffTarget);
  REQUIRE(simulation.state().ball().owner == PlayerId(8));
}

TEST_CASE("A shot over the keeper's reach beats him", "[shotOutcome]") {
  Chance chance;
  chance.keeper = {.x = 59.0, .y = 20.0};
  // Above what he takes at his feet, and under the crossbar.
  MatchSimulation simulation = shotMatch(chance, shotAt(goal().center, 1.2));
  const auto events = play(simulation, 30);

  REQUIRE(goal().heightMeters > 1.2);
  REQUIRE(outcomeOf(events) == ShotOutcome::kGoal);
}

TEST_CASE("A shot that stops short of the goal is off target", "[shotOutcome]") {
  MatchSimulation simulation = shotMatch({}, shotAt(goal().center, 0.0, 3.0));
  const auto events = play(simulation, 200);

  REQUIRE(outcomeOf(events) == ShotOutcome::kOffTarget);
  REQUIRE(simulation.state().ball().isAtRest());
  REQUIRE(simulation.state().ball().position.x < 60.0);
  // Resolved once: it stays at rest without another outcome.
  REQUIRE(eventsOf<ShotResolved>(play(simulation, 30)).empty());
}

TEST_CASE("Every shot has exactly one outcome, whatever the seed", "[shotOutcome]") {
  for (std::uint64_t seed = 1; seed <= 60; ++seed) {
    CAPTURE(seed);
    Chance chance;
    chance.shooting = ShotConfig{};
    chance.seed = seed;
    chance.keeper = {.x = 59.5, .y = 20.0};
    chance.defender = {.x = 56.0, .y = 21.0};
    // At the post, so the woodwork, the defender and the keeper all get
    // their share.
    MatchSimulation simulation =
        shotMatch(chance, shotAt(goal().postAtMaxY() - Vec2{.x = 0.0, .y = 0.3}, 0.8));
    const auto events = play(simulation, 600);

    const ShotOutcome outcome = outcomeOf(events);
    REQUIRE((outcome == ShotOutcome::kGoal) == (eventsOf<GoalScored>(events).size() == 1));
    REQUIRE((outcome == ShotOutcome::kGoal) == (simulation.state().score().home == 1));
    if (outcome == ShotOutcome::kOffTarget || outcome == ShotOutcome::kSaved) {
      REQUIRE(eventsOf<ShotDeflected>(events).empty());
    }
  }
}

TEST_CASE("A ball rolling into the goal is a goal without a shot", "[shotOutcome]") {
  const auto rolledIn = [](const std::uint32_t lastTouch) {
    const Chance chance;
    MatchSimulation simulation(
        {.initialState = chanceState(
             chance, {.position = {.x = 58.0, .y = 20.0},
                      .velocity = {.x = 10.0, .y = 0.0},
                      .owner = std::nullopt,
                      .lastTouch = BallTouch{.playerId = PlayerId(lastTouch), .tick = SimTick(0)}}),
         .seed = 1,
         .ticksPerSecond = 30,
         .systems = ballSystems(chance),
         .commands = {}});
    auto events = play(simulation, 30);
    REQUIRE(eventsOf<ShotResolved>(events).empty());
    REQUIRE(simulation.state().score() == Score{.home = 1, .away = 0});
    const auto goals = eventsOf<GoalScored>(events);
    REQUIRE(goals.size() == 1);
    return goals.front();
  };

  const GoalScored scored = rolledIn(2);
  REQUIRE(scored.side == TeamSide::kHome);
  REQUIRE(scored.scorer == PlayerId(2));
  REQUIRE_FALSE(scored.ownGoal);

  // Off a defender into his own goal: it counts for the other side.
  const GoalScored own = rolledIn(9);
  REQUIRE(own.side == TeamSide::kHome);
  REQUIRE(own.scorer == PlayerId(9));
  REQUIRE(own.ownGoal);
  REQUIRE_FALSE(own.assist.has_value());
}

TEST_CASE("A ball nobody touched scores for the side attacking that goal", "[shotOutcome]") {
  const Chance chance;
  MatchSimulation simulation(
      {.initialState = chanceState(chance, {.position = {.x = 2.0, .y = 20.0},
                                            .velocity = {.x = -10.0, .y = 0.0},
                                            .owner = std::nullopt,
                                            .lastTouch = std::nullopt}),
       .seed = 1,
       .ticksPerSecond = 30,
       .systems = ballSystems(chance),
       .commands = {}});
  const auto goals = eventsOf<GoalScored>(play(simulation, 30));

  REQUIRE(goals.size() == 1);
  REQUIRE(goals.front().side == TeamSide::kAway);
  REQUIRE_FALSE(goals.front().scorer.has_value());
  REQUIRE(simulation.state().score() == Score{.home = 0, .away = 1});
  // Counted once: the ball stays on the line.
  REQUIRE(eventsOf<GoalScored>(play(simulation, 30)).empty());
}

TEST_CASE("The teammate whose pass the scorer received has the assist", "[shotOutcome]") {
  Chance chance;
  chance.teammate = {.x = 40.0, .y = 20.0};
  // Home 2 passes to home 1, who receives it and then shoots.
  std::vector<MatchSystem> systems{{.name = "shot decision",
                                    .update = [](const MatchStepContext& /*context*/,
                                                 const MatchState& state, MatchStateWriter& next) {
                                      if (state.ball().owner == PlayerId(1) &&
                                          state.lastReception()) {
                                        next.setPendingAction(0, shotAt(goal().center, 1.0));
                                      }
                                    }}};
  std::ranges::move(ballSystems(chance), std::back_inserter(systems));
  MatchSimulation simulation(
      {.initialState = chanceState(chance, {.position = {.x = 40.5, .y = 20.0},
                                            .velocity = {},
                                            .owner = PlayerId(2),
                                            .lastTouch = std::nullopt}),
       .seed = 1,
       .ticksPerSecond = 30,
       .systems = std::move(systems),
       .commands = {
           {.tick = SimTick(0),
            .command = ElyverseFootball::SimMatch::PassCommand{.playerId = PlayerId(2),
                                                               .target = kShooter,
                                                               .speed = 12.0,
                                                               .receiver = PlayerId(1)}}}});
  const auto goals = eventsOf<GoalScored>(play(simulation, 90));

  REQUIRE(goals.size() == 1);
  REQUIRE(goals.front().scorer == PlayerId(1));
  REQUIRE(goals.front().assist == PlayerId(2));
}
