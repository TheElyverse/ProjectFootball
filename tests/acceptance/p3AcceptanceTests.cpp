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
#include <map>
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
#include "setPiecePlan.hpp"
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

// How long each match lasts, in real minutes: long enough for every kind of
// set piece to come up over the six.
constexpr double kMinutes = 6.0;

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

// The longest the match may go without its ball changing owner, and lie dead
// for a set piece, in real seconds: over these matches 11.2 s and 8 s at most
// when measured (docs/scenarios.md#p3-acceptance).
constexpr double kMaxWithoutOwnerChange = 30.0;
constexpr double kMaxDeadBall = 25.0;

// What the rules and set pieces did in a match: how often each set piece was
// taken, by kind name, and how many fouls and offsides were called.
struct Rules {
  std::map<std::string, int> setPieces;
  int fouls = 0;
  int offsides = 0;
};

// Plays the match to full time -- within maxMatchTicks(), which no match that
// plays on gets near -- failing at the first invalid step, non-finite value or
// stall.
[[nodiscard]] Rules played(const MatchSetup& setup) {
  using ElyverseFootball::SimMatch::FoulCalled;
  using ElyverseFootball::SimMatch::OffsideCalled;
  using ElyverseFootball::SimMatch::PossessionChanged;
  using ElyverseFootball::SimMatch::SetPieceTaken;
  MatchSimulation simulation = startMatch(setup);
  const double secondsPerTick = 1.0 / setup.config.ticksPerSecond;
  const std::int64_t bound =
      ElyverseFootball::SimMatch::maxMatchTicks(setup.config.clock, setup.config.ticksPerSecond);
  Rules rules;
  std::int64_t sinceChange = 0;
  std::int64_t dead = 0;
  while (simulation.state().clock().period != MatchPeriod::kFullTime) {
    REQUIRE(simulation.tick().value() < bound);
    // A step that would leave an invalid state fails; so would this test.
    REQUIRE(simulation.step().has_value());
    const auto& state = simulation.state();
    if (simulation.tick().value() % 300 == 0) {
      REQUIRE(findNonFiniteValues(state).empty());
    }
    bool changed = false;
    for (const MatchEvent& event : simulation.events()) {
      changed = changed || std::holds_alternative<PossessionChanged>(event);
      if (const auto* taken = std::get_if<SetPieceTaken>(&event)) {
        ++rules.setPieces[std::string(ElyverseFootball::SimTactics::setPieceKindName(taken->kind))];
      }
      rules.fouls += std::holds_alternative<FoulCalled>(event) ? 1 : 0;
      rules.offsides += std::holds_alternative<OffsideCalled>(event) ? 1 : 0;
    }
    sinceChange = changed ? 0 : sinceChange + 1;
    dead = state.setPiece() ? dead + 1 : 0;
    // Somebody plays the ball on, and a set piece is taken.
    CAPTURE(simulation.tick().value());
    REQUIRE(static_cast<double>(sinceChange) * secondsPerTick < kMaxWithoutOwnerChange);
    REQUIRE(static_cast<double>(dead) * secondsPerTick < kMaxDeadBall);
  }
  REQUIRE(findNonFiniteValues(simulation.state()).empty());
  return rules;
}

}  // namespace

TEST_CASE("P3: whole eleven-a-side matches with every rule stay valid and never stall",
          "[acceptance][p3]") {
  struct Pairing {
    const char* home;
    const char* away;
  };
  Rules all;
  for (const Pairing pairing : {Pairing{.home = "pressing", .away = "counter"},
                                Pairing{.home = "possession", .away = "pressing"},
                                Pairing{.home = "counter", .away = "possession"}}) {
    for (std::uint64_t seed = 1; seed <= 2; ++seed) {
      CAPTURE(pairing.home, pairing.away, seed);
      const Rules rules = played(wholeMatch(pairing.home, pairing.away, seed, kMinutes));
      for (const auto& [kind, count] : rules.setPieces) {
        all.setPieces[kind] += count;
      }
      all.fouls += rules.fouls;
      all.offsides += rules.offsides;
    }
  }
  // Every kind of set piece came up and was taken, and offside was called:
  // over these six matches 5 corners, 1 throw-in, 3 goal kicks, 2 free kicks
  // and 2 offsides when measured. Fouls are rare in matches this short; the
  // Release run checks that they are called (tests/benchmark/p3Acceptance.cmake).
  CAPTURE(all.fouls, all.offsides);
  for (const char* kind : {"kickoff", "throwIn", "corner", "goalKick", "freeKick"}) {
    CAPTURE(kind);
    REQUIRE(all.setPieces[kind] >= 1);
  }
  REQUIRE(all.offsides >= 1);
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
  CHECK(replay->checkpoints.back().stateHash == 0xc43807dfc11834bdULL);
  CHECK(replay->checkpoints.back().eventHash == 0x8cf06647a228f7c3ULL);
}
