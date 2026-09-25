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
#include "referenceTactic.hpp"
#include "scenarios.hpp"
#include "tactic.hpp"
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
  // Events carry the tick of the step that recorded them, one before the
  // frame's.
  CHECK(second.at("events").at(0).at("tick") == 0);
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
      CHECK(decision.at("tick") == frame.at("tick").get<int>() - 1);
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

// /dev/full accepts the open and fails every write: Linux only.
#if defined(__linux__)
TEST_CASE("saveDebugFrames reports a write that fails on close", "[debugFrames]") {
  // An empty recording is a few hundred bytes, so it stays in the stream's
  // buffer and only fails when close() flushes it.
  const auto saved = saveDebugFrames(DebugRecording{}, "/dev/full");
  REQUIRE_FALSE(saved.has_value());
  CHECK(saved.error().find("write failed") != std::string::npos);
}
#endif

TEST_CASE("Frames show team shapes, regions, assignments and action decisions", "[debugFrames]") {
  using ElyverseFootball::SimTactics::referenceTacticSpec;
  using ElyverseFootball::SimTactics::Tactic;
  auto tactic = Tactic::create(referenceTacticSpec());
  REQUIRE(tactic.has_value());
  auto state = makeSevenASideKickoff(Pitch(60.0, 40.0), {}, {.home = *tactic, .away = *tactic});
  REQUIRE(state.has_value());
  const MatchSetup tactical{
      .initialState = *std::move(state),
      .config = {},
      .seed = 42,
      .commands = {{.tick = SimTick(0), .command = GiveBallCommand{.playerId = PlayerId(4)}}}};
  const DebugRecording recording = record(tactical, 30);
  const auto json = nlohmann::json::parse(toDebugFramesJson(recording));

  const auto& last = json.at("frames").back();
  const auto& home = last.at("teams").at(0);
  CHECK(home.at("side") == "home");
  CHECK(home.at("phase") == "progression");
  CHECK(home.at("shape").at("defensiveLine").is_number());
  CHECK(home.at("shape").at("width").is_number());
  CHECK(last.at("teams").at(1).at("phase") == "defensiveBlock");
  CHECK(home.at("tactic") == "reference");
  CHECK(home.at("instruction").at("lineHeight") == 0.35);
  CHECK(home.at("press").is_null());

  // The zones, once in the header.
  CHECK(json.at("zones").at("laneBoundaries") == nlohmann::json({8.0, 16.0, 24.0, 32.0}));
  CHECK(json.at("zones").at("thirdBoundaries") == nlohmann::json({20.0, 40.0}));

  // Pitch control only in the frames whose step refreshed it: every tenth.
  int grids = 0;
  for (const auto& frame : json.at("frames")) {
    if (!frame.at("pitchControl").is_null()) {
      ++grids;
      CHECK(frame.at("pitchControl").at("home").size() ==
            frame.at("pitchControl").at("columns").get<std::size_t>() *
                frame.at("pitchControl").at("rows").get<std::size_t>());
    }
  }
  CHECK(grids == 3);

  const auto& centreBack = last.at("players").at(1);
  CHECK(centreBack.at("region").at("center").size() == 2);
  CHECK(centreBack.at("action").at("type").is_string());
  // The goalkeeper holds his region and decides nothing.
  CHECK(last.at("players").at(0).at("action").is_null());

  bool decided = false;
  for (const auto& frame : json.at("frames")) {
    for (const auto& action : frame.at("actions")) {
      CHECK(action.at("candidates").at(0).at("type") == "holdPosition");
      CHECK(action.at("candidates").at(0).at("dominant").is_string());
      decided = true;
    }
  }
  CHECK(decided);
}

TEST_CASE("Frames show a team's press with its roles", "[debugFrames]") {
  using ElyverseFootball::SimTactics::PressingTrigger;
  using ElyverseFootball::SimTactics::referenceTacticSpec;
  using ElyverseFootball::SimTactics::Tactic;
  auto spec = referenceTacticSpec();
  spec.principles.pressingLine = 0.3;
  spec.principles.pressingTriggers = {PressingTrigger::kReceiverFacingOwnGoal,
                                      PressingTrigger::kBackPass, PressingTrigger::kPoorFirstTouch,
                                      PressingTrigger::kIsolatedReceiver};
  for (auto& phase : spec.phases) {
    phase.pressingIntensity = 1.0;
  }
  const auto presser = Tactic::create(spec);
  const auto reference = Tactic::create(referenceTacticSpec());
  REQUIRE(presser.has_value());
  REQUIRE(reference.has_value());
  const auto setup =
      ElyverseFootball::SimMatch::makeTacticMatch({.home = *reference, .away = *presser}, 3);
  REQUIRE(setup.has_value());
  const auto json = nlohmann::json::parse(toDebugFramesJson(record(*setup, 900)));

  bool pressed = false;
  for (const auto& frame : json.at("frames")) {
    const auto& press = frame.at("teams").at(1).at("press");
    if (!press.is_null()) {
      CHECK(press.at("carrier") <= 7);
      CHECK_FALSE(press.at("assignments").empty());
      CHECK(press.at("assignments").at(0).at("role").is_string());
      pressed = true;
    }
  }
  CHECK(pressed);
}
