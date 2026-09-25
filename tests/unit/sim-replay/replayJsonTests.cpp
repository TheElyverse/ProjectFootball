#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <string>
#include <utility>

#include "kickoffScenario.hpp"
#include "matchSetup.hpp"
#include "referenceTactic.hpp"
#include "replay.hpp"
#include "replayJson.hpp"
#include "scenarios.hpp"
#include "tactic.hpp"

using ElyverseFootball::SimCore::SimTick;
using ElyverseFootball::SimMatch::makeSevenASideKickoff;
using ElyverseFootball::SimMatch::MatchSetup;
using ElyverseFootball::SimMatch::Pitch;
using ElyverseFootball::SimReplay::loadReplay;
using ElyverseFootball::SimReplay::parseReplayJson;
using ElyverseFootball::SimReplay::recordMatch;
using ElyverseFootball::SimReplay::ReplayErrorCode;
using ElyverseFootball::SimReplay::toReplayJson;

namespace {

// A valid replay document to break one piece at a time.
[[nodiscard]] std::string validJson() {
  auto state = makeSevenASideKickoff(Pitch(60.0, 40.0));
  REQUIRE(state.has_value());
  const MatchSetup setup{
      .initialState = *std::move(state), .config = {}, .seed = 5, .commands = {}};
  const auto replay = recordMatch(setup, SimTick(3), 1, "2026-09-24T10:00:00Z");
  REQUIRE(replay.has_value());
  return toReplayJson(*replay);
}

// validJson() with the first occurrence of `from` replaced by `to`.
[[nodiscard]] std::string validJsonWith(const std::string& from, const std::string& replacement) {
  std::string json = validJson();
  const auto position = json.find(from);
  REQUIRE(position != std::string::npos);
  json.replace(position, from.size(), replacement);
  return json;
}

void requireRejected(const std::string& json, const ReplayErrorCode code,
                     const std::string& fragment) {
  const auto replay = parseReplayJson(json);
  REQUIRE_FALSE(replay.has_value());
  CAPTURE(replay.error().message);
  REQUIRE(replay.error().code == code);
  REQUIRE(replay.error().message.find(fragment) != std::string::npos);
}

constexpr auto kMalformed = ReplayErrorCode::kMalformed;

}  // namespace

TEST_CASE("A valid document parses", "[replayJson]") {
  REQUIRE(parseReplayJson(validJson()).has_value());
}

TEST_CASE("Documents that are not JSON objects are rejected", "[replayJson]") {
  requireRejected("{\"schemaVersion\": 4,", kMalformed, "not a valid JSON document");
  requireRejected("[1, 2]", kMalformed, "expected a JSON object");
}

TEST_CASE("Other schema versions are rejected with the version found", "[replayJson]") {
  requireRejected(validJsonWith("\"schemaVersion\": 4", "\"schemaVersion\": 1"),
                  ReplayErrorCode::kUnsupportedSchemaVersion,
                  "schema version 1 holds replay metadata only");
  // Older playable versions name the way to a current file.
  for (const char* version : {"2", "3"}) {
    requireRejected(
        validJsonWith("\"schemaVersion\": 4", std::string("\"schemaVersion\": ") + version),
        ReplayErrorCode::kUnsupportedSchemaVersion,
        std::string("schema version ") + version +
            " is no longer supported, expected 4; record the scenario again");
  }
  requireRejected(validJsonWith("\"schemaVersion\": 4", "\"schemaVersion\": 5"),
                  ReplayErrorCode::kUnsupportedSchemaVersion, "unsupported schema version 5");
}

TEST_CASE("A replay from another core version is rejected", "[replayJson]") {
  requireRejected(validJsonWith(R"("coreVersion": ")", R"("coreVersion": "9)"),
                  ReplayErrorCode::kIncompatibleCoreVersion, "recorded with core version 9");
}

