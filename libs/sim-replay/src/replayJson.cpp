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
#include "tacticHash.hpp"
#include "tacticJson.hpp"
#include "version.hpp"

namespace ElyverseFootball::SimReplay {
namespace {

// ordered_json keeps keys in insertion order, so files read top to bottom the
// way docs/tooling.md#replay-format lists them. Key order is not part of the
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

[[nodiscard]] Json attributesJson(const SimMatch::PlayerAttributes& attributes) {
  return {{"maxSpeed", attributes.maxSpeed},
          {"acceleration", attributes.acceleration},
          {"shotAccuracy", attributes.shotAccuracy},
          {"shotTechnique", attributes.shotTechnique},
          {"strongFoot", SimMatch::footName(attributes.strongFoot)},
          {"weakFootAccuracy", attributes.weakFootAccuracy},
          {"keeperPositioning", attributes.keeperPositioning},
          {"keeperAnticipation", attributes.keeperAnticipation},
          {"keeperReflexes", attributes.keeperReflexes},
          {"keeperHandling", attributes.keeperHandling},
          {"jumping", attributes.jumping},
          {"heading", attributes.heading},
          {"strength", attributes.strength},
          {"ballControl", attributes.ballControl},
          {"dribbling", attributes.dribbling},
          {"decisionMaking", attributes.decisionMaking}};
}

[[nodiscard]] Json playerJson(const PlayerMatchState& player) {
  Json json;
  json["id"] = player.playerId.value();
  json["side"] = SimMatch::teamSideName(player.side);
  json["position"] = vec2Json(player.position);
  json["velocity"] = vec2Json(player.velocity);
  json["attributes"] = attributesJson(player.attributes);
  json["target"] = player.target ? vec2Json(*player.target) : Json(nullptr);
  json["facing"] = vec2Json(player.facing);
  json["fitness"] = {{"sprintReserve", player.fitness.sprintReserve},
                     {"playedTicks", player.fitness.playedTicks},
                     {"topSpeedShare", player.fitness.topSpeedShare}};
  json["yellowCards"] = player.yellowCards;
  json["sentOff"] = player.sentOff;
  return json;
}

[[nodiscard]] Json touchJson(const std::optional<SimMatch::BallTouch>& touch) {
  if (!touch) {
    return nullptr;
  }
  return {{"playerId", touch->playerId.value()}, {"tick", touch->tick.value()}};
}

[[nodiscard]] Json carryTouchJson(const std::optional<SimMatch::CarryTouch>& touch) {
  if (!touch) {
    return nullptr;
  }
  return {{"tick", touch->tick.value()}, {"seconds", touch->seconds}};
}

[[nodiscard]] std::string hashText(const std::uint64_t hash) {
  return std::format("{:016x}", hash);
}

// A tactic is embedded in its own file format
// (docs/tactics.md#tactic-file-format), so a replay's tactic can be cut out and
// loaded as a tactic file, next to its content hash, which identifies the
// version played and is checked on reading.
[[nodiscard]] Json tacticJson(const std::optional<SimTactics::Tactic>& tactic) {
  if (!tactic) {
    return nullptr;
  }
  return {{"contentHash", hashText(SimTactics::contentHash(*tactic))},
          {"tactic", Json::parse(SimTactics::toTacticJson(*tactic))}};
}

[[nodiscard]] Json tacticsJson(const SimMatch::TeamTactics& tactics) {
  return {{"home", tacticJson(tactics.home)}, {"away", tacticJson(tactics.away)}};
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
                  {"height", state.ball().height},
                  {"verticalVelocity", state.ball().verticalVelocity},
                  {"spin", state.ball().spin},
                  {"sideSpin", state.ball().sideSpin},
                  {"owner", owner ? Json(owner->value()) : Json(nullptr)},
                  {"held", state.ball().held},
                  {"carryTouch", carryTouchJson(state.ball().touch)},
                  {"lastTouch", touchJson(state.ball().lastTouch)}};
  json["score"] = {{"home", state.score().home}, {"away", state.score().away}};
  json["tactics"] = tacticsJson(state.tactics());
  return json;
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

void addCommandFields(Json& json, const SimMatch::ChangeTacticCommand& command) {
  json["type"] = "changeTactic";
  json["side"] = SimMatch::teamSideName(command.side);
  json["tactic"] = tacticJson(command.tactic);
}

void addCommandFields(Json& json, const SimMatch::SubstituteCommand& command) {
  json["type"] = "substitute";
  json["side"] = SimMatch::teamSideName(command.side);
  json["off"] = command.off.value();
  json["on"] = command.on.value();
  json["attributes"] = attributesJson(command.attributes);
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

  [[nodiscard]] bool boolean() const {
    if (!value_->is_boolean()) {
      fail("expected true or false");
    }
    return value_->get<bool>();
  }

  [[nodiscard]] std::string string() const {
    if (!value_->is_string()) {
      fail("expected a string");
    }
    return value_->get<std::string>();
  }

  [[nodiscard]] const std::string& path() const noexcept { return path_; }
  [[nodiscard]] const Json& json() const noexcept { return *value_; }

 private:
  const Json* value_;
  std::string path_;
};

constexpr std::int64_t kMaxTick = std::int64_t{1} << 53;
// More goals than any match has; keeps a score inside an int.
constexpr std::int64_t kMaxGoals = 1'000'000;

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

[[nodiscard]] std::optional<SimMatch::CarryTouch> readCarryTouch(const Field& field) {
  if (field.isNull()) {
    return std::nullopt;
  }
  return SimMatch::CarryTouch{.tick = SimTick(field.member("tick").integerIn(-kMaxTick, kMaxTick)),
                              .seconds = field.member("seconds").number()};
}

[[nodiscard]] SimMatch::Foot readFoot(const Field& field) {
  const std::string foot = field.string();
  if (foot == "left") {
    return SimMatch::Foot::kLeft;
  }
  if (foot == "right") {
    return SimMatch::Foot::kRight;
  }
  field.fail(std::format(R"(expected "left" or "right", got "{}")", foot));
}

[[nodiscard]] SimMatch::PlayerAttributes readAttributes(const Field& attributes) {
  return {.maxSpeed = attributes.member("maxSpeed").number(),
          .acceleration = attributes.member("acceleration").number(),
          .shotAccuracy = attributes.member("shotAccuracy").number(),
          .shotTechnique = attributes.member("shotTechnique").number(),
          .strongFoot = readFoot(attributes.member("strongFoot")),
          .weakFootAccuracy = attributes.member("weakFootAccuracy").number(),
          .keeperPositioning = attributes.member("keeperPositioning").number(),
          .keeperAnticipation = attributes.member("keeperAnticipation").number(),
          .keeperReflexes = attributes.member("keeperReflexes").number(),
          .keeperHandling = attributes.member("keeperHandling").number(),
          .jumping = attributes.member("jumping").number(),
          .heading = attributes.member("heading").number(),
          .strength = attributes.member("strength").number(),
          .ballControl = attributes.member("ballControl").number(),
          .dribbling = attributes.member("dribbling").number(),
          .decisionMaking = attributes.member("decisionMaking").number()};
}

[[nodiscard]] PlayerMatchState readPlayer(const Field& field) {
  const Field target = field.member("target");
  const Field fitness = field.member("fitness");
  return {.playerId = readPlayerId(field.member("id")),
          .side = readSide(field.member("side")),
          .position = readVec2(field.member("position")),
          .velocity = readVec2(field.member("velocity")),
          .attributes = readAttributes(field.member("attributes")),
          .target = target.isNull() ? std::nullopt : std::optional(readVec2(target)),
          .facing = readVec2(field.member("facing")),
          .fitness = {.sprintReserve = fitness.member("sprintReserve").number(),
                      .playedTicks = fitness.member("playedTicks")
                                         .integerIn(0, std::numeric_limits<std::int64_t>::max()),
                      .topSpeedShare = fitness.member("topSpeedShare").number()},
          .yellowCards = static_cast<int>(field.member("yellowCards").integerIn(0, 2)),
          .sentOff = field.member("sentOff").boolean()};
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

[[nodiscard]] std::uint64_t readHash(const Field& field) {
  if (field.string().size() != 16) {
    field.fail("expected 16 hexadecimal digits");
  }
  return readUnsigned(field, 16, "16 hexadecimal digits");
}

[[nodiscard]] std::optional<SimTactics::Tactic> readTactic(const Field& field) {
  if (field.isNull()) {
    return std::nullopt;
  }
  const Field content = field.member("tactic");
  auto tactic = SimTactics::parseTacticJson(content.json().dump(), content.path());
  if (!tactic) {
    throw FormatError(tactic.error().code == SimTactics::TacticFileErrorCode::kInvalidTactic
                          ? ReplayErrorCode::kInvalidSetup
                          : ReplayErrorCode::kMalformed,
                      tactic.error().message);
  }
  const Field recorded = field.member("contentHash");
  if (const std::uint64_t hash = SimTactics::contentHash(*tactic); hash != readHash(recorded)) {
    recorded.fail(std::format("tactic '{}' has content hash {}, the replay recorded {}",
                              tactic->name(), hashText(hash), recorded.string()),
                  ReplayErrorCode::kInvalidSetup);
  }
  return *std::move(tactic);
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
  const Field score = field.member("score");
  auto state = MatchState::create(
      {.pitch = *pitch,
       .players = std::move(players),
       .ball = {.position = readVec2(ball.member("position")),
                .velocity = readVec2(ball.member("velocity")),
                .owner = readOwner(ball.member("owner")),
                .lastTouch = readTouch(ball.member("lastTouch")),
                .height = ball.member("height").number(),
                .verticalVelocity = ball.member("verticalVelocity").number(),
                .spin = ball.member("spin").number(),
                .sideSpin = ball.member("sideSpin").number(),
                .held = ball.member("held").boolean(),
                .touch = readCarryTouch(ball.member("carryTouch"))},
       .playersPerSide = static_cast<int>(field.member("playersPerSide").integerIn(1, 1000)),
       .score = {.home = static_cast<int>(score.member("home").integerIn(0, kMaxGoals)),
                 .away = static_cast<int>(score.member("away").integerIn(0, kMaxGoals))}},
      {.home = readTactic(field.member("tactics").member("home")),
       .away = readTactic(field.member("tactics").member("away"))});
  if (!state) {
    std::string problems = "invalid match state";
    for (const SimMatch::MatchStateError& error : state.error()) {
      problems += "; " + error.message;
    }
    field.fail(problems, ReplayErrorCode::kInvalidSetup);
  }
  return *std::move(state);
}

// ---------------------------------------------------------------------------
// Configuration
//
// Each configuration lists its fields once, in the order the file holds them.
// ConfigWriter writes them and ConfigReader reads them back, so the two
// directions cannot disagree about a field.

// Interval and tick-rate fields hold at most this many ticks.
constexpr std::int64_t kMaxIntervalTicks = 100000;

template <typename Visitor>
void fieldsOf(Visitor& visit, SimMatch::BallPhysics& ball) {
  visit.number("rollingDeceleration", ball.rollingDeceleration);
  visit.number("carryDistance", ball.carryDistance);
  visit.number("minCarrySpeedFactor", ball.minCarrySpeedFactor);
  visit.number("maxCarrySpeedFactor", ball.maxCarrySpeedFactor);
  visit.number("gravity", ball.gravity);
  visit.number("airDrag", ball.airDrag);
  visit.number("bounceRestitution", ball.bounceRestitution);
  visit.number("bounceGrip", ball.bounceGrip);
  visit.number("spinTransfer", ball.spinTransfer);
  visit.number("spinDecay", ball.spinDecay);
  visit.number("magnusCurvature", ball.magnusCurvature);
  visit.number("restingVerticalSpeed", ball.restingVerticalSpeed);
}

template <typename Visitor>
void fieldsOf(Visitor& visit, SimMatch::PerceptionConfig& perception) {
  visit.integer("intervalTicks", perception.intervalTicks, 1, kMaxIntervalTicks);
  visit.number("viewDistance", perception.viewDistance);
  visit.number("fieldOfViewDegrees", perception.fieldOfViewDegrees);
  visit.number("awarenessRadius", perception.awarenessRadius);
  visit.number("memorySeconds", perception.memorySeconds);
  visit.number("extrapolationSeconds", perception.extrapolationSeconds);
}

template <typename Visitor>
void fieldsOf(Visitor& visit, SimMatch::PassConfig& passing) {
  visit.number("arrivalSpeed", passing.arrivalSpeed);
  visit.number("maxSpeed", passing.maxSpeed);
  visit.number("directionError", passing.directionError);
  visit.number("speedError", passing.speedError);
  visit.number("pressureRadius", passing.pressureRadius);
  visit.number("pressureErrorFactor", passing.pressureErrorFactor);
  visit.number("maxLoftedSpeed", passing.maxLoftedSpeed);
  visit.number("loftedErrorFactor", passing.loftedErrorFactor);
}

template <typename Visitor>
void fieldsOf(Visitor& visit, SimMatch::ReceptionConfig& reception) {
  visit.number("controlRadius", reception.controlRadius);
  visit.number("reclaimDelaySeconds", reception.reclaimDelaySeconds);
  visit.number("controlHeight", reception.controlHeight);
  visit.number("handsRadius", reception.handsRadius);
  visit.number("handsHeight", reception.handsHeight);
}

template <typename Visitor>
void fieldsOf(Visitor& visit, SimMatch::PursuitConfig& pursuit) {
  visit.integer("intervalTicks", pursuit.intervalTicks, 1, kMaxIntervalTicks);
  visit.number("sampleSeconds", pursuit.sampleSeconds);
  visit.number("horizonSeconds", pursuit.horizonSeconds);
}

template <typename Visitor>
void fieldsOf(Visitor& visit, SimMatch::PassScoringConfig& scoring) {
  visit.number("minConfidence", scoring.minConfidence);
  visit.number("minPassDistance", scoring.minPassDistance);
  visit.number("maxPassDistance", scoring.maxPassDistance);
  visit.number("interceptionMarginSeconds", scoring.interceptionMarginSeconds);
  visit.number("pressureRadius", scoring.pressureRadius);
  visit.number("minCompletion", scoring.minCompletion);
  visit.number("completionWeight", scoring.completionWeight);
  visit.number("progressionWeight", scoring.progressionWeight);
  visit.number("pressureWeight", scoring.pressureWeight);
  visit.number("riskWeight", scoring.riskWeight);
  visit.number("loftedBias", scoring.loftedBias);
}

template <typename Visitor>
void fieldsOf(Visitor& visit, SimMatch::ShotScoringConfig& shooting) {
  visit.number("minConfidence", shooting.minConfidence);
  visit.number("maxShotDistance", shooting.maxShotDistance);
  visit.number("minOpening", shooting.minOpening);
  visit.integer("zoneColumns", shooting.zoneColumns, 0, SimMatch::kMaxShotZones);
  visit.integer("zoneRows", shooting.zoneRows, 0, SimMatch::kMaxShotZones);
  visit.number("shotSpeed", shooting.shotSpeed);
  visit.number("curledSpeed", shooting.curledSpeed);
  visit.number("chippedSpeed", shooting.chippedSpeed);
  visit.number("curlSpin", shooting.curlSpin);
  visit.number("spinError", shooting.spinError);
  visit.number("speedError", shooting.speedError);
  visit.number("maxLaunchSlope", shooting.maxLaunchSlope);
  visit.number("keeperDiveReach", shooting.keeperDiveReach);
  visit.number("keeperJumpReach", shooting.keeperJumpReach);
  visit.number("keeperDiveSeconds", shooting.keeperDiveSeconds);
  visit.number("blockReach", shooting.blockReach);
  visit.number("blockMarginSeconds", shooting.blockMarginSeconds);
  visit.number("saveMarginSeconds", shooting.saveMarginSeconds);
  visit.number("spreadAtZero", shooting.spreadAtZero);
  visit.number("spreadPerMeter", shooting.spreadPerMeter);
  visit.number("pressureRadius", shooting.pressureRadius);
  visit.number("pressureSpread", shooting.pressureSpread);
  visit.number("frameMargin", shooting.frameMargin);
  visit.number("reboundShare", shooting.reboundShare);
  visit.number("maxBlockRisk", shooting.maxBlockRisk);
  visit.number("minGoalChance", shooting.minGoalChance);
  visit.number("goalWeight", shooting.goalWeight);
  visit.number("secondBallWeight", shooting.secondBallWeight);
  visit.number("lossWeight", shooting.lossWeight);
}

template <typename Visitor>
void fieldsOf(Visitor& visit, SimMatch::DistributionConfig& distribution) {
  visit.number("holdSeconds", distribution.holdSeconds);
  visit.number("clearSeconds", distribution.clearSeconds);
  visit.number("throwRange", distribution.throwRange);
  visit.number("minLongDistance", distribution.minLongDistance);
  visit.number("flightSeconds", distribution.flightSeconds);
  visit.number("contestMarginSeconds", distribution.contestMarginSeconds);
  visit.number("directnessWeight", distribution.directnessWeight);
}

template <typename Visitor>
void fieldsOf(Visitor& visit, SimMatch::CarryPhaseFactors& factors) {
  visit.number("progression", factors.progression);
  visit.number("space", factors.space);
  visit.number("shot", factors.shot);
  visit.number("bypassed", factors.bypassed);
  visit.number("risk", factors.risk);
}

template <typename Visitor>
void fieldsOf(Visitor& visit, SimMatch::CrossScoringConfig& crossing) {
  visit.boolean("enabled", crossing.enabled);
  visit.number("minConfidence", crossing.minConfidence);
  visit.number("maxDepthShare", crossing.maxDepthShare);
  visit.number("crossSpeed", crossing.crossSpeed);
  visit.number("headerHeight", crossing.headerHeight);
  visit.number("drivenSpeed", crossing.drivenSpeed);
  visit.number("drivenHeight", crossing.drivenHeight);
  visit.number("cutBackSpeed", crossing.cutBackSpeed);
  visit.number("curlShare", crossing.curlShare);
  visit.number("contestMarginSeconds", crossing.contestMarginSeconds);
  visit.number("minWinChance", crossing.minWinChance);
  visit.number("winWeight", crossing.winWeight);
  visit.number("threatWeight", crossing.threatWeight);
  visit.number("lossWeight", crossing.lossWeight);
}

template <typename Visitor>
void fieldsOf(Visitor& visit, SimMatch::CarryScoringConfig& carrying) {
  visit.boolean("enabled", carrying.enabled);
  visit.number("minConfidence", carrying.minConfidence);
  visit.number("horizonSeconds", carrying.horizonSeconds);
  visit.number("minCarryDistance", carrying.minCarryDistance);
  visit.number("gapRadius", carrying.gapRadius);
  visit.number("sameDirection", carrying.sameDirection);
  visit.number("pitchMargin", carrying.pitchMargin);
  visit.number("safetySeconds", carrying.safetySeconds);
  visit.number("lossMarginSeconds", carrying.lossMarginSeconds);
  visit.number("touchSeconds", carrying.touchSeconds);
  visit.number("minTouchDistance", carrying.minTouchDistance);
  visit.number("maxTouchDistance", carrying.maxTouchDistance);
  visit.number("touchShare", carrying.touchShare);
  visit.number("lookBackSeconds", carrying.lookBackSeconds);
  visit.number("turnDistance", carrying.turnDistance);
  visit.number("dribbleRadius", carrying.dribbleRadius);
  visit.number("dribbleCone", carrying.dribbleCone);
  visit.number("dribbleClearance", carrying.dribbleClearance);
  visit.number("dribbleBeyond", carrying.dribbleBeyond);
  visit.number("maxTouchError", carrying.maxTouchError);
  visit.number("minTouchError", carrying.minTouchError);
  visit.number("maxDribbleError", carrying.maxDribbleError);
  visit.number("minDribbleError", carrying.minDribbleError);
  visit.number("slowDribbleShare", carrying.slowDribbleShare);
  visit.number("quickDribbleShare", carrying.quickDribbleShare);
  visit.number("shotDistance", carrying.shotDistance);
  visit.number("fullOpening", carrying.fullOpening);
  visit.number("minKeepChance", carrying.minKeepChance);
  visit.number("pressureRadius", carrying.pressureRadius);
  visit.number("keepWeight", carrying.keepWeight);
  visit.number("progressionWeight", carrying.progressionWeight);
  visit.number("spaceWeight", carrying.spaceWeight);
  visit.number("shotWeight", carrying.shotWeight);
  visit.number("bypassWeight", carrying.bypassWeight);
  visit.number("riskWeight", carrying.riskWeight);
  visit.number("holdWeight", carrying.holdWeight);
  visit.number("turnCost", carrying.turnCost);
  visit.number("turnSeconds", carrying.turnSeconds);
  visit.object("buildUpFactors", carrying.buildUp);
  visit.object("progressionFactors", carrying.progression);
  visit.object("finalThirdFactors", carrying.finalThird);
  visit.object("attackingTransitionFactors", carrying.attackingTransition);
}

template <typename Visitor>
void fieldsOf(Visitor& visit, SimMatch::DecisionConfig& decisions) {
  visit.integer("intervalTicks", decisions.intervalTicks, 1, kMaxIntervalTicks);
  visit.number("minHoldSeconds", decisions.minHoldSeconds);
  visit.number("temperature", decisions.temperature);
  visit.number("skillTemperatureSpread", decisions.skillTemperatureSpread);
  visit.number("maxMisjudgement", decisions.maxMisjudgement);
  visit.object("scoring", decisions.scoring);
  visit.object("shooting", decisions.shooting);
  visit.object("distribution", decisions.distribution);
  visit.number("stuckSeconds", decisions.stuckSeconds);
  visit.number("clearTouchlineMargin", decisions.clearTouchlineMargin);
  visit.number("clearGoalLineMargin", decisions.clearGoalLineMargin);
  visit.object("carrying", decisions.carrying);
  visit.object("crossing", decisions.crossing);
}

template <typename Visitor>
void fieldsOf(Visitor& visit, SimMatch::PhaseConfig& phases) {
  visit.integer("intervalTicks", phases.intervalTicks, 1, kMaxIntervalTicks);
  visit.number("transitionSeconds", phases.transitionSeconds);
  visit.number("hysteresisMeters", phases.hysteresisMeters);
}

template <typename Visitor>
void fieldsOf(Visitor& visit, SimMatch::PitchControlConfig& pitchControl) {
  visit.integer("intervalTicks", pitchControl.intervalTicks, 1, kMaxIntervalTicks);
  visit.number("cellSize", pitchControl.cellSize);
  visit.number("controlSeconds", pitchControl.controlSeconds);
}

template <typename Visitor>
void fieldsOf(Visitor& visit, SimMatch::PositioningConfig& positioning) {
  visit.integer("intervalTicks", positioning.intervalTicks, 1, kMaxIntervalTicks);
  visit.number("candidateSpacing", positioning.candidateSpacing);
  visit.number("targetDistanceScale", positioning.targetDistanceScale);
  visit.number("spacingRadius", positioning.spacingRadius);
  visit.number("pressureRadius", positioning.pressureRadius);
  visit.number("minConfidence", positioning.minConfidence);
  visit.number("hysteresisCost", positioning.hysteresisCost);
  visit.number("maxShiftMeters", positioning.maxShiftMeters);
}

template <typename Visitor>
void fieldsOf(Visitor& visit, SimMatch::OffBallConfig& offBall) {
  visit.number("nearBallRadius", offBall.nearBallRadius);
  visit.integer("nearIntervalTicks", offBall.nearIntervalTicks, 1, kMaxIntervalTicks);
  visit.integer("farIntervalTicks", offBall.farIntervalTicks, 1, kMaxIntervalTicks);
  visit.number("supportDistance", offBall.supportDistance);
  visit.number("spaceSearchRadius", offBall.spaceSearchRadius);
  visit.number("runDepth", offBall.runDepth);
  visit.number("laneRadius", offBall.laneRadius);
  visit.number("pressedRadius", offBall.pressedRadius);
  visit.number("effortScale", offBall.effortScale);
  visit.number("holdResponsibility", offBall.holdResponsibility);
  visit.number("temperature", offBall.temperature);
  visit.number("responsibilityWeight", offBall.responsibilityWeight);
  visit.number("regionWeight", offBall.regionWeight);
  visit.number("spaceWeight", offBall.spaceWeight);
  visit.number("laneWeight", offBall.laneWeight);
  visit.number("urgencyWeight", offBall.urgencyWeight);
  visit.number("effortWeight", offBall.effortWeight);
}

template <typename Visitor>
void fieldsOf(Visitor& visit, SimMatch::DefensiveConfig& defensive) {
  visit.number("markRadius", defensive.markRadius);
  visit.number("markDistance", defensive.markDistance);
  visit.number("trackRadius", defensive.trackRadius);
  visit.number("runnerSpeed", defensive.runnerSpeed);
  visit.number("trackLeadSeconds", defensive.trackLeadSeconds);
  visit.number("coverDistance", defensive.coverDistance);
  visit.number("pressRadius", defensive.pressRadius);
  visit.number("pressDistance", defensive.pressDistance);
  visit.number("idleSeconds", defensive.idleSeconds);
  visit.number("idlePressRadius", defensive.idlePressRadius);
  visit.number("laneMinDistance", defensive.laneMinDistance);
  visit.number("effortScale", defensive.effortScale);
  visit.number("holdResponsibility", defensive.holdResponsibility);
  visit.number("temperature", defensive.temperature);
  visit.number("responsibilityWeight", defensive.responsibilityWeight);
  visit.number("regionWeight", defensive.regionWeight);
  visit.number("spaceWeight", defensive.spaceWeight);
  visit.number("urgencyWeight", defensive.urgencyWeight);
  visit.number("effortWeight", defensive.effortWeight);
}

template <typename Visitor>
void fieldsOf(Visitor& visit, SimMatch::DuelConfig& duels) {
  visit.integer("intervalTicks", duels.intervalTicks, 1, kMaxIntervalTicks);
  visit.number("protectSeconds", duels.protectSeconds);
  visit.number("engageRadius", duels.engageRadius);
  visit.number("breakRadius", duels.breakRadius);
  visit.number("jockeyDistance", duels.jockeyDistance);
  visit.number("turnStep", duels.turnStep);
  visit.number("accelerateStep", duels.accelerateStep);
  visit.number("tackleReach", duels.tackleReach);
  visit.number("footRadius", duels.footRadius);
  visit.number("bodyRadius", duels.bodyRadius);
  visit.number("footError", duels.footError);
  visit.number("minHold", duels.minHold);
  visit.number("maxHold", duels.maxHold);
  visit.number("looseSpeed", duels.looseSpeed);
  visit.number("contactDistance", duels.contactDistance);
  visit.number("shovePerSecond", duels.shovePerSecond);
  visit.number("commitCost", duels.commitCost);
  visit.number("turnCost", duels.turnCost);
  visit.number("recoveryPerSecond", duels.recoveryPerSecond);
  visit.number("stumbleSeconds", duels.stumbleSeconds);
  visit.number("shieldWeight", duels.shieldWeight);
  visit.number("turnWeight", duels.turnWeight);
  visit.number("accelerateWeight", duels.accelerateWeight);
  visit.number("jockeyWeight", duels.jockeyWeight);
  visit.number("commitWeight", duels.commitWeight);
  visit.number("decisionTemperature", duels.decisionTemperature);
}

template <typename Visitor>
void fieldsOf(Visitor& visit, SimMatch::PressingConfig& pressing) {
  visit.integer("intervalTicks", pressing.intervalTicks, 1, kMaxIntervalTicks);
  visit.number("triggerRadius", pressing.triggerRadius);
  visit.number("recentSeconds", pressing.recentSeconds);
  visit.number("heavyTouchSpeed", pressing.heavyTouchSpeed);
  visit.number("slowPassSpeed", pressing.slowPassSpeed);
  visit.number("isolationRadius", pressing.isolationRadius);
  visit.number("facingOwnGoal", pressing.facingOwnGoal);
  visit.number("backPassMeters", pressing.backPassMeters);
  visit.integer("maxJoiners", pressing.maxJoiners, 1, 100);
  visit.number("phaseIntensity", pressing.phaseIntensity);
  visit.number("maxPressSeconds", pressing.maxPressSeconds);
}

template <typename Visitor>
void fieldsOf(Visitor& visit, SimMatch::RestartConfig& restarts) {
  visit.boolean("enabled", restarts.enabled);
  visit.number("maxHoldSeconds", restarts.maxHoldSeconds);
}

template <typename Visitor>
void fieldsOf(Visitor& visit, SimMatch::ShotConfig& shooting) {
  visit.number("spreadAtZero", shooting.spreadAtZero);
  visit.number("spreadPerMeter", shooting.spreadPerMeter);
  visit.number("speedError", shooting.speedError);
  visit.number("maxLaunchSlope", shooting.maxLaunchSlope);
  visit.number("topspin", shooting.topspin);
  visit.number("chipBackspin", shooting.chipBackspin);
  visit.number("spinError", shooting.spinError);
  visit.number("pressureErrorFactor", shooting.pressureErrorFactor);
  visit.number("balanceErrorFactor", shooting.balanceErrorFactor);
  visit.number("weakFootErrorFactor", shooting.weakFootErrorFactor);
  visit.number("unsettledErrorFactor", shooting.unsettledErrorFactor);
  visit.number("weakFootSide", shooting.weakFootSide);
  visit.number("unsettledSeconds", shooting.unsettledSeconds);
  visit.number("unsettledBallSpeed", shooting.unsettledBallSpeed);
  visit.number("deflectionSpeed", shooting.deflectionSpeed);
  visit.number("blockRadius", shooting.blockRadius);
  visit.number("blockReach", shooting.blockReach);
  visit.number("lungeRadius", shooting.lungeRadius);
  visit.number("lungeHeight", shooting.lungeHeight);
  visit.number("minDeflectedSpeed", shooting.minDeflectedSpeed);
  visit.number("maxDeflectedSpeed", shooting.maxDeflectedSpeed);
  visit.number("blockedBelow", shooting.blockedBelow);
  visit.number("deflectionSpread", shooting.deflectionSpread);
  visit.number("deflectionLift", shooting.deflectionLift);
}

template <typename Visitor>
void fieldsOf(Visitor& visit, SimMatch::WoodworkConfig& woodwork) {
  visit.number("radius", woodwork.radius);
  visit.number("restitution", woodwork.restitution);
}

template <typename Visitor>
void fieldsOf(Visitor& visit, SimMatch::GoalkeeperConfig& goalkeeper) {
  visit.number("lineDepth", goalkeeper.lineDepth);
  visit.number("highDepthShare", goalkeeper.highDepthShare);
  visit.number("possessionDepthShare", goalkeeper.possessionDepthShare);
  visit.number("positionErrorAlong", goalkeeper.positionErrorAlong);
  visit.number("positionErrorAcross", goalkeeper.positionErrorAcross);
  visit.number("cautiousMargin", goalkeeper.cautiousMargin);
  visit.number("boldMargin", goalkeeper.boldMargin);
  visit.number("sweepHysteresis", goalkeeper.sweepHysteresis);
  visit.number("misjudgement", goalkeeper.misjudgement);
}

template <typename Visitor>
void fieldsOf(Visitor& visit, SimMatch::ShotStoppingConfig& saves) {
  visit.number("slowestReaction", saves.slowestReaction);
  visit.number("quickestReaction", saves.quickestReaction);
  visit.number("readError", saves.readError);
  visit.number("readyHeight", saves.readyHeight);
  visit.number("diveReach", saves.diveReach);
  visit.number("jumpReach", saves.jumpReach);
  visit.number("diveSpeed", saves.diveSpeed);
  visit.number("bodyReach", saves.bodyReach);
  visit.number("catchableSpeed", saves.catchableSpeed);
  visit.number("minParrySpeed", saves.minParrySpeed);
  visit.number("maxParrySpeed", saves.maxParrySpeed);
  visit.number("parryTilt", saves.parryTilt);
  visit.number("parrySpread", saves.parrySpread);
  visit.number("carelessTryMargin", saves.carelessTryMargin);
  visit.number("carefulTryMargin", saves.carefulTryMargin);
  visit.number("wideMargin", saves.wideMargin);
  visit.number("diveBelowSpeed", saves.diveBelowSpeed);
  visit.number("standingRecovery", saves.standingRecovery);
  visit.number("stretchRecovery", saves.stretchRecovery);
  visit.number("reflexRecoveryShare", saves.reflexRecoveryShare);
}

template <typename Visitor>
void fieldsOf(Visitor& visit, SimMatch::AerialConfig& aerial) {
  visit.number("headHeight", aerial.headHeight);
  visit.number("headRadius", aerial.headRadius);
  visit.number("lowestJump", aerial.lowestJump);
  visit.number("highestJump", aerial.highestJump);
  visit.number("timingError", aerial.timingError);
  visit.number("attemptMargin", aerial.attemptMargin);
  visit.number("contestRadius", aerial.contestRadius);
  visit.number("landingSeconds", aerial.landingSeconds);
  visit.number("reachWeight", aerial.reachWeight);
  visit.number("reachCap", aerial.reachCap);
  visit.number("arrivalWeight", aerial.arrivalWeight);
  visit.number("strengthWeight", aerial.strengthWeight);
  visit.number("bodyWeight", aerial.bodyWeight);
  visit.number("keeperAdvantage", aerial.keeperAdvantage);
  visit.number("duelTemperature", aerial.duelTemperature);
  visit.number("contestedDrop", aerial.contestedDrop);
  visit.number("shotRange", aerial.shotRange);
  visit.number("shotSpeed", aerial.shotSpeed);
  visit.number("shotHeight", aerial.shotHeight);
  visit.number("shotInside", aerial.shotInside);
  visit.number("passRange", aerial.passRange);
  visit.number("knockDownRange", aerial.knockDownRange);
  visit.number("passSeconds", aerial.passSeconds);
  visit.number("openDistance", aerial.openDistance);
  visit.number("passWeight", aerial.passWeight);
  visit.number("clearanceZone", aerial.clearanceZone);
  visit.number("clearanceDistance", aerial.clearanceDistance);
  visit.number("maxHeaderSpeed", aerial.maxHeaderSpeed);
  visit.number("decisionTemperature", aerial.decisionTemperature);
  visit.number("directionError", aerial.directionError);
  visit.number("speedError", aerial.speedError);
  visit.number("mistimedErrorFactor", aerial.mistimedErrorFactor);
}

template <typename Visitor>
void fieldsOf(Visitor& visit, SimMatch::MatchClockConfig& clock) {
  visit.number("durationMinutes", clock.durationMinutes);
  visit.number("throwInLostSeconds", clock.throwInLostSeconds);
  visit.number("goalKickLostSeconds", clock.goalKickLostSeconds);
  visit.number("cornerLostSeconds", clock.cornerLostSeconds);
  visit.number("kickoffLostSeconds", clock.kickoffLostSeconds);
  visit.number("freeKickLostSeconds", clock.freeKickLostSeconds);
  visit.number("penaltyLostSeconds", clock.penaltyLostSeconds);
  visit.number("cardLostSeconds", clock.cardLostSeconds);
}

template <typename Visitor>
void fieldsOf(Visitor& visit, SimMatch::FitnessConfig& fitness) {
  visit.number("sprintSpeedShare", fitness.sprintSpeedShare);
  visit.number("sprintSeconds", fitness.sprintSeconds);
  visit.number("recoverySeconds", fitness.recoverySeconds);
  visit.number("fullMatchSpeedLoss", fitness.fullMatchSpeedLoss);
}

template <typename Visitor>
void fieldsOf(Visitor& visit, SimMatch::SetPieceConfig& setPieces) {
  visit.number("throwInSeconds", setPieces.throwInSeconds);
  visit.number("cornerSeconds", setPieces.cornerSeconds);
  visit.number("goalKickSeconds", setPieces.goalKickSeconds);
  visit.number("kickoffSeconds", setPieces.kickoffSeconds);
  visit.number("placeRadius", setPieces.placeRadius);
  visit.number("waitSeconds", setPieces.waitSeconds);
  visit.number("throwInDistance", setPieces.throwInDistance);
  visit.number("freeKickSeconds", setPieces.freeKickSeconds);
  visit.number("penaltySeconds", setPieces.penaltySeconds);
  visit.number("freeKickRange", setPieces.freeKickRange);
  visit.integer("wallPlayers", setPieces.wallPlayers, 0, 10);
  visit.number("wallSpacing", setPieces.wallSpacing);
  visit.number("readyRadius", setPieces.readyRadius);
  visit.number("flightSeconds", setPieces.flightSeconds);
  visit.number("markDistance", setPieces.markDistance);
  visit.integer("cornerRunners", setPieces.cornerRunners, 0, 10);
  visit.integer("shortOptions", setPieces.shortOptions, 0, 10);
}

template <typename Visitor>
void fieldsOf(Visitor& visit, SimMatch::FoulConfig& fouls) {
  visit.boolean("enabled", fouls.enabled);
  visit.number("fromBehindDot", fouls.fromBehindDot);
  visit.number("denyingDistance", fouls.denyingDistance);
  visit.number("advantageDepth", fouls.advantageDepth);
}

template <typename Visitor>
void fieldsOf(Visitor& visit, SimMatch::CoachingConfig& coaching) {
  visit.number("instructionDelaySeconds", coaching.instructionDelaySeconds);
  visit.integer("maxSubstitutions", coaching.maxSubstitutions, 0, SimMatch::kMaxSubstitutions);
}

template <typename Visitor>
void fieldsOf(Visitor& visit, SimMatch::OffsideConfig& offside) {
  visit.boolean("enabled", offside.enabled);
  visit.number("levelMeters", offside.levelMeters);
}

template <typename Visitor>
void fieldsOf(Visitor& visit, MatchConfig& config) {
  visit.integer("ticksPerSecond", config.ticksPerSecond, 1, kMaxIntervalTicks);
  visit.object("ball", config.ball);
  visit.object("perception", config.perception);
  visit.object("passing", config.passing);
  visit.object("reception", config.reception);
  visit.object("pursuit", config.pursuit);
  visit.object("decisions", config.decisions);
  visit.object("phases", config.phases);
  visit.object("pitchControl", config.pitchControl);
  visit.object("positioning", config.positioning);
  visit.object("offBall", config.offBall);
  visit.object("defensive", config.defensive);
  visit.object("duels", config.duels);
  visit.object("pressing", config.pressing);
  visit.object("restarts", config.restarts);
  visit.object("shooting", config.shooting);
  visit.object("woodwork", config.woodwork);
  visit.object("goalkeeper", config.goalkeeper);
  visit.object("shotStopping", config.shotStopping);
  visit.object("aerial", config.aerial);
  visit.object("clock", config.clock);
  visit.object("fitness", config.fitness);
  visit.object("offside", config.offside);
  visit.object("fouls", config.fouls);
  visit.object("setPieces", config.setPieces);
  visit.object("coaching", config.coaching);
}

// Writes the fields it visits into a JSON object.
class ConfigWriter {
 public:
  void number(const std::string_view key, const double value) { json_[std::string(key)] = value; }

  void boolean(const std::string_view key, const bool value) { json_[std::string(key)] = value; }

  void integer(const std::string_view key, const int value, std::int64_t /*min*/,
               std::int64_t /*max*/) {
    json_[std::string(key)] = value;
  }

  template <typename Config>
  void object(const std::string_view key, Config& config) {
    ConfigWriter nested;
    fieldsOf(nested, config);
    json_[std::string(key)] = std::move(nested.json_);
  }

  [[nodiscard]] Json take() && { return std::move(json_); }

 private:
  Json json_ = Json::object();
};

// Reads the fields it visits from a JSON object; each must be there.
class ConfigReader {
 public:
  explicit ConfigReader(Field field) : field_(std::move(field)) {}

  void number(const std::string_view key, double& value) const {
    value = field_.member(key).number();
  }

  void boolean(const std::string_view key, bool& value) const {
    value = field_.member(key).boolean();
  }

  void integer(const std::string_view key, int& value, const std::int64_t min,
               const std::int64_t max) const {
    value = static_cast<int>(field_.member(key).integerIn(min, max));
  }

  template <typename Config>
  void object(const std::string_view key, Config& config) const {
    ConfigReader nested(field_.member(key));
    fieldsOf(nested, config);
  }

 private:
  Field field_;
};

[[nodiscard]] Json configJson(MatchConfig config) {
  ConfigWriter writer;
  fieldsOf(writer, config);
  return std::move(writer).take();
}

[[nodiscard]] MatchConfig readConfig(const Field& field) {
  MatchConfig config;
  ConfigReader reader(field);
  fieldsOf(reader, config);
  return config;
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
  if (type == "changeTactic") {
    const TeamSide side = readSide(field.member("side"));
    auto tactic = readTactic(field.member("tactic"));
    if (!tactic) {
      field.member("tactic").fail("a tactic change needs a tactic");
    }
    return SimMatch::ChangeTacticCommand{.side = side, .tactic = *std::move(tactic)};
  }
  if (type == "substitute") {
    return SimMatch::SubstituteCommand{.side = readSide(field.member("side")),
                                       .off = readPlayerId(field.member("off")),
                                       .on = readPlayerId(field.member("on")),
                                       .attributes = readAttributes(field.member("attributes"))};
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

[[nodiscard]] std::vector<ReplayCheckpoint> readCheckpoints(const Field& field) {
  std::vector<ReplayCheckpoint> checkpoints;
  for (const Field& entry : field.elements()) {
    checkpoints.push_back({.tick = SimTick(entry.member("tick").integerIn(0, kMaxTick)),
                           .stateHash = readHash(entry.member("stateHash")),
                           .eventHash = readHash(entry.member("eventHash"))});
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
  if (version > 1 && version < kReplaySchemaVersion) {
    schema.fail(std::format("schema version {} is no longer supported, expected {}; record the "
                            "scenario again with the same seed",
                            version, kReplaySchemaVersion),
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
          .checkpointIntervalTicks = static_cast<int>(
              root.member("checkpointIntervalTicks").integerIn(1, std::numeric_limits<int>::max())),
          .checkpoints = readCheckpoints(root.member("checkpoints"))};
}

}  // namespace

std::string toReplayJson(const Replay& replay) {
  Json json;
  json["schemaVersion"] = kReplaySchemaVersion;
  json["coreVersion"] = replay.coreVersion;
  json["createdAt"] = replay.createdAt;
  // A decimal string, never a JSON number: see docs/tooling.md#replay-format.
  json["seed"] = std::to_string(replay.setup.seed);
  json["gameTime"] = replay.finalTick.value();
  json["config"] = configJson(replay.setup.config);
  json["initialState"] = stateJson(replay.setup.initialState);
  json["commands"] = commandsJson(replay.setup.commands);
  json["checkpointIntervalTicks"] = replay.checkpointIntervalTicks;
  json["checkpoints"] = Json::array();
  for (const ReplayCheckpoint& checkpoint : replay.checkpoints) {
    json["checkpoints"].push_back({{"tick", checkpoint.tick.value()},
                                   {"stateHash", hashText(checkpoint.stateHash)},
                                   {"eventHash", hashText(checkpoint.eventHash)}});
  }
  return json.dump(2) + "\n";
}

std::expected<Replay, ReplayError> parseReplayJson(const std::string_view json) {
  const Json document = Json::parse(json, nullptr, false);
  if (document.is_discarded()) {
    return std::unexpected(ReplayError{.code = ReplayErrorCode::kMalformed,
                                       .message = "not a valid JSON document",
                                       .divergence = std::nullopt});
  }
  if (!document.is_object()) {
    return std::unexpected(ReplayError{.code = ReplayErrorCode::kMalformed,
                                       .message = "expected a JSON object at the top level",
                                       .divergence = std::nullopt});
  }
  try {
    return readReplay(Field(document, ""));
  } catch (const FormatError& error) {
    return std::unexpected(
        ReplayError{.code = error.code(), .message = error.what(), .divergence = std::nullopt});
  }
}

std::expected<void, ReplayError> saveReplay(const Replay& replay,
                                            const std::filesystem::path& path) {
  std::ofstream out(path, std::ios::binary);
  out << toReplayJson(replay);
  out.close();
  if (!out) {
    return std::unexpected(ReplayError{.code = ReplayErrorCode::kIoError,
                                       .message = "cannot write " + path.string(),
                                       .divergence = std::nullopt});
  }
  return {};
}

std::expected<Replay, ReplayError> loadReplay(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return std::unexpected(ReplayError{.code = ReplayErrorCode::kIoError,
                                       .message = "cannot read " + path.string(),
                                       .divergence = std::nullopt});
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
