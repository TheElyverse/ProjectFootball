#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <cstddef>
#include <cstdint>
#include <expected>
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
using ElyverseFootball::SimMatch::MatchStateErrorCode;
using ElyverseFootball::SimMatch::MatchStateWriter;
using ElyverseFootball::SimMatch::MatchStepContext;
using ElyverseFootball::SimMatch::MatchStepError;
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
                          .systems = std::move(systems),
                          .commands = {}});
}

void stepTimes(MatchSimulation& simulation, const int steps) {
  for (int step = 0; step < steps; ++step) {
    REQUIRE(simulation.step().has_value());
  }
}

// For tests that expect step() to throw: reaching the end is a failure.
void stepExpectingThrow(MatchSimulation& simulation) {
  const auto result = simulation.step();
  FAIL("step() returned instead of throwing, with a value: " << result.has_value());
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
  MatchSimulation simulation({.initialState = kickoff(),
                              .seed = kSeed,
                              .ticksPerSecond = 60,
                              .systems = {},
                              .commands = {}});

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

TEST_CASE("A system runs every intervalTicks ticks, offset by its phase", "[matchSimulation]") {
  std::vector<std::string> log;
  MatchSystem third = recorder("third", &log);
  third.intervalTicks = 3;
  third.phaseTicks = 1;
  MatchSystem second = recorder("second", &log);
  second.intervalTicks = 2;
  MatchSimulation simulation =
      simulationOf({recorder("every", &log), std::move(third), std::move(second)});

  stepTimes(simulation, 5);

  REQUIRE(log == std::vector<std::string>{"every@0", "second@0", "every@1", "third@1", "every@2",
                                          "second@2", "every@3", "every@4", "third@4", "second@4"});
}

TEST_CASE("Fields keep their values in ticks their system skips", "[matchSimulation]") {
  const double startX = kickoff().ball().position.x;
  MatchSimulation simulation = simulationOf({
      {.name = "nudge ball",
       .update =
           [](const MatchStepContext&, const MatchState& current, MatchStateWriter& next) {
             next.setBallPosition(current.ball().position + Vec2{.x = 1.0, .y = 0.0});
           },
       .intervalTicks = 3},
  });

  stepTimes(simulation, 7);

  // Runs at ticks 0, 3 and 6; in between the ball stays where it was.
  REQUIRE(simulation.state().ball().position.x == startX + 3.0);
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

  REQUIRE(simulation.step().has_value());

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

  REQUIRE(simulation.step().has_value());

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

  REQUIRE(simulation.step().has_value());

  REQUIRE(simulation.state().ball().position == behindGoalLine);
}

TEST_CASE("A non-finite value stops the simulation at the last good state", "[matchSimulation]") {
  std::vector<std::string> log;
  MatchSimulation simulation = simulationOf({
      recorder("before", &log),
      {.name = "break ball",
       .update =
           [](const MatchStepContext& context, const MatchState&, MatchStateWriter& next) {
             if (context.tick() == SimTick(2)) {
               next.setBallPosition({.x = std::numeric_limits<double>::quiet_NaN(), .y = 20.0});
             }
           }},
      recorder("after", &log),
  });
  stepTimes(simulation, 2);
  const MatchState lastGood = simulation.state();
  const MatchState beforeLastGood = simulation.previousState();
  log.clear();

  const auto failed = simulation.step();

  REQUIRE_FALSE(failed.has_value());
  REQUIRE(failed.error().tick == SimTick(2));
  REQUIRE(failed.error().systemName == "break ball");
  REQUIRE(failed.error().errors.size() == 1);
  REQUIRE(failed.error().errors.front().code == MatchStateErrorCode::kNonFiniteBallPosition);
  // The step stopped at the system that broke the state.
  REQUIRE(log == std::vector<std::string>{"before@2"});

  REQUIRE(simulation.hasFailed());
  REQUIRE(simulation.tick() == SimTick(2));
  REQUIRE(simulation.state() == lastGood);
  REQUIRE(simulation.previousState() == beforeLastGood);

  log.clear();
  REQUIRE(simulation.step() == std::unexpected(failed.error()));
  REQUIRE(log.empty());
  REQUIRE(simulation.tick() == SimTick(2));
}

TEST_CASE("A non-finite player value names the player", "[matchSimulation]") {
  MatchSimulation simulation = simulationOf({
      {.name = "sprint",
       .update =
           [](const MatchStepContext&, const MatchState&, MatchStateWriter& next) {
             next.setPlayerVelocity(5, {.x = std::numeric_limits<double>::infinity(), .y = 0.0});
           }},
  });

  const auto failed = simulation.step();

  REQUIRE_FALSE(failed.has_value());
  REQUIRE(failed.error().errors.size() == 1);
  const auto& error = failed.error().errors.front();
  REQUIRE(error.code == MatchStateErrorCode::kNonFinitePlayerVelocity);
  CAPTURE(error.message);
  REQUIRE(error.message.find("player at index 5 (id 6, home)") != std::string::npos);
}

TEST_CASE("A system that throws stops the simulation", "[matchSimulation]") {
  MatchSimulation simulation = simulationOf({
      chaseNearestOpponent(),
      {.name = "throws",
       .update =
           [](const MatchStepContext& context, const MatchState&, MatchStateWriter&) {
             if (context.tick() == SimTick(1)) {
               throw std::runtime_error("broken system");
             }
           }},
      integrate(),
  });
  REQUIRE(simulation.step().has_value());
  const MatchState lastGood = simulation.state();

  REQUIRE_THROWS_AS(stepExpectingThrow(simulation), std::runtime_error);

  REQUIRE(simulation.hasFailed());
  REQUIRE(simulation.tick() == SimTick(1));
  REQUIRE(simulation.state() == lastGood);
  REQUIRE(simulation.step() == std::unexpected(MatchStepError{
                                   .tick = SimTick(1), .systemName = "throws", .errors = {}}));
}

TEST_CASE("A system cannot address a player past the end of the squad", "[matchSimulation]") {
  MatchSimulation simulation = simulationOf({
      {.name = "out of range",
       .update =
           [](const MatchStepContext&, const MatchState& current, MatchStateWriter& next) {
             next.setPlayerPosition(current.players().size(), Vec2{});
           }},
  });

  REQUIRE_THROWS_AS(stepExpectingThrow(simulation), std::out_of_range);
}

TEST_CASE("MatchSimulation rejects an invalid configuration", "[matchSimulation]") {
  const auto noop = [](const MatchStepContext&, const MatchState&, MatchStateWriter&) {};

  SECTION("a tick rate below one") {
    REQUIRE_THROWS_AS(MatchSimulation({.initialState = kickoff(),
                                       .seed = kSeed,
                                       .ticksPerSecond = 0,
                                       .systems = {},
                                       .commands = {}}),
                      std::invalid_argument);
  }
  SECTION("a system without a name") {
    REQUIRE_THROWS_AS(simulationOf({{.name = "", .update = noop}}), std::invalid_argument);
  }
  SECTION("a system without an update function") {
    REQUIRE_THROWS_AS(simulationOf({{.name = "empty", .update = {}}}), std::invalid_argument);
  }
  SECTION("an interval below one tick") {
    const int interval = GENERATE(0, -1);
    REQUIRE_THROWS_AS(simulationOf({{.name = "never", .update = noop, .intervalTicks = interval}}),
                      std::invalid_argument);
  }
  SECTION("a phase outside the interval") {
    const int phase = GENERATE(-1, 3, 4);
    REQUIRE_THROWS_AS(
        simulationOf(
            {{.name = "misplaced", .update = noop, .intervalTicks = 3, .phaseTicks = phase}}),
        std::invalid_argument);
  }
  SECTION("two systems with the same name") {
    REQUIRE_THROWS_AS(
        simulationOf({{.name = "twice", .update = noop}, {.name = "twice", .update = noop}}),
        std::invalid_argument);
  }
}
