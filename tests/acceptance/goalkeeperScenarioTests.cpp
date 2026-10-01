// The goalkeeper scenarios (docs/goalkeeper.md): away's keeper, player 8,
// moves along the bisector of his goal's angle as the ball crosses in front
// of it, comes for a through ball he reaches first, and leaves one home's
// striker reaches first.

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "ids.hpp"
#include "matchEvents.hpp"
#include "matchSetup.hpp"
#include "matchSimulation.hpp"
#include "matchState.hpp"
#include "pitch.hpp"
#include "scenarios.hpp"
#include "simTime.hpp"
#include "vec2.hpp"

using ElyverseFootball::SimCore::PlayerId;
using ElyverseFootball::SimCore::SimTick;
using ElyverseFootball::SimCore::Vec2;
using ElyverseFootball::SimMatch::findScenario;
using ElyverseFootball::SimMatch::GoalEnd;
using ElyverseFootball::SimMatch::MatchSimulation;
using ElyverseFootball::SimMatch::startMatch;
using ElyverseFootball::SimMatch::SweepDiagnostic;

namespace {

constexpr std::uint64_t kSeeds = 10;
constexpr PlayerId kKeeper{8};
constexpr PlayerId kStriker{2};
constexpr std::size_t kKeeperIndex = 7;

[[nodiscard]] MatchSimulation start(const std::string_view scenario, const std::uint64_t seed) {
  const auto* definition = findScenario(scenario);
  REQUIRE(definition != nullptr);
  auto setup = definition->make(seed);
  REQUIRE(setup.has_value());
  MatchSimulation simulation = startMatch(*setup);
  simulation.setCollectDiagnostics(true);
  return simulation;
}

// How far the keeper stands from the bisector of the angle away's posts make
// as seen from the ball: the line from the ball to the point of the goal line
// that divides the goal in the ratio of the ball's distances to the posts.
[[nodiscard]] double offBisector(const MatchSimulation& simulation) {
  const auto& state = simulation.state();
  const auto goal = state.pitch().goal(GoalEnd::kMaxX);
  const Vec2 ball = state.ball().position;
  const double toLow = (ball - goal.postAtMinY()).length();
  const double toHigh = (ball - goal.postAtMaxY()).length();
  const Vec2 foot =
      goal.postAtMinY() + ((goal.postAtMaxY() - goal.postAtMinY()) * (toLow / (toLow + toHigh)));
  const Vec2 along = ball - foot;
  const Vec2 offset = state.players()[kKeeperIndex].position - foot;
  return std::abs((along.x * offset.y) - (along.y * offset.x)) / along.length();
}

// What the keeper decided about the through ball, in order, and who first had
// it at his feet.
struct SweepRun {
  std::vector<bool> calls;
  std::optional<PlayerId> firstOwner;
  // How far from his goal line he went before anyone had the ball, and how
  // deep his penalty area is.
  double furthestOut = 0.0;
  double areaDepth = 0.0;
};

[[nodiscard]] SweepRun sweep(const std::string_view scenario, const std::uint64_t seed) {
  MatchSimulation simulation = start(scenario, seed);
  SweepRun run;
  const double goalLine = simulation.state().pitch().lengthMeters();
  run.areaDepth = simulation.state().pitch().markings().penaltyAreaDepthMeters;
  while (simulation.tick() < SimTick(150) && !run.firstOwner) {
    REQUIRE(simulation.step().has_value());
    for (const SweepDiagnostic& call : simulation.sweepDiagnostics()) {
      REQUIRE(call.player == kKeeper);
      run.calls.push_back(call.call.coming);
    }
    const auto& owner = simulation.state().ball().owner;
    // Player 1 has the ball until he plays the pass at tick 1.
    if (owner && *owner != PlayerId(1)) {
      run.firstOwner = owner;
    }
    run.furthestOut =
        std::max(run.furthestOut, goalLine - simulation.state().players()[kKeeperIndex].position.x);
  }
  return run;
}

}  // namespace

TEST_CASE("Keeper: he follows the ball across on the bisector of his goal's angle",
          "[acceptance][goalkeeper]") {
  for (std::uint64_t seed = 1; seed <= kSeeds; ++seed) {
    CAPTURE(seed);
    MatchSimulation simulation = start("keeper-arc", seed);
    std::optional<double> lowSide;
    double worst = 0.0;
    while (simulation.tick() < SimTick(270)) {
      REQUIRE(simulation.step().has_value());
      const Vec2 keeper = simulation.state().players()[kKeeperIndex].position;
      // The ball still on the low side of the goal: so is he.
      if (simulation.tick() == SimTick(60)) {
        REQUIRE(simulation.state().ball().position.y < 15.0);
        lowSide = keeper.y;
      }
      // Off his line, short of the edge of his area.
      if (simulation.tick() > SimTick(30)) {
        REQUIRE(keeper.x < 58.0);
        REQUIRE(keeper.x > 60.0 - simulation.state().pitch().markings().penaltyAreaDepthMeters);
      }
      // Carried to the far touchline, the ball has stopped by tick 180: a
      // second later he has settled on the bisector, off it by no more than
      // an average keeper's placing error.
      if (simulation.tick() >= SimTick(210)) {
        worst = std::max(worst, offBisector(simulation));
      }
    }
    REQUIRE(lowSide.value_or(20.0) < 20.0);
    REQUIRE(simulation.state().players()[kKeeperIndex].position.y > 20.0);
    CAPTURE(worst);
    REQUIRE(worst < 0.75);
  }
}

TEST_CASE("Keeper: he comes for a through ball he reaches first", "[acceptance][goalkeeper]") {
  for (std::uint64_t seed = 1; seed <= kSeeds; ++seed) {
    CAPTURE(seed);
    const SweepRun run = sweep("keeper-sweep-claim", seed);
    REQUIRE_FALSE(run.calls.empty());
    REQUIRE(run.calls.front());
    REQUIRE(run.firstOwner == kKeeper);
  }
}

TEST_CASE("Keeper: he leaves a through ball the striker reaches first",
          "[acceptance][goalkeeper]") {
  for (std::uint64_t seed = 1; seed <= kSeeds; ++seed) {
    CAPTURE(seed);
    const SweepRun run = sweep("keeper-sweep-leave", seed);
    REQUIRE_FALSE(run.calls.empty());
    // He stays home and leaves the ball.
    REQUIRE(std::ranges::none_of(run.calls, [](const bool coming) { return coming; }));
    REQUIRE(run.firstOwner == kStriker);
    // Not out to meet the ball: inside his area all along.
    REQUIRE(run.furthestOut < run.areaDepth);
  }
}
