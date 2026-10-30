// Free kicks and penalties (docs/set-pieces.md#free-kicks,
// docs/set-pieces.md#penalties): the eleven-a-side reference against itself,
// a free kick or penalty for home awarded at tick 0. The wall stands at the
// legal distance and takes some shots, others clear it; penalties are scored
// and saved, a parry live for the rebound; an indirect free kick scores no
// goal directly.

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

#include "ids.hpp"
#include "matchEvents.hpp"
#include "matchFixtures.hpp"
#include "matchSetup.hpp"
#include "matchSimulation.hpp"
#include "matchState.hpp"
#include "pitch.hpp"
#include "restart.hpp"
#include "simTime.hpp"

using ElyverseFootball::SimCore::PlayerId;
using ElyverseFootball::SimCore::SimTick;
using ElyverseFootball::SimCore::Vec2;
using ElyverseFootball::SimMatch::awardFreeKick;
using ElyverseFootball::SimMatch::BallState;
using ElyverseFootball::SimMatch::findPlayerIndex;
using ElyverseFootball::SimMatch::GoalEnd;
using ElyverseFootball::SimMatch::GoalScored;
using ElyverseFootball::SimMatch::makeMatchSystems;
using ElyverseFootball::SimMatch::MatchEvent;
using ElyverseFootball::SimMatch::MatchSimulation;
using ElyverseFootball::SimMatch::MatchState;
using ElyverseFootball::SimMatch::MatchStateWriter;
using ElyverseFootball::SimMatch::MatchStepContext;
using ElyverseFootball::SimMatch::MatchSystem;
using ElyverseFootball::SimMatch::PlayerMatchState;
using ElyverseFootball::SimMatch::RestartKind;
using ElyverseFootball::SimMatch::SaveAttempted;
using ElyverseFootball::SimMatch::SaveResult;
using ElyverseFootball::SimMatch::SetPieceTaken;
using ElyverseFootball::SimMatch::ShotAttempted;
using ElyverseFootball::SimMatch::ShotDeflected;
using ElyverseFootball::SimMatch::ShotResolved;
using ElyverseFootball::SimMatch::TeamSide;
using ElyverseFootball::TestSupport::setupOf;

namespace {

// The match from tactic-match-eleven with a free kick or penalty of this kind
// for home at `spot` awarded in its first step.
[[nodiscard]] MatchSimulation awarded(const RestartKind kind, const Vec2 spot,
                                      const std::uint64_t seed) {
  auto setup = setupOf("tactic-match-eleven", seed);
  setup.commands.clear();
  MatchSystem award{.name = "award",
                    .update =
                        [kind, spot](const MatchStepContext& context, const MatchState& current,
                                     MatchStateWriter& next) {
                          if (context.tick() == SimTick(0)) {
                            (void)awardFreeKick(
                                context, current, next,
                                {.kind = kind, .side = TeamSide::kHome, .spot = spot}, {});
                          }
                        },
                    .intervalTicks = 1,
                    .phaseTicks = 0};
  std::vector<MatchSystem> systems{award};
  for (MatchSystem& system : makeMatchSystems(setup.config)) {
    systems.push_back(std::move(system));
  }
  return MatchSimulation({.initialState = setup.initialState,
                          .seed = setup.seed,
                          .ticksPerSecond = setup.config.ticksPerSecond,
                          .systems = std::move(systems),
                          .commands = {}});
}

// What came of the taker's first shot, if he shot.
struct Kick {
  std::optional<MatchState> takenFrom;
  std::optional<SetPieceTaken> taken;
  std::optional<ShotAttempted> shot;
  // Who deflected the shot, if anyone did, and how it ended.
  std::optional<PlayerId> blockedBy;
  std::optional<ElyverseFootball::SimMatch::ShotOutcome> outcome;
  bool goal = false;
  // The keeper who parried the shot into play, if one did.
  std::optional<PlayerId> parriedBy;
  bool rebound = false;
};

[[nodiscard]] Kick kicked(MatchSimulation& simulation, const SimTick until) {
  Kick kick;
  bool resolved = false;
  while (simulation.tick() < until && !kick.goal && !kick.rebound &&
         (!resolved || kick.parriedBy)) {
    const MatchState before = simulation.state();
    REQUIRE(simulation.step().has_value());
    for (const MatchEvent& event : simulation.events()) {
      if (const auto* taken = std::get_if<SetPieceTaken>(&event); taken != nullptr && !kick.taken) {
        kick.takenFrom = before;
        kick.taken = *taken;
      } else if (const auto* shot = std::get_if<ShotAttempted>(&event);
                 shot != nullptr && kick.taken && shot->shooter == kick.taken->taker &&
                 !kick.shot) {
        kick.shot = *shot;
      } else if (const auto* deflected = std::get_if<ShotDeflected>(&event);
                 deflected != nullptr && kick.shot && !resolved && !kick.blockedBy) {
        kick.blockedBy = deflected->player;
      } else if (std::holds_alternative<GoalScored>(event) && kick.shot) {
        kick.goal = true;
      } else if (const auto* save = std::get_if<SaveAttempted>(&event);
                 save != nullptr && kick.shot && save->result == SaveResult::kParriedIntoPlay) {
        kick.parriedBy = save->keeper;
      } else if (const auto* end = std::get_if<ShotResolved>(&event);
                 end != nullptr && kick.shot && end->shotTick == kick.shot->tick) {
        resolved = true;
        kick.outcome = end->outcome;
      }
    }
    // After a parry, whoever touches the ball next plays the rebound.
    const auto& touch = simulation.state().ball().lastTouch;
    if (kick.parriedBy && touch && touch->playerId != *kick.parriedBy) {
      kick.rebound = true;
    }
    // The taker played it to a teammate instead of shooting.
    if (kick.taken && !kick.shot && touch && touch->playerId != kick.taken->taker) {
      break;
    }
  }
  return kick;
}

}  // namespace

