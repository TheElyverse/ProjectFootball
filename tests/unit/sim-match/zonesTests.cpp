#include <catch2/catch_test_macros.hpp>
#include <optional>
#include <utility>
#include <vector>

#include "matchState.hpp"
#include "referenceTactic.hpp"
#include "tactic.hpp"
#include "zones.hpp"

using ElyverseFootball::SimCore::PlayerId;
using ElyverseFootball::SimCore::Vec2;
using ElyverseFootball::SimMatch::behindLine;
using ElyverseFootball::SimMatch::betweenLines;
using ElyverseFootball::SimMatch::isGoalkeeper;
using ElyverseFootball::SimMatch::Lane;
using ElyverseFootball::SimMatch::laneCenterY;
using ElyverseFootball::SimMatch::laneName;
using ElyverseFootball::SimMatch::laneOf;
using ElyverseFootball::SimMatch::laneRect;
using ElyverseFootball::SimMatch::MatchState;
using ElyverseFootball::SimMatch::measureTeamShape;
using ElyverseFootball::SimMatch::Pitch;
using ElyverseFootball::SimMatch::PitchRect;
using ElyverseFootball::SimMatch::PlayerMatchState;
using ElyverseFootball::SimMatch::restDefenceZone;
using ElyverseFootball::SimMatch::TeamShape;
using ElyverseFootball::SimMatch::TeamSide;
using ElyverseFootball::SimMatch::TeamTactics;
using ElyverseFootball::SimMatch::Third;
using ElyverseFootball::SimMatch::thirdOf;
using ElyverseFootball::SimMatch::thirdRect;
using ElyverseFootball::SimTactics::referenceTacticSpec;
using ElyverseFootball::SimTactics::Tactic;

namespace {

const Pitch kPitch(60.0, 40.0);

// Home in a 1-2-1-3 around its own half, away in a flat back line and a
// midfield; ids 1-7 home, 8-14 away. Slot 0 of each side is its goalkeeper
// when it plays the reference tactic.
[[nodiscard]] MatchState shapedState(TeamTactics tactics) {
  const std::vector<Vec2> home{
      {.x = 2.0, .y = 20.0}, {.x = 12.0, .y = 12.0}, {.x = 12.0, .y = 28.0}, {.x = 20.0, .y = 20.0},
      {.x = 30.0, .y = 4.0}, {.x = 30.0, .y = 36.0}, {.x = 36.0, .y = 20.0}};
  const std::vector<Vec2> away{{.x = 58.0, .y = 20.0}, {.x = 45.0, .y = 10.0},
                               {.x = 45.0, .y = 30.0}, {.x = 44.0, .y = 20.0},
                               {.x = 35.0, .y = 8.0},  {.x = 35.0, .y = 32.0},
                               {.x = 32.0, .y = 20.0}};
  std::vector<PlayerMatchState> players;
  PlayerId::ValueType nextId = 1;
  for (const auto& [side, positions] :
       {std::pair{TeamSide::kHome, &home}, std::pair{TeamSide::kAway, &away}}) {
    for (const Vec2 position : *positions) {
      players.push_back({.playerId = PlayerId(nextId++),
                         .side = side,
                         .position = position,
                         .velocity = {},
                         .attributes = {},
                         .target = std::nullopt,
                         .facing = {.x = side == TeamSide::kHome ? 1.0 : -1.0, .y = 0.0}});
    }
  }
  auto state = MatchState::create({.pitch = kPitch,
                                   .players = std::move(players),
                                   .ball = {.position = {.x = 30.0, .y = 20.0},
                                            .velocity = {},
                                            .owner = std::nullopt,
                                            .lastTouch = std::nullopt},
                                   .playersPerSide = 7},
                                  std::move(tactics));
  REQUIRE(state.has_value());
  return *std::move(state);
}

[[nodiscard]] Tactic referenceTactic() {
  auto tactic = Tactic::create(referenceTacticSpec());
  REQUIRE(tactic.has_value());
  return *std::move(tactic);
}

// The side's shape, which the test requires to exist.
[[nodiscard]] TeamShape shapeOf(const MatchState& state, const TeamSide side) {
  const auto shape = measureTeamShape(state, side);
  REQUIRE(shape.has_value());
  return shape.value_or(TeamShape{});
}

}  // namespace

