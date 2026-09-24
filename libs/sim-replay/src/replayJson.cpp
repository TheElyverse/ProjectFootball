#include "replayJson.hpp"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <format>
#include <fstream>
#include <iterator>
#include <limits>
#include <nlohmann/json.hpp>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

#include "matchCommand.hpp"
#include "matchSetup.hpp"
#include "matchState.hpp"
#include "version.hpp"

namespace ElyverseFootball::SimReplay {
namespace {

// ordered_json keeps keys in insertion order, so files read top to bottom
// the way docs/replay-format.md lists them. Key order is not part of the
// format.
using Json = nlohmann::ordered_json;
using SimCore::PlayerId;
using SimCore::SimTick;
using SimCore::Vec2;
using SimMatch::MatchCommand;
using SimMatch::MatchConfig;
using SimMatch::MatchSetup;
using SimMatch::MatchState;
using SimMatch::MovePlayerCommand;
using SimMatch::PlayerMatchState;
using SimMatch::ScheduledCommand;
using SimMatch::TeamSide;

// ---------------------------------------------------------------------------
// Writing

[[nodiscard]] Json vec2Json(const Vec2 vector) {
  return {{"x", vector.x}, {"y", vector.y}};
}

[[nodiscard]] Json playerJson(const PlayerMatchState& player) {
  Json json;
  json["id"] = player.playerId.value();
  json["side"] = SimMatch::teamSideName(player.side);
  json["position"] = vec2Json(player.position);
  json["velocity"] = vec2Json(player.velocity);
  json["attributes"] = {{"maxSpeed", player.attributes.maxSpeed},
                        {"acceleration", player.attributes.acceleration}};
  json["target"] = player.target ? vec2Json(*player.target) : Json(nullptr);
  json["facing"] = vec2Json(player.facing);
  return json;
}

[[nodiscard]] Json touchJson(const std::optional<SimMatch::BallTouch>& touch) {
  if (!touch) {
    return nullptr;
  }
  return {{"playerId", touch->playerId.value()}, {"tick", touch->tick.value()}};
}

[[nodiscard]] Json stateJson(const MatchState& state) {
  Json json;
  json["pitch"] = {{"length", state.pitch().lengthMeters()},
                   {"width", state.pitch().widthMeters()}};
  json["playersPerSide"] = state.playersPerSide();
  json["players"] = Json::array();
  for (const PlayerMatchState& player : state.players()) {
    json["players"].push_back(playerJson(player));
  }
  const auto& owner = state.ball().owner;
  json["ball"] = {{"position", vec2Json(state.ball().position)},
                  {"velocity", vec2Json(state.ball().velocity)},
                  {"owner", owner ? Json(owner->value()) : Json(nullptr)},
                  {"lastTouch", touchJson(state.ball().lastTouch)}};
  return json;
}

[[nodiscard]] Json configJson(const MatchConfig& config) {
  const SimMatch::PerceptionConfig& perception = config.perception;
  return {{"ticksPerSecond", config.ticksPerSecond},
          {"ball",
           {{"rollingDeceleration", config.ball.rollingDeceleration},
            {"carryDistance", config.ball.carryDistance}}},
          {"perception",
           {{"intervalTicks", perception.intervalTicks},
            {"viewDistance", perception.viewDistance},
            {"fieldOfViewDegrees", perception.fieldOfViewDegrees},
            {"awarenessRadius", perception.awarenessRadius},
            {"memorySeconds", perception.memorySeconds},
            {"extrapolationSeconds", perception.extrapolationSeconds}}},
          {"passing",
           {{"arrivalSpeed", config.passing.arrivalSpeed},
            {"maxSpeed", config.passing.maxSpeed},
            {"directionError", config.passing.directionError},
            {"speedError", config.passing.speedError}}},
          {"reception",
           {{"controlRadius", config.reception.controlRadius},
            {"reclaimDelaySeconds", config.reception.reclaimDelaySeconds}}},
          {"pursuit",
           {{"intervalTicks", config.pursuit.intervalTicks},
            {"sampleSeconds", config.pursuit.sampleSeconds},
            {"horizonSeconds", config.pursuit.horizonSeconds}}}};
}

void addCommandFields(Json& json, const MovePlayerCommand& command) {
  json["type"] = "movePlayer";
  json["playerId"] = command.playerId.value();
  json["target"] = vec2Json(command.target);
}

void addCommandFields(Json& json, const SimMatch::GiveBallCommand& command) {
  json["type"] = "giveBall";
  json["playerId"] = command.playerId.value();
}

void addCommandFields(Json& json, const SimMatch::PassCommand& command) {
  json["type"] = "pass";
  json["playerId"] = command.playerId.value();
  json["target"] = vec2Json(command.target);
  json["speed"] = command.speed;
  json["receiver"] = command.receiver ? Json(command.receiver->value()) : Json(nullptr);
}

// Commands in execution order, each with its position within its tick.
[[nodiscard]] Json commandsJson(std::vector<ScheduledCommand> commands) {
  std::ranges::stable_sort(commands, {}, &ScheduledCommand::tick);
  Json json = Json::array();
  std::optional<SimTick> previousTick;
  int order = 0;
  for (const ScheduledCommand& command : commands) {
    order = previousTick == command.tick ? order + 1 : 0;
    previousTick = command.tick;
    Json entry;
    entry["tick"] = command.tick.value();
    entry["order"] = order;
    std::visit([&entry](const auto& alternative) { addCommandFields(entry, alternative); },
               command.command);
    json.push_back(std::move(entry));
  }
  return json;
}

[[nodiscard]] std::string hashText(const std::uint64_t hash) {
  return std::format("{:016x}", hash);
}

// ---------------------------------------------------------------------------
// Reading

// Thrown inside this file only, and turned into a ReplayError at the edge.
class FormatError : public std::runtime_error {
 public:
  FormatError(const ReplayErrorCode code, const std::string& message)
      : std::runtime_error(message), code_(code) {}

