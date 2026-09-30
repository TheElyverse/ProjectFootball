// The shot scenarios (docs/shot-decisions.md): player 1's first decision on
// the ball, over many seeds, shoots at a clear chance, passes from a hopeless
// angle, and mostly passes past a defender in the way. A shot blocked as it is
// struck is a loose ball, not an intercepted pass.

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <optional>
#include <string_view>
#include <variant>

#include "ids.hpp"
#include "matchEvents.hpp"
#include "matchSetup.hpp"
#include "matchSimulation.hpp"
#include "passing.hpp"
#include "scenarios.hpp"
#include "shotCandidate.hpp"

using ElyverseFootball::SimCore::PlayerId;
using ElyverseFootball::SimMatch::DecisionDiagnostic;
using ElyverseFootball::SimMatch::DecisionOutcome;
using ElyverseFootball::SimMatch::DiagnosticsFilter;
using ElyverseFootball::SimMatch::findScenario;
using ElyverseFootball::SimMatch::LooseBallRecovered;
using ElyverseFootball::SimMatch::MatchSimulation;
using ElyverseFootball::SimMatch::PassConfig;
using ElyverseFootball::SimMatch::PassIntercepted;
using ElyverseFootball::SimMatch::PassReceived;
using ElyverseFootball::SimMatch::ShotAttempted;
using ElyverseFootball::SimMatch::ShotCandidate;
using ElyverseFootball::SimMatch::ShotRejection;
using ElyverseFootball::SimMatch::startMatch;

namespace {

constexpr std::uint64_t kSeeds = 100;
// Two seconds: player 1 decides after holding the ball half a second.
constexpr int kTicks = 60;

// Player 1's first decision in the scenario, and whether a shot followed.
struct FirstDecision {
  std::optional<DecisionDiagnostic> decision;
  bool shotTaken = false;
  double shotSpeed = 0.0;
};

[[nodiscard]] FirstDecision firstDecision(const std::string_view scenario,
                                          const std::uint64_t seed) {
  const auto* definition = findScenario(scenario);
  REQUIRE(definition != nullptr);
  auto setup = definition->make(seed);
  REQUIRE(setup.has_value());
  MatchSimulation simulation = startMatch(*setup);
  simulation.setCollectDiagnostics(true);
  simulation.setDiagnosticsFilter(
      DiagnosticsFilter{.players = {PlayerId(1)}, .from = std::nullopt, .to = std::nullopt});
  FirstDecision first;
  for (int tick = 0; tick < kTicks; ++tick) {
    REQUIRE(simulation.step().has_value());
    for (const DecisionDiagnostic& decision : simulation.diagnostics()) {
      if (!first.decision) {
        first.decision = decision;
      }
    }
    for (const auto& event : simulation.events()) {
      if (const auto* shot = std::get_if<ShotAttempted>(&event)) {
        first.shotTaken = true;
        first.shotSpeed = shot->speed;
      }
    }
  }
  REQUIRE(first.decision.has_value());
  return first;
}

}  // namespace

TEST_CASE("Shots: a clear chance is taken", "[acceptance][shots]") {
  int shots = 0;
  double fastest = 0.0;
  for (std::uint64_t seed = 1; seed <= kSeeds; ++seed) {
    const FirstDecision first = firstDecision("clear-chance", seed);
    fastest = std::max(fastest, first.shotSpeed);
    const DecisionDiagnostic& decision = first.decision.value_or(DecisionDiagnostic{});
    REQUIRE_FALSE(decision.shots.empty());
    REQUIRE(decision.shots.front().isValid());
    if (decision.outcome == DecisionOutcome::kShot) {
      ++shots;
      REQUIRE(first.shotTaken);
    }
  }
  CAPTURE(shots, fastest);
  // An average decision maker takes it at least nine times in ten.
  REQUIRE(shots >= 90);
  // Struck at the speed it was judged at, harder than any pass.
  REQUIRE(fastest > PassConfig{}.maxSpeed);
}

TEST_CASE("Shots: a hopeless angle is passed from", "[acceptance][shots]") {
  for (std::uint64_t seed = 1; seed <= kSeeds; ++seed) {
    const FirstDecision first = firstDecision("hopeless-angle", seed);
    const DecisionDiagnostic& decision = first.decision.value_or(DecisionDiagnostic{});
    REQUIRE_FALSE(decision.shots.empty());
    REQUIRE(decision.shots.front().rejection == ShotRejection::kTooNarrow);
    REQUIRE(decision.outcome == DecisionOutcome::kPassed);
    REQUIRE_FALSE(first.shotTaken);
  }
}

TEST_CASE("Shots: a blocked lane is mostly passed around", "[acceptance][shots]") {
  int shots = 0;
  int passes = 0;
  int blocked = 0;
  for (std::uint64_t seed = 1; seed <= kSeeds; ++seed) {
    const DecisionDiagnostic decision =
        firstDecision("blocked-lane", seed).decision.value_or(DecisionDiagnostic{});
    REQUIRE_FALSE(decision.shots.empty());
    // The middle of the goal, low: straight past the defender.
    const auto middle = std::ranges::find_if(decision.shots, [](const ShotCandidate& shot) {
      return shot.column == 3 && shot.row == 0;
    });
    REQUIRE(middle != decision.shots.end());
    blocked += middle != decision.shots.end() && middle->blockRisk >= 0.5 ? 1 : 0;
    shots += decision.outcome == DecisionOutcome::kShot ? 1 : 0;
    passes += decision.outcome == DecisionOutcome::kPassed ? 1 : 0;
  }
  CAPTURE(shots, passes, blocked);
  REQUIRE(passes > shots);
  REQUIRE(blocked >= 90);
}

TEST_CASE("Shots: a shot blocked as it is struck is a loose ball", "[acceptance][shots]") {
  int blocked = 0;
  for (const std::string_view scenario : {"clear-chance", "blocked-lane"}) {
    const auto* definition = findScenario(scenario);
    REQUIRE(definition != nullptr);
    for (std::uint64_t seed = 1; seed <= kSeeds; ++seed) {
      auto setup = definition->make(seed);
      REQUIRE(setup.has_value());
      MatchSimulation simulation = startMatch(*setup);
      for (int tick = 0; tick < 5 * kTicks; ++tick) {
        REQUIRE(simulation.step().has_value());
        bool shot = false;
        for (const auto& event : simulation.events()) {
          shot = shot || std::holds_alternative<ShotAttempted>(event);
          if (shot) {
            REQUIRE_FALSE(std::holds_alternative<PassIntercepted>(event));
            REQUIRE_FALSE(std::holds_alternative<PassReceived>(event));
            blocked += std::holds_alternative<LooseBallRecovered>(event) ? 1 : 0;
          }
        }
      }
    }
  }
  CAPTURE(blocked);
  // The keeper or the defender in the way takes some shots the tick they are
  // struck.
  REQUIRE(blocked > 0);
}
