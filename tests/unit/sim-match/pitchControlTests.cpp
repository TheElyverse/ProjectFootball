#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

#include "kickoffScenario.hpp"
#include "matchSetup.hpp"
#include "matchSimulation.hpp"
#include "matchState.hpp"
#include "matchStateHash.hpp"
#include "pitchControl.hpp"
#include "pitchControlGrid.hpp"
#include "spatialQueries.hpp"

using Catch::Matchers::WithinAbs;
using ElyverseFootball::SimCore::SimTick;
using ElyverseFootball::SimCore::Vec2;
using ElyverseFootball::SimMatch::computePitchControl;
using ElyverseFootball::SimMatch::estimateArrivalSeconds;
using ElyverseFootball::SimMatch::GridCell;
using ElyverseFootball::SimMatch::hashMatchState;
using ElyverseFootball::SimMatch::makePitchControlSystem;
using ElyverseFootball::SimMatch::makeSevenASideKickoff;
using ElyverseFootball::SimMatch::MatchSimulation;
using ElyverseFootball::SimMatch::MatchState;
using ElyverseFootball::SimMatch::Pitch;
using ElyverseFootball::SimMatch::PitchControlConfig;
using ElyverseFootball::SimMatch::PitchControlGrid;
using ElyverseFootball::SimMatch::PitchRect;
using ElyverseFootball::SimMatch::PlayerMatchState;
using ElyverseFootball::SimMatch::TeamSide;

namespace {

[[nodiscard]] MatchState kickoff() {
  auto state = makeSevenASideKickoff(Pitch(60.0, 40.0));
  REQUIRE(state.has_value());
  return *std::move(state);
}

// Home players on whole-meter positions, running; away the exact mirror
// image through the halfway line, running the mirrored way.
[[nodiscard]] MatchState mirrored() {
  const std::vector<Vec2> positions{
      {.x = 3.0, .y = 20.0}, {.x = 12.0, .y = 9.0},  {.x = 12.0, .y = 31.0}, {.x = 21.0, .y = 20.0},
      {.x = 25.0, .y = 6.0}, {.x = 25.0, .y = 34.0}, {.x = 28.0, .y = 17.0}};
  std::vector<PlayerMatchState> players;
  ElyverseFootball::SimCore::PlayerId::ValueType nextId = 1;
  for (const TeamSide side : {TeamSide::kHome, TeamSide::kAway}) {
    for (const Vec2 position : positions) {
      const double sign = side == TeamSide::kHome ? 1.0 : -1.0;
      players.push_back({.playerId = ElyverseFootball::SimCore::PlayerId(nextId++),
                         .side = side,
                         .position = {.x = side == TeamSide::kHome ? position.x : 60.0 - position.x,
                                      .y = position.y},
                         .velocity = {.x = 2.0 * sign, .y = 1.0},
                         .attributes = {},
                         .target = std::nullopt,
                         .facing = {.x = sign, .y = 0.0}});
    }
  }
  auto state = MatchState::create({.pitch = Pitch(60.0, 40.0),
                                   .players = std::move(players),
                                   .ball = {.position = {.x = 30.0, .y = 20.0},
                                            .velocity = {},
                                            .owner = std::nullopt,
                                            .lastTouch = std::nullopt},
                                   .playersPerSide = 7});
  REQUIRE(state.has_value());
  return *std::move(state);
}

}  // namespace

TEST_CASE("The grid covers the pitch in square cells", "[pitchControl]") {
  const PitchControlGrid grid = computePitchControl(kickoff(), {});
  REQUIRE(grid.columns() == 30);
  REQUIRE(grid.rows() == 20);
  REQUIRE(grid.cellCenter({.column = 0, .row = 0}) == Vec2{.x = 1.0, .y = 1.0});
  REQUIRE(grid.cellCenter({.column = 29, .row = 19}) == Vec2{.x = 59.0, .y = 39.0});

  // A pitch that is not a whole number of cells gets one more, partial cell.
  const auto odd = makeSevenASideKickoff(Pitch(61.0, 40.5));
  REQUIRE(odd.has_value());
  const PitchControlGrid oddGrid = computePitchControl(*odd, {});
  REQUIRE(oddGrid.columns() == 31);
  REQUIRE(oddGrid.rows() == 21);
}

