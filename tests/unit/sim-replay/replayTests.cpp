#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

#include "kickoffScenario.hpp"
#include "matchCommand.hpp"
#include "matchEvents.hpp"
#include "matchSetup.hpp"
#include "matchSimulation.hpp"
#include "matchStateHash.hpp"
#include "referenceTactic.hpp"
#include "replay.hpp"
#include "replayJson.hpp"
#include "scenarios.hpp"
#include "stableHash.hpp"
#include "tactic.hpp"
#include "version.hpp"

using ElyverseFootball::SimCore::coreVersion;
using ElyverseFootball::SimCore::PlayerId;
using ElyverseFootball::SimCore::SimTick;
using ElyverseFootball::SimCore::StableHasher;
using ElyverseFootball::SimMatch::addEvent;
using ElyverseFootball::SimMatch::GiveBallCommand;
using ElyverseFootball::SimMatch::hashMatchState;
using ElyverseFootball::SimMatch::makeSevenASideKickoff;
using ElyverseFootball::SimMatch::MatchSetup;
using ElyverseFootball::SimMatch::MatchSimulation;
using ElyverseFootball::SimMatch::MovePlayerCommand;
using ElyverseFootball::SimMatch::Pitch;
using ElyverseFootball::SimMatch::PossessionChanged;
using ElyverseFootball::SimMatch::ScheduledCommand;
using ElyverseFootball::SimMatch::startMatch;
using ElyverseFootball::SimReplay::kDefaultCheckpointIntervalTicks;
using ElyverseFootball::SimReplay::loadReplay;
using ElyverseFootball::SimReplay::parseReplayJson;
using ElyverseFootball::SimReplay::playReplay;
using ElyverseFootball::SimReplay::recordMatch;
using ElyverseFootball::SimReplay::Replay;
using ElyverseFootball::SimReplay::ReplayCheckpoint;
using ElyverseFootball::SimReplay::ReplayDivergence;
using ElyverseFootball::SimReplay::ReplayErrorCode;
using ElyverseFootball::SimReplay::ReplayRecorder;
using ElyverseFootball::SimReplay::saveReplay;
using ElyverseFootball::SimReplay::toReplayJson;

namespace {

[[nodiscard]] ScheduledCommand move(const std::int64_t tick, const std::uint32_t playerId,
                                    const double targetX, const double targetY) {
  return {.tick = SimTick(tick),
          .command = MovePlayerCommand{.playerId = PlayerId(playerId),
                                       .target = {.x = targetX, .y = targetY}}};
}

[[nodiscard]] MatchSetup setup(const std::uint64_t seed = 42) {
  auto state = makeSevenASideKickoff(Pitch(60.0, 40.0), {.x = 6.5, .y = -1.25});
  REQUIRE(state.has_value());
  return {.initialState = *std::move(state),
          .config = {},
          .seed = seed,
          .commands = {move(0, 7, 40.0, 10.0), move(0, 11, 25.0, 30.0), move(45, 7, 10.0, 35.0),
                       move(45, 2, 30.0, 20.0), move(90, 11, 55.0, 2.0)}};
}

[[nodiscard]] Replay recorded(const MatchSetup& matchSetup = setup(),
                              const std::int64_t ticks = 300,
                              const int checkpointIntervalTicks = kDefaultCheckpointIntervalTicks) {
  auto replay =
      recordMatch(matchSetup, SimTick(ticks), checkpointIntervalTicks, "2026-09-24T10:00:00Z");
  REQUIRE(replay.has_value());
  return *std::move(replay);
}

[[nodiscard]] bool mentions(const std::string& message, const std::string& fragment) {
  return message.find(fragment) != std::string::npos;
}

}  // namespace

TEST_CASE("A recorded replay holds the contract and checkpoints", "[replay]") {
  const MatchSetup matchSetup = setup();
  const Replay replay = recorded(matchSetup, 100);

  REQUIRE(replay.coreVersion == coreVersion());
  REQUIRE(replay.setup == matchSetup);
  REQUIRE(replay.finalTick == SimTick(100));
  REQUIRE(replay.checkpoints.size() == 5);  // ticks 0, 30, 60, 90 and the final 100
  REQUIRE(replay.checkpoints.front() ==
          ReplayCheckpoint{.tick = SimTick(0),
                           .stateHash = hashMatchState(matchSetup.initialState),
                           .eventHash = StableHasher().value()});
  REQUIRE(replay.checkpoints.back().tick == SimTick(100));
}