TEST_CASE("Missing and mistyped fields are named by their path", "[replayJson]") {
  requireRejected(validJsonWith("\"gameTime\"", "\"gameTimes\""), kMalformed, "gameTime: missing");
  requireRejected(validJsonWith(R"("seed": "5")", "\"seed\": 5"), kMalformed,
                  "seed: expected a string");
  requireRejected(validJsonWith(R"("seed": "5")", R"("seed": "-5")"), kMalformed,
                  "seed: expected an unsigned 64-bit integer");
  requireRejected(validJsonWith(R"("seed": "5")", R"("seed": "18446744073709551616")"), kMalformed,
                  "seed: expected an unsigned 64-bit integer");
  requireRejected(validJsonWith("\"length\": 60.0", R"("length": "60")"), kMalformed,
                  "initialState.pitch.length: expected a number");
  requireRejected(validJsonWith(R"("side": "home")", R"("side": "left")"), kMalformed,
                  R"(initialState.players[0].side: expected "home" or "away")");
  requireRejected(validJsonWith("\"ticksPerSecond\": 30", "\"ticksPerSecond\": 0"), kMalformed,
                  "config.ticksPerSecond: expected an integer from 1");
  requireRejected(validJsonWith(R"("stateHash": ")", R"("stateHash": "0)"), kMalformed,
                  "checkpoints[0].stateHash: expected 16 hexadecimal digits");
  requireRejected(validJsonWith(R"("eventHash": ")", R"("eventHash": "x)"), kMalformed,
                  "checkpoints[0].eventHash: expected 16 hexadecimal digits");
  requireRejected(validJsonWith(R"("eventHash")", R"("eventHashes")"), kMalformed,
                  "checkpoints[0].eventHash: missing");
}

TEST_CASE("An invalid initial state is rejected with its errors", "[replayJson]") {
  requireRejected(validJsonWith("\"id\": 2", "\"id\": 1"), ReplayErrorCode::kInvalidSetup,
                  "duplicate player id 1");
  requireRejected(validJsonWith("\"maxSpeed\": 7.5", "\"maxSpeed\": -7.5"),
                  ReplayErrorCode::kInvalidSetup, "expected both positive and finite");
}

TEST_CASE("The ball's owner is read from the initial state", "[replayJson]") {
  const auto owned = parseReplayJson(validJsonWith(R"("owner": null)", R"("owner": 7)"));
  REQUIRE(owned.has_value());
  REQUIRE(owned->setup.initialState.ball().owner == ElyverseFootball::SimCore::PlayerId(7));

  requireRejected(validJsonWith(R"("owner": null)", R"("owner": 99)"),
                  ReplayErrorCode::kInvalidSetup, "the ball belongs to player 99");
}

TEST_CASE("Commands must state their execution order explicitly", "[replayJson]") {
  const std::string command =
      R"({"tick": 2, "order": 0, "type": "movePlayer", "playerId": 3, "target": {"x": 1.0, "y": 2.0}})";
  const auto withCommands = [](const std::string& commands) {
    return validJsonWith("\"commands\": []", "\"commands\": [" + commands + "]");
  };
  const auto replace = [](std::string text, const std::string& from,
                          const std::string& replacement) {
    text.replace(text.find(from), from.size(), replacement);
    return text;
  };

  const std::string secondInTick = replace(command, "\"order\": 0", "\"order\": 1");
  REQUIRE(parseReplayJson(withCommands(command + "," + secondInTick)).has_value());

  requireRejected(withCommands(command + "," + command), kMalformed,
                  "commands[1].order: expected 1, got 0");
  requireRejected(withCommands(secondInTick), kMalformed, "commands[0].order: expected 0, got 1");
  requireRejected(withCommands(command + "," + replace(command, "\"tick\": 2", "\"tick\": 1")),
                  kMalformed, "commands[1].tick: commands must be sorted by tick");
  requireRejected(withCommands(replace(command, "movePlayer", "teleport")), kMalformed,
                  "commands[0].type: unknown command type \"teleport\"");
}

TEST_CASE("Loading a missing file is an I/O error", "[replayJson]") {
  const auto replay = loadReplay(std::filesystem::temp_directory_path() / "no-such-replay.json");

  REQUIRE_FALSE(replay.has_value());
  REQUIRE(replay.error().code == ReplayErrorCode::kIoError);
}

