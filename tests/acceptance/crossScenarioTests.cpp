// The cross scenarios (docs/scenarios.md#the-cross-scenarios): player 1, wide
// in away's final third, crosses early for a striker running in, cuts the ball
// back along the ground, and crosses against a keeper who claims it; every
// cross is contested by the existing reception and aerial duels.

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>
#include <variant>

#include "crossArea.hpp"
#include "ids.hpp"
#include "matchEvents.hpp"
#include "matchFixtures.hpp"
#include "matchSetup.hpp"
#include "matchSimulation.hpp"
#include "simTime.hpp"
#include "stableHash.hpp"

using ElyverseFootball::SimCore::PlayerId;
using ElyverseFootball::SimCore::SimTick;
using ElyverseFootball::SimMatch::addEvent;
using ElyverseFootball::SimMatch::AerialContest;
using ElyverseFootball::SimMatch::AerialPlay;
using ElyverseFootball::SimMatch::CrossArea;
using ElyverseFootball::SimMatch::CrossAttempted;
using ElyverseFootball::SimMatch::DecisionDiagnostic;
using ElyverseFootball::SimMatch::DecisionOutcome;
using ElyverseFootball::SimMatch::DiagnosticsFilter;
using ElyverseFootball::SimMatch::MatchEvent;
using ElyverseFootball::SimMatch::MatchSimulation;
using ElyverseFootball::SimMatch::PassIntercepted;
using ElyverseFootball::SimMatch::PassReceived;
using ElyverseFootball::SimMatch::startMatch;
using ElyverseFootball::TestSupport::setupOf;

namespace {

constexpr std::uint64_t kSeeds = 30;
constexpr PlayerId kCrosser{1};
constexpr PlayerId kStriker{2};
constexpr PlayerId kKeeper{8};

// Three seconds of a cross scenario: player 1's first decision, his cross,
// who got to it first -- the striker, the keeper or someone else -- and how,
// and the hash of every event.
struct PlayedCross {
  std::optional<DecisionDiagnostic> decision;
  std::optional<CrossAttempted> cross;
  std::optional<PlayerId> firstTo;
  bool header = false;
  std::uint64_t eventHash = 0;
};

// Who took the ball in this event, and whether in the air.
void noteFirst(const MatchEvent& event, PlayedCross& played) {
  if (const auto* received = std::get_if<PassReceived>(&event)) {
    played.firstTo = received->receiver;
  } else if (const auto* intercepted = std::get_if<PassIntercepted>(&event)) {
    played.firstTo = intercepted->interceptor;
  } else if (const auto* aerial = std::get_if<AerialContest>(&event);
             aerial != nullptr && aerial->winner) {
    played.firstTo = aerial->winner;
    played.header = aerial->play != AerialPlay::kCaught && aerial->play != AerialPlay::kPunched;
  }
}

[[nodiscard]] PlayedCross playedCross(const std::string_view scenario, const std::uint64_t seed) {
  MatchSimulation simulation = startMatch(setupOf(scenario, seed));
  simulation.setCollectDiagnostics(true);
  simulation.setDiagnosticsFilter(
      DiagnosticsFilter{.players = {kCrosser}, .from = std::nullopt, .to = std::nullopt});
  PlayedCross played;
  ElyverseFootball::SimCore::StableHasher events;
  while (simulation.tick() < SimTick(90)) {
    REQUIRE(simulation.step().has_value());
    for (const DecisionDiagnostic& decision : simulation.diagnostics()) {
      if (!played.decision) {
        played.decision = decision;
      }
    }
    for (const MatchEvent& event : simulation.events()) {
      addEvent(events, event);
      if (const auto* cross = std::get_if<CrossAttempted>(&event); cross != nullptr) {
        played.cross = played.cross.value_or(*cross);
      } else if (played.cross && !played.firstTo) {
        noteFirst(event, played);
      }
    }
  }
  REQUIRE(played.decision.has_value());
  played.eventHash = events.value();
  return played;
}

}  // namespace

namespace {

// How often the keeper gets to player 1's cross first in kSeeds runs.
[[nodiscard]] int claimsIn(const std::string_view scenario) {
  int claims = 0;
  for (std::uint64_t seed = 1; seed <= kSeeds; ++seed) {
    claims += playedCross(scenario, seed).firstTo == kKeeper ? 1 : 0;
  }
  return claims;
}

}  // namespace

TEST_CASE("Crosses: a wide player crosses early for a run into the box", "[acceptance][cross]") {
  int crossed = 0;
  int striker = 0;
  int headers = 0;
  for (std::uint64_t seed = 1; seed <= kSeeds; ++seed) {
    CAPTURE(seed);
    const PlayedCross played = playedCross("early-cross", seed);
    if (played.decision.value_or(DecisionDiagnostic{}).outcome != DecisionOutcome::kCrossed) {
      continue;
    }
    ++crossed;
    REQUIRE(played.cross.has_value());
    const CrossAttempted cross = played.cross.value_or(CrossAttempted{});
    // At an area or a run, in the air, not at his feet, and meant for the
    // striker.
    REQUIRE(cross.runner == kStriker);
    REQUIRE(cross.height > 0.0);
    striker += played.firstTo == kStriker ? 1 : 0;
    headers += played.firstTo == kStriker && played.header ? 1 : 0;
  }
  CAPTURE(crossed, striker, headers);
  REQUIRE(crossed >= 20);
  // Contested in the air: the striker gets there first in a fair share, and
  // heads it more than once.
  REQUIRE(striker >= 8);
  REQUIRE(headers >= 3);
}

TEST_CASE("Crosses: a player at the goal line cuts the ball back", "[acceptance][cross]") {
  int cutBacks = 0;
  int received = 0;
  for (std::uint64_t seed = 1; seed <= kSeeds; ++seed) {
    CAPTURE(seed);
    const PlayedCross played = playedCross("cut-back", seed);
    if (!played.cross) {
      continue;
    }
    const CrossAttempted cross = played.cross.value_or(CrossAttempted{});
    cutBacks += cross.area == CrossArea::kCutBack ? 1 : 0;
    REQUIRE(cross.height == 0.0);
    received += played.firstTo == kStriker ? 1 : 0;
  }
  CAPTURE(cutBacks, received);
  REQUIRE(cutBacks >= 25);
  REQUIRE(received >= 25);
}

TEST_CASE("Crosses: a keeper quicker than the crosser imagines claims the cross",
          "[acceptance][cross]") {
  // The same early cross: an ordinary keeper claims it now and then, the quick
  // one -- whom the crosser judges like anyone -- far more often.
  const int ordinary = claimsIn("early-cross");
  const int quick = claimsIn("cross-keeper-claims");
  CAPTURE(ordinary, quick);
  REQUIRE(std::cmp_greater_equal(quick, kSeeds / 2));
  REQUIRE(quick >= ordinary + 5);
}

TEST_CASE("Crosses: the scenarios run alike twice", "[acceptance][cross]") {
  for (const std::string_view scenario : {"early-cross", "cut-back", "cross-keeper-claims"}) {
    CAPTURE(scenario);
    REQUIRE(playedCross(scenario, 3).eventHash == playedCross(scenario, 3).eventHash);
  }
}
