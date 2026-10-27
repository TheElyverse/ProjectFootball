// The stuck-carrier scenarios (docs/on-ball.md#pass-decisions,
// docs/team-tactics.md#pressing): a carrier without a valid option looks round
// for one and, still without one after stuckSeconds, plays the best pass he
// rejected; a carrier who stands on the ball is gone at by the nearest opponent
// even when the opponent's tactic does not press.

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "ids.hpp"
#include "matchEvents.hpp"
#include "matchFixtures.hpp"
#include "matchSetup.hpp"
#include "matchSimulation.hpp"
#include "matchState.hpp"
#include "matchStateHash.hpp"
#include "scenarios.hpp"
#include "simTime.hpp"

using ElyverseFootball::SimCore::PlayerId;
using ElyverseFootball::SimCore::SimTick;
using ElyverseFootball::SimMatch::ActionDiagnostic;
using ElyverseFootball::SimMatch::DecisionDiagnostic;
using ElyverseFootball::SimMatch::DecisionOutcome;
using ElyverseFootball::SimMatch::DuelStarted;
using ElyverseFootball::SimMatch::hashMatchState;
using ElyverseFootball::SimMatch::MatchEvent;
using ElyverseFootball::SimMatch::MatchSetup;
using ElyverseFootball::SimMatch::MatchSimulation;
using ElyverseFootball::SimMatch::OnBallReason;
using ElyverseFootball::SimMatch::PassAttempted;
using ElyverseFootball::SimMatch::startMatch;
using ElyverseFootball::SimMatch::TeamSide;
using ElyverseFootball::TestSupport::setupOf;

namespace {

constexpr std::uint64_t kSeeds = 10;
constexpr PlayerId kCarrier{1};
constexpr double kTicksPerSecond = 30.0;

[[nodiscard]] MatchSimulation start(const MatchSetup& setup) {
  MatchSimulation simulation = startMatch(setup);
  simulation.setCollectDiagnostics(true);
  return simulation;
}

[[nodiscard]] double seconds(const SimTick since, const SimTick until) {
  return static_cast<double>(until.value() - since.value()) / kTicksPerSecond;
}

}  // namespace

TEST_CASE("Stuck carrier: without a valid option he looks round, then plays his best pass",
          "[acceptance][stuckCarrier]") {
  for (std::uint64_t seed = 1; seed <= kSeeds; ++seed) {
    CAPTURE(seed);
    const MatchSetup setup = setupOf("no-option-forced", seed);
    MatchSimulation simulation = start(setup);
    std::vector<DecisionDiagnostic> decisions;
    bool kicked = false;
    while (!kicked && simulation.tick() < SimTick(300)) {
      REQUIRE(simulation.step().has_value());
      for (const DecisionDiagnostic& decision : simulation.diagnostics()) {
        if (decision.player == kCarrier) {
          decisions.push_back(decision);
        }
      }
      for (const MatchEvent& event : simulation.events()) {
        const auto* attempted = std::get_if<PassAttempted>(&event);
        kicked = kicked || (attempted != nullptr && attempted->passer == kCarrier);
      }
    }
    REQUIRE(kicked);
    REQUIRE(decisions.size() >= 2);
    // Every decision before the last looked for an option, alternating sides.
    for (std::size_t index = 0; index + 1 < decisions.size(); ++index) {
      CAPTURE(index);
      const DecisionDiagnostic& decision = decisions[index];
      REQUIRE(decision.outcome == DecisionOutcome::kNoValidOption);
      REQUIRE(decision.target.has_value());
      REQUIRE(decision.reason ==
              (index % 2 == 0 ? OnBallReason::kScanFirstSide : OnBallReason::kScanOtherSide));
    }
    // The last played the best rejected pass once he had looked long enough.
    const DecisionDiagnostic& forced = decisions.back();
    REQUIRE(forced.reason == OnBallReason::kForcedRejected);
    REQUIRE(forced.outcome == DecisionOutcome::kPassed);
    REQUIRE(forced.chosen.has_value());
    const double looked = seconds(decisions.front().tick, forced.tick);
    REQUIRE(looked >= setup.config.decisions.stuckSeconds);
    REQUIRE(looked < setup.config.decisions.stuckSeconds + 0.25);
  }
}

TEST_CASE("Stuck carrier: a side that does not press still goes at a carrier on the ball",
          "[acceptance][stuckCarrier]") {
  for (std::uint64_t seed = 1; seed <= kSeeds; ++seed) {
    CAPTURE(seed);
    const MatchSetup setup = setupOf("idle-carrier-challenged", seed);
    MatchSimulation simulation = start(setup);
    std::optional<SimTick> idlePress;
    std::optional<DuelStarted> duel;
    while (!duel && simulation.tick() < SimTick(450)) {
      REQUIRE(simulation.step().has_value());
      for (const ActionDiagnostic& action : simulation.actionDiagnostics()) {
        if (action.idlePress && !idlePress) {
          idlePress = action.tick;
        }
      }
      for (const MatchEvent& event : simulation.events()) {
        if (const auto* started = std::get_if<DuelStarted>(&event); started != nullptr && !duel) {
          duel = *started;
        }
      }
    }
    // Nobody goes at him before he has stood on the ball for idleSeconds;
    // then the centre back does, and takes him on.
    REQUIRE(idlePress.has_value());
    REQUIRE(seconds(SimTick(0), idlePress.value_or(SimTick(0))) >
            setup.config.defensive.idleSeconds);
    REQUIRE(duel.has_value());
    const DuelStarted started = duel.value_or(DuelStarted{});
    REQUIRE(started.carrier == kCarrier);
    REQUIRE(started.defender == PlayerId(9));
    REQUIRE(started.tick > idlePress.value_or(SimTick(0)));
    REQUIRE(simulation.state().players()[8].side == TeamSide::kAway);
  }
}

TEST_CASE("Stuck carrier: the scenarios run alike every time", "[acceptance][stuckCarrier]") {
  for (const std::string_view name :
       {"no-passing-option", "no-option-forced", "idle-carrier-challenged"}) {
    CAPTURE(name);
    const auto run = [name] {
      MatchSimulation simulation = startMatch(setupOf(name, 7));
      while (simulation.tick() < SimTick(300)) {
        REQUIRE(simulation.step().has_value());
      }
      return hashMatchState(simulation.state());
    };
    REQUIRE(run() == run());
  }
}