TEST_CASE("Tactics survive a round trip through the replay format", "[replayJson]") {
  using ElyverseFootball::SimTactics::referenceTacticSpec;
  using ElyverseFootball::SimTactics::Tactic;
  auto awaySpec = referenceTacticSpec();
  awaySpec.name = "away";
  const auto home = Tactic::create(referenceTacticSpec());
  const auto away = Tactic::create(awaySpec);
  REQUIRE(home.has_value());
  REQUIRE(away.has_value());
  const auto kickoff = makeSevenASideKickoff(Pitch(60.0, 40.0));
  REQUIRE(kickoff.has_value());
  auto state = ElyverseFootball::SimMatch::MatchState::create(
      {.pitch = kickoff->pitch(),
       .players = {kickoff->players().begin(), kickoff->players().end()},
       .ball = kickoff->ball(),
       .playersPerSide = kickoff->playersPerSide()},
      {.home = *home, .away = *away});
  REQUIRE(state.has_value());
  const MatchSetup setup{.initialState = *state, .config = {}, .seed = 5, .commands = {}};
  const auto replay = recordMatch(setup, SimTick(3), 1, "2026-09-25T10:00:00Z");
  REQUIRE(replay.has_value());

  const auto parsed = parseReplayJson(toReplayJson(*replay));
  REQUIRE(parsed.has_value());
  REQUIRE(parsed->setup.initialState.tactics() == state->tactics());
  REQUIRE(*parsed == *replay);
}

TEST_CASE("A malformed tactic in a replay is rejected with its field", "[replayJson]") {
  // A scripted side's tactic is null; anything but a tactic object there is
  // malformed.
  requireRejected(validJsonWith(R"("home": null)", R"("home": "reference")"), kMalformed,
                  "initialState.tactics.home: expected an object");
  requireRejected(validJsonWith(R"("tactics": {)", R"("tacticz": {)"), kMalformed,
                  "initialState.tactics: missing");
}

TEST_CASE("A tactic change needs a side and a tactic", "[replayJson]") {
  const auto withCommand = [](const std::string& command) {
    return validJsonWith("\"commands\": []", "\"commands\": [" + command + "]");
  };
  requireRejected(
      withCommand(
          R"({"tick": 1, "order": 0, "type": "changeTactic", "side": "home", "tactic": null})"),
      kMalformed, "commands[0].tactic: a tactic change needs a tactic");
  requireRejected(
      withCommand(
          R"({"tick": 1, "order": 0, "type": "changeTactic", "side": "both", "tactic": null})"),
      kMalformed, "commands[0].side");
  requireRejected(withCommand(R"({"tick": 1, "order": 0, "type": "changeTactic", "side": "home",
                                  "tactic": {"format": "elyverse-tactic"}})"),
                  kMalformed, "commands[0].tactic");
}

TEST_CASE("A tactic whose content does not match its hash is rejected", "[replayJson]") {
  using ElyverseFootball::SimTactics::referenceTacticSpec;
  using ElyverseFootball::SimTactics::Tactic;
  const auto tactic = Tactic::create(referenceTacticSpec());
  REQUIRE(tactic.has_value());
  const auto setup =
      ElyverseFootball::SimMatch::makeTacticMatch({.home = *tactic, .away = *tactic}, 5);
  REQUIRE(setup.has_value());
  const auto replay = recordMatch(*setup, SimTick(3), 1, "2026-09-25T10:00:00Z");
  REQUIRE(replay.has_value());
  const std::string json = toReplayJson(*replay);
  // The reference tactic's pinned content hash (tacticHashTests.cpp).
  REQUIRE(json.find(R"("contentHash": "d1f008d25b46aabf")") != std::string::npos);

  // An edited tactic no longer matches the hash recorded with it.
  std::string edited = json;
  const std::string from = R"("pressingLine": 0.6)";
  edited.replace(edited.find(from), from.size(), R"("pressingLine": 0.5)");
  requireRejected(edited, ReplayErrorCode::kInvalidSetup,
                  "initialState.tactics.home.contentHash: tactic 'reference' has content hash ");
}

TEST_CASE("The checkpoint interval is recorded and must be positive", "[replayJson]") {
  REQUIRE(validJson().find(R"("checkpointIntervalTicks": 1)") != std::string::npos);
  requireRejected(
      validJsonWith(R"("checkpointIntervalTicks": 1)", R"("checkpointIntervalTicks": 0)"),
      kMalformed, "checkpointIntervalTicks");
}