TEST_CASE("Lanes are seen from the side's attacking direction", "[zones]") {
  // Home attacks +x, so its left touchline is y = 40; away's is y = 0.
  REQUIRE(laneOf(TeamSide::kHome, {.x = 30.0, .y = 38.0}, kPitch) == Lane::kLeftWing);
  REQUIRE(laneOf(TeamSide::kHome, {.x = 30.0, .y = 30.0}, kPitch) == Lane::kLeftHalfspace);
  REQUIRE(laneOf(TeamSide::kHome, {.x = 30.0, .y = 20.0}, kPitch) == Lane::kCentre);
  REQUIRE(laneOf(TeamSide::kHome, {.x = 30.0, .y = 10.0}, kPitch) == Lane::kRightHalfspace);
  REQUIRE(laneOf(TeamSide::kHome, {.x = 30.0, .y = 2.0}, kPitch) == Lane::kRightWing);
  REQUIRE(laneOf(TeamSide::kAway, {.x = 30.0, .y = 2.0}, kPitch) == Lane::kLeftWing);
  REQUIRE(laneOf(TeamSide::kAway, {.x = 30.0, .y = 38.0}, kPitch) == Lane::kRightWing);
  // Boundaries belong to the lane further right; off the pitch to the nearest.
  REQUIRE(laneOf(TeamSide::kAway, {.x = 0.0, .y = 8.0}, kPitch) == Lane::kLeftHalfspace);
  REQUIRE(laneOf(TeamSide::kAway, {.x = 0.0, .y = 40.0}, kPitch) == Lane::kRightWing);
  REQUIRE(laneOf(TeamSide::kHome, {.x = 30.0, .y = 45.0}, kPitch) == Lane::kLeftWing);
  // y = 24 is exactly the centre/right-halfspace boundary on this 40 m pitch.
  REQUIRE(laneOf(TeamSide::kAway, {.x = 30.0, .y = 24.0}, kPitch) == Lane::kRightHalfspace);
  REQUIRE(laneName(Lane::kRightHalfspace) == "rightHalfspace");
}

TEST_CASE("A lane's rectangle and centre line match laneOf", "[zones]") {
  REQUIRE(laneRect(TeamSide::kHome, Lane::kLeftWing, kPitch) ==
          PitchRect{.min = {.x = 0.0, .y = 32.0}, .max = {.x = 60.0, .y = 40.0}});
  REQUIRE(laneRect(TeamSide::kAway, Lane::kLeftWing, kPitch) ==
          PitchRect{.min = {.x = 0.0, .y = 0.0}, .max = {.x = 60.0, .y = 8.0}});
  for (const TeamSide side : {TeamSide::kHome, TeamSide::kAway}) {
    for (const Lane lane : {Lane::kLeftWing, Lane::kLeftHalfspace, Lane::kCentre,
                            Lane::kRightHalfspace, Lane::kRightWing}) {
      const Vec2 centre{.x = 30.0, .y = laneCenterY(side, lane, kPitch)};
      REQUIRE(laneOf(side, centre, kPitch) == lane);
      REQUIRE(laneRect(side, lane, kPitch).contains(centre));
    }
  }
}

TEST_CASE("Thirds are measured from the side's own goal line", "[zones]") {
  REQUIRE(thirdOf(TeamSide::kHome, {.x = 5.0, .y = 20.0}, kPitch) == Third::kDefensive);
  REQUIRE(thirdOf(TeamSide::kHome, {.x = 20.0, .y = 20.0}, kPitch) == Third::kMiddle);
  REQUIRE(thirdOf(TeamSide::kHome, {.x = 55.0, .y = 20.0}, kPitch) == Third::kAttacking);
  REQUIRE(thirdOf(TeamSide::kAway, {.x = 55.0, .y = 20.0}, kPitch) == Third::kDefensive);
  REQUIRE(thirdRect(TeamSide::kAway, Third::kDefensive, kPitch) ==
          PitchRect{.min = {.x = 40.0, .y = 0.0}, .max = {.x = 60.0, .y = 40.0}});
  REQUIRE(thirdRect(TeamSide::kHome, Third::kAttacking, kPitch) ==
          PitchRect{.min = {.x = 40.0, .y = 0.0}, .max = {.x = 60.0, .y = 40.0}});
}

