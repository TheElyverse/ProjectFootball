#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <format>
#include <nlohmann/json.hpp>
#include <string>
#include <utility>

#include "debugFrames.hpp"
#include "kickoffScenario.hpp"
#include "matchCommand.hpp"
#include "matchSetup.hpp"
#include "matchSimulation.hpp"
#include "matchStateHash.hpp"
#include "version.hpp"

using ElyverseFootball::SimCore::PlayerId;
using ElyverseFootball::SimCore::SimTick;
using ElyverseFootball::SimMatch::GiveBallCommand;
using ElyverseFootball::SimMatch::hashMatchState;
using ElyverseFootball::SimMatch::makeSevenASideKickoff;
using ElyverseFootball::SimMatch::MatchSetup;
using ElyverseFootball::SimMatch::MatchSimulation;
using ElyverseFootball::SimMatch::Pitch;
using ElyverseFootball::SimMatch::startMatch;
using ElyverseFootball::SimReplay::DebugFrameRecorder;
using ElyverseFootball::SimReplay::DebugRecording;
using ElyverseFootball::SimReplay::kDebugFramesVersion;
using ElyverseFootball::SimReplay::saveDebugFrames;
using ElyverseFootball::SimReplay::toDebugFramesJson;

namespace {

// The kickoff with the ball given to the home center midfielder, who faces his teammates ahead of
// him,, so the recording has possession events and pass decisions.
[[nodiscard]] MatchSetup setup() {
  auto state = makeSevenASideKickoff(Pitch(60.0, 40.0), {});
  REQUIRE(state.has_value());
  return {.initialState = *std::move(state),
          .config = {},
          .seed = 42,
          .commands = {{.tick = SimTick(0), .command = GiveBallCommand{.playerId = PlayerId(4)}}}};
}

[[nodiscard]] DebugRecording record(const MatchSetup& matchSetup, const std::int64_t ticks) {
  MatchSimulation simulation = startMatch(matchSetup);
  simulation.setCollectDiagnostics(true);
  DebugFrameRecorder recorder(matchSetup, "test");
  while (simulation.tick() < SimTick(ticks)) {
    REQUIRE(simulation.step().has_value());
    recorder.recordStep(simulation);
  }
  return recorder.recording();
}

}  // namespace

TEST_CASE("DebugFrameRecorder records the initial state and one frame per step", "[debugFrames]") {
  const MatchSetup matchSetup = setup();
  const DebugRecording recording = record(matchSetup, 60);

  REQUIRE(recording.frames.size() == 61);
  CHECK(recording.coreVersion == ElyverseFootball::SimCore::coreVersion());
  CHECK(recording.scenario == "test");
  CHECK(recording.seed == 42);
  CHECK(recording.frames.front().state == matchSetup.initialState);
  CHECK(recording.frames.front().events.empty());
  for (std::size_t index = 0; index < recording.frames.size(); ++index) {
    CHECK(recording.frames[index].tick == SimTick(static_cast<std::int64_t>(index)));
  }
  // The ball is given in the first step, and the midfielder decides while he
  // holds it.
  CHECK_FALSE(recording.frames[1].events.empty());
  bool anyDecision = false;
  for (const auto& frame : recording.frames) {
    anyDecision = anyDecision || !frame.decisions.empty();
  }
  CHECK(anyDecision);
}

TEST_CASE("toDebugFramesJson writes the documented format", "[debugFrames]") {
  const DebugRecording recording = record(setup(), 60);
  const auto json = nlohmann::json::parse(toDebugFramesJson(recording));

  CHECK(json.at("format") == "elyverse-debug-frames");
  CHECK(json.at("version") == kDebugFramesVersion);
  CHECK(json.at("seed") == "42");
  CHECK(json.at("ticksPerSecond") == 30);
  CHECK(json.at("pitch").at("length") == 60.0);
  CHECK(json.at("players").size() == 14);
  CHECK(json.at("players").at(0).at("side") == "home");
  REQUIRE(json.at("frames").size() == recording.frames.size());

  const auto& first = json.at("frames").at(0);
  CHECK(first.at("tick") == 0);
  CHECK(first.at("stateHash") ==
        std::format("{:016x}", hashMatchState(recording.frames.front().state)));
  CHECK(first.at("ball").at("owner").is_null());
  CHECK(first.at("players").at(0).at("position").size() == 2);

  const auto& second = json.at("frames").at(1);
  CHECK(second.at("ball").at("owner") == 4);
  CHECK(second.at("events").at(0).at("type") == "possessionChanged");
  CHECK(second.at("events").at(0).at("newOwner") == 4);
}

TEST_CASE("toDebugFramesJson rounds to three decimals and lists decision candidates",
          "[debugFrames]") {
  const auto json = nlohmann::json::parse(toDebugFramesJson(record(setup(), 60)));

  // The first decisions come before the midfielder has seen anyone, so only
  // later ones have candidates.
  bool sawCandidate = false;
  for (const auto& frame : json.at("frames")) {
    for (const auto& player : frame.at("players")) {
      for (const double coordinate : player.at("position")) {
        CHECK(std::abs((coordinate * 1000.0) - std::round(coordinate * 1000.0)) < 1e-6);
      }
    }
    for (const auto& decision : frame.at("decisions")) {
      CHECK(decision.at("player") == 4);
      for (const auto& candidate : decision.at("candidates")) {
        sawCandidate = true;
        CHECK(candidate.contains("utility"));
        CHECK(candidate.contains("rejection"));
      }
    }
  }
  CHECK(sawCandidate);
}

TEST_CASE("saveDebugFrames reports a path it cannot write", "[debugFrames]") {
  const DebugRecording recording = record(setup(), 1);
  const auto missing = std::filesystem::temp_directory_path() / "no-such-directory" / "f.json";
  const auto saved = saveDebugFrames(recording, missing);
  REQUIRE_FALSE(saved.has_value());
  CHECK(saved.error().find("cannot open") != std::string::npos);
}
