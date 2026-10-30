// P3 acceptance (docs/scenarios.md#p3-acceptance): whole eleven-a-side matches
// of the tactical identities with every rule on -- restarts and set pieces,
// offside, fouls and cards -- played from the kickoff to full time. They stay
// valid, never stall, and replay identically; the final hashes of one are
// pinned. The statistical guardrails and the performance budget of whole
// 90-minute matches are Release-only (tests/benchmark/p3Acceptance.cmake).

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <variant>

#include "matchClock.hpp"
#include "matchEvents.hpp"
#include "matchSetup.hpp"
#include "matchSimulation.hpp"
#include "matchState.hpp"
#include "replay.hpp"
#include "scenarios.hpp"
#include "simTime.hpp"
#include "tacticJson.hpp"

using ElyverseFootball::SimCore::SimTick;
using ElyverseFootball::SimMatch::findNonFiniteValues;
using ElyverseFootball::SimMatch::MatchEvent;
using ElyverseFootball::SimMatch::MatchPeriod;
using ElyverseFootball::SimMatch::MatchSetup;
using ElyverseFootball::SimMatch::MatchSimulation;
using ElyverseFootball::SimMatch::startMatch;
using ElyverseFootball::SimReplay::kDefaultCheckpointIntervalTicks;
using ElyverseFootball::SimReplay::playReplay;
using ElyverseFootball::SimReplay::recordMatch;

namespace {

// A tactical identity from data/tactics.
[[nodiscard]] ElyverseFootball::SimTactics::Tactic identity(const std::string& name) {
  auto tactic = ElyverseFootball::SimTactics::loadTactic(std::filesystem::path(PF_DATA_DIR) /
                                                         "tactics" / (name + ".json"));
  REQUIRE(tactic.has_value());
  return *std::move(tactic);
}

// Home's identity against away's on the standard pitch with every rule on, a
// whole match of `minutes` real minutes.
// NOLINTBEGIN(bugprone-easily-swappable-parameters) -- home, then away.
[[nodiscard]] MatchSetup wholeMatch(const std::string& home, const std::string& away,
                                    const std::uint64_t seed, const double minutes) {
  auto setup = ElyverseFootball::SimMatch::makeTacticMatch(
      {.home = identity(home), .away = identity(away)}, seed);
  REQUIRE(setup.has_value());
  setup->config.restarts.enabled = true;
  setup->config.offside.enabled = true;
  setup->config.fouls.enabled = true;
  setup->config.clock.durationMinutes = minutes;
  return *std::move(setup);
}
// NOLINTEND(bugprone-easily-swappable-parameters)

// How long, in real seconds, the match went at most without its ball changing
// owner, and lay dead for a set piece at a stretch.
struct Stalls {
  double withoutOwnerChange = 0.0;
  double deadBall = 0.0;
  bool fullTime = false;
};

[[nodiscard]] Stalls played(const MatchSetup& setup) {
  MatchSimulation simulation = startMatch(setup);
  const double secondsPerTick = 1.0 / setup.config.ticksPerSecond;
  const std::int64_t bound =
      ElyverseFootball::SimMatch::maxMatchTicks(setup.config.clock, setup.config.ticksPerSecond);
  Stalls stalls;
  std::int64_t sinceChange = 0;
  std::int64_t dead = 0;
  while (simulation.tick().value() < bound) {
    // A step that would leave an invalid state fails; so would this test.
    const auto stepped = simulation.step();
    REQUIRE(stepped.has_value());
    const auto& state = simulation.state();
    if (simulation.tick().value() % 300 == 0) {
      REQUIRE(findNonFiniteValues(state).empty());
    }
    bool changed = false;
    for (const MatchEvent& event : simulation.events()) {
      changed =
          changed || std::holds_alternative<ElyverseFootball::SimMatch::PossessionChanged>(event);
    }
    sinceChange = changed ? 0 : sinceChange + 1;
    dead = state.setPiece() ? dead + 1 : 0;
    stalls.withoutOwnerChange =
        std::max(stalls.withoutOwnerChange, static_cast<double>(sinceChange) * secondsPerTick);
    stalls.deadBall = std::max(stalls.deadBall, static_cast<double>(dead) * secondsPerTick);
    if (state.clock().period == MatchPeriod::kFullTime) {
      stalls.fullTime = true;
      break;
    }
  }
  REQUIRE(findNonFiniteValues(simulation.state()).empty());
  return stalls;
}

}  // namespace

TEST_CASE("P3: whole eleven-a-side matches with every rule stay valid and never stall",
          "[acceptance][p3]") {
  struct Pairing {
    const char* home;
    const char* away;
  };
  for (const Pairing pairing : {Pairing{.home = "pressing", .away = "counter"},
                                Pairing{.home = "possession", .away = "pressing"},
                                Pairing{.home = "counter", .away = "possession"}}) {
    for (std::uint64_t seed = 1; seed <= 2; ++seed) {
      CAPTURE(pairing.home, pairing.away, seed);
      const Stalls stalls = played(wholeMatch(pairing.home, pairing.away, seed, 3.0));
      // To full time within the bound no match that plays on gets near.
      REQUIRE(stalls.fullTime);
      // Somebody plays the ball on: the longest a ball kept its owner, or lay
      // free for nobody, was 11.2 s over these six matches
      // (docs/scenarios.md#p3-acceptance).
      CAPTURE(stalls.withoutOwnerChange, stalls.deadBall);
      REQUIRE(stalls.withoutOwnerChange < 30.0);
      // A set piece is taken: within its setup time and waitSeconds, 8 s at
      // most over these six matches.
      REQUIRE(stalls.deadBall < 25.0);
    }
  }
}

TEST_CASE("P3: a whole match with every rule replays with identical hashes, pinned",
          "[acceptance][p3][replay]") {
  const MatchSetup setup = wholeMatch("pressing", "counter", 3, 2.0);
  const auto replay = recordMatch(setup, SimTick(1'000'000), kDefaultCheckpointIntervalTicks, "");
  REQUIRE(replay.has_value());
  const auto playback = playReplay(*replay);
  REQUIRE(playback.has_value());
  REQUIRE(playback->checkpointsVerified == replay->checkpoints.size());
  // Identical on every platform and compiler CI builds with; they change only
  // with the simulation's behaviour, the identities or the state layout.
  CHECK(replay->checkpoints.back().stateHash == 0x5d3a387ce404e72aULL);
  CHECK(replay->checkpoints.back().eventHash == 0xd5f20c0274a5363eULL);
}
