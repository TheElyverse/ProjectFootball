// The duel scenarios (docs/duels.md): a centre back takes on a carrier with
// his back to goal once he has stood on the ball for idleSeconds. Who is
// stronger and better on the ball decides how the duel tends to go -- won by
// the defender, kept by the carrier, or knocked loose for others to chase --
// but every duel is decided step by step.

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
#include "matchStateHash.hpp"
#include "scenarios.hpp"
#include "simTime.hpp"

using ElyverseFootball::SimCore::PlayerId;
using ElyverseFootball::SimCore::SimTick;
using ElyverseFootball::SimMatch::DuelEnded;
using ElyverseFootball::SimMatch::DuelOutcome;
using ElyverseFootball::SimMatch::DuelStarted;
using ElyverseFootball::SimMatch::findScenario;
using ElyverseFootball::SimMatch::hashMatchState;
using ElyverseFootball::SimMatch::MatchEvent;
using ElyverseFootball::SimMatch::MatchSetup;
using ElyverseFootball::SimMatch::MatchSimulation;
using ElyverseFootball::SimMatch::PossessionChanged;
using ElyverseFootball::SimMatch::startMatch;
using ElyverseFootball::SimMatch::TeamSide;

namespace {

constexpr PlayerId kCarrier{1};
constexpr PlayerId kFirstAway{8};
constexpr int kSeeds = 30;

[[nodiscard]] MatchSetup setupOf(const std::string_view scenario, const std::uint64_t seed) {
  const auto* definition = findScenario(scenario);
  REQUIRE(definition != nullptr);
  auto setup = definition->make(seed);
  REQUIRE(setup.has_value());
  return *std::move(setup);
}

// The first duel on the carrier, and who has the ball once it is over: the
// carrier after a duel he kept or was fouled in, the winner after one he
// lost, whoever takes a loose ball first; nobody if no duel ends in twenty
// seconds or a loose ball is still free then.
struct FirstDuel {
  std::optional<DuelStarted> started;
  std::optional<DuelEnded> ended;
  std::optional<PlayerId> then;
  std::optional<TeamSide> side;
};

[[nodiscard]] FirstDuel firstDuel(const std::string_view scenario, const std::uint64_t seed) {
  MatchSimulation simulation = startMatch(setupOf(scenario, seed));
  FirstDuel duel;
  while (!duel.then && simulation.tick() < SimTick(600)) {
    REQUIRE(simulation.step().has_value());
    for (const MatchEvent& event : simulation.events()) {
      if (const auto* started = std::get_if<DuelStarted>(&event);
          started != nullptr && !duel.started) {
        REQUIRE(started->carrier == kCarrier);
        duel.started = *started;
      }
      if (const auto* ended = std::get_if<DuelEnded>(&event); ended != nullptr && !duel.ended) {
        // Every duel that ends was started first, by the same players, and
        // took a step at least: the defender sizes the carrier up first.
        REQUIRE(duel.started.has_value());
        REQUIRE(ended->carrier == duel.started.value_or(DuelStarted{}).carrier);
        REQUIRE(ended->defender == duel.started.value_or(DuelStarted{}).defender);
        REQUIRE(ended->tick > duel.started.value_or(DuelStarted{}).tick);
        duel.ended = *ended;
        if (ended->outcome == DuelOutcome::kKept || ended->outcome == DuelOutcome::kFoul) {
          duel.then = ended->carrier;
        }
      }
      if (const auto* changed = std::get_if<PossessionChanged>(&event);
          changed != nullptr && duel.ended && !duel.then) {
        duel.then = changed->newOwner;
      }
    }
  }
  if (duel.then) {
    const auto index = findPlayerIndex(simulation.state(), *duel.then);
    REQUIRE(index.has_value());
    duel.side = simulation.state().players()[index.value_or(0)].side;
  }
  return duel;
}

// How often the given side has the ball after the first duel, in kSeeds runs
// of the scenario.
[[nodiscard]] int keptBy(const std::string_view scenario, const TeamSide side) {
  int duels = 0;
  int kept = 0;
  for (std::uint64_t seed = 1; seed <= static_cast<std::uint64_t>(kSeeds); ++seed) {
    CAPTURE(seed);
    const FirstDuel duel = firstDuel(scenario, seed);
    if (!duel.started) {
      continue;
    }
    // An away player -- almost always the centre back -- takes the carrier
    // on, once he has stood on the ball for idleSeconds.
    ++duels;
    REQUIRE(duel.started->defender.value() >= kFirstAway.value());
    REQUIRE(duel.started->tick > SimTick(60));
    kept += duel.side == side ? 1 : 0;
  }
  // Now and then the centre back has left him before idleSeconds have
  // passed, and nobody takes him on.
  REQUIRE(duels >= kSeeds - 3);
  return kept;
}

}  // namespace

TEST_CASE("Duel: a strong centre back wins the ball from a weak carrier", "[acceptance][duel]") {
  // At the time of writing away has the ball after 25 of thirty duels: it
  // pushes the carrier off it, or wins it cleanly once he turns.
  const int won = keptBy("duel-ball-won", TeamSide::kAway);
  CAPTURE(won);
  REQUIRE(won >= 21);
}

TEST_CASE("Duel: a strong carrier keeps the ball from a weak centre back", "[acceptance][duel]") {
  // At the time of writing home keeps it after 15 of thirty duels -- he
  // shields it, pushes the defender off balance and gets away or plays it on
  // -- and after 3 the other way round.
  const int kept = keptBy("duel-ball-kept", TeamSide::kHome);
  const int keptWhenWeak = keptBy("duel-ball-won", TeamSide::kHome);
  CAPTURE(kept, keptWhenWeak);
  REQUIRE(kept >= 12);
  REQUIRE(kept >= 3 * keptWhenWeak);
}

TEST_CASE("Duel: an even duel knocks the ball loose for others to chase", "[acceptance][duel]") {
  int loose = 0;
  int others = 0;
  for (std::uint64_t seed = 1; seed <= static_cast<std::uint64_t>(kSeeds); ++seed) {
    CAPTURE(seed);
    const FirstDuel duel = firstDuel("duel-loose-ball", seed);
    if (!duel.ended || duel.ended->outcome != DuelOutcome::kLoose) {
      continue;
    }
    ++loose;
    others += duel.then && duel.then != kCarrier && duel.then != duel.ended->defender ? 1 : 0;
  }
  CAPTURE(loose, others);
  // At the time of writing 12 of thirty duels end with the ball loose, and a
  // third player gets to it first after 5 of them.
  REQUIRE(loose >= 8);
  REQUIRE(others >= 2);
}

TEST_CASE("Duel: the scenarios run alike every time", "[acceptance][duel]") {
  for (const std::string_view name : {"duel-ball-won", "duel-ball-kept", "duel-loose-ball"}) {
    CAPTURE(name);
    const auto run = [name] {
      MatchSimulation simulation = startMatch(setupOf(name, 7));
      while (simulation.tick() < SimTick(300)) {
        REQUIRE(simulation.step().has_value());
      }
      return std::pair{hashMatchState(simulation.state()), simulation.tick()};
    };
    REQUIRE(run() == run());
  }
}
