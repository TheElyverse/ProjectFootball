#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

#include "kickoffScenario.hpp"
#include "matchCommand.hpp"
#include "matchSimulation.hpp"
#include "matchState.hpp"
#include "vec2.hpp"

using ElyverseFootball::SimCore::PlayerId;
using ElyverseFootball::SimCore::SimTick;
using ElyverseFootball::SimCore::Vec2;
using ElyverseFootball::SimMatch::kDefaultTicksPerSecond;
using ElyverseFootball::SimMatch::makeSevenASideKickoff;
using ElyverseFootball::SimMatch::MatchCommandErrorCode;
using ElyverseFootball::SimMatch::MatchSimulation;
using ElyverseFootball::SimMatch::MatchState;
using ElyverseFootball::SimMatch::MatchStateWriter;
using ElyverseFootball::SimMatch::MatchStepContext;
using ElyverseFootball::SimMatch::MatchSystem;
using ElyverseFootball::SimMatch::MovePlayerCommand;
using ElyverseFootball::SimMatch::Pitch;
using ElyverseFootball::SimMatch::ScheduledCommand;

namespace {

constexpr std::uint64_t kSeed = 7;

[[nodiscard]] MatchState kickoff() {
  auto state = makeSevenASideKickoff(Pitch(60.0, 40.0));
  REQUIRE(state.has_value());
  return *std::move(state);
}

[[nodiscard]] MatchSimulation simulationOf(std::vector<ScheduledCommand> commands,
                                           std::vector<MatchSystem> systems = {}) {
  return MatchSimulation({.initialState = kickoff(),
                          .seed = kSeed,
                          .ticksPerSecond = kDefaultTicksPerSecond,
                          .systems = std::move(systems),
                          .commands = std::move(commands)});
}

[[nodiscard]] ScheduledCommand move(const std::int64_t tick, const std::uint32_t playerId,
                                    const Vec2 target) {
  return {.tick = SimTick(tick),
          .command = MovePlayerCommand{.playerId = PlayerId(playerId), .target = target}};
}

// Player ids 1..14 sit at index id - 1 in the kickoff fixture.
[[nodiscard]] std::optional<Vec2> targetOf(const MatchSimulation& simulation,
                                           const std::uint32_t playerId) {
  return simulation.state().players()[playerId - 1].target;
}

void stepTimes(MatchSimulation& simulation, const int steps) {
  for (int step = 0; step < steps; ++step) {
    REQUIRE(simulation.step().has_value());
  }
}

}  // namespace

TEST_CASE("A move command sets the target in the step of its tick", "[matchCommand]") {
  MatchSimulation simulation = simulationOf({move(2, 4, {.x = 20.0, .y = 10.0})});

  stepTimes(simulation, 2);
  REQUIRE_FALSE(targetOf(simulation, 4).has_value());

  stepTimes(simulation, 1);
  REQUIRE(targetOf(simulation, 4) == Vec2{.x = 20.0, .y = 10.0});
}

TEST_CASE("Systems see a command's effect in the step that applies it", "[matchCommand]") {
  std::vector<std::optional<Vec2>> seen;
  MatchSimulation simulation = simulationOf(
      {move(0, 1, {.x = 5.0, .y = 5.0})},
      {{.name = "watch",
        .update = [&seen](const MatchStepContext&, const MatchState& current, MatchStateWriter&) {
          seen.push_back(current.players().front().target);
        }}});

  stepTimes(simulation, 1);

  REQUIRE(seen == std::vector<std::optional<Vec2>>{Vec2{.x = 5.0, .y = 5.0}});
}

TEST_CASE("A target off the pitch is moved onto its nearest point", "[matchCommand]") {
  MatchSimulation simulation =
      simulationOf({move(0, 2, {.x = -4.0, .y = 12.0}), move(0, 3, {.x = 70.0, .y = 55.0})});

  stepTimes(simulation, 1);

  REQUIRE(targetOf(simulation, 2) == Vec2{.x = 0.0, .y = 12.0});
  REQUIRE(targetOf(simulation, 3) == Vec2{.x = 60.0, .y = 40.0});
}