TEST_CASE("Free kicks: the wall takes some shots, others clear it", "[acceptance][freeKicks]") {
  const Vec2 spot{.x = 105.0 - 22.0, .y = 34.0};
  int shots = 0;
  int blocked = 0;
  int cleared = 0;
  for (std::uint64_t seed = 1; seed <= 30; ++seed) {
    CAPTURE(seed);
    MatchSimulation simulation = awarded(RestartKind::kDirectFreeKick, spot, seed);
    const Kick kick = kicked(simulation, SimTick(1200));
    REQUIRE(kick.takenFrom.has_value());
    // The wall: away players a centre circle's radius from the ball, between
    // it and the goal; nobody of away nearer.
    std::vector<PlayerId> wall;
    const MatchState* const taken = kick.takenFrom ? &*kick.takenFrom : nullptr;
    REQUIRE(taken != nullptr);
    for (const PlayerMatchState& player : taken->players()) {
      const double distance = ElyverseFootball::SimCore::distance(player.position, spot);
      if (player.side == TeamSide::kAway) {
        REQUIRE(distance >= taken->pitch().markings().centerCircleRadiusMeters);
        if (distance < 11.0 && player.position.x > spot.x) {
          wall.push_back(player.playerId);
        }
      }
    }
    REQUIRE(wall.size() >= 3);
    if (kick.shot) {
      ++shots;
      const bool byWall = kick.blockedBy && std::ranges::find(wall, *kick.blockedBy) != wall.end();
      blocked += byWall ? 1 : 0;
      // Over the wall or round it: not deflected, and on target or into the goal.
      using ElyverseFootball::SimMatch::ShotOutcome;
      cleared += !kick.blockedBy &&
                         (kick.outcome == ShotOutcome::kGoal || kick.outcome == ShotOutcome::kSaved)
                     ? 1
                     : 0;
    }
  }
  CAPTURE(shots, blocked, cleared);
  REQUIRE(blocked >= 1);
  REQUIRE(cleared >= 1);
}

TEST_CASE("Penalties: scored, and saved with the rebound live", "[acceptance][freeKicks]") {
  const Vec2 spot = ElyverseFootball::SimMatch::Pitch(105.0, 68.0).penaltySpot(GoalEnd::kMaxX);
  int goals = 0;
  int rebounds = 0;
  for (std::uint64_t seed = 1; seed <= 40; ++seed) {
    CAPTURE(seed);
    MatchSimulation simulation = awarded(RestartKind::kPenalty, spot, seed);
    const Kick kick = kicked(simulation, SimTick(900));
    REQUIRE(kick.takenFrom.has_value());
    REQUIRE(kick.shot.has_value());
    // The keeper on his line, everyone else but the taker outside the area.
    const MatchState* const taken = kick.takenFrom ? &*kick.takenFrom : nullptr;
    REQUIRE(taken != nullptr);
    const MatchState& state = *taken;
    const auto area = state.pitch().penaltyArea(GoalEnd::kMaxX);
    int inside = 0;
    for (const PlayerMatchState& player : state.players()) {
      inside += area.contains(player.position) ? 1 : 0;
    }
    REQUIRE(inside == 2);
    const auto keeper = findPlayerIndex(state, state.players()[11].playerId);
    REQUIRE(keeper.has_value());
    REQUIRE(state.players()[11].position.x > 104.0);
    goals += kick.goal ? 1 : 0;
    rebounds += kick.rebound ? 1 : 0;
  }
  CAPTURE(goals, rebounds);
  // About three in four penalties are scored; between half and nine in ten.
  REQUIRE(goals >= 20);
  REQUIRE(goals <= 36);
  REQUIRE(rebounds >= 1);
}