  [[nodiscard]] ReplayErrorCode code() const noexcept { return code_; }

 private:
  ReplayErrorCode code_;
};

// A JSON value and the path that leads to it, so every error can name the
// field it is about: "initialState.players[3].position.x: expected a number".
class Field {
 public:
  Field(const Json& value, std::string path) : value_(&value), path_(std::move(path)) {}

  [[noreturn]] void fail(const std::string_view problem,
                         const ReplayErrorCode code = ReplayErrorCode::kMalformed) const {
    throw FormatError(code, std::format("{}: {}", path_, problem));
  }

  [[nodiscard]] Field member(const std::string_view key) const {
    if (!value_->is_object()) {
      fail("expected an object");
    }
    const auto found = value_->find(key);
    const std::string path = path_.empty() ? std::string(key) : std::format("{}.{}", path_, key);
    if (found == value_->end()) {
      throw FormatError(ReplayErrorCode::kMalformed, std::format("{}: missing", path));
    }
    return {*found, path};
  }

  [[nodiscard]] bool isNull() const noexcept { return value_->is_null(); }

  [[nodiscard]] std::vector<Field> elements() const {
    if (!value_->is_array()) {
      fail("expected an array");
    }
    std::vector<Field> elements;
    elements.reserve(value_->size());
    for (std::size_t index = 0; const Json& element : *value_) {
      elements.emplace_back(element, std::format("{}[{}]", path_, index));
      ++index;
    }
    return elements;
  }

  [[nodiscard]] double number() const {
    if (!value_->is_number()) {
      fail("expected a number");
    }
    return value_->get<double>();
  }

  [[nodiscard]] std::int64_t integer() const {
    if (value_->is_number_unsigned()) {
      const auto value = value_->get<std::uint64_t>();
      if (value > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        fail("integer out of range");
      }
      return static_cast<std::int64_t>(value);
    }
    if (!value_->is_number_integer()) {
      fail("expected an integer");
    }
    return value_->get<std::int64_t>();
  }

  [[nodiscard]] std::int64_t integerIn(const std::int64_t min, const std::int64_t max) const {
    const std::int64_t value = integer();
    if (value < min || value > max) {
      fail(std::format("expected an integer from {} to {}, got {}", min, max, value));
    }
    return value;
  }

  [[nodiscard]] std::string string() const {
    if (!value_->is_string()) {
      fail("expected a string");
    }
    return value_->get<std::string>();
  }