TEST_CASE("A state that already remembers something cannot be recorded", "[replay]") {
  // Perception memories are not part of the replay format, so a state
  // copied from a running match could not be played back.
  MatchSimulation simulation = startMatch(setup());
  for (int step = 0; step < 3; ++step) {
    REQUIRE(simulation.step().has_value());
  }
  MatchSetup copied = setup();
  copied.initialState = simulation.state();
  copied.commands.clear();

  REQUIRE_THROWS_AS(ReplayRecorder(copied), std::invalid_argument);
  REQUIRE_NOTHROW(ReplayRecorder(setup()));
}

TEST_CASE("Playing a replay back reproduces every checkpoint", "[replay]") {
  const Replay replay = recorded();

  const auto playback = playReplay(replay);

  REQUIRE(playback.has_value());
  REQUIRE(playback->finalTick == SimTick(300));
  REQUIRE(playback->elapsedSeconds == 10.0);
  REQUIRE(playback->checkpointsVerified == replay.checkpoints.size());
  REQUIRE(playback->finalStateHash == replay.checkpoints.back().stateHash);
  REQUIRE(playback->finalEventHash == replay.checkpoints.back().eventHash);
}

TEST_CASE("Checkpoints hash the events published so far", "[replay]") {
  MatchSetup matchSetup = setup();
  matchSetup.commands.push_back(
      {.tick = SimTick(10), .command = GiveBallCommand{.playerId = PlayerId(4)}});
  const Replay replay = recorded(matchSetup, 60);

  // No event before the ball is given at tick 10; its possession change is
  // published by the step that reaches tick 11.
  REQUIRE(replay.checkpoints.at(0).eventHash == StableHasher().value());
  StableHasher expected;
  addEvent(expected,
           PossessionChanged{
               .tick = SimTick(10), .previousOwner = std::nullopt, .newOwner = PlayerId(4)});
  MatchSimulation simulation = startMatch(matchSetup);
  StableHasher events;
  while (simulation.tick() < SimTick(30)) {
    REQUIRE(simulation.step().has_value());
    for (const auto& event : simulation.events()) {
      addEvent(events, event);
    }
  }
  REQUIRE(replay.checkpoints.at(1).tick == SimTick(30));
  REQUIRE(replay.checkpoints.at(1).eventHash == events.value());
  REQUIRE(replay.checkpoints.at(1).eventHash != StableHasher().value());
}

TEST_CASE("Commands scheduled during a run are recorded", "[replay]") {
  MatchSetup matchSetup = setup();
  matchSetup.commands.clear();
  MatchSimulation simulation = startMatch(matchSetup);
  ReplayRecorder recorder(matchSetup, 10);
  for (int tick = 0; tick < 120; ++tick) {
    if (tick == 40) {
      REQUIRE(simulation.schedule(move(40, 3, 50.0, 5.0)).has_value());
      REQUIRE(simulation.schedule(move(80, 3, 5.0, 5.0)).has_value());
      REQUIRE(simulation.schedule(move(500, 3, 1.0, 1.0)).has_value());
    }
    REQUIRE(simulation.step().has_value());
    recorder.recordStep(simulation);
  }

  const Replay replay = recorder.finish(simulation, "");

  // The command for tick 500 was never reached, so it is not part of the match.
  REQUIRE(replay.setup.commands == std::vector{move(40, 3, 50.0, 5.0), move(80, 3, 5.0, 5.0)});
  const auto playback = playReplay(replay);
  REQUIRE(playback.has_value());
  REQUIRE(playback->finalStateHash == hashMatchState(simulation.state()));
}

TEST_CASE("A replay survives the JSON round trip unchanged", "[replay]") {
  MatchSetup matchSetup = setup(std::numeric_limits<std::uint64_t>::max());
  // Values without a short decimal form must come back bit for bit.
  matchSetup.config.ball.rollingDeceleration = 1.0 / 3.0;
  matchSetup.commands.push_back(move(120, 5, 0.1, 1e-300));
  matchSetup.commands.push_back(
      {.tick = SimTick(60),
       .command = ElyverseFootball::SimMatch::GiveBallCommand{.playerId = PlayerId(9)}});
  matchSetup.commands.push_back(
      {.tick = SimTick(90),
       .command = ElyverseFootball::SimMatch::PassCommand{.playerId = PlayerId(9),
                                                          .target = {.x = 20.0, .y = 5.0},
                                                          .speed = 11.0,
                                                          .receiver = PlayerId(10)}});
  const Replay replay = recorded(matchSetup, 150);

  const auto parsed = parseReplayJson(toReplayJson(replay));

  REQUIRE(parsed.has_value());
  REQUIRE(*parsed == replay);
  REQUIRE(playReplay(*parsed).has_value());
}

