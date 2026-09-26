// The P2 golden scenarios (docs/golden-scenarios.md): in each short,
// hand-placed situation one tactical behaviour must show, checked through
// events and diagnostics over fixed seeds. Every scenario also replays
// identically.

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <expected>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

#include "goldenScenarios.hpp"
#include "matchEvents.hpp"
#include "matchSetup.hpp"
#include "matchSimulation.hpp"
#include "matchState.hpp"
#include "replay.hpp"
#include "replayJson.hpp"
#include "scenarios.hpp"
#include "simTime.hpp"
#include "tacticalPhase.hpp"
#include "tacticalState.hpp"
#include "teamPress.hpp"

using ElyverseFootball::SimCore::PlayerId;
using ElyverseFootball::SimCore::SimTick;
using ElyverseFootball::SimMatch::MatchEvent;
using ElyverseFootball::SimMatch::MatchSetup;
using ElyverseFootball::SimMatch::MatchSimulation;
using ElyverseFootball::SimMatch::MatchState;
using ElyverseFootball::SimMatch::TeamSide;

namespace {

constexpr std::uint64_t kSeeds = 20;

using Builder = std::function<std::expected<MatchSetup, std::string>(std::uint64_t)>;

// Runs the scenario of every seed for `ticks` ticks with diagnostics on and
// counts the seeds for which `happened` returns true in some step.
[[nodiscard]] int seedsWhere(const Builder& build, const std::int64_t ticks,
                             const std::function<bool(const MatchSimulation&)>& happened) {
  int count = 0;
  for (std::uint64_t seed = 1; seed <= kSeeds; ++seed) {
    const auto setup = build(seed);
    REQUIRE(setup.has_value());
    MatchSimulation simulation = ElyverseFootball::SimMatch::startMatch(*setup);
    simulation.setCollectDiagnostics(true);
    while (simulation.tick() < SimTick(ticks)) {
      REQUIRE(simulation.step().has_value());
      if (happened(simulation)) {
        ++count;
        break;
      }
    }
  }
  return count;
}

template <typename Event>
[[nodiscard]] const Event* eventOf(const MatchSimulation& simulation) {
  for (const MatchEvent& event : simulation.events()) {
    if (const auto* typed = std::get_if<Event>(&event)) {
      return typed;
    }
  }
  return nullptr;
}

}  // namespace

TEST_CASE("Golden: in a 3v2 after a regain the carrier plays forward at once",
          "[acceptance][p2][golden]") {
  using ElyverseFootball::SimMatch::PassAttempted;
  // The first pass of the midfielder who won the ball goes forward to one of
  // the three attackers within 1.5 s of the regain at tick 10.
  const int forward =
      seedsWhere(&ElyverseFootball::SimMatch::makeTransitionThreeVersusTwo, 55,
                 [](const MatchSimulation& simulation) {
                   const auto* pass = eventOf<PassAttempted>(simulation);
                   return pass != nullptr && pass->passer == PlayerId(4) &&
                          pass->intendedReceiver.value_or(PlayerId(0)).value() >= 5 &&
                          pass->intendedReceiver.value_or(PlayerId(0)).value() <= 7 &&
                          pass->target.x > pass->from.x + 3.0;
                 });
  CAPTURE(forward);
  REQUIRE(forward >= 16);
}

TEST_CASE("Golden: an isolated winger triggers the press", "[acceptance][p2][golden]") {
  using ElyverseFootball::SimMatch::PressingStarted;
  const int pressed = seedsWhere(
      &ElyverseFootball::SimMatch::makeIsolatedWinger, 90, [](const MatchSimulation& simulation) {
        const auto* started = eventOf<PressingStarted>(simulation);
        return started != nullptr && started->side == TeamSide::kAway &&
               started->carrier == PlayerId(6) &&
               started->trigger ==
                   ElyverseFootball::SimTactics::PressingTrigger::kIsolatedReceiver &&
               started->assignments.size() >= 3;
      });
  CAPTURE(pressed);
  REQUIRE(pressed == static_cast<int>(kSeeds));
}

namespace {

// Seeds in which home's press in the pressing trap wins the ball within six
// seconds.
[[nodiscard]] int trapRegains(const ElyverseFootball::SimMatch::TrapSpot spot,
                              const double intensity) {
  using ElyverseFootball::SimMatch::PressingEnded;
  using ElyverseFootball::SimMatch::PressOutcome;
  return seedsWhere(
      [spot, intensity](const std::uint64_t seed) {
        return ElyverseFootball::SimMatch::makePressingTrap(seed, spot, intensity);
      },
      180,
      [](const MatchSimulation& simulation) {
        const auto* ended = eventOf<PressingEnded>(simulation);
        return ended != nullptr && ended->side == TeamSide::kHome &&
               ended->outcome == PressOutcome::kBallRegained;
      });
}

// Seeds in which the trigger, not home's phase, starts the press: home's
// pressing line puts it in the pressing phase here, so a phase press would
// otherwise stand in for broken trigger detection unnoticed.
[[nodiscard]] int trapTriggered(const ElyverseFootball::SimMatch::TrapSpot spot) {
  using ElyverseFootball::SimMatch::PressingStarted;
  return seedsWhere(
      [spot](const std::uint64_t seed) {
        return ElyverseFootball::SimMatch::makePressingTrap(seed, spot, 1.0);
      },
      180,
      [](const MatchSimulation& simulation) {
        const auto* started = eventOf<PressingStarted>(simulation);
        return started != nullptr && started->side == TeamSide::kHome &&
               started->trigger ==
                   ElyverseFootball::SimTactics::PressingTrigger::kReceiverFacingOwnGoal;
      });
}

}  // namespace

