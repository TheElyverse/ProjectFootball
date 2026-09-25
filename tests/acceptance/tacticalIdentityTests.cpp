// The signatures of the tactical identities under data/tactics/
// (docs/tactical-identities.md): the same fixture and the same systems,
// only the tactic files differ. Checked over fixed seeds, so the results are
// reproducible.

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

#include "ballMovement.hpp"
#include "ids.hpp"
#include "matchCommand.hpp"
#include "matchEvents.hpp"
#include "matchSetup.hpp"
#include "matchSimulation.hpp"
#include "matchState.hpp"
#include "pitch.hpp"
#include "scenarios.hpp"
#include "simTime.hpp"
#include "tactic.hpp"
#include "tacticJson.hpp"
#include "tacticalPhase.hpp"
#include "vec2.hpp"

using ElyverseFootball::SimCore::PlayerId;
using ElyverseFootball::SimCore::SimTick;
using ElyverseFootball::SimCore::Vec2;
using ElyverseFootball::SimMatch::BallState;
using ElyverseFootball::SimMatch::carriedBallPosition;
using ElyverseFootball::SimMatch::GiveBallCommand;
using ElyverseFootball::SimMatch::kDefaultPlayersPerSide;
using ElyverseFootball::SimMatch::makeTacticMatch;
using ElyverseFootball::SimMatch::MatchConfig;
using ElyverseFootball::SimMatch::MatchEvent;
using ElyverseFootball::SimMatch::MatchSetup;
using ElyverseFootball::SimMatch::MatchSimulation;
using ElyverseFootball::SimMatch::MatchState;
using ElyverseFootball::SimMatch::PassAttempted;
using ElyverseFootball::SimMatch::Pitch;
using ElyverseFootball::SimMatch::PlayerMatchState;
using ElyverseFootball::SimMatch::startMatch;
using ElyverseFootball::SimMatch::TeamSide;
using ElyverseFootball::SimTactics::loadTactic;
using ElyverseFootball::SimTactics::Tactic;
using ElyverseFootball::SimTactics::TacticalPhase;

