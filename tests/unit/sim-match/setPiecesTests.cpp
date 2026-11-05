// The set piece rules (docs/set-pieces.md): the places the players take, the
// distance the other side keeps, the planned taker and delivery, the side a
// set piece belongs to while it is set up, and the kickoffs that end it.

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <utility>
#include <variant>
#include <vector>

#include "fouls.hpp"
#include "ids.hpp"
#include "matchEvents.hpp"
#include "matchFixtures.hpp"
#include "matchSetup.hpp"
#include "matchSimulation.hpp"
#include "matchState.hpp"
#include "pitch.hpp"
#include "referenceTactic.hpp"
#include "setPiecePlan.hpp"
#include "setPieces.hpp"
#include "simTime.hpp"
#include "tactic.hpp"
#include "tacticalPhases.hpp"
#include "vec2.hpp"

using ElyverseFootball::SimCore::distance;
using ElyverseFootball::SimCore::SimTick;
using ElyverseFootball::SimCore::Vec2;
using ElyverseFootball::SimMatch::benchPosition;
using ElyverseFootball::SimMatch::makeMatchSystems;
using ElyverseFootball::SimMatch::MatchSimulation;
using ElyverseFootball::SimMatch::MatchState;
using ElyverseFootball::SimMatch::MatchStateWriter;
using ElyverseFootball::SimMatch::MatchStepContext;
using ElyverseFootball::SimMatch::MatchSystem;
using ElyverseFootball::SimMatch::PassIntent;
using ElyverseFootball::SimMatch::PeriodEnded;
using ElyverseFootball::SimMatch::plannedDelivery;
using ElyverseFootball::SimMatch::plannedTaker;
using ElyverseFootball::SimMatch::PlayerMatchState;
using ElyverseFootball::SimMatch::SetPiece;
using ElyverseFootball::SimMatch::SetPieceConfig;
using ElyverseFootball::SimMatch::setPieceTargets;
using ElyverseFootball::SimMatch::teamOnTheBall;
using ElyverseFootball::SimMatch::TeamSide;
using ElyverseFootball::SimTactics::referenceElevenASideTacticSpec;
using ElyverseFootball::SimTactics::SetPieceDelivery;
using ElyverseFootball::SimTactics::SetPieceKind;
using ElyverseFootball::SimTactics::Tactic;
using ElyverseFootball::SimTactics::TacticSpec;
using ElyverseFootball::TestSupport::setupOf;

namespace {

// Home's and away's first outfield players, by index.
constexpr std::size_t kHomeOutfield = 1;
constexpr std::size_t kAwayOutfield = 12;

// The eleven-a-side reference against itself from the kickoff line-up, both
// sides playing `spec`, with these players moved.
[[nodiscard]] MatchState matchWith(const TacticSpec& spec,
                                   const std::vector<std::pair<std::size_t, Vec2>>& moved = {}) {
  const MatchState initial = setupOf("tactic-match-eleven", 1).initialState;
  auto tactic = Tactic::create(spec);
  REQUIRE(tactic.has_value());
  std::vector<PlayerMatchState> players(initial.players().begin(), initial.players().end());
  for (const auto& [index, position] : moved) {
    players[index].position = position;
  }
  auto state = MatchState::create({.pitch = initial.pitch(),
                                   .players = std::move(players),
                                   .ball = initial.ball(),
                                   .playersPerSide = initial.playersPerSide()},
                                  {.home = *tactic, .away = *tactic});
  REQUIRE(state.has_value());
  return *std::move(state);
}

[[nodiscard]] SetPiece setPieceOf(const MatchState& state, const SetPieceKind kind, const Vec2 spot,
                                  const std::size_t taker = kHomeOutfield) {
  return {.kind = kind,
          .side = TeamSide::kHome,
          .taker = state.players()[taker].playerId,
          .spot = spot,
          .awarded = SimTick(0)};
}

}  // namespace

