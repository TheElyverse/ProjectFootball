#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstddef>
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

TEST_CASE("The header lists a substitute who comes on during the recording", "[debugFrames]") {
  DebugRecording recording = record(setup(), 2);
  // The last frame as if away's player 9 had been substituted by player 30.
  const auto& last = recording.frames.back().state;
  std::vector<ElyverseFootball::SimMatch::PlayerMatchState> players(last.players().begin(),
                                                                    last.players().end());
  players.at(8).playerId = PlayerId(30);
  auto substituted =
      ElyverseFootball::SimMatch::MatchState::create({.pitch = last.pitch(),
                                                      .players = std::move(players),
                                                      .ball = last.ball(),
                                                      .playersPerSide = last.playersPerSide()},
                                                     last.tactics());
  REQUIRE(substituted.has_value());
  recording.frames.push_back({.state = *std::move(substituted),
                              .tick = SimTick(3),
                              .events = {},
                              .decisions = {},
                              .actions = {}});
  const auto json = nlohmann::json::parse(toDebugFramesJson(recording));
  REQUIRE(json.at("players").size() == 15);
  CHECK(json.at("players").at(14).at("id") == 30);
  CHECK(json.at("players").at(14).at("side") == "away");
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
        CHECK(candidate.at("judgedInterceptionRisk") == candidate.at("interceptionRisk"));
      }
    }
  }
  CHECK(sawCandidate);
}

TEST_CASE("Frames show what became of a shot and the score", "[debugFrames]") {
  const auto* scenario = ElyverseFootball::SimMatch::findScenario("clear-chance");
  REQUIRE(scenario != nullptr);
  bool hitWoodwork = false;
  bool scored = false;
  for (std::uint64_t seed = 1; seed <= 20; ++seed) {
    const auto matchSetup = scenario->make(seed);
    REQUIRE(matchSetup.has_value());
    const auto json = nlohmann::json::parse(toDebugFramesJson(record(*matchSetup, 45)));
    CHECK(json.at("frames").at(0).at("score") == nlohmann::json{{"home", 0}, {"away", 0}});
    for (const auto& frame : json.at("frames")) {
      for (const auto& event : frame.at("events")) {
        const std::string type = event.at("type");
        if (type == "shotAttempted") {
          CHECK(event.at("struckAt").size() == 2);
          CHECK(event.at("struckHeight").is_number());
        } else if (type == "shotHitWoodwork") {
          hitWoodwork = true;
          const std::string part = event.at("part");
          CHECK((part == "postAtMinY" || part == "postAtMaxY" || part == "crossbar"));
          CHECK(event.at("shotTick").is_number_integer());
          CHECK(event.at("position").size() == 2);
        } else if (type == "shotResolved") {
          CHECK(event.at("shooter") == 1);
          CHECK(event.at("outcome").is_string());
        } else if (type == "goalScored") {
          scored = true;
          CHECK(event.at("side") == "home");
          CHECK(event.at("scorer") == 1);
          CHECK(event.at("assist").is_null());
          CHECK(event.at("ownGoal") == false);
          CHECK(event.at("score") == nlohmann::json{{"home", 1}, {"away", 0}});
          // The frame the goal led to stands at the new score.
          CHECK(frame.at("score") == nlohmann::json{{"home", 1}, {"away", 0}});
        }
      }
    }
  }
  CHECK(hitWoodwork);
  CHECK(scored);
}

TEST_CASE("saveDebugFrames reports a path it cannot write", "[debugFrames]") {
  const DebugRecording recording = record(setup(), 1);
  const auto missing = std::filesystem::temp_directory_path() / "no-such-directory" / "f.json";
  const auto saved = saveDebugFrames(recording, missing);
  REQUIRE_FALSE(saved.has_value());
  CHECK(saved.error().contains("cannot open"));
}