TEST_CASE("Third classification agrees with thirdRect at a non-exact boundary", "[zones]") {
  // A pitch length not divisible by 3 exercises the floating-point rounding
  // that thirdOf() must classify the same way as thirdRect() does.
  const Pitch oddPitch(61.0, 40.0);
  const double boundary = thirdRect(TeamSide::kAway, Third::kMiddle, oddPitch).max.x;
  REQUIRE(thirdOf(TeamSide::kAway, {.x = boundary, .y = 20.0}, oddPitch) == Third::kMiddle);
}

TEST_CASE("A team's shape leaves out its goalkeeper", "[zones]") {
  const MatchState state = shapedState({.home = referenceTactic(), .away = referenceTactic()});
  REQUIRE(isGoalkeeper(state, 0));
  REQUIRE_FALSE(isGoalkeeper(state, 1));
  REQUIRE(isGoalkeeper(state, 7));

  const TeamShape home = shapeOf(state, TeamSide::kHome);
  // Outfield depths 12, 12, 20, 30, 30, 36.
  REQUIRE(home.defensiveLine == 12.0);
  REQUIRE(home.midfieldLine == 25.0);
  REQUIRE(home.frontLine == 36.0);
  REQUIRE(home.length == 24.0);
  REQUIRE(home.minY == 4.0);
  REQUIRE(home.maxY == 36.0);
  REQUIRE(home.width == 32.0);
  REQUIRE(home.centroid == Vec2{.x = 140.0 / 6.0, .y = 20.0});

  // Away's depths from x = 60: 15, 15, 16, 25, 25, 28.
  const TeamShape away = shapeOf(state, TeamSide::kAway);
  REQUIRE(away.defensiveLine == 15.0);
  REQUIRE(away.midfieldLine == 20.5);
  REQUIRE(away.frontLine == 28.0);
}

TEST_CASE("A scripted side's shape includes every player", "[zones]") {
  const MatchState state = shapedState({});
  REQUIRE_FALSE(isGoalkeeper(state, 0));
  REQUIRE(shapeOf(state, TeamSide::kHome).defensiveLine == 2.0);
}

TEST_CASE("Dynamic zones follow the opponent's shape", "[zones]") {
  const MatchState state = shapedState({.home = referenceTactic(), .away = referenceTactic()});
  const TeamShape shape = shapeOf(state, TeamSide::kAway);

  // Between away's defensive line (x = 45) and its midfield line (x = 39.5),
  // across its outfield width (y 8 to 32).
  REQUIRE(betweenLines(TeamSide::kAway, shape, kPitch) ==
          PitchRect{.min = {.x = 39.5, .y = 8.0}, .max = {.x = 45.0, .y = 32.0}});
  // Home's striker at (36, 20) is in front of it; a player at (42, 20) in it.
  REQUIRE_FALSE(betweenLines(TeamSide::kAway, shape, kPitch).contains({.x = 36.0, .y = 20.0}));
  REQUIRE(betweenLines(TeamSide::kAway, shape, kPitch).contains({.x = 42.0, .y = 20.0}));
  REQUIRE(behindLine(TeamSide::kAway, shape, kPitch) ==
          PitchRect{.min = {.x = 45.0, .y = 0.0}, .max = {.x = 60.0, .y = 40.0}});
}

TEST_CASE("Rest defence stands behind the ball, never behind the goal line", "[zones]") {
  REQUIRE(restDefenceZone(TeamSide::kHome, {.x = 40.0, .y = 5.0}, kPitch) ==
          PitchRect{.min = {.x = 20.0, .y = 8.0}, .max = {.x = 35.0, .y = 32.0}});
  REQUIRE(restDefenceZone(TeamSide::kAway, {.x = 20.0, .y = 20.0}, kPitch) ==
          PitchRect{.min = {.x = 25.0, .y = 8.0}, .max = {.x = 40.0, .y = 32.0}});
  REQUIRE(restDefenceZone(TeamSide::kHome, {.x = 3.0, .y = 20.0}, kPitch) ==
          PitchRect{.min = {.x = 0.0, .y = 8.0}, .max = {.x = 0.0, .y = 32.0}});
}
