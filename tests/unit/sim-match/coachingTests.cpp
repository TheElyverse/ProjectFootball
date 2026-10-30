// Coaching a running match (docs/match-engine.md#coaching): substitutions wait
// for a stoppage, within the limit, and a player who came off does not return;
// the commands are checked when they are scheduled.

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <variant>
#include <vector>

#include "ids.hpp"
#include "matchCommand.hpp"
#include "matchEvents.hpp"
#include "matchFixtures.hpp"
#include "matchSetup.hpp"
#include "matchSimulation.hpp"
#include "matchState.hpp"
#include "simTime.hpp"

using ElyverseFootball::SimCore::PlayerId;
using ElyverseFootball::SimCore::SimTick;
using ElyverseFootball::SimMatch::findPlayerIndex;
using ElyverseFootball::SimMatch::MatchCommandErrorCode;
using ElyverseFootball::SimMatch::MatchEvent;
using ElyverseFootball::SimMatch::MatchSetup;
using ElyverseFootball::SimMatch::MatchSimulation;
using ElyverseFootball::SimMatch::startMatch;
using ElyverseFootball::SimMatch::SubstituteCommand;
using ElyverseFootball::SimMatch::SubstitutionMade;
using ElyverseFootball::SimMatch::SubstitutionRefusal;
using ElyverseFootball::SimMatch::SubstitutionRefused;
using ElyverseFootball::SimMatch::TeamSide;
using ElyverseFootball::TestSupport::setupOf;

namespace {

// Home's striker, at index 10 of the eleven-a-side reference, and the ids of
// the players who come on.
constexpr std::size_t kStriker = 10;
constexpr PlayerId kFirstOn{100};
constexpr PlayerId kSecondOn{101};

// What the first stoppage after the order did with the substitutions.
struct Stoppage {
  std::optional<SimTick> tick;
  std::vector<SubstitutionMade> made;
  std::vector<SubstitutionRefused> refused;
  // Whether the ball was dead for a set piece once the step was over.
  bool deadBall = false;
  // Whether a set piece was set up before it with the order already given.
  bool missed = false;
};

[[nodiscard]] Stoppage firstStoppage(MatchSimulation& simulation) {
  Stoppage stoppage;
  while (!stoppage.tick && simulation.tick() < SimTick(std::int64_t{30} * 600)) {
    REQUIRE(simulation.step().has_value());
    for (const MatchEvent& event : simulation.events()) {
      if (const auto* made = std::get_if<SubstitutionMade>(&event)) {
        stoppage.made.push_back(*made);
        stoppage.tick = made->tick;
      } else if (const auto* refused = std::get_if<SubstitutionRefused>(&event)) {
        stoppage.refused.push_back(*refused);
        stoppage.tick = refused->tick;
      }
    }
    if (stoppage.tick) {
      stoppage.deadBall = simulation.state().setPiece().has_value();
    } else if (simulation.state().setPiece()) {
      stoppage.missed = true;
    }
  }
  return stoppage;
}

[[nodiscard]] SubstituteCommand substitution(const MatchSetup& setup, const PlayerId incoming) {
  return {.side = TeamSide::kHome,
          .off = setup.initialState.players()[kStriker].playerId,
          .on = incoming,
          .attributes = {}};
}

}  // namespace

TEST_CASE("A substitution waits for the next stoppage, and who came off does not return",
          "[coaching]") {
  MatchSetup setup = setupOf("tactic-match-eleven", 3);
  const PlayerId striker = setup.initialState.players()[kStriker].playerId;
  setup.commands.push_back({.tick = SimTick(30), .command = substitution(setup, kFirstOn)});
  MatchSimulation simulation = startMatch(setup);

  const Stoppage stoppage = firstStoppage(simulation);
  REQUIRE(stoppage.tick.has_value());
  REQUIRE(stoppage.deadBall);
  REQUIRE_FALSE(stoppage.missed);
  REQUIRE(stoppage.refused.empty());
  REQUIRE(stoppage.made.size() == 1);
  REQUIRE(stoppage.made.front().off == striker);
  REQUIRE(stoppage.made.front().on == kFirstOn);
  REQUIRE(stoppage.made.front().ordered == SimTick(30));

  // The newcomer stands in the striker's place, fresh; the striker is gone.
  const auto& state = simulation.state();
  REQUIRE(findPlayerIndex(state, kFirstOn) == kStriker);
  REQUIRE_FALSE(findPlayerIndex(state, striker).has_value());
  REQUIRE(state.cameOff(striker));
  REQUIRE(state.substitutionsMade(TeamSide::kHome) == 1);
  REQUIRE(state.players()[kStriker].fitness.playedTicks == 0);
  REQUIRE(state.pendingSubstitutions().empty());

  // He cannot come back on.
  const auto back = simulation.schedule(
      {.tick = simulation.tick(),
       .command = SubstituteCommand{
           .side = TeamSide::kHome, .off = kFirstOn, .on = striker, .attributes = {}}});
  REQUIRE_FALSE(back.has_value());
  REQUIRE(back.error().code == MatchCommandErrorCode::kInvalidSubstitution);

  // Play goes on with the newcomer.
  ElyverseFootball::TestSupport::stepTimes(simulation, 300);
  REQUIRE(simulation.state().players()[kStriker].fitness.playedTicks > 0);
}