TEST_CASE("Commands of one tick apply in scheduling order", "[matchCommand]") {
  MatchSimulation simulation = simulationOf({});
  REQUIRE(simulation.schedule(move(1, 5, {.x = 1.0, .y = 1.0})).has_value());
  REQUIRE(simulation.schedule(move(0, 6, {.x = 3.0, .y = 3.0})).has_value());
  REQUIRE(simulation.schedule(move(1, 5, {.x = 2.0, .y = 2.0})).has_value());

  stepTimes(simulation, 2);

  // The later of two commands for the same player and tick wins.
  REQUIRE(targetOf(simulation, 5) == Vec2{.x = 2.0, .y = 2.0});
  REQUIRE(std::vector(simulation.appliedCommands().begin(), simulation.appliedCommands().end()) ==
          std::vector{move(0, 6, {.x = 3.0, .y = 3.0}), move(1, 5, {.x = 1.0, .y = 1.0}),
                      move(1, 5, {.x = 2.0, .y = 2.0})});
}

TEST_CASE("Only applied commands appear in the command log", "[matchCommand]") {
  MatchSimulation simulation =
      simulationOf({move(0, 1, {.x = 1.0, .y = 1.0}), move(10, 1, {.x = 2.0, .y = 2.0})});

  stepTimes(simulation, 5);

  REQUIRE(simulation.appliedCommands().size() == 1);
  REQUIRE(simulation.appliedCommands().front() == move(0, 1, {.x = 1.0, .y = 1.0}));
}

TEST_CASE("schedule rejects invalid commands and keeps the queue", "[matchCommand]") {
  MatchSimulation simulation = simulationOf({});
  stepTimes(simulation, 3);

  SECTION("a tick already simulated") {
    const auto result = simulation.schedule(move(2, 1, {.x = 1.0, .y = 1.0}));
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().code == MatchCommandErrorCode::kTickInPast);
    REQUIRE(result.error().message == "command for tick 2 arrives at tick 3");
  }
  SECTION("an unknown player") {
    const auto result = simulation.schedule(move(3, 99, {.x = 1.0, .y = 1.0}));
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().code == MatchCommandErrorCode::kUnknownPlayer);
    REQUIRE(result.error().message == "no player with id 99");
  }
  SECTION("a non-finite target") {
    const auto result =
        simulation.schedule(move(3, 1, {.x = std::numeric_limits<double>::infinity(), .y = 1.0}));
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().code == MatchCommandErrorCode::kNonFiniteTarget);
  }

  stepTimes(simulation, 5);
  REQUIRE(simulation.appliedCommands().empty());
  for (std::uint32_t playerId = 1; playerId <= 14; ++playerId) {
    REQUIRE_FALSE(targetOf(simulation, playerId).has_value());
  }
}

TEST_CASE("A command for the current tick applies in the next step", "[matchCommand]") {
  MatchSimulation simulation = simulationOf({});
  stepTimes(simulation, 4);

  REQUIRE(simulation.schedule(move(4, 9, {.x = 44.0, .y = 4.0})).has_value());
  stepTimes(simulation, 1);

  REQUIRE(targetOf(simulation, 9) == Vec2{.x = 44.0, .y = 4.0});
}

TEST_CASE("An invalid command in the spec is rejected at construction", "[matchCommand]") {
  REQUIRE_THROWS_AS(simulationOf({move(0, 1, {.x = 1.0, .y = 1.0}), move(0, 0, {})}),
                    std::invalid_argument);
  REQUIRE_THROWS_AS(simulationOf({move(-1, 1, {.x = 1.0, .y = 1.0})}), std::invalid_argument);
}

TEST_CASE("A failed step applies no command", "[matchCommand]") {
  MatchSimulation simulation = simulationOf(
      {move(1, 1, {.x = 9.0, .y = 9.0})},
      {{.name = "break at tick 1",
        .update = [](const MatchStepContext& context, const MatchState&, MatchStateWriter& next) {
          if (context.tick() == SimTick(1)) {
            next.setBallVelocity({.x = std::numeric_limits<double>::quiet_NaN()});
          }
        }}});
  stepTimes(simulation, 1);

  REQUIRE_FALSE(simulation.step().has_value());

  REQUIRE_FALSE(targetOf(simulation, 1).has_value());
  REQUIRE(simulation.appliedCommands().empty());
}

TEST_CASE("A copy keeps the queued commands", "[matchCommand]") {
  MatchSimulation original = simulationOf({move(3, 7, {.x = 30.0, .y = 30.0})});
  stepTimes(original, 1);

  MatchSimulation copy = original;
  stepTimes(copy, 3);

  REQUIRE(targetOf(copy, 7) == Vec2{.x = 30.0, .y = 30.0});
  REQUIRE_FALSE(targetOf(original, 7).has_value());
}