TEST_CASE("The other side keeps two metres at a throw-in, the circle's radius at a corner",
          "[setPieces]") {
  const Vec2 line{.x = 60.0, .y = 0.0};
  const Vec2 flag{.x = 105.0, .y = 0.0};
  const auto throwIn =
      matchWith(referenceElevenASideTacticSpec(), {{kAwayOutfield, {.x = 60.5, .y = 0.5}}});
  const auto atLine =
      setPieceTargets(throwIn, setPieceOf(throwIn, SetPieceKind::kThrowIn, line), {}, {});
  REQUIRE(atLine[kAwayOutfield].has_value());
  REQUIRE(distance(atLine[kAwayOutfield].value_or(line), line) >= 2.0 - 1e-9);

  const auto corner =
      matchWith(referenceElevenASideTacticSpec(), {{kAwayOutfield, {.x = 103.0, .y = 2.0}}});
  const double radius = corner.pitch().markings().centerCircleRadiusMeters;
  const auto atFlag =
      setPieceTargets(corner, setPieceOf(corner, SetPieceKind::kCorner, flag), {}, {});
  for (std::size_t index = 0; index < atFlag.size(); ++index) {
    if (corner.players()[index].side == TeamSide::kAway && atFlag[index]) {
      CAPTURE(index);
      REQUIRE(distance(atFlag[index].value_or(flag), flag) >= radius - 1e-9);
    }
  }
  REQUIRE(atFlag[kAwayOutfield].has_value());
}

TEST_CASE("A planned taker takes it unless sent off", "[setPieces]") {
  TacticSpec spec = referenceElevenASideTacticSpec();
  spec.setPieces.corner.taker = 7;
  const MatchState state = matchWith(spec);
  REQUIRE(plannedTaker(state, TeamSide::kHome, SetPieceKind::kCorner, 2) == 7);
  REQUIRE(plannedTaker(state, TeamSide::kHome, SetPieceKind::kThrowIn, 2) == 2);

  MatchSimulation simulation(
      {.initialState = state,
       .seed = 1,
       .ticksPerSecond = 30,
       .systems = {{.name = "send off",
                    .update =
                        [](const MatchStepContext& /*context*/, const MatchState& current,
                           MatchStateWriter& next) {
                          next.sendOff(7, benchPosition(current.pitch()));
                        },
                    .intervalTicks = 1,
                    .phaseTicks = 0}},
       .commands = {}});
  REQUIRE(simulation.step().has_value());
  REQUIRE(plannedTaker(simulation.state(), TeamSide::kHome, SetPieceKind::kCorner, 2) == 2);
}

TEST_CASE(
    "The planned delivery: lofted to a corner's area, short along the line, long to the "
    "front",
    "[setPieces]") {
  TacticSpec spec = referenceElevenASideTacticSpec();
  const Vec2 flag{.x = 105.0, .y = 0.0};
  SECTION("far post: lofted, meant for a runner") {
    const MatchState state = matchWith(spec);
    const auto pass =
        plannedDelivery(state, setPieceOf(state, SetPieceKind::kCorner, flag), {}, {}, {});
    REQUIRE(pass.has_value());
    const PassIntent delivered = pass.value_or(PassIntent{});
    REQUIRE(delivered.lofted);
    REQUIRE(delivered.receiver.has_value());
    REQUIRE(delivered.target.y > state.pitch().widthMeters() / 2.0);
  }
  SECTION("short: along the ground, along the goal line") {
    spec.setPieces.corner.delivery = SetPieceDelivery::kShort;
    const MatchState state = matchWith(spec);
    const auto pass =
        plannedDelivery(state, setPieceOf(state, SetPieceKind::kCorner, flag), {}, {}, {});
    REQUIRE(pass.has_value());
    const PassIntent delivered = pass.value_or(PassIntent{});
    REQUIRE_FALSE(delivered.lofted);
    REQUIRE(distance(delivered.target, flag) < 12.0);
  }
  SECTION("long goal kick: lofted to the most advanced outfield player") {
    spec.setPieces.goalKick.delivery = SetPieceDelivery::kLong;
    const MatchState state = matchWith(spec);
    const Vec2 spot{.x = 5.5, .y = 34.0};
    const auto pass =
        plannedDelivery(state, setPieceOf(state, SetPieceKind::kGoalKick, spot, 0), {}, {}, {});
    REQUIRE(pass.has_value());
    const PassIntent delivered = pass.value_or(PassIntent{});
    REQUIRE(delivered.lofted);
    double front = 0.0;
    for (const PlayerMatchState& player : state.players()) {
      if (player.side == TeamSide::kHome) {
        front = std::max(front, player.position.x);
      }
    }
    REQUIRE(delivered.target.x > front - 5.0);
  }
}