TEST_CASE("Golden: a coordinated press at the touchline traps the receiver",
          "[acceptance][p2][golden]") {
  using ElyverseFootball::SimMatch::TrapSpot;
  const int coordinated = trapRegains(TrapSpot::kTouchline, 1.0);
  const int lone = trapRegains(TrapSpot::kTouchline, 0.25);
  const int centre = trapRegains(TrapSpot::kCentre, 1.0);
  const int triggered = trapTriggered(TrapSpot::kTouchline);
  CAPTURE(coordinated, lone, centre, triggered);
  // The receiver facing his own goal is what starts the press, in every seed.
  REQUIRE(triggered == static_cast<int>(kSeeds));
  // At the time of writing 13, 2 and 0 of 20.
  REQUIRE(coordinated >= 10);
  // Uncoordinated: one presser leaves the carrier's lanes open.
  REQUIRE(coordinated >= lone + 6);
  // The touchline is half the trap: in the centre the carrier escapes.
  REQUIRE(coordinated >= centre + 6);
}

TEST_CASE("Golden: the striker runs in behind the line and is tracked",
          "[acceptance][p2][golden]") {
  using ElyverseFootball::SimMatch::ActionType;
  const auto chose = [](const MatchSimulation& simulation, const PlayerId player,
                        const ActionType type) {
    return std::ranges::any_of(simulation.actionDiagnostics(),
                               [player, type](const auto& decision) {
                                 return decision.player == player && decision.chosen &&
                                        decision.candidates.at(*decision.chosen).type == type;
                               });
  };
  const int runs = seedsWhere(&ElyverseFootball::SimMatch::makeRunBehindTheLine, 30,
                              [&chose](const MatchSimulation& simulation) {
                                return chose(simulation, PlayerId(7), ActionType::kRunInBehind);
                              });
  const int tracked = seedsWhere(
      &ElyverseFootball::SimMatch::makeRunBehindTheLine, 90, [](const MatchSimulation& simulation) {
        // An away defender follows the striker.
        return std::ranges::any_of(simulation.actionDiagnostics(), [](const auto& decision) {
          if (!decision.chosen || decision.player.value() <= 7) {
            return false;
          }
          const auto& action = decision.candidates.at(*decision.chosen);
          return action.type == ActionType::kTrackRunner && action.subject == PlayerId(7);
        });
      });
  CAPTURE(runs, tracked);
  REQUIRE(runs >= 18);
  REQUIRE(tracked >= 12);
}

TEST_CASE("Golden: every golden scenario replays identically", "[acceptance][p2][golden]") {
  for (const char* name :
       {"transition-3v2", "isolated-winger", "touchline-trap", "lone-press", "run-behind-line"}) {
    CAPTURE(name);
    const auto* scenario = ElyverseFootball::SimMatch::findScenario(name);
    REQUIRE(scenario != nullptr);
    const auto setup = scenario->make(3);
    REQUIRE(setup.has_value());
    const auto replay =
        ElyverseFootball::SimReplay::recordMatch(*setup, SimTick(180), 30, "2026-09-25T12:00:00Z");
    REQUIRE(replay.has_value());
    const auto parsed = ElyverseFootball::SimReplay::parseReplayJson(
        ElyverseFootball::SimReplay::toReplayJson(*replay));
    REQUIRE(parsed.has_value());
    const auto playback = ElyverseFootball::SimReplay::playReplay(*parsed);
    REQUIRE(playback.has_value());
    REQUIRE(playback->finalStateHash == replay->checkpoints.back().stateHash);
  }
}

TEST_CASE("Golden: the pressing tactic helper rejects values a tactic cannot hold",
          "[acceptance][p2][golden]") {
  using ElyverseFootball::SimMatch::goldenPressingTactic;
  using ElyverseFootball::SimTactics::PressingTrigger;
  const std::vector<PressingTrigger> trigger{PressingTrigger::kReceiverFacingOwnGoal};
  REQUIRE_THROWS_AS(goldenPressingTactic(2.0, trigger, 1.0), std::invalid_argument);
  REQUIRE_THROWS_AS(goldenPressingTactic(1.0, trigger, -0.5), std::invalid_argument);
  REQUIRE_THROWS_AS(
      goldenPressingTactic(1.0, {PressingTrigger::kBackPass, PressingTrigger::kBackPass}, 1.0),
      std::invalid_argument);
  REQUIRE_NOTHROW(goldenPressingTactic(1.0, trigger, 1.0));
}
