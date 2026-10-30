// The shot scenarios (docs/on-ball.md#shot-decisions): player 1's first
// decision on the ball, over many seeds, shoots at a clear chance, passes from
// a hopeless angle, and mostly passes past a defender in the way. A shot
// blocked as it is struck is a loose ball, not an intercepted pass, and a clear
// chance is mostly scored (docs/on-ball.md#shooting).

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>
#include <variant>

#include "ids.hpp"
#include "matchEvents.hpp"
#include "matchSetup.hpp"
#include "matchSimulation.hpp"
#include "matchState.hpp"
#include "passing.hpp"
#include "scenarios.hpp"
#include "shotCandidate.hpp"
#include "shotType.hpp"
#include "stableHash.hpp"

using ElyverseFootball::SimCore::PlayerId;
using ElyverseFootball::SimMatch::addEvent;
using ElyverseFootball::SimMatch::DecisionDiagnostic;
using ElyverseFootball::SimMatch::DecisionOutcome;
using ElyverseFootball::SimMatch::DiagnosticsFilter;
using ElyverseFootball::SimMatch::findScenario;
using ElyverseFootball::SimMatch::LooseBallRecovered;
using ElyverseFootball::SimMatch::MatchSimulation;
using ElyverseFootball::SimMatch::MatchState;
using ElyverseFootball::SimMatch::MatchStateSpec;
using ElyverseFootball::SimMatch::PassConfig;
using ElyverseFootball::SimMatch::PassIntercepted;
using ElyverseFootball::SimMatch::PassReceived;
using ElyverseFootball::SimMatch::ShotAttempted;
using ElyverseFootball::SimMatch::ShotCandidate;
using ElyverseFootball::SimMatch::ShotHitWoodwork;
using ElyverseFootball::SimMatch::ShotOutcome;
using ElyverseFootball::SimMatch::ShotRejection;
using ElyverseFootball::SimMatch::ShotResolved;
using ElyverseFootball::SimMatch::ShotType;
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