TEST_CASE("Substitutions beyond the limit are refused", "[coaching]") {
  MatchSetup setup = setupOf("tactic-match-eleven", 3);
  setup.config.coaching.maxSubstitutions = 1;
  setup.commands.push_back({.tick = SimTick(30), .command = substitution(setup, kFirstOn)});
  SubstituteCommand second = substitution(setup, kSecondOn);
  second.off = setup.initialState.players()[kStriker - 1].playerId;
  setup.commands.push_back({.tick = SimTick(30), .command = second});
  MatchSimulation simulation = startMatch(setup);

  const Stoppage stoppage = firstStoppage(simulation);
  REQUIRE(stoppage.made.size() == 1);
  REQUIRE(stoppage.refused.size() == 1);
  REQUIRE(stoppage.refused.front().on == kSecondOn);
  REQUIRE(stoppage.refused.front().reason == SubstitutionRefusal::kLimitReached);
  REQUIRE(findPlayerIndex(simulation.state(), second.off).has_value());
}

TEST_CASE("A substitution is checked when it is scheduled", "[coaching]") {
  const MatchSetup setup = setupOf("tactic-match-eleven", 3);
  MatchSimulation simulation = startMatch(setup);
  const auto rejected = [&simulation](const SubstituteCommand& command) {
    const auto result = simulation.schedule({.tick = SimTick(0), .command = command});
    REQUIRE_FALSE(result.has_value());
    return result.error().code;
  };
  SubstituteCommand wrongSide = substitution(setup, kFirstOn);
  wrongSide.side = TeamSide::kAway;
  REQUIRE(rejected(wrongSide) == MatchCommandErrorCode::kInvalidSubstitution);
  REQUIRE(rejected(substitution(setup, setup.initialState.players()[0].playerId)) ==
          MatchCommandErrorCode::kInvalidSubstitution);
  REQUIRE(rejected(substitution(setup, PlayerId())) == MatchCommandErrorCode::kInvalidSubstitution);
  SubstituteCommand slow = substitution(setup, kFirstOn);
  slow.attributes.maxSpeed = 0.0;
  REQUIRE(rejected(slow) == MatchCommandErrorCode::kInvalidSubstitution);
  REQUIRE(simulation.schedule({.tick = SimTick(0), .command = substitution(setup, kFirstOn)})
              .has_value());
}

TEST_CASE("Two substitutions in one stoppage, the keeper's among them", "[coaching]") {
  MatchSetup setup = setupOf("tactic-match-eleven", 3);
  const PlayerId keeper = setup.initialState.players()[0].playerId;
  setup.commands.push_back({.tick = SimTick(30), .command = substitution(setup, kFirstOn)});
  setup.commands.push_back(
      {.tick = SimTick(30),
       .command = SubstituteCommand{
           .side = TeamSide::kHome, .off = keeper, .on = kSecondOn, .attributes = {}}});
  MatchSimulation simulation = startMatch(setup);

  const Stoppage stoppage = firstStoppage(simulation);
  REQUIRE(stoppage.made.size() == 2);
  REQUIRE(stoppage.made.front().tick == stoppage.made.back().tick);
  const auto& state = simulation.state();
  // The new keeper keeps the goal: the slot makes the keeper.
  REQUIRE(findPlayerIndex(state, kSecondOn) == 0);
  REQUIRE(ElyverseFootball::SimMatch::guardsGoal(state.players(), state.tactics(), 0));
  REQUIRE(state.substitutionsMade(TeamSide::kHome) == 2);
  ElyverseFootball::TestSupport::stepTimes(simulation, 300);
}