TEST_CASE("A match with a tactic change replays identically", "[replay]") {
  using ElyverseFootball::SimMatch::ChangeTacticCommand;
  using ElyverseFootball::SimMatch::TeamSide;
  using ElyverseFootball::SimTactics::referenceTacticSpec;
  using ElyverseFootball::SimTactics::Tactic;
  const auto reference = Tactic::create(referenceTacticSpec());
  auto boldSpec = referenceTacticSpec();
  boldSpec.name = "bold";
  for (auto& phase : boldSpec.phases) {
    phase.lineHeight = 0.45;
    phase.pressingIntensity = 1.0;
    phase.passingRisk = 0.9;
  }
  const auto bold = Tactic::create(boldSpec);
  REQUIRE(reference.has_value());
  REQUIRE(bold.has_value());
  const auto unchanged =
      ElyverseFootball::SimMatch::makeTacticMatch({.home = *reference, .away = *reference}, 11);
  REQUIRE(unchanged.has_value());
  MatchSetup changed = *unchanged;
  changed.commands.push_back(
      {.tick = SimTick(90),
       .command = ChangeTacticCommand{.side = TeamSide::kAway, .tactic = *bold}});

  const Replay replay = recorded(changed, 300);
  const auto parsed = parseReplayJson(toReplayJson(replay));
  REQUIRE(parsed.has_value());
  REQUIRE(*parsed == replay);
  const auto playback = playReplay(*parsed);
  REQUIRE(playback.has_value());
  REQUIRE(playback->finalStateHash == replay.checkpoints.back().stateHash);
  // The change takes effect: the match without it ends elsewhere.
  REQUIRE(recorded(*unchanged, 300).checkpoints.back().stateHash !=
          replay.checkpoints.back().stateHash);
}

TEST_CASE("The seed is written as a decimal string", "[replay]") {
  const std::string json =
      toReplayJson(recorded(setup(std::numeric_limits<std::uint64_t>::max()), 1));

  REQUIRE(mentions(json, R"("seed": "18446744073709551615")"));
}

