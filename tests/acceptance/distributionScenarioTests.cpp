// The goalkeeper distribution scenarios (docs/goalkeeper-distribution.md):
// home's keeper, player 1, throws the ball out of his hands to a free centre
// back, and goes long from his feet when a high press closes his short
// options; away's keeper, player 8, takes a goal kick from his goal area.

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <variant>

#include "ids.hpp"
#include "matchEvents.hpp"
#include "matchSetup.hpp"
#include "matchSimulation.hpp"
#include "matchState.hpp"
#include "passCandidate.hpp"
#include "pitch.hpp"
#include "reception.hpp"
#include "restart.hpp"
#include "restartKind.hpp"
#include "scenarios.hpp"
#include "simTime.hpp"
#include "vec2.hpp"

using ElyverseFootball::SimCore::PlayerId;
using ElyverseFootball::SimCore::SimTick;
using ElyverseFootball::SimCore::Vec2;
using ElyverseFootball::SimMatch::AerialContest;
using ElyverseFootball::SimMatch::DecisionDiagnostic;
using ElyverseFootball::SimMatch::DecisionOutcome;
using ElyverseFootball::SimMatch::findScenario;
using ElyverseFootball::SimMatch::GoalEnd;
using ElyverseFootball::SimMatch::goalKickSpot;
using ElyverseFootball::SimMatch::hasReleasedBall;
using ElyverseFootball::SimMatch::LooseBallRecovered;
using ElyverseFootball::SimMatch::MatchEvent;
using ElyverseFootball::SimMatch::MatchSimulation;
using ElyverseFootball::SimMatch::PassAttempted;
using ElyverseFootball::SimMatch::PassCandidate;
using ElyverseFootball::SimMatch::PassIntercepted;
using ElyverseFootball::SimMatch::PassReceived;
using ElyverseFootball::SimMatch::RestartKind;
using ElyverseFootball::SimMatch::RestartTaken;
using ElyverseFootball::SimMatch::startMatch;

namespace {

constexpr std::uint64_t kSeeds = 10;
constexpr PlayerId kHomeKeeper{1};
constexpr PlayerId kAwayKeeper{8};
constexpr std::size_t kAwayKeeperIndex = 7;

[[nodiscard]] MatchSimulation start(const std::string_view scenario, const std::uint64_t seed) {
  const auto* definition = findScenario(scenario);
  REQUIRE(definition != nullptr);
  auto setup = definition->make(seed);
  REQUIRE(setup.has_value());
  MatchSimulation simulation = startMatch(*setup);
  simulation.setCollectDiagnostics(true);
  return simulation;
}

// A keeper's first distribution: the decision that chose it, the pass it
// became, whether he had released the ball from his hands then, how high the
// ball got, and what became of it -- who received, intercepted or recovered
// it, or went up for it.
struct Distribution {
  std::optional<DecisionDiagnostic> decision;
  std::optional<PassAttempted> pass;
  bool released = false;
  double peak = 0.0;
  std::optional<MatchEvent> settled;

  // The option he chose.
  [[nodiscard]] PassCandidate chosen() const {
    REQUIRE(decision.has_value());
    REQUIRE(decision->chosen.has_value());
    return decision->candidates.at(decision->chosen.value_or(0));
  }
};

[[nodiscard]] bool settles(const MatchEvent& event) {
  return std::holds_alternative<PassReceived>(event) ||
         std::holds_alternative<PassIntercepted>(event) ||
         std::holds_alternative<LooseBallRecovered>(event) ||
         std::holds_alternative<AerialContest>(event);
}

// Steps until the keeper's first pass has been settled, or `until`.
[[nodiscard]] Distribution distribute(MatchSimulation& simulation, const PlayerId keeper,
                                      const std::size_t keeperIndex, const SimTick until) {
  Distribution run;
  while (simulation.tick() < until && !run.settled) {
    REQUIRE(simulation.step().has_value());
    if (!run.pass) {
      for (const DecisionDiagnostic& decision : simulation.diagnostics()) {
        if (decision.player == keeper && decision.outcome == DecisionOutcome::kPassed) {
          run.decision = decision;
        }
      }
    }
    for (const MatchEvent& event : simulation.events()) {
      const auto* attempted = std::get_if<PassAttempted>(&event);
      if (!run.pass && attempted != nullptr && attempted->passer == keeper) {
        run.pass = *attempted;
        run.released = hasReleasedBall(simulation.state(), keeperIndex);
      } else if (run.pass && !run.settled && settles(event)) {
        run.settled = event;
      }
    }
    if (run.pass) {
      run.peak = std::max(run.peak, simulation.state().ball().height);
    }
  }
  return run;
}

}  // namespace

