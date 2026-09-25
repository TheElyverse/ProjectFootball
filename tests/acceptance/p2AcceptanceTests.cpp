// Milestone P2 acceptance: tactical behavior that only shows over whole
// matches. Seven-a-side matches between tactics, checked statistically over
// fixed seeds, so the results are reproducible.

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <utility>
#include <variant>

#include "kickoffScenario.hpp"
#include "matchCommand.hpp"
#include "matchEvents.hpp"
#include "matchSetup.hpp"
#include "matchSimulation.hpp"
#include "matchState.hpp"
#include "referenceTactic.hpp"
#include "responsibility.hpp"
#include "tactic.hpp"

using ElyverseFootball::SimCore::PlayerId;
using ElyverseFootball::SimCore::SimTick;
using ElyverseFootball::SimMatch::GiveBallCommand;
using ElyverseFootball::SimMatch::makeSevenASideKickoff;
using ElyverseFootball::SimMatch::MatchEvent;
using ElyverseFootball::SimMatch::MatchSetup;
using ElyverseFootball::SimMatch::MatchSimulation;
using ElyverseFootball::SimMatch::Pitch;
using ElyverseFootball::SimMatch::PressingEnded;
using ElyverseFootball::SimMatch::PressOutcome;
using ElyverseFootball::SimMatch::startMatch;
using ElyverseFootball::SimMatch::TeamSide;
using ElyverseFootball::SimTactics::PressingTrigger;
using ElyverseFootball::SimTactics::referenceTacticSpec;
using ElyverseFootball::SimTactics::Tactic;

namespace {

// Home's presses over a minute of play against the reference tactic,
// per seed.
struct PressTally {
  int presses = 0;
  int regained = 0;

  [[nodiscard]] double successRate() const {
    return presses == 0 ? 0.0 : static_cast<double>(regained) / static_cast<double>(presses);
  }
};

// Home presses high on four triggers with the given intensity: at 0.25 a
// single player commits, at 1.0 four do -- presser, lane blockers and cover.
[[nodiscard]] PressTally homePresses(const double intensity) {
  constexpr std::uint64_t seeds = 10;
  auto spec = referenceTacticSpec();
  spec.name = "press";
  spec.principles.pressingLine = 0.5;
  spec.principles.pressingTriggers = {PressingTrigger::kReceiverFacingOwnGoal,
                                      PressingTrigger::kBackPass, PressingTrigger::kPoorFirstTouch,
                                      PressingTrigger::kIsolatedReceiver};
  for (auto& phase : spec.phases) {
    phase.pressingIntensity = intensity;
  }
  const auto home = Tactic::create(spec);
  const auto away = Tactic::create(referenceTacticSpec());
  REQUIRE(home.has_value());
  REQUIRE(away.has_value());
  PressTally tally;
  for (std::uint64_t seed = 1; seed <= seeds; ++seed) {
    auto state = makeSevenASideKickoff(Pitch(60.0, 40.0), {}, {.home = *home, .away = *away});
    REQUIRE(state.has_value());
    MatchSimulation simulation = startMatch(MatchSetup{
        .initialState = *std::move(state),
        .config = {},
        .seed = seed,
        .commands = {{.tick = SimTick(0), .command = GiveBallCommand{.playerId = PlayerId(14)}}}});
    while (simulation.tick() < SimTick(1800)) {
      REQUIRE(simulation.step().has_value());
      for (const MatchEvent& event : simulation.events()) {
        if (const auto* end = std::get_if<PressingEnded>(&event);
            end != nullptr && end->side == TeamSide::kHome) {
          ++tally.presses;
          tally.regained += end->outcome == PressOutcome::kBallRegained ? 1 : 0;
        }
      }
    }
  }
  return tally;
}

}  // namespace

TEST_CASE("P2: coordinated pressing wins the ball more often than a lone presser",
          "[acceptance][p2]") {
  // Ten minutes of play each. A lone presser leaves the carrier's lanes
  // open; a coordinated press closes the nearest ones and covers behind.
  const PressTally lone = homePresses(0.25);
  const PressTally coordinated = homePresses(1.0);
  CAPTURE(lone.presses, lone.regained, coordinated.presses, coordinated.regained);
  REQUIRE(lone.presses >= 50);
  REQUIRE(coordinated.presses >= 50);
  REQUIRE(coordinated.successRate() > lone.successRate() + 0.05);
}
