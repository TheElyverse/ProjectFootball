#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "kickoffScenario.hpp"
#include "matchSimulation.hpp"
#include "matchState.hpp"
#include "random.hpp"
#include "vec2.hpp"

using ElyverseFootball::SimCore::deriveSeed;
using ElyverseFootball::SimCore::PlayerId;
using ElyverseFootball::SimCore::RandomNumberGenerator;
using ElyverseFootball::SimCore::RandomNumberGeneratorDomain;
using ElyverseFootball::SimCore::SimTick;
using ElyverseFootball::SimCore::Vec2;
using ElyverseFootball::SimMatch::kDefaultTicksPerSecond;
using ElyverseFootball::SimMatch::makeSevenASideKickoff;
using ElyverseFootball::SimMatch::MatchSimulation;
using ElyverseFootball::SimMatch::MatchState;
using ElyverseFootball::SimMatch::MatchStateWriter;
using ElyverseFootball::SimMatch::MatchStepContext;
using ElyverseFootball::SimMatch::MatchSystem;
using ElyverseFootball::SimMatch::Pitch;
using ElyverseFootball::SimMatch::PlayerMatchState;

namespace {

constexpr std::uint64_t kSeed = 42;

[[nodiscard]] MatchState kickoff() {
  auto state = makeSevenASideKickoff(Pitch(60.0, 40.0));
  REQUIRE(state.has_value());
  return *std::move(state);
}

[[nodiscard]] MatchSimulation simulationOf(std::vector<MatchSystem> systems,
                                           MatchState initialState = kickoff(),
                                           const std::uint64_t seed = kSeed) {
  return MatchSimulation({.initialState = std::move(initialState),
                          .seed = seed,
                          .ticksPerSecond = kDefaultTicksPerSecond,
                          .systems = std::move(systems)});
}

void stepTimes(MatchSimulation& simulation, const int steps) {
  for (int step = 0; step < steps; ++step) {
    simulation.step();
  }
}

// Every player runs at 5 m/s toward the nearest opponent; ties go to the lower
// id. Reads only the current state, so it is a fair test of order
// independence: min() over distances does not depend on iteration order.
[[nodiscard]] MatchSystem chaseNearestOpponent() {
  return {.name = "chase",
          .update = [](const MatchStepContext&, const MatchState& current, MatchStateWriter& next) {
            constexpr double kSpeed = 5.0;
            for (std::size_t index = 0; const PlayerMatchState& player : current.players()) {
              const PlayerMatchState* target = nullptr;
              double bestDistanceSquared = std::numeric_limits<double>::infinity();
              for (const PlayerMatchState& other : current.players()) {
                if (other.side == player.side) {
                  continue;
                }
                const double distanceSquared = (other.position - player.position).lengthSquared();
                const bool closer = distanceSquared < bestDistanceSquared;
                const bool tieWithLowerId = distanceSquared == bestDistanceSquared &&
                                            target != nullptr &&
                                            other.playerId.value() < target->playerId.value();
                if (closer || tieWithLowerId) {
                  target = &other;
                  bestDistanceSquared = distanceSquared;
                }
              }
              const Vec2 offset = target->position - player.position;
              const double length = offset.length();
              next.setPlayerVelocity(index, length > 0.0 ? offset * (kSpeed / length) : Vec2{});
              ++index;
            }
          }};
}

// Moves players and the ball by their current velocity.
[[nodiscard]] MatchSystem integrate() {
  return {.name = "integrate",
          .update = [](const MatchStepContext& context, const MatchState& current,
                       MatchStateWriter& next) {
            const double tickSeconds = context.secondsPerTick();
            for (std::size_t index = 0; const PlayerMatchState& player : current.players()) {
              next.setPlayerPosition(index, player.position + (player.velocity * tickSeconds));
              ++index;
            }
            next.setBallPosition(current.ball().position + (current.ball().velocity * tickSeconds));
          }};
}

// Gives the ball a new random velocity every tick from the execution stream.
[[nodiscard]] MatchSystem kickBallRandomly() {
  return {.name = "random kick",
          .update = [](const MatchStepContext& context, const MatchState&, MatchStateWriter& next) {
            RandomNumberGenerator& random = context.random(RandomNumberGeneratorDomain::kExecution);
            const double velocityX = random.nextUniform() - 0.5;
            const double velocityY = random.nextUniform() - 0.5;
            next.setBallVelocity({.x = velocityX, .y = velocityY});
          }};
}

// Appends "<name>@<tick>" to the log on every run.
[[nodiscard]] MatchSystem recorder(const std::string& name, std::vector<std::string>* log) {
  return {
      .name = name,
      .update = [name, log](const MatchStepContext& context, const MatchState&, MatchStateWriter&) {
        log->push_back(name + "@" + std::to_string(context.tick().value()));
      }};
}

[[nodiscard]] MatchState withReversedPlayers(const MatchState& state) {
  std::vector<PlayerMatchState> players(state.players().begin(), state.players().end());
  std::ranges::reverse(players);
  auto reversed = MatchState::create({.pitch = state.pitch(),
                                      .players = std::move(players),
                                      .ball = state.ball(),
                                      .playersPerSide = state.playersPerSide()});
  REQUIRE(reversed.has_value());
  return *std::move(reversed);
}

[[nodiscard]] std::map<PlayerId::ValueType, PlayerMatchState> byId(const MatchState& state) {
  std::map<PlayerId::ValueType, PlayerMatchState> players;
  for (const PlayerMatchState& player : state.players()) {
    players.emplace(player.playerId.value(), player);
  }
  return players;
}

}  // namespace

