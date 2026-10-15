#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstddef>
#include <utility>
#include <vector>

#include "carrierScan.hpp"
#include "kickoffScenario.hpp"
#include "matchState.hpp"
#include "pitch.hpp"
#include "referenceTactic.hpp"
#include "tactic.hpp"
#include "vec2.hpp"

using ElyverseFootball::SimCore::Vec2;
using ElyverseFootball::SimMatch::clearanceTarget;
using ElyverseFootball::SimMatch::GoalEnd;
using ElyverseFootball::SimMatch::LookTarget;
using ElyverseFootball::SimMatch::lookTargets;
using ElyverseFootball::SimMatch::makeSevenASideKickoff;
using ElyverseFootball::SimMatch::MatchState;
using ElyverseFootball::SimMatch::MatchStateSpec;
using ElyverseFootball::SimMatch::Pitch;
using ElyverseFootball::SimMatch::TeamTactics;

namespace {

// The kickoff fixture with home's players moved to `home` and the one at
// `carrier` on the ball, facing `facing`.
[[nodiscard]] MatchState withHome(const std::array<Vec2, 7>& home, const std::size_t carrier,
                                  const Vec2 facing, TeamTactics tactics = {}) {
  auto kickoff = makeSevenASideKickoff(Pitch(60.0, 40.0));
  REQUIRE(kickoff.has_value());
  MatchStateSpec spec{.pitch = kickoff->pitch(),
                      .players = {kickoff->players().begin(), kickoff->players().end()},
                      .ball = kickoff->ball(),
                      .playersPerSide = kickoff->playersPerSide()};
  for (std::size_t index = 0; index < home.size(); ++index) {
    spec.players[index].position = home.at(index);
    spec.players[index].target.reset();
  }
  spec.players[carrier].facing = facing;
  spec.ball.position = home.at(carrier);
  spec.ball.owner = spec.players[carrier].playerId;
  auto state = MatchState::create(spec, std::move(tactics));
  REQUIRE(state.has_value());
  return *std::move(state);
}

[[nodiscard]] bool near(const Vec2 first, const Vec2 second) {
  return std::sqrt((first - second).lengthSquared()) < 1e-9;
}

}  // namespace

TEST_CASE("A carrier looks first to the side his turn to goal is shorter on", "[carrierScan]") {
  // Player 4 at the left touchline (home's left is y = 40) faces up the
  // pitch: the goal lies to his right. One teammate stands to his left,
  // the others to his right.
  const MatchState state = withHome({{{.x = 5.0, .y = 20.0},
                                      {.x = 30.0, .y = 39.5},
                                      {.x = 30.0, .y = 10.0},
                                      {.x = 40.0, .y = 38.0},
                                      {.x = 35.0, .y = 30.0},
                                      {.x = 20.0, .y = 20.0},
                                      {.x = 50.0, .y = 25.0}}},
                                    3, {.x = 1.0, .y = 0.0});
  const std::vector<LookTarget> targets = lookTargets(state, 3);
  REQUIRE(targets.size() == 2);
  REQUIRE(targets[0].firstSide);
  REQUIRE(near(targets[0].point, {.x = 28.0, .y = 21.0}));
  REQUIRE_FALSE(targets[1].firstSide);
  REQUIRE(near(targets[1].point, {.x = 30.0, .y = 39.5}));
}

TEST_CASE("A winger at the goal line turns toward the box on either wing", "[carrierScan]") {
  // Facing the opponent's goal line, every teammate behind him: on the left
  // he turns right, on the right left -- both times toward the middle.
  for (const double wingY : {38.0, 2.0}) {
    CAPTURE(wingY);
    const MatchState state = withHome({{{.x = 5.0, .y = 20.0},
                                        {.x = 20.0, .y = 12.0},
                                        {.x = 20.0, .y = 28.0},
                                        {.x = 30.0, .y = 20.0},
                                        {.x = 58.0, .y = wingY},
                                        {.x = 40.0, .y = 20.0},
                                        {.x = 45.0, .y = 20.0}}},
                                      4, {.x = 1.0, .y = 0.0});
    const std::vector<LookTarget> targets = lookTargets(state, 4);
    REQUIRE(targets.size() == 1);
    REQUIRE(targets[0].firstSide);
    REQUIRE(std::abs(targets[0].point.y - 20.0) < std::abs(wingY - 20.0));
  }
}

TEST_CASE("A forward looks at the penalty spot first", "[carrierScan]") {
  auto reference = ElyverseFootball::SimTactics::Tactic::create(
      ElyverseFootball::SimTactics::referenceTacticSpec());
  REQUIRE(reference.has_value());
  // The striker (slot 6, runInBehind) faces his own goal at the halfway
  // line, a teammate on either side.
  const MatchState state =
      withHome({{{.x = 5.0, .y = 20.0},
                 {.x = 15.0, .y = 12.0},
                 {.x = 15.0, .y = 28.0},
                 {.x = 20.0, .y = 20.0},
                 {.x = 25.0, .y = 5.0},
                 {.x = 25.0, .y = 35.0},
                 {.x = 30.0, .y = 20.0}}},
               6, {.x = -1.0, .y = 0.0}, {.home = *reference, .away = std::nullopt});
  const std::vector<LookTarget> targets = lookTargets(state, 6);
  REQUIRE(targets.size() == 2);
  REQUIRE(targets[0].firstSide);
  REQUIRE(targets[0].point == state.pitch().penaltySpot(GoalEnd::kMaxX));
  REQUIRE_FALSE(targets[1].firstSide);
}

TEST_CASE("A clearance goes up the near touchline as far as the kick reaches", "[carrierScan]") {
  const MatchState state = withHome({{{.x = 5.0, .y = 20.0},
                                      {.x = 15.0, .y = 12.0},
                                      {.x = 15.0, .y = 28.0},
                                      {.x = 10.0, .y = 10.0},
                                      {.x = 25.0, .y = 5.0},
                                      {.x = 25.0, .y = 35.0},
                                      {.x = 30.0, .y = 20.0}}},
                                    3, {.x = 1.0, .y = 0.0});
  // 40 m from (10, 10) to y = 5: sqrt(40² − 5²) up the pitch. Without a
  // pitch-control grid the near side.
  const Vec2 target = clearanceTarget(state, 3, 40.0, 5.0, 10.0);
  REQUIRE(target.y == 5.0);
  REQUIRE(std::abs(target.x - (10.0 + std::sqrt(1575.0))) < 1e-9);
  // A longer kick stops short of the goal line.
  REQUIRE(clearanceTarget(state, 3, 100.0, 5.0, 10.0) == Vec2{.x = 50.0, .y = 5.0});
}