  [[nodiscard]] const std::string& path() const noexcept { return path_; }

 private:
  const Json* value_;
  std::string path_;
};

constexpr std::int64_t kMaxTick = std::int64_t{1} << 53;

[[nodiscard]] Vec2 readVec2(const Field& field) {
  return {.x = field.member("x").number(), .y = field.member("y").number()};
}

[[nodiscard]] TeamSide readSide(const Field& field) {
  const std::string side = field.string();
  if (side == "home") {
    return TeamSide::kHome;
  }
  if (side == "away") {
    return TeamSide::kAway;
  }
  field.fail(std::format(R"(expected "home" or "away", got "{}")", side));
}

[[nodiscard]] PlayerId readPlayerId(const Field& field) {
  return PlayerId(static_cast<PlayerId::ValueType>(
      field.integerIn(0, std::numeric_limits<PlayerId::ValueType>::max() - 1)));
}

[[nodiscard]] std::optional<PlayerId> readOwner(const Field& field) {
  if (field.isNull()) {
    return std::nullopt;
  }
  return readPlayerId(field);
}

[[nodiscard]] std::optional<SimMatch::BallTouch> readTouch(const Field& field) {
  if (field.isNull()) {
    return std::nullopt;
  }
  return SimMatch::BallTouch{.playerId = readPlayerId(field.member("playerId")),
                             .tick = SimTick(field.member("tick").integerIn(-kMaxTick, kMaxTick))};
}

[[nodiscard]] PlayerMatchState readPlayer(const Field& field) {
  const Field attributes = field.member("attributes");
  const Field target = field.member("target");
  return {.playerId = readPlayerId(field.member("id")),
          .side = readSide(field.member("side")),
          .position = readVec2(field.member("position")),
          .velocity = readVec2(field.member("velocity")),
          .attributes = {.maxSpeed = attributes.member("maxSpeed").number(),
                         .acceleration = attributes.member("acceleration").number()},
          .target = target.isNull() ? std::nullopt : std::optional(readVec2(target)),
          .facing = readVec2(field.member("facing"))};
}

[[nodiscard]] MatchState readState(const Field& field) {
  const Field pitchField = field.member("pitch");
  const std::optional<SimMatch::Pitch> pitch = [&pitchField]() -> std::optional<SimMatch::Pitch> {
    try {
      return SimMatch::Pitch(pitchField.member("length").number(),
                             pitchField.member("width").number());
    } catch (const std::invalid_argument&) {
      return std::nullopt;
    }
  }();
  if (!pitch) {
    pitchField.fail("pitch dimensions must be positive and finite", ReplayErrorCode::kInvalidSetup);
  }

  std::vector<PlayerMatchState> players;
  for (const Field& player : field.member("players").elements()) {
    players.push_back(readPlayer(player));
  }
  const Field ball = field.member("ball");
  auto state = MatchState::create(
      {.pitch = *pitch,
       .players = std::move(players),
       .ball = {.position = readVec2(ball.member("position")),
                .velocity = readVec2(ball.member("velocity")),
                .owner = readOwner(ball.member("owner")),
                .lastTouch = readTouch(ball.member("lastTouch"))},
       .playersPerSide = static_cast<int>(field.member("playersPerSide").integerIn(1, 1000))});
  if (!state) {
    std::string problems = "invalid match state";
    for (const SimMatch::MatchStateError& error : state.error()) {
      problems += "; " + error.message;
    }
    field.fail(problems, ReplayErrorCode::kInvalidSetup);
  }
  return *std::move(state);
}

[[nodiscard]] SimMatch::PerceptionConfig readPerception(const Field& field) {
  return {.intervalTicks = static_cast<int>(field.member("intervalTicks").integerIn(1, 100000)),
          .viewDistance = field.member("viewDistance").number(),
          .fieldOfViewDegrees = field.member("fieldOfViewDegrees").number(),
          .awarenessRadius = field.member("awarenessRadius").number(),
          .memorySeconds = field.member("memorySeconds").number(),
          .extrapolationSeconds = field.member("extrapolationSeconds").number()};
}

[[nodiscard]] SimMatch::PassConfig readPassing(const Field& field) {
  return {.arrivalSpeed = field.member("arrivalSpeed").number(),
          .maxSpeed = field.member("maxSpeed").number(),
          .directionError = field.member("directionError").number(),
          .speedError = field.member("speedError").number()};
}

[[nodiscard]] SimMatch::PursuitConfig readPursuit(const Field& field) {
  return {.intervalTicks = static_cast<int>(field.member("intervalTicks").integerIn(1, 100000)),
          .sampleSeconds = field.member("sampleSeconds").number(),
          .horizonSeconds = field.member("horizonSeconds").number()};
}

[[nodiscard]] MatchConfig readConfig(const Field& field) {
  return {
      .ticksPerSecond = static_cast<int>(field.member("ticksPerSecond").integerIn(1, 100000)),
      .ball = {.rollingDeceleration = field.member("ball").member("rollingDeceleration").number(),
               .carryDistance = field.member("ball").member("carryDistance").number()},
      .perception = readPerception(field.member("perception")),
      .passing = readPassing(field.member("passing")),
      .reception = {.controlRadius = field.member("reception").member("controlRadius").number(),
                    .reclaimDelaySeconds =
                        field.member("reception").member("reclaimDelaySeconds").number()},
      .pursuit = readPursuit(field.member("pursuit"))};
}

[[nodiscard]] MatchCommand readCommand(const Field& field) {
  const std::string type = field.member("type").string();
  if (type == "movePlayer") {
    return MovePlayerCommand{.playerId = readPlayerId(field.member("playerId")),
                             .target = readVec2(field.member("target"))};
  }
  if (type == "giveBall") {
    return SimMatch::GiveBallCommand{.playerId = readPlayerId(field.member("playerId"))};
  }
  if (type == "pass") {
    return SimMatch::PassCommand{.playerId = readPlayerId(field.member("playerId")),
                                 .target = readVec2(field.member("target")),
                                 .speed = field.member("speed").number(),
                                 .receiver = readOwner(field.member("receiver"))};
  }
  field.member("type").fail(std::format("unknown command type \"{}\"", type));
}

// Execution order is explicit: ticks never decrease, and "order" counts 0, 1,
// 2, ... within a tick. A file that disagrees with its own array order is
// rejected rather than silently re-sorted.
[[nodiscard]] std::vector<ScheduledCommand> readCommands(const Field& field) {
  std::vector<ScheduledCommand> commands;
  std::optional<std::int64_t> previousTick;
  std::int64_t expectedOrder = 0;
  for (const Field& entry : field.elements()) {
    const std::int64_t tick = entry.member("tick").integerIn(0, kMaxTick);
    if (previousTick && tick < *previousTick) {
      entry.member("tick").fail(
          std::format("commands must be sorted by tick, got {} after {}", tick, *previousTick));
    }
    expectedOrder = previousTick == tick ? expectedOrder + 1 : 0;
    previousTick = tick;
    const std::int64_t order = entry.member("order").integer();
    if (order != expectedOrder) {
      entry.member("order").fail(std::format("expected {}, got {}", expectedOrder, order));
    }
    commands.push_back({.tick = SimTick(tick), .command = readCommand(entry)});
  }
  return commands;
}

[[nodiscard]] std::uint64_t readUnsigned(const Field& field, const int base,
                                         const std::string_view expected) {
  const std::string text = field.string();
  std::uint64_t value = 0;
  // NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic) -- std::from_chars only takes a
  // raw [begin, end) pointer range.
  const char* const end = text.data() + text.size();
  const auto [ptr, error] = std::from_chars(text.data(), end, value, base);
  // NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic)
  if (text.empty() || error != std::errc{} || ptr != end) {
    field.fail(std::format("expected {}, got \"{}\"", expected, text));
  }
  return value;
}

[[nodiscard]] std::vector<ReplayCheckpoint> readCheckpoints(const Field& field) {
  std::vector<ReplayCheckpoint> checkpoints;
  for (const Field& entry : field.elements()) {
    const Field hash = entry.member("stateHash");
    if (hash.string().size() != 16) {
      hash.fail("expected 16 hexadecimal digits");
    }
    checkpoints.push_back({.tick = SimTick(entry.member("tick").integerIn(0, kMaxTick)),
                           .stateHash = readUnsigned(hash, 16, "16 hexadecimal digits")});
  }
  return checkpoints;
}

void checkVersions(const Field& root) {
  const Field schema = root.member("schemaVersion");
  const std::int64_t version = schema.integer();
  if (version == 1) {
    schema.fail("schema version 1 holds replay metadata only and cannot be played back; expected " +
                    std::to_string(kReplaySchemaVersion),
                ReplayErrorCode::kUnsupportedSchemaVersion);
  }
  if (version != kReplaySchemaVersion) {
    schema.fail(
        std::format("unsupported schema version {}, expected {}", version, kReplaySchemaVersion),
        ReplayErrorCode::kUnsupportedSchemaVersion);
  }
  const Field core = root.member("coreVersion");
  if (core.string() != SimCore::coreVersion()) {
    core.fail(std::format("replay was recorded with core version {}, this build is {}",
                          core.string(), SimCore::coreVersion()),
              ReplayErrorCode::kIncompatibleCoreVersion);
  }
}

[[nodiscard]] Replay readReplay(const Field& root) {
  checkVersions(root);
  return {.coreVersion = root.member("coreVersion").string(),
          .createdAt = root.member("createdAt").string(),
          .setup = {.initialState = readState(root.member("initialState")),
                    .config = readConfig(root.member("config")),
                    .seed = readUnsigned(root.member("seed"), 10, "an unsigned 64-bit integer"),
                    .commands = readCommands(root.member("commands"))},
          .finalTick = SimTick(root.member("gameTime").integerIn(0, kMaxTick)),
          .checkpoints = readCheckpoints(root.member("checkpoints"))};
}

}  // namespace

std::string toReplayJson(const Replay& replay) {
  Json json;
  json["schemaVersion"] = kReplaySchemaVersion;
  json["coreVersion"] = replay.coreVersion;
  json["createdAt"] = replay.createdAt;
  // A decimal string, never a JSON number: see docs/replay-format.md.
  json["seed"] = std::to_string(replay.setup.seed);
  json["gameTime"] = replay.finalTick.value();
  json["config"] = configJson(replay.setup.config);
  json["initialState"] = stateJson(replay.setup.initialState);
  json["commands"] = commandsJson(replay.setup.commands);
  json["checkpoints"] = Json::array();
  for (const ReplayCheckpoint& checkpoint : replay.checkpoints) {
    json["checkpoints"].push_back(
        {{"tick", checkpoint.tick.value()}, {"stateHash", hashText(checkpoint.stateHash)}});
  }
  return json.dump(2) + "\n";
}

std::expected<Replay, ReplayError> parseReplayJson(const std::string_view json) {
  const Json document = Json::parse(json, nullptr, false);
  if (document.is_discarded()) {
    return std::unexpected(
        ReplayError{.code = ReplayErrorCode::kMalformed, .message = "not a valid JSON document"});
  }
  if (!document.is_object()) {
    return std::unexpected(ReplayError{.code = ReplayErrorCode::kMalformed,
                                       .message = "expected a JSON object at the top level"});
  }
  try {
    return readReplay(Field(document, ""));
  } catch (const FormatError& error) {
    return std::unexpected(ReplayError{.code = error.code(), .message = error.what()});
  }
}

std::expected<void, ReplayError> saveReplay(const Replay& replay,
                                            const std::filesystem::path& path) {
  std::ofstream out(path, std::ios::binary);
  out << toReplayJson(replay);
  out.close();
  if (!out) {
    return std::unexpected(
        ReplayError{.code = ReplayErrorCode::kIoError, .message = "cannot write " + path.string()});
  }
  return {};
}

std::expected<Replay, ReplayError> loadReplay(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return std::unexpected(
        ReplayError{.code = ReplayErrorCode::kIoError, .message = "cannot read " + path.string()});
  }
  const std::string contents{std::istreambuf_iterator<char>(input),
                             std::istreambuf_iterator<char>()};
  auto replay = parseReplayJson(contents);
  if (!replay) {
    replay.error().message = path.string() + ": " + replay.error().message;
  }
  return replay;
}

}  // namespace ElyverseFootball::SimReplay