TEST_CASE("Commands of the same tick carry their execution order", "[replay]") {
  const std::string json = toReplayJson(recorded(setup(), 100));

  REQUIRE(mentions(json, R"("tick": 45,
      "order": 1,)"));
}

TEST_CASE("A saved replay loads and plays back identically", "[replay]") {
  const Replay replay = recorded();
  const auto path = std::filesystem::temp_directory_path() / "sim-replay-test-replay.json";

  REQUIRE(saveReplay(replay, path).has_value());
  const auto loaded = loadReplay(path);
  std::filesystem::remove(path);

  REQUIRE(loaded.has_value());
  REQUIRE(*loaded == replay);
  const auto original = playReplay(replay);
  const auto reloaded = playReplay(*loaded);
  REQUIRE(original.has_value());
  REQUIRE(reloaded.has_value());
  REQUIRE(reloaded->finalStateHash == original->finalStateHash);
}

TEST_CASE("Playback detects a replay that does not reproduce", "[replay]") {
  SECTION("a changed checkpoint") {
    Replay replay = recorded();
    replay.checkpoints.at(4).stateHash ^= 1U;
    const auto playback = playReplay(replay);
    REQUIRE_FALSE(playback.has_value());
    REQUIRE(playback.error().code == ReplayErrorCode::kCheckpointMismatch);
    CAPTURE(playback.error().message);
    REQUIRE(mentions(playback.error().message,
                     "diverged after tick 90, at or before tick 120: state hash at tick 120"));
    REQUIRE(playback.error().divergence == ReplayDivergence{.lastMatching = SimTick(90),
                                                            .firstDiverging = SimTick(120),
                                                            .stateDiffers = true,
                                                            .eventsDiffer = false});
  }
  SECTION("a changed event hash") {
    Replay replay = recorded();
    replay.checkpoints.at(4).eventHash ^= 1U;
    const auto playback = playReplay(replay);
    REQUIRE_FALSE(playback.has_value());
    REQUIRE(playback.error().code == ReplayErrorCode::kCheckpointMismatch);
    CAPTURE(playback.error().message);
    REQUIRE(mentions(playback.error().message, ": event hash at tick 120"));
    REQUIRE(playback.error().divergence == ReplayDivergence{.lastMatching = SimTick(90),
                                                            .firstDiverging = SimTick(120),
                                                            .stateDiffers = false,
                                                            .eventsDiffer = true});
  }
  SECTION("a changed command") {
    Replay replay = recorded();
    replay.setup.commands.at(2) = move(45, 7, 10.0, 34.0);
    const auto playback = playReplay(replay);
    REQUIRE_FALSE(playback.has_value());
    REQUIRE(playback.error().code == ReplayErrorCode::kCheckpointMismatch);
    REQUIRE(mentions(playback.error().message, "state hash at tick 60"));
  }
  SECTION("a changed command, found to the tick with a checkpoint per tick") {
    const Replay original = recorded(setup(), 150, 1);
    REQUIRE(original.checkpointIntervalTicks == 1);
    REQUIRE(original.checkpoints.size() == 151);
    Replay replay = original;
    replay.setup.commands.at(2) = move(45, 7, 10.0, 34.0);
    const auto playback = playReplay(replay);
    REQUIRE_FALSE(playback.has_value());
    // The command applies in the step from tick 45, whose state is tick 46's.
    REQUIRE(playback.error().divergence == ReplayDivergence{.lastMatching = SimTick(45),
                                                            .firstDiverging = SimTick(46),
                                                            .stateDiffers = true,
                                                            .eventsDiffer = false});
    REQUIRE(mentions(playback.error().message, "diverged after tick 45, at or before tick 46"));
  }
}

TEST_CASE("Playback rejects an unusable replay", "[replay]") {
  SECTION("another core version") {
    Replay replay = recorded(setup(), 10);
    replay.coreVersion = "0.0.1";
    const auto playback = playReplay(replay);
    REQUIRE_FALSE(playback.has_value());
    REQUIRE(playback.error().code == ReplayErrorCode::kIncompatibleCoreVersion);
  }
  SECTION("a checkpoint past the final tick") {
    Replay replay = recorded(setup(), 10);
    replay.checkpoints.push_back({.tick = SimTick(11), .stateHash = 0, .eventHash = 0});
    REQUIRE(playReplay(replay).error().code == ReplayErrorCode::kInvalidSetup);
  }
  SECTION("no checkpoint of the final state") {
    Replay replay = recorded(setup(), 40);
    replay.checkpoints.pop_back();
    const auto playback = playReplay(replay);
    REQUIRE(playback.error().code == ReplayErrorCode::kInvalidSetup);
    REQUIRE(mentions(playback.error().message, "final tick 40"));
  }
  SECTION("no checkpoint of the initial state") {
    Replay replay = recorded(setup(), 40);
    replay.checkpoints.erase(replay.checkpoints.begin());
    REQUIRE(playReplay(replay).error().code == ReplayErrorCode::kInvalidSetup);
  }
  SECTION("no checkpoints at all") {
    Replay replay = recorded(setup(), 40);
    replay.checkpoints.clear();
    REQUIRE(playReplay(replay).error().code == ReplayErrorCode::kInvalidSetup);
  }
  SECTION("a command that never runs") {
    Replay replay = recorded(setup(), 40);
    replay.setup.commands.push_back(move(40, 7, 1.0, 1.0));
    const auto playback = playReplay(replay);
    REQUIRE(playback.error().code == ReplayErrorCode::kInvalidSetup);
    REQUIRE(mentions(playback.error().message, "command at tick 40 never runs"));
  }
  SECTION("a command for an unknown player") {
    Replay replay = recorded(setup(), 10);
    replay.setup.commands.push_back(move(5, 99, 1.0, 1.0));
    const auto playback = playReplay(replay);
    REQUIRE(playback.error().code == ReplayErrorCode::kInvalidSetup);
    REQUIRE(mentions(playback.error().message, "no player with id 99"));
  }
}