TEST_CASE("Free kicks: an indirect free kick scores no goal directly", "[acceptance][freeKicks]") {
  using ElyverseFootball::SimMatch::BallTouch;
  using ElyverseFootball::SimMatch::RestartTaken;
  // Home's 10 has just struck the ball into away's goal from close in.
  const auto run = [](const bool indirect) {
    auto setup = setupOf("tactic-match-eleven", 1);
    setup.commands.clear();
    const MatchState& state = setup.initialState;
    const PlayerId striker = state.players()[9].playerId;
    BallState ball = state.ball();
    ball.owner.reset();
    ball.position = {.x = 104.0, .y = 36.8};
    ball.velocity = {.x = 20.0, .y = 0.0};
    ball.lastTouch = BallTouch{.playerId = striker, .tick = SimTick(0)};
    auto struck = MatchState::create({.pitch = state.pitch(),
                                      .players = {state.players().begin(), state.players().end()},
                                      .ball = ball,
                                      .playersPerSide = state.playersPerSide()},
                                     state.tactics());
    REQUIRE(struck.has_value());
    setup.initialState = *std::move(struck);
    MatchSystem kick{
        .name = "kick",
        .update =
            [indirect, striker](const MatchStepContext& context, const MatchState& /*current*/,
                                MatchStateWriter& next) {
              if (indirect && context.tick() == SimTick(0)) {
                next.setIndirectKick(BallTouch{.playerId = striker, .tick = context.tick()});
              }
            },
        .intervalTicks = 1,
        .phaseTicks = 0};
    std::vector<MatchSystem> systems{kick};
    for (MatchSystem& system : makeMatchSystems(setup.config)) {
      systems.push_back(std::move(system));
    }
    MatchSimulation simulation({.initialState = setup.initialState,
                                .seed = 1,
                                .ticksPerSecond = 30,
                                .systems = std::move(systems),
                                .commands = {}});
    int goals = 0;
    std::optional<RestartKind> restart;
    for (int tick = 0; tick < 30 && !restart; ++tick) {
      REQUIRE(simulation.step().has_value());
      for (const MatchEvent& event : simulation.events()) {
        goals += std::holds_alternative<GoalScored>(event) ? 1 : 0;
        if (const auto* taken = std::get_if<RestartTaken>(&event)) {
          restart = taken->kind;
        }
      }
    }
    return std::pair{goals, restart};
  };
  const auto [direct, directRestart] = run(true);
  REQUIRE(direct == 0);
  REQUIRE(directRestart == RestartKind::kGoalKick);
  const auto [counted, countedRestart] = run(false);
  REQUIRE(counted == 1);
  REQUIRE(countedRestart == RestartKind::kKickoff);
}

TEST_CASE(
    "Free kicks: an indirect free kick's taker does not shoot, and it counts after another "
    "touch",
    "[acceptance][freeKicks]") {
  const Vec2 spot{.x = 105.0 - 22.0, .y = 34.0};
  int played = 0;
  for (std::uint64_t seed = 1; seed <= 30; ++seed) {
    CAPTURE(seed);
    MatchSimulation simulation = awarded(RestartKind::kIndirectFreeKick, spot, seed);
    std::optional<PlayerId> taker;
    bool cleared = false;
    while (simulation.tick() < SimTick(1200) && !cleared) {
      const MatchState before = simulation.state();
      REQUIRE(simulation.step().has_value());
      for (const MatchEvent& event : simulation.events()) {
        if (const auto* taken = std::get_if<SetPieceTaken>(&event)) {
          taker = taken->taker;
          REQUIRE(simulation.state().indirectKick().has_value());
          REQUIRE(simulation.state()
                      .indirectKick()
                      .value_or(ElyverseFootball::SimMatch::BallTouch{})
                      .playerId == taken->taker);
        } else if (const auto* shot = std::get_if<ShotAttempted>(&event);
                   shot != nullptr && shot->shooter == taker) {
          // No shot by him while the kick is indirect.
          REQUIRE_FALSE(before.indirectKick().has_value());
        }
      }
      // Another player's touch: the step after, the kick counts like any ball.
      const auto& touch = before.ball().lastTouch;
      if (taker && before.indirectKick() && touch && touch->playerId != *taker) {
        REQUIRE_FALSE(simulation.state().indirectKick().has_value());
        cleared = true;
      }
    }
    REQUIRE(taker.has_value());
    played += cleared ? 1 : 0;
  }
  // Nearly every one is played on to someone else.
  REQUIRE(played >= 25);
}