TEST_CASE("Distribution: the keeper throws the ball out to a free centre back",
          "[acceptance][distribution]") {
  for (std::uint64_t seed = 1; seed <= kSeeds; ++seed) {
    CAPTURE(seed);
    MatchSimulation simulation = start("keeper-build-up", seed);
    REQUIRE(simulation.state().ball().held);
    const Distribution run = distribute(simulation, kHomeKeeper, 0, SimTick(150));
    REQUIRE(run.decision.has_value());
    REQUIRE(run.decision->held);
    REQUIRE_FALSE(run.chosen().lofted);
    REQUIRE(run.pass.has_value());
    const auto receiver = run.pass.value_or(PassAttempted{}).intendedReceiver;
    REQUIRE((receiver == PlayerId(2) || receiver == PlayerId(3)));
    // Out of his hands, along the ground, and his hands are barred until
    // someone else has touched it.
    REQUIRE(run.released);
    REQUIRE(run.peak == 0.0);
    REQUIRE(run.settled.has_value());
    const auto* received = std::get_if<PassReceived>(&run.settled.value());
    REQUIRE(received != nullptr);
    REQUIRE(received->receiver == receiver);
    REQUIRE_FALSE(simulation.state().tactical(0).handsReleased.has_value());
  }
}

TEST_CASE("Distribution: under a high press the keeper goes long", "[acceptance][distribution]") {
  for (std::uint64_t seed = 1; seed <= kSeeds; ++seed) {
    CAPTURE(seed);
    MatchSimulation simulation = start("keeper-long-kick", seed);
    REQUIRE_FALSE(simulation.state().ball().held);
    const Distribution run = distribute(simulation, kHomeKeeper, 0, SimTick(240));
    REQUIRE(run.decision.has_value());
    REQUIRE_FALSE(run.decision->held);
    REQUIRE(run.chosen().lofted);
    REQUIRE(run.pass.has_value());
    const auto receiver = run.pass.value_or(PassAttempted{}).intendedReceiver;
    REQUIRE((receiver == PlayerId(5) || receiver == PlayerId(6) || receiver == PlayerId(7)));
    // Through the air, and somebody gets to it where it comes down.
    REQUIRE(run.peak > 5.0);
    REQUIRE(run.settled.has_value());
  }
}

TEST_CASE("Distribution: the keeper takes a goal kick from his goal area",
          "[acceptance][distribution]") {
  for (std::uint64_t seed = 1; seed <= kSeeds; ++seed) {
    CAPTURE(seed);
    MatchSimulation simulation = start("goal-kick", seed);
    std::optional<RestartTaken> restart;
    while (simulation.tick() < SimTick(150) && !restart) {
      REQUIRE(simulation.step().has_value());
      for (const MatchEvent& event : simulation.events()) {
        if (const auto* taken = std::get_if<RestartTaken>(&event)) {
          restart = *taken;
        }
      }
    }
    REQUIRE(restart.has_value());
    REQUIRE(restart->kind == RestartKind::kGoalKick);
    REQUIRE(restart->player == kAwayKeeper);

    // The ball lies on the front edge of his goal area, at his feet.
    const auto& state = simulation.state();
    const Vec2 spot = goalKickSpot(state.pitch(), restart->position);
    REQUIRE(state.pitch().goalArea(GoalEnd::kMaxX).contains(spot));
    REQUIRE(state.ball().position == spot);
    REQUIRE(state.ball().owner == kAwayKeeper);
    REQUIRE_FALSE(state.ball().held);
    REQUIRE(state.players()[kAwayKeeperIndex].position.x > spot.x);

    // And he plays it from there.
    const Distribution run = distribute(simulation, kAwayKeeper, kAwayKeeperIndex,
                                        SimTick(simulation.tick().value() + 150));
    REQUIRE(run.pass.has_value());
    REQUIRE(ElyverseFootball::SimCore::distance(run.pass.value_or(PassAttempted{}).from, spot) <
            0.5);
  }
}
