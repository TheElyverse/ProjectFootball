// Milestone M0 acceptance: fourteen players on scripted runs with repeated
// target changes and a rolling ball, simulated for minutes. Checks that the
// match stays valid, that it reproduces exactly -- run again, played back
// from a replay file, and advanced in different batch sizes -- and pins its
// state hash so an unintended behavior change cannot slip through.

#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "matchSetup.hpp"
#include "matchSimulation.hpp"
#include "matchState.hpp"
#include "matchStateHash.hpp"
#include "replay.hpp"
#include "replayJson.hpp"
#include "scenarios.hpp"

using ElyverseFootball::SimCore::SimTick;
using ElyverseFootball::SimMatch::findNonFiniteValues;
using ElyverseFootball::SimMatch::findScenario;
using ElyverseFootball::SimMatch::hashMatchState;
using ElyverseFootball::SimMatch::MatchSetup;
using ElyverseFootball::SimMatch::MatchSimulation;
using ElyverseFootball::SimMatch::PlayerMatchState;
using ElyverseFootball::SimMatch::startMatch;
using ElyverseFootball::SimReplay::kDefaultCheckpointIntervalTicks;
using ElyverseFootball::SimReplay::parseReplayJson;
using ElyverseFootball::SimReplay::playReplay;
using ElyverseFootball::SimReplay::recordMatch;
using ElyverseFootball::SimReplay::Replay;
using ElyverseFootball::SimReplay::toReplayJson;

namespace {

constexpr std::uint64_t kSeed = 42;
// Three minutes at 30 Hz: every scripted waypoint and long after.
constexpr SimTick::ValueType kTicks = 5400;

[[nodiscard]] MatchSetup m0Setup() {
  const auto* scenario = findScenario("m0-acceptance");
  REQUIRE(scenario != nullptr);
  auto setup = scenario->make(kSeed);
  REQUIRE(setup.has_value());
  return *std::move(setup);
}

[[nodiscard]] Replay record() {
  auto replay = recordMatch(m0Setup(), SimTick(kTicks), kDefaultCheckpointIntervalTicks, "");
  REQUIRE(replay.has_value());
  return *std::move(replay);
}

// Advances the simulation to kTicks in batches of the given size, continuing
// each batch from a copy of the previous one -- the snapshot a save/restore
// or a lookahead buffer would take -- and records the state hash at every
// checkpoint tick.
[[nodiscard]] std::vector<std::uint64_t> checkpointHashesInBatches(
    const SimTick::ValueType batchSize) {
  MatchSimulation simulation = startMatch(m0Setup());
  std::vector<std::uint64_t> hashes{hashMatchState(simulation.state())};
  while (simulation.tick().value() < kTicks) {
    MatchSimulation snapshot = simulation;
    for (SimTick::ValueType step = 0; step < batchSize && snapshot.tick().value() < kTicks;
         ++step) {
      REQUIRE(snapshot.step().has_value());
      if (snapshot.tick().value() % kDefaultCheckpointIntervalTicks == 0) {
        hashes.push_back(hashMatchState(snapshot.state()));
      }
    }
    simulation = std::move(snapshot);
  }
  return hashes;
}

}  // namespace

TEST_CASE("M0: the scenario moves every player and the ball", "[acceptance][m0]") {
  const MatchSetup setup = m0Setup();
  REQUIRE(setup.initialState.players().size() == 14);
  REQUIRE(setup.initialState.ball().velocity.lengthSquared() > 0.0);

  MatchSimulation simulation = startMatch(setup);
  std::vector<double> distance(setup.initialState.players().size(), 0.0);
  std::size_t redirectedWhileRunning = 0;
  for (SimTick::ValueType tick = 0; tick < kTicks; ++tick) {
    const std::vector<PlayerMatchState> before(simulation.state().players().begin(),
                                               simulation.state().players().end());
    REQUIRE(simulation.step().has_value());
    for (std::size_t index = 0; const PlayerMatchState& player : simulation.state().players()) {
      const PlayerMatchState& previous = before.at(index);
      distance.at(index) += std::sqrt((player.position - previous.position).lengthSquared());
      if (player.target != previous.target && previous.velocity.lengthSquared() > 1.0) {
        ++redirectedWhileRunning;
      }
      ++index;
    }
  }

  for (std::size_t index = 0; index < distance.size(); ++index) {
    CAPTURE(index, distance.at(index));
    REQUIRE(distance.at(index) > 50.0);
  }
  // Targets change mid-stride, not only after arrival.
  REQUIRE(redirectedWhileRunning >= 14);
  REQUIRE_FALSE(simulation.state().ball().position == setup.initialState.ball().position);
}

TEST_CASE("M0: an extended run never produces an invalid state", "[acceptance][m0]") {
  MatchSimulation simulation = startMatch(m0Setup());
  const auto& pitch = simulation.state().pitch();

  for (SimTick::ValueType tick = 0; tick < 3 * kTicks; ++tick) {
    REQUIRE(simulation.step().has_value());
    const auto& state = simulation.state();
    // The loop already refuses non-finite values; check them here as well so
    // this test states the acceptance criterion on its own.
    REQUIRE(findNonFiniteValues(state).empty());
    REQUIRE(pitch.contains(state.ball().position));
    for (const PlayerMatchState& player : state.players()) {
      REQUIRE(player.velocity.lengthSquared() <=
              player.attributes.maxSpeed * player.attributes.maxSpeed * (1.0 + 1e-12));
      REQUIRE(pitch.contains(player.target.value_or(player.position)));
    }
  }
  REQUIRE_FALSE(simulation.hasFailed());
}

TEST_CASE("M0: repeated runs produce identical checkpoint hashes", "[acceptance][m0]") {
  const Replay first = record();
  const Replay second = record();

  REQUIRE(first.checkpoints.size() == 181);
  REQUIRE(first.checkpoints == second.checkpoints);
}

TEST_CASE("M0: replay playback reproduces every checkpoint", "[acceptance][m0]") {
  const Replay replay = record();

  const auto parsed = parseReplayJson(toReplayJson(replay));
  REQUIRE(parsed.has_value());
  const auto playback = playReplay(*parsed);

  REQUIRE(playback.has_value());
  REQUIRE(playback->checkpointsVerified == replay.checkpoints.size());
  REQUIRE(playback->finalStateHash == replay.checkpoints.back().stateHash);
}

TEST_CASE("M0: batch sizes do not change the result", "[acceptance][m0]") {
  const std::vector<std::uint64_t> reference = checkpointHashesInBatches(kTicks);
  std::vector<std::uint64_t> recorded;
  for (const auto& checkpoint : record().checkpoints) {
    recorded.push_back(checkpoint.stateHash);
  }
  REQUIRE(reference == recorded);

  for (const SimTick::ValueType batchSize : {1, 7, 30, 451}) {
    CAPTURE(batchSize);
    REQUIRE(checkpointHashesInBatches(batchSize) == reference);
  }
}

TEST_CASE("M0: the final state hash is pinned", "[acceptance][m0]") {
  // Identical on every platform and compiler CI builds with. It changes only
  // with the simulation's behavior, the scenario or the state layout -- each
  // of which must bump the core version and update this value deliberately.
  REQUIRE(record().checkpoints.back().stateHash == 0x7b1d5a7abf85b953ULL);
}