TEST_CASE("A set piece is its side's ball while it is set up", "[setPieces]") {
  const MatchState initial = matchWith(referenceElevenASideTacticSpec());
  const SetPiece throwIn{.kind = SetPieceKind::kThrowIn,
                         .side = TeamSide::kAway,
                         .taker = initial.players()[kAwayOutfield].playerId,
                         .spot = {.x = 40.0, .y = 0.0},
                         .awarded = SimTick(0)};
  MatchSimulation simulation(
      {.initialState = initial,
       .seed = 1,
       .ticksPerSecond = 30,
       .systems = {{.name = "award",
                    .update =
                        [throwIn](const MatchStepContext& /*context*/,
                                  const MatchState& /*current*/, MatchStateWriter& next) {
                          next.setBallOwner(std::nullopt);
                          next.setBallLastTouch(ElyverseFootball::SimMatch::BallTouch{
                              .playerId = ElyverseFootball::SimCore::PlayerId(2),
                              .tick = SimTick(0)});
                          next.setSetPiece(throwIn);
                        },
                    .intervalTicks = 1,
                    .phaseTicks = 0}},
       .commands = {}});
  REQUIRE(simulation.step().has_value());
  // Home's player touched the ball last; the throw-in is away's all the same.
  REQUIRE(simulation.state().ball().lastTouch.has_value());
  REQUIRE(teamOnTheBall(simulation.state()) == TeamSide::kAway);
}

TEST_CASE("A kickoff ends a set piece being set up; the second half's is one itself",
          "[setPieces]") {
  auto setup = setupOf("tactic-match-eleven", 1);
  setup.config.clock.durationMinutes = 1.0;
  setup.config.restarts.enabled = true;
  // When the first half ends without interference.
  std::optional<SimTick> halfTime;
  {
    MatchSimulation simulation({.initialState = setup.initialState,
                                .seed = setup.seed,
                                .ticksPerSecond = setup.config.ticksPerSecond,
                                .systems = makeMatchSystems(setup.config),
                                .commands = setup.commands});
    while (!halfTime && simulation.tick() < SimTick(3000)) {
      REQUIRE(simulation.step().has_value());
      const auto events = simulation.events();
      if (!ElyverseFootball::TestSupport::eventsOf<PeriodEnded>({events.begin(), events.end()})
               .empty()) {
        halfTime = SimTick(simulation.tick().value() - 1);
      }
    }
  }
  REQUIRE(halfTime.has_value());
  // The same match with a throw-in for home awarded in that very tick.
  const SimTick awardTick = halfTime.value_or(SimTick(0));
  MatchSystem award{.name = "award",
                    .update =
                        [awardTick](const MatchStepContext& context, const MatchState& current,
                                    MatchStateWriter& next) {
                          if (context.tick() == awardTick) {
                            next.setSetPiece(SetPiece{.kind = SetPieceKind::kThrowIn,
                                                      .side = TeamSide::kHome,
                                                      .taker = current.players()[3].playerId,
                                                      .spot = {.x = 40.0, .y = 0.0},
                                                      .awarded = awardTick});
                          }
                        },
                    .intervalTicks = 1,
                    .phaseTicks = 0};
  std::vector<MatchSystem> systems{award};
  for (MatchSystem& system : makeMatchSystems(setup.config)) {
    systems.push_back(std::move(system));
  }
  MatchSimulation simulation({.initialState = setup.initialState,
                              .seed = setup.seed,
                              .ticksPerSecond = setup.config.ticksPerSecond,
                              .systems = std::move(systems),
                              .commands = setup.commands});
  while (simulation.tick() <= awardTick) {
    REQUIRE(simulation.step().has_value());
  }
  const auto& setPiece = simulation.state().setPiece();
  REQUIRE(setPiece.has_value());
  REQUIRE(setPiece.value_or(SetPiece{}).kind == SetPieceKind::kKickoff);
  REQUIRE(setPiece.value_or(SetPiece{}).side == TeamSide::kAway);
  REQUIRE(simulation.state().ball().owner == setPiece.value_or(SetPiece{}).taker);
}

TEST_CASE("Set piece configurations are checked", "[setPieces]") {
  using ElyverseFootball::SimMatch::validate;
  REQUIRE_NOTHROW(validate(SetPieceConfig{}));
  for (const auto& broken :
       {SetPieceConfig{.throwInSeconds = -1.0}, SetPieceConfig{.readyRadius = 0.0},
        SetPieceConfig{.flightSeconds = 0.0}, SetPieceConfig{.cornerRunners = -1},
        SetPieceConfig{.throwInDistance = -2.0}}) {
    REQUIRE_THROWS_AS(validate(broken), std::invalid_argument);
  }
}