TEST_CASE("A cell's arrival time is its team's fastest player's", "[pitchControl]") {
  const MatchState state = kickoff();
  const PitchControlGrid grid = computePitchControl(state, {});
  const GridCell cell{.column = 7, .row = 12};
  for (const TeamSide side : {TeamSide::kHome, TeamSide::kAway}) {
    double earliest = std::numeric_limits<double>::infinity();
    for (const PlayerMatchState& player : state.players()) {
      if (player.side == side) {
        earliest = std::min(
            earliest,
            estimateArrivalSeconds(player, grid.cellCenter(cell), state.pitch()).value_or(0.0));
      }
    }
    REQUIRE(grid.arrivalSeconds(side, cell) == earliest);
  }
}

TEST_CASE("Control is a logistic of the arrival advantage", "[pitchControl]") {
  const PitchControlGrid grid({.columns = 1,
                               .rows = 1,
                               .cellSize = 2.0,
                               .controlSeconds = 0.5,
                               .homeArrival = {1.0},
                               .awayArrival = {1.5}});
  const GridCell cell{};
  REQUIRE_THAT(grid.control(TeamSide::kHome, cell), WithinAbs(1.0 / (1.0 + std::exp(-1.0)), 1e-15));
  REQUIRE_THAT(grid.control(TeamSide::kHome, cell) + grid.control(TeamSide::kAway, cell),
               WithinAbs(1.0, 1e-15));
  const PitchControlGrid even({.columns = 1,
                               .rows = 1,
                               .cellSize = 2.0,
                               .controlSeconds = 0.5,
                               .homeArrival = {2.0},
                               .awayArrival = {2.0}});
  REQUIRE(even.control(TeamSide::kHome, cell) == 0.5);
}

TEST_CASE("Mirrored positions give mirrored control, without NaNs", "[pitchControl]") {
  // Away mirrors home through the halfway line exactly, so every arrival time
  // has a bit-identical mirror.
  const PitchControlGrid grid = computePitchControl(mirrored(), {});
  for (std::size_t column = 0; column < grid.columns(); ++column) {
    for (std::size_t row = 0; row < grid.rows(); ++row) {
      const GridCell cell{.column = column, .row = row};
      const GridCell mirror{.column = grid.columns() - 1 - column, .row = row};
      const double home = grid.control(TeamSide::kHome, cell);
      REQUIRE(std::isfinite(home));
      REQUIRE(home >= 0.0);
      REQUIRE(home <= 1.0);
      REQUIRE(home == grid.control(TeamSide::kAway, mirror));
    }
  }
  REQUIRE_THAT(grid.share(TeamSide::kHome), WithinAbs(0.5, 1e-12));
}

TEST_CASE("Regions are contested, dominated or empty", "[pitchControl]") {
  const PitchControlGrid grid = computePitchControl(kickoff(), {});
  SECTION("contested: the centre circle") {
    const PitchRect centre{.min = {.x = 24.0, .y = 14.0}, .max = {.x = 36.0, .y = 26.0}};
    const auto control = grid.regionControl(TeamSide::kHome, centre);
    REQUIRE(control.has_value());
    REQUIRE_THAT(control.value_or(0.0), WithinAbs(0.5, 1e-12));
  }
  SECTION("dominated: in front of the home goal") {
    const PitchRect box{.min = {.x = 0.0, .y = 10.0}, .max = {.x = 12.0, .y = 30.0}};
    REQUIRE(grid.regionControl(TeamSide::kHome, box).value_or(0.0) > 0.99);
    REQUIRE(grid.regionControl(TeamSide::kAway, box).value_or(1.0) < 0.01);
  }
  SECTION("empty: no cell centre inside") {
    const PitchRect sliver{.min = {.x = 10.2, .y = 10.2}, .max = {.x = 10.8, .y = 10.8}};
    REQUIRE_FALSE(grid.regionControl(TeamSide::kHome, sliver).has_value());
    const PitchRect offPitch{.min = {.x = 70.0, .y = 0.0}, .max = {.x = 80.0, .y = 40.0}};
    REQUIRE_FALSE(grid.regionControl(TeamSide::kAway, offPitch).has_value());
  }
}

