#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <string>
#include <utility>

#include "kickoffScenario.hpp"
#include "matchSetup.hpp"
#include "replay.hpp"
#include "replayJson.hpp"

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
  requireRejected("{\"schemaVersion\": 2,", kMalformed, "not a valid JSON document");
  requireRejected("[1, 2]", kMalformed, "expected a JSON object");
}

TEST_CASE("Other schema versions are rejected with the version found", "[replayJson]") {
  requireRejected(validJsonWith("\"schemaVersion\": 2", "\"schemaVersion\": 1"),
                  ReplayErrorCode::kUnsupportedSchemaVersion,
                  "schema version 1 holds replay metadata only");
  requireRejected(validJsonWith("\"schemaVersion\": 2", "\"schemaVersion\": 3"),
                  ReplayErrorCode::kUnsupportedSchemaVersion, "unsupported schema version 3");
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
