// Eleven-a-side (docs/scenarios.md#the-tactic-match): the reference tactic for
// eleven against itself on the standard pitch, from the eleven-a-side kickoff
// fixture. Two minutes stay valid and moving, replay with identical tick and
// event hashes through the file format, and end on pinned hashes.

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <utility>

#include "matchSetup.hpp"
#include "matchSimulation.hpp"
#include "matchState.hpp"
#include "pitch.hpp"
#include "replay.hpp"
#include "replayJson.hpp"
#include "scenarios.hpp"
#include "simTime.hpp"

using ElyverseFootball::SimCore::SimTick;
using ElyverseFootball::SimMatch::findNonFiniteValues;
using ElyverseFootball::SimMatch::findScenario;
using ElyverseFootball::SimMatch::kStandardLengthMeters;
using ElyverseFootball::SimMatch::kStandardWidthMeters;
using ElyverseFootball::SimMatch::MatchSetup;
using ElyverseFootball::SimMatch::MatchSimulation;
using ElyverseFootball::SimMatch::startMatch;
using ElyverseFootball::SimReplay::kDefaultCheckpointIntervalTicks;
using ElyverseFootball::SimReplay::parseReplayJson;
using ElyverseFootball::SimReplay::playReplay;
using ElyverseFootball::SimReplay::recordMatch;
using ElyverseFootball::SimReplay::Replay;
using ElyverseFootball::SimReplay::toReplayJson;

namespace {

constexpr std::uint64_t kSeed = 11;
constexpr SimTick::ValueType kTicks = 3600;

[[nodiscard]] MatchSetup elevenASide(const std::uint64_t seed = kSeed) {
  const auto* definition = findScenario("tactic-match-eleven");
  REQUIRE(definition != nullptr);
  auto setup = definition->make(seed);
  REQUIRE(setup.has_value());
  return *std::move(setup);
}

[[nodiscard]] Replay recorded() {
  auto replay = recordMatch(elevenASide(), SimTick(kTicks), kDefaultCheckpointIntervalTicks, "");
  REQUIRE(replay.has_value());
  return *std::move(replay);
}

}  // namespace

TEST_CASE("Eleven-a-side: two full sides play on the standard pitch", "[acceptance][eleven]") {
  const MatchSetup setup = elevenASide();
  REQUIRE(setup.initialState.playersPerSide() == 11);
  REQUIRE(setup.initialState.players().size() == 22);
  REQUIRE(setup.initialState.pitch().lengthMeters() == kStandardLengthMeters);
  REQUIRE(setup.initialState.pitch().widthMeters() == kStandardWidthMeters);

  MatchSimulation simulation = startMatch(setup);
  for (SimTick::ValueType tick = 0; tick < kTicks; ++tick) {
    REQUIRE(simulation.step().has_value());
    REQUIRE(findNonFiniteValues(simulation.state()).empty());
  }
  // Everyone has moved from where he lined up.
  for (std::size_t index = 0; index < simulation.state().players().size(); ++index) {
    CAPTURE(index);
    REQUIRE_FALSE(simulation.state().players()[index].position ==
                  setup.initialState.players()[index].position);
  }
}

TEST_CASE("Eleven-a-side: a match replays with identical tick and event hashes",
          "[acceptance][eleven]") {
  const Replay first = recorded();
  REQUIRE(first.checkpoints == recorded().checkpoints);
  const auto parsed = parseReplayJson(toReplayJson(first));
  REQUIRE(parsed.has_value());
  REQUIRE(*parsed == first);
  const auto playback = playReplay(*parsed);
  REQUIRE(playback.has_value());
  REQUIRE(playback->checkpointsVerified == first.checkpoints.size());
  REQUIRE(playback->finalStateHash == first.checkpoints.back().stateHash);
  REQUIRE(playback->finalEventHash == first.checkpoints.back().eventHash);
}

TEST_CASE("Eleven-a-side: the final hashes are pinned", "[acceptance][eleven]") {
  // Identical on every platform and compiler CI builds with; they change only
  // with the simulation's behaviour, the scenario or the state layout.
  const Replay replay = recorded();
  CHECK(replay.checkpoints.back().stateHash == 0xd0c98f4594d80043ULL);
  CHECK(replay.checkpoints.back().eventHash == 0x091262d7a5f895e0ULL);
}