TEST_CASE("Control at a position is its cell's", "[pitchControl]") {
  const PitchControlGrid grid = computePitchControl(kickoff(), {});
  REQUIRE(grid.cellAt({.x = 3.9, .y = 0.0}) == GridCell{.column = 1, .row = 0});
  REQUIRE(grid.cellAt({.x = 60.0, .y = 40.0}) == GridCell{.column = 29, .row = 19});
  REQUIRE(grid.cellAt({.x = -5.0, .y = 99.0}) == GridCell{.column = 0, .row = 19});
  REQUIRE_FALSE(grid.cellAt({.x = std::nan(""), .y = 1.0}).has_value());
  REQUIRE(grid.controlAt(TeamSide::kHome, {.x = 13.0, .y = 21.0}) ==
          grid.control(TeamSide::kHome, {.column = 6, .row = 10}));
  REQUIRE(grid.controlAt(TeamSide::kHome, {.x = std::nan(""), .y = 1.0}) == 0.5);
  REQUIRE_THROWS_AS(grid.control(TeamSide::kHome, {.column = 30, .row = 0}), std::out_of_range);
}

TEST_CASE("A grid needs cells, arrival times for each and positive sizes", "[pitchControl]") {
  REQUIRE_THROWS_AS(PitchControlGrid({.columns = 0,
                                      .rows = 3,
                                      .cellSize = 2.0,
                                      .controlSeconds = 0.5,
                                      .homeArrival = {},
                                      .awayArrival = {}}),
                    std::invalid_argument);
  REQUIRE_THROWS_AS(PitchControlGrid({.columns = 2,
                                      .rows = 1,
                                      .cellSize = 2.0,
                                      .controlSeconds = 0.5,
                                      .homeArrival = {1.0},
                                      .awayArrival = {1.0, 2.0}}),
                    std::invalid_argument);
  REQUIRE_THROWS_AS(PitchControlGrid({.columns = 1,
                                      .rows = 1,
                                      .cellSize = 0.0,
                                      .controlSeconds = 0.5,
                                      .homeArrival = {1.0},
                                      .awayArrival = {1.0}}),
                    std::invalid_argument);
  REQUIRE_THROWS_AS(PitchControlGrid({.columns = 1,
                                      .rows = 1,
                                      .cellSize = 2.0,
                                      .controlSeconds = -1.0,
                                      .homeArrival = {1.0},
                                      .awayArrival = {1.0}}),
                    std::invalid_argument);
}

TEST_CASE("The system refreshes the cached grid on its interval", "[pitchControl]") {
  const MatchState initial = kickoff();
  REQUIRE_FALSE(initial.pitchControl().has_value());
  MatchSimulation simulation({.initialState = initial,
                              .seed = 1,
                              .ticksPerSecond = 30,
                              .systems = {makePitchControlSystem({.intervalTicks = 5})},
                              .commands = {}});
  REQUIRE(simulation.step().has_value());
  REQUIRE(simulation.state().pitchControl() == computePitchControl(initial, {}));
  REQUIRE(hashMatchState(simulation.state()) != hashMatchState(initial));

  // Deterministic: a second run holds an equal grid.
  MatchSimulation again({.initialState = initial,
                         .seed = 1,
                         .ticksPerSecond = 30,
                         .systems = {makePitchControlSystem({.intervalTicks = 5})},
                         .commands = {}});
  REQUIRE(again.step().has_value());
  REQUIRE(hashMatchState(again.state()) == hashMatchState(simulation.state()));
}

TEST_CASE("The pitch-control system rejects an invalid configuration", "[pitchControl]") {
  REQUIRE_THROWS_AS(makePitchControlSystem({.intervalTicks = 0}), std::invalid_argument);
  REQUIRE_THROWS_AS(makePitchControlSystem({.intervalTicks = 10, .cellSize = 0.1}),
                    std::invalid_argument);
  REQUIRE_THROWS_AS(
      makePitchControlSystem({.intervalTicks = 10, .cellSize = 2.0, .controlSeconds = 0.0}),
      std::invalid_argument);
  REQUIRE_NOTHROW(makePitchControlSystem({}));
}

// Run with: sim-match-tests "[!benchmark]". docs/pitch-control.md records the
// result.
TEST_CASE("Pitch control refresh cost", "[!benchmark][pitchControl]") {
  const MatchState state = kickoff();
  BENCHMARK("seven-a-side, 2 m cells") {
    return computePitchControl(state, {});
  };
}