// /dev/full accepts the open and fails every write: Linux only.
#ifdef __linux__
TEST_CASE("saveDebugFrames reports a write that fails on close", "[debugFrames]") {
  // An empty recording is a few hundred bytes, so it stays in the stream's
  // buffer and only fails when close() flushes it.
  const auto saved = saveDebugFrames(DebugRecording{}, "/dev/full");
  REQUIRE_FALSE(saved.has_value());
  CHECK(saved.error().contains("write failed"));
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
  // With his side on the ball the goalkeeper only holds his region or
  // supports the carrier; without it he decides nothing.
  const std::string keeperAction = last.at("players").at(0).at("action").at("type");
  CHECK((keeperAction == "holdPosition" || keeperAction == "supportCarrier"));
  CHECK(last.at("players").at(7).at("action").is_null());

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

namespace {

// The frames of a registered scenario's first ticks, as JSON.
[[nodiscard]] nlohmann::json scenarioFrames(const std::string& scenario, const std::int64_t ticks) {
  const auto* definition = ElyverseFootball::SimMatch::findScenario(scenario);
  REQUIRE(definition != nullptr);
  const auto made = definition->make(3);
  REQUIRE(made.has_value());
  return nlohmann::json::parse(toDebugFramesJson(record(*made, ticks)));
}

}  // namespace

TEST_CASE("The header holds the goals and areas to scale", "[debugFrames]") {
  using ElyverseFootball::SimMatch::GoalEnd;
  const auto json = nlohmann::json::parse(toDebugFramesJson(record(setup(), 1)));
  const auto& geometry = json.at("geometry");
  REQUIRE(geometry.at("ends").size() == 2);
  // The sandbox's markings, scaled to its 60 x 40 m pitch.
  const Pitch pitch(60.0, 40.0);
  const auto& far = geometry.at("ends").at(1);
  CHECK(far.at("goalLineX") == 60.0);
  CHECK(far.at("crossbar") == pitch.goal(GoalEnd::kMaxX).heightMeters);
  // Positions to the millimetre, as every position in the frames.
  const auto near = [](const nlohmann::json& value, const double expected) {
    return std::abs(value.get<double>() - expected) <= 0.0005;
  };
  CHECK(near(far.at("posts").at(1).at(1), pitch.goal(GoalEnd::kMaxX).postAtMaxY().y));
  CHECK(near(far.at("penaltyArea").at("min").at(0), pitch.penaltyArea(GoalEnd::kMaxX).min.x));
  CHECK(near(far.at("penaltySpot").at(0), pitch.penaltySpot(GoalEnd::kMaxX).x));
  CHECK(geometry.at("centerCircleRadius") == pitch.markings().centerCircleRadiusMeters);
}

TEST_CASE("Frames show a set piece's places and the keepers' bisectors", "[debugFrames]") {
  const auto json = scenarioFrames("corner", 30);
  const auto& frame = json.at("frames").at(5);
  const auto& setPiece = frame.at("setPiece");
  REQUIRE(setPiece.is_object());
  CHECK(setPiece.at("kind") == "corner");
  CHECK(setPiece.at("side") == "home");
  CHECK(setPiece.at("places").size() >= 5);
  // Both sides play a tactic: both keepers stand on their bisector.
  REQUIRE(frame.at("keepers").size() == 2);
  CHECK(frame.at("keepers").at(0).contains("onLine"));
  CHECK(json.at("frames").at(0).at("setPiece").is_null());
}

TEST_CASE("A decision lists the shots its player weighed", "[debugFrames]") {
  const auto json = scenarioFrames("clear-chance", 120);
  bool shot = false;
  for (const auto& frame : json.at("frames")) {
    for (const auto& decision : frame.at("decisions")) {
      if (decision.at("outcome") == "shot") {
        REQUIRE(decision.at("chosenShot").is_number());
        REQUIRE(decision.at("shots").size() > decision.at("chosenShot").get<std::size_t>());
        CHECK(decision.at("shots").at(0).contains("goalChance"));
        shot = true;
      }
    }
  }
  REQUIRE(shot);
}