namespace {

constexpr std::uint64_t kSeeds = 20;

[[nodiscard]] Tactic preset(const std::string& name) {
  auto tactic = loadTactic(std::filesystem::path(PF_DATA_DIR) / "tactics" / (name + ".json"));
  INFO(name);
  REQUIRE(tactic.has_value());
  return *std::move(tactic);
}

// Where home won the ball back over one-minute tactic matches against the
// reference tactic, one per seed.
struct RegainTally {
  int regains = 0;
  // Regains in the opponent's half.
  int highRegains = 0;
};

[[nodiscard]] RegainTally homeRegains(const std::string& home) {
  RegainTally tally;
  for (std::uint64_t seed = 1; seed <= kSeeds; ++seed) {
    auto setup = makeTacticMatch({.home = preset(home), .away = preset("reference")}, seed);
    REQUIRE(setup.has_value());
    MatchSimulation simulation = startMatch(*setup);
    while (simulation.tick() < SimTick(1800)) {
      const bool hadBall = simulation.state().possession().team == TeamSide::kHome;
      REQUIRE(simulation.step().has_value());
      const MatchState& state = simulation.state();
      if (!hadBall && state.possession().team == TeamSide::kHome &&
          state.possession().fromOpponent) {
        ++tally.regains;
        tally.highRegains += state.ball().position.x > state.pitch().lengthMeters() / 2.0 ? 1 : 0;
      }
    }
  }
  return tally;
}

// A hand-placed player, in the tactic's slot order.
struct Placement {
  double x;
  double y;
};

using Side = std::array<Placement, kDefaultPlayersPerSide>;

// Home's holding midfielder (player 4) has just won the ball in midfield:
// away's midfielder had it at tick 0, home takes it at tick 10, so home is in
// its attacking transition. The striker is ahead of him with a defender
// near the lane; the centre backs are behind him, free.
[[nodiscard]] MatchSetup regainInMidfield(Tactic home, const std::uint64_t seed) {
  constexpr Side kHome{{{.x = 5.0, .y = 20.0},
                        {.x = 20.0, .y = 12.0},
                        {.x = 20.0, .y = 28.0},
                        {.x = 30.0, .y = 20.0},
                        {.x = 34.0, .y = 5.0},
                        {.x = 34.0, .y = 35.0},
                        {.x = 44.0, .y = 17.0}}};
  constexpr Side kAway{{{.x = 56.0, .y = 20.0},
                        {.x = 48.0, .y = 14.0},
                        {.x = 48.0, .y = 26.0},
                        {.x = 31.0, .y = 21.0},
                        {.x = 26.0, .y = 8.0},
                        {.x = 26.0, .y = 32.0},
                        {.x = 24.0, .y = 20.0}}};
  const Pitch pitch(60.0, 40.0);
  std::vector<PlayerMatchState> players;
  PlayerId::ValueType nextId = 1;
  for (const auto& [side, placements, facing] :
       {std::tuple{TeamSide::kHome, &kHome, 1.0}, std::tuple{TeamSide::kAway, &kAway, -1.0}}) {
    for (const Placement& placement : *placements) {
      players.push_back({.playerId = PlayerId(nextId++),
                         .side = side,
                         .position = {.x = placement.x, .y = placement.y},
                         .velocity = {},
                         .attributes = {},
                         .target = std::nullopt,
                         .facing = {.x = facing, .y = 0.0}});
    }
  }
  const MatchConfig config;
  const BallState ball{.position = carriedBallPosition(players[10], config.ball, pitch),
                       .velocity = {},
                       .owner = std::nullopt,
                       .lastTouch = std::nullopt};
  auto state = MatchState::create({.pitch = pitch,
                                   .players = std::move(players),
                                   .ball = ball,
                                   .playersPerSide = kDefaultPlayersPerSide},
                                  {.home = std::move(home), .away = preset("reference")});
  REQUIRE(state.has_value());
  return {.initialState = *std::move(state),
          .config = config,
          .seed = seed,
          .commands = {{.tick = SimTick(0), .command = GiveBallCommand{.playerId = PlayerId(11)}},
                       {.tick = SimTick(10), .command = GiveBallCommand{.playerId = PlayerId(4)}}}};
}

// How often player 4's first pass after the regain goes forward, over the
// seeds; every seed must see him pass within three seconds, in the
// attacking transition.
[[nodiscard]] int forwardFirstPasses(const std::string& home) {
  int forward = 0;
  for (std::uint64_t seed = 1; seed <= kSeeds; ++seed) {
    MatchSimulation simulation = startMatch(regainInMidfield(preset(home), seed));
    // How far forward the first pass went, once he has played it.
    std::optional<double> gain;
    while (!gain && simulation.tick() < SimTick(100)) {
      REQUIRE(simulation.step().has_value());
      for (const MatchEvent& event : simulation.events()) {
        const auto* pass = std::get_if<PassAttempted>(&event);
        if (pass != nullptr && pass->passer == PlayerId(4)) {
          gain = pass->target.x - pass->from.x;
          const auto& phase = simulation.state().phase(TeamSide::kHome);
          CHECK(phase.transform([](const auto& team) { return team.phase; }) ==
                TacticalPhase::kAttackingTransition);
        }
      }
    }
    CAPTURE(home, seed);
    REQUIRE(gain.has_value());
    forward += gain.value_or(0.0) > 5.0 ? 1 : 0;
  }
  return forward;
}

}  // namespace

TEST_CASE("P2: the pressing tactic wins the ball back more often and higher up",
          "[acceptance][p2][identities]") {
  const RegainTally pressing = homeRegains("pressing");
  const RegainTally counter = homeRegains("counter");
  CAPTURE(pressing.regains, pressing.highRegains, counter.regains, counter.highRegains);
  // Twenty minutes each; at the time of writing 108 regains, 62 of them in
  // the opponent's half, against 67 and 32.
  REQUIRE(pressing.regains > counter.regains);
  REQUIRE(pressing.highRegains > counter.highRegains * 3 / 2);
}

TEST_CASE("P2: the counter tactic plays forward straight after a regain",
          "[acceptance][p2][identities]") {
  const int counter = forwardFirstPasses("counter");
  const int possession = forwardFirstPasses("possession");
  CAPTURE(counter, possession);
  // Out of twenty; at the time of writing 18 against 5.
  REQUIRE(counter >= possession + 8);
}