TEST_CASE("Each step advances the simulation by exactly one tick", "[matchSimulation]") {
  MatchSimulation simulation = simulationOf({});
  REQUIRE(simulation.tick() == SimTick(0));

  REQUIRE(simulation.step() == SimTick(1));
  REQUIRE(simulation.tick() == SimTick(1));
  REQUIRE(simulation.step() == SimTick(2));
  REQUIRE(simulation.tick() == SimTick(2));
}

TEST_CASE("300 ticks at 30 Hz are ten seconds of simulation time", "[matchSimulation]") {
  MatchSimulation simulation = simulationOf({});
  REQUIRE(simulation.ticksPerSecond() == 30);

  stepTimes(simulation, 300);

  REQUIRE(simulation.tick() == SimTick(300));
  REQUIRE(simulation.elapsedSeconds() == 10.0);
}

TEST_CASE("The tick rate is a setting", "[matchSimulation]") {
  MatchSimulation simulation(
      {.initialState = kickoff(), .seed = kSeed, .ticksPerSecond = 60, .systems = {}});

  stepTimes(simulation, 300);

  REQUIRE(simulation.ticksPerSecond() == 60);
  REQUIRE(simulation.elapsedSeconds() == 5.0);
}

TEST_CASE("Without systems a step leaves the state unchanged", "[matchSimulation]") {
  MatchSimulation simulation = simulationOf({});

  stepTimes(simulation, 10);

  REQUIRE(simulation.state() == kickoff());
  REQUIRE(simulation.previousState() == kickoff());
}

TEST_CASE("Systems run in registration order on every tick", "[matchSimulation]") {
  std::vector<std::string> log;
  MatchSimulation simulation =
      simulationOf({recorder("c", &log), recorder("a", &log), recorder("b", &log)});

  stepTimes(simulation, 3);

  REQUIRE(log ==
          std::vector<std::string>{"c@0", "a@0", "b@0", "c@1", "a@1", "b@1", "c@2", "a@2", "b@2"});
}

TEST_CASE("Systems read the current state, not what an earlier system wrote", "[matchSimulation]") {
  const double startX = kickoff().ball().position.x;
  std::vector<double> seenByLaterSystem;
  MatchSimulation simulation = simulationOf({
      {.name = "move ball",
       .update =
           [](const MatchStepContext&, const MatchState& current, MatchStateWriter& next) {
             next.setBallPosition(current.ball().position + Vec2{.x = 1.0, .y = 0.0});
           }},
      {.name = "watch ball",
       .update =
           [&seenByLaterSystem](const MatchStepContext&, const MatchState& current,
                                MatchStateWriter&) {
             seenByLaterSystem.push_back(current.ball().position.x);
           }},
  });

  stepTimes(simulation, 2);

  REQUIRE(seenByLaterSystem == std::vector<double>{startX, startX + 1.0});
  REQUIRE(simulation.state().ball().position.x == startX + 2.0);
}

TEST_CASE("previousState is the state before the last step", "[matchSimulation]") {
  MatchSimulation simulation = simulationOf({chaseNearestOpponent(), integrate()});
  stepTimes(simulation, 5);
  const MatchState beforeStep = simulation.state();

  simulation.step();

  REQUIRE(simulation.previousState() == beforeStep);
  REQUIRE_FALSE(simulation.state() == beforeStep);
}