// The scenario with player 1 of this decisionMaking, the default if empty
// (docs/on-ball.md#decision-making).
[[nodiscard]] FirstDecision firstDecision(const std::string_view scenario, const std::uint64_t seed,
                                          const std::optional<double> decisionMaking = {}) {
  const auto* definition = findScenario(scenario);
  REQUIRE(definition != nullptr);
  auto setup = definition->make(seed);
  REQUIRE(setup.has_value());
  if (decisionMaking) {
    const MatchState& initial = setup->initialState;
    MatchStateSpec spec{.pitch = initial.pitch(),
                        .players = {initial.players().begin(), initial.players().end()},
                        .ball = initial.ball(),
                        .playersPerSide = initial.playersPerSide(),
                        .score = initial.score()};
    spec.players.front().attributes.decisionMaking = *decisionMaking;
    auto state = MatchState::create(spec, initial.tactics());
    REQUIRE(state.has_value());
    setup->initialState = *std::move(state);
  }
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

TEST_CASE("Shots: decision-making shapes how surely a clear chance is taken",
          "[acceptance][shots]") {
  // How often player 1, of this decisionMaking, shoots first.
  const auto shotsOf = [](const double decisionMaking) {
    int shots = 0;
    for (std::uint64_t seed = 1; seed <= kSeeds; ++seed) {
      const FirstDecision first = firstDecision("clear-chance", seed, decisionMaking);
      if (first.decision && first.decision->outcome == DecisionOutcome::kShot) {
        ++shots;
      }
    }
    return shots;
  };
  const int best = shotsOf(1.0);
  const int average = shotsOf(0.5);
  const int worst = shotsOf(0.0);
  CAPTURE(best, average, worst);
  REQUIRE(best >= 98);
  REQUIRE(average >= 90);
  REQUIRE(std::cmp_greater(worst, kSeeds / 2));
  // So clear a chance leaves little room: the better decider shoots at least
  // as often (the gradient on a closer choice: tests/unit/sim-match/passDecisionTests.cpp).
  REQUIRE(best >= average);
  REQUIRE(average >= worst);
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
      return shot.column == 3 && shot.row == 0 && shot.type == ShotType::kDriven;
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

TEST_CASE("Shots: a blocked shot is a loose ball", "[acceptance][shots]") {
  int blocked = 0;
  for (const std::string_view scenario : {"clear-chance", "blocked-lane"}) {
    const auto* definition = findScenario(scenario);
    REQUIRE(definition != nullptr);
    for (std::uint64_t seed = 1; seed <= kSeeds; ++seed) {
      auto setup = definition->make(seed);
      REQUIRE(setup.has_value());
      MatchSimulation simulation = startMatch(*setup);
      bool flying = false;
      for (int tick = 0; tick < 5 * kTicks; ++tick) {
        REQUIRE(simulation.step().has_value());
        bool resolved = false;
        for (const auto& event : simulation.events()) {
          flying = flying || std::holds_alternative<ShotAttempted>(event);
          resolved = resolved || std::holds_alternative<ShotResolved>(event);
          if (flying) {
            REQUIRE_FALSE(std::holds_alternative<PassIntercepted>(event));
            REQUIRE_FALSE(std::holds_alternative<PassReceived>(event));
            blocked += std::holds_alternative<LooseBallRecovered>(event) ? 1 : 0;
          }
        }
        flying = flying && !resolved;
      }
    }
  }
  CAPTURE(blocked);
  // The keeper or the defender in the way takes some shots on their way.
  REQUIRE(blocked > 0);
}

TEST_CASE("Shots: a clear chance is mostly scored", "[acceptance][shots]") {
  const auto* definition = findScenario("clear-chance");
  REQUIRE(definition != nullptr);
  int shots = 0;
  int goals = 0;
  int onTarget = 0;
  int woodwork = 0;
  for (std::uint64_t seed = 1; seed <= kSeeds; ++seed) {
    auto setup = definition->make(seed);
    REQUIRE(setup.has_value());
    MatchSimulation simulation = startMatch(*setup);
    // Player 1's first shot, until it has become what it will be.
    std::optional<ShotOutcome> outcome;
    bool hit = false;
    for (int tick = 0; tick < 5 * kTicks && !outcome; ++tick) {
      REQUIRE(simulation.step().has_value());
      for (const auto& event : simulation.events()) {
        hit = hit || std::holds_alternative<ShotHitWoodwork>(event);
        if (const auto* resolved = std::get_if<ShotResolved>(&event)) {
          outcome = resolved->outcome;
        }
      }
    }
    if (!outcome) {
      continue;
    }
    ++shots;
    woodwork += hit ? 1 : 0;
    goals += outcome == ShotOutcome::kGoal ? 1 : 0;
    onTarget += outcome == ShotOutcome::kGoal || outcome == ShotOutcome::kSaved ? 1 : 0;
    // A goal is a goal on the scoreboard, and nothing else is.
    REQUIRE((simulation.state().score().home == 1) == (outcome == ShotOutcome::kGoal));
    REQUIRE(simulation.state().score().away == 0);
  }
  CAPTURE(shots, goals, onTarget, woodwork);
  REQUIRE(shots >= 90);
  // Eight meters out with only the keeper to beat: an average finisher scores
  // most of them, and execution makes him miss some -- real football turns
  // about half of such big chances into goals. At the time of writing he
  // scores 54 of 100 and hits the woodwork with 18.
  REQUIRE(goals > 50);
  REQUIRE(goals <= 90);
  REQUIRE(onTarget >= goals);
}

namespace {

// A shot scenario played for four seconds: player 1's first decision, the
// type of the first shot, what became of it, and the hash of every event.
struct PlayedShot {
  DecisionDiagnostic decision;
  std::optional<ShotType> type;
  std::optional<ShotOutcome> outcome;
  std::uint64_t eventHash = 0;
};

[[nodiscard]] PlayedShot playedShot(const std::string_view scenario, const std::uint64_t seed) {
  const auto* definition = findScenario(scenario);
  REQUIRE(definition != nullptr);
  auto setup = definition->make(seed);
  REQUIRE(setup.has_value());
  MatchSimulation simulation = startMatch(*setup);
  simulation.setCollectDiagnostics(true);
  simulation.setDiagnosticsFilter(
      DiagnosticsFilter{.players = {PlayerId(1)}, .from = std::nullopt, .to = std::nullopt});
  PlayedShot played;
  std::optional<DecisionDiagnostic> first;
  ElyverseFootball::SimCore::StableHasher events;
  for (int tick = 0; tick < 120; ++tick) {
    REQUIRE(simulation.step().has_value());
    for (const DecisionDiagnostic& decision : simulation.diagnostics()) {
      if (!first) {
        first = decision;
      }
    }
    for (const auto& event : simulation.events()) {
      addEvent(events, event);
      if (const auto* shot = std::get_if<ShotAttempted>(&event); shot != nullptr && !played.type) {
        played.type = shot->type;
      }
      if (const auto* resolved = std::get_if<ShotResolved>(&event);
          resolved != nullptr && !played.outcome) {
        played.outcome = resolved->outcome;
      }
    }
  }
  REQUIRE(first.has_value());
  played.decision = first.value_or(DecisionDiagnostic{});
  played.eventHash = events.value();
  return played;
}

}  // namespace

TEST_CASE("Shots: a curled shot goes around a wall", "[acceptance][shots]") {
  int curled = 0;
  int blocked = 0;
  for (std::uint64_t seed = 1; seed <= kSeeds; ++seed) {
    const PlayedShot played = playedShot("curl-around-wall", seed);
    const DecisionDiagnostic& decision = played.decision;
    CAPTURE(seed);
    // Every driven shot runs into the wall; the best shot bends around it.
    for (const ShotCandidate& shot : decision.shots) {
      if (shot.type == ShotType::kDriven) {
        REQUIRE(shot.rejection == ShotRejection::kBlocked);
      }
    }
    REQUIRE(decision.shots.front().type == ShotType::kCurled);
    REQUIRE(decision.shots.front().isValid());
    REQUIRE(decision.shots.front().blockRisk < 0.1);
    curled += played.type == ShotType::kCurled ? 1 : 0;
    blocked += played.outcome == ShotOutcome::kBlocked ? 1 : 0;
  }
  CAPTURE(curled, blocked);
  REQUIRE(curled >= 90);
  // It curls past the wall's reach: only a stray one comes off it.
  REQUIRE(blocked <= 20);
}

TEST_CASE("Shots: an advanced keeper is chipped", "[acceptance][shots]") {
  int chipped = 0;
  int goals = 0;
  for (std::uint64_t seed = 1; seed <= kSeeds; ++seed) {
    const PlayedShot played = playedShot("chip-advanced-keeper", seed);
    CAPTURE(seed);
    // Driven past him the keeper saves it; over him it goes in.
    REQUIRE(played.decision.shots.front().type == ShotType::kChipped);
    const auto driven = std::ranges::find_if(played.decision.shots, [](const ShotCandidate& shot) {
      return shot.type == ShotType::kDriven;
    });
    REQUIRE(driven != played.decision.shots.end());
    REQUIRE(driven->saveRisk > played.decision.shots.front().saveRisk);
    chipped += played.type == ShotType::kChipped ? 1 : 0;
    goals += played.outcome == ShotOutcome::kGoal ? 1 : 0;
  }
  CAPTURE(chipped, goals);
  REQUIRE(chipped >= 90);
  REQUIRE(goals >= 30);
}

TEST_CASE("Shots: a curving flight replays alike", "[acceptance][shots]") {
  for (const std::string_view scenario : {"curl-around-wall", "chip-advanced-keeper"}) {
    CAPTURE(scenario);
    REQUIRE(playedShot(scenario, 7).eventHash == playedShot(scenario, 7).eventHash);
  }
}
