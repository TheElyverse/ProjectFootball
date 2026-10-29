// The offside line in whole matches (docs/match-engine.md#offside): without
// the rule, carriers often pass to a teammate standing in an offside position;
// with it, carriers and runners keep to the line the defenders hold, and the
// rule has few offsides to call.

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <optional>
#include <variant>
#include <vector>

#include "matchEvents.hpp"
#include "matchSetup.hpp"
#include "matchSimulation.hpp"
#include "matchState.hpp"
#include "offside.hpp"
#include "referenceTactic.hpp"
#include "scenarios.hpp"
#include "simTime.hpp"
#include "tactic.hpp"

using ElyverseFootball::SimCore::SimTick;
using ElyverseFootball::SimCore::Vec2;
using ElyverseFootball::SimMatch::findPlayerIndex;
using ElyverseFootball::SimMatch::isOffsidePosition;
using ElyverseFootball::SimMatch::makeTacticMatch;
using ElyverseFootball::SimMatch::MatchEvent;
using ElyverseFootball::SimMatch::MatchSimulation;
using ElyverseFootball::SimMatch::MatchState;
using ElyverseFootball::SimMatch::OffsideCalled;
using ElyverseFootball::SimMatch::offsideLine;
using ElyverseFootball::SimMatch::PassAttempted;
using ElyverseFootball::SimMatch::PlayerMatchState;
using ElyverseFootball::SimMatch::startMatch;
using ElyverseFootball::SimTactics::referenceElevenASideTacticSpec;
using ElyverseFootball::SimTactics::Tactic;

namespace {

struct Tally {
  // Passes meant for a teammate in an offside position when the ball was
  // played, and the offsides the rule called.
  int passesToOffside = 0;
  int offsides = 0;
};

// Whether the pass's intended receiver stood in an offside position in the
// state the pass was played from.
[[nodiscard]] bool toOffside(const PassAttempted& pass, const MatchState& before) {
  const auto passer = findPlayerIndex(before, pass.passer);
  const auto receiver =
      pass.intendedReceiver ? findPlayerIndex(before, *pass.intendedReceiver) : std::nullopt;
  if (!passer || !receiver) {
    return false;
  }
  const auto side = before.players()[*passer].side;
  std::vector<Vec2> opponents;
  for (const PlayerMatchState& player : before.players()) {
    if (player.side != side) {
      opponents.push_back(player.position);
    }
  }
  const double line = offsideLine(side, opponents, before.ball().position, before.pitch());
  return isOffsidePosition(side, before.players()[*receiver].position, line, before.pitch(), {});
}

// Three minutes of eleven-a-side reference against itself, restarts on, over
// ten seeds.
[[nodiscard]] Tally played(const bool offside) {
  const auto reference = Tactic::create(referenceElevenASideTacticSpec());
  REQUIRE(reference.has_value());
  Tally tally;
  for (std::uint64_t seed = 1; seed <= 10; ++seed) {
    auto setup = makeTacticMatch({.home = *reference, .away = *reference}, seed);
    REQUIRE(setup.has_value());
    setup->config.restarts.enabled = true;
    setup->config.offside.enabled = offside;
    MatchSimulation simulation = startMatch(*setup);
    while (simulation.tick() < SimTick(5400)) {
      REQUIRE(simulation.step().has_value());
      for (const MatchEvent& event : simulation.events()) {
        if (const auto* pass = std::get_if<PassAttempted>(&event)) {
          tally.passesToOffside += toOffside(*pass, simulation.previousState()) ? 1 : 0;
        }
        tally.offsides += std::holds_alternative<OffsideCalled>(event) ? 1 : 0;
      }
    }
  }
  return tally;
}

}  // namespace

TEST_CASE("Offside: carriers and runners keep to the line the defenders hold",
          "[acceptance][offside]") {
  const Tally without = played(false);
  const Tally with = played(true);
  CAPTURE(without.passesToOffside, without.offsides, with.passesToOffside, with.offsides);
  REQUIRE(without.offsides == 0);
  // Without the rule a fair number of passes would be offside; with it, the
  // calls are a few. At the time of writing 26 passes to an offside position
  // without it, 6 with it, 4 of them called.
  REQUIRE(without.passesToOffside >= 20);
  REQUIRE(with.offsides * 3 < without.passesToOffside);
  REQUIRE(with.passesToOffside * 3 < without.passesToOffside);
}