TEST_CASE("The result of a step does not depend on player order", "[matchSimulation]") {
  // Updating players in place, one after another, would let later players
  // react to earlier players' new positions. Reading only the current state
  // gives every player the same view, whatever its index.
  MatchSimulation stateOrder = simulationOf({chaseNearestOpponent(), integrate()});
  MatchSimulation reversedOrder =
      simulationOf({chaseNearestOpponent(), integrate()}, withReversedPlayers(kickoff()));

  stepTimes(stateOrder, 90);
  stepTimes(reversedOrder, 90);

  REQUIRE(byId(stateOrder.state()) == byId(reversedOrder.state()));
  REQUIRE_FALSE(stateOrder.state() == kickoff());
}

TEST_CASE("The same spec reproduces the same match", "[matchSimulation]") {
  MatchSimulation first = simulationOf({kickBallRandomly(), chaseNearestOpponent(), integrate()});
  MatchSimulation second = simulationOf({kickBallRandomly(), chaseNearestOpponent(), integrate()});

  stepTimes(first, 300);
  stepTimes(second, 300);

  REQUIRE(first.state() == second.state());
}

TEST_CASE("A different seed plays a different match", "[matchSimulation]") {
  MatchSimulation first = simulationOf({kickBallRandomly(), integrate()}, kickoff(), 1);
  MatchSimulation second = simulationOf({kickBallRandomly(), integrate()}, kickoff(), 2);

  stepTimes(first, 30);
  stepTimes(second, 30);

  REQUIRE_FALSE(first.state().ball() == second.state().ball());
}

TEST_CASE("A copy continues exactly like the original", "[matchSimulation]") {
  MatchSimulation original =
      simulationOf({kickBallRandomly(), chaseNearestOpponent(), integrate()});
  stepTimes(original, 150);

  MatchSimulation copy = original;
  stepTimes(original, 150);
  stepTimes(copy, 150);

  REQUIRE(copy.tick() == original.tick());
  REQUIRE(copy.state() == original.state());
  REQUIRE(copy.previousState() == original.previousState());
}

TEST_CASE("Systems draw from streams derived from the match seed", "[matchSimulation]") {
  std::vector<std::uint64_t> draws;
  MatchSimulation simulation = simulationOf({
      {.name = "draw",
       .update =
           [&draws](const MatchStepContext& context, const MatchState&, MatchStateWriter&) {
             draws.push_back(context.random(RandomNumberGeneratorDomain::kExecution).nextU64());
             draws.push_back(context.random(RandomNumberGeneratorDomain::kInjuries).nextU64());
           }},
  });

  simulation.step();

  RandomNumberGenerator execution(deriveSeed(kSeed, RandomNumberGeneratorDomain::kExecution));
  RandomNumberGenerator injuries(deriveSeed(kSeed, RandomNumberGeneratorDomain::kInjuries));
  REQUIRE(draws == std::vector<std::uint64_t>{execution.nextU64(), injuries.nextU64()});
}

TEST_CASE("Positions off the pitch are part of a running match", "[matchSimulation]") {
  const Vec2 behindGoalLine{.x = -2.0, .y = 20.0};
  MatchSimulation simulation = simulationOf({
      {.name = "clear ball",
       .update = [behindGoalLine](
                     const MatchStepContext&, const MatchState&,
                     MatchStateWriter& next) { next.setBallPosition(behindGoalLine); }},
  });

  simulation.step();

  REQUIRE(simulation.state().ball().position == behindGoalLine);
}

TEST_CASE("A system cannot address a player past the end of the squad", "[matchSimulation]") {
  MatchSimulation simulation = simulationOf({
      {.name = "out of range",
       .update =
           [](const MatchStepContext&, const MatchState& current, MatchStateWriter& next) {
             next.setPlayerPosition(current.players().size(), Vec2{});
           }},
  });

  REQUIRE_THROWS_AS(simulation.step(), std::out_of_range);
}

TEST_CASE("MatchSimulation rejects an invalid configuration", "[matchSimulation]") {
  const auto noop = [](const MatchStepContext&, const MatchState&, MatchStateWriter&) {};

  SECTION("a tick rate below one") {
    REQUIRE_THROWS_AS(
        MatchSimulation(
            {.initialState = kickoff(), .seed = kSeed, .ticksPerSecond = 0, .systems = {}}),
        std::invalid_argument);
  }
  SECTION("a system without a name") {
    REQUIRE_THROWS_AS(simulationOf({{.name = "", .update = noop}}), std::invalid_argument);
  }
  SECTION("a system without an update function") {
    REQUIRE_THROWS_AS(simulationOf({{.name = "empty", .update = {}}}), std::invalid_argument);
  }
  SECTION("two systems with the same name") {
    REQUIRE_THROWS_AS(
        simulationOf({{.name = "twice", .update = noop}, {.name = "twice", .update = noop}}),
        std::invalid_argument);
  }
}
