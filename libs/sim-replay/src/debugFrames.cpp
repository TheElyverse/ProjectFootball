#include "debugFrames.hpp"

#include <cmath>
#include <cstddef>
#include <format>
#include <fstream>
#include <nlohmann/json.hpp>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include "matchStateHash.hpp"
#include "observation.hpp"
#include "passCandidate.hpp"
#include "version.hpp"

namespace ElyverseFootball::SimReplay {
namespace {

using Json = nlohmann::ordered_json;
using SimCore::PlayerId;
using SimCore::Vec2;
using SimMatch::MatchState;

// Millimeters are plenty for looking at a match, and three decimals keep the
// file a fraction of its full-precision size.
[[nodiscard]] double rounded(const double value) noexcept {
  constexpr double kScale = 1000.0;
  return std::round(value * kScale) / kScale;
}

// [x, y]: frames hold thousands of vectors, so they are arrays, not objects.
[[nodiscard]] Json vec2Json(const Vec2 vector) {
  return Json::array({rounded(vector.x), rounded(vector.y)});
}

[[nodiscard]] Json idJson(const std::optional<PlayerId>& playerId) {
  return playerId ? Json(playerId->value()) : Json(nullptr);
}

[[nodiscard]] Json observationJson(const SimMatch::Observation& observation) {
  Json json;
  json["entity"] =
      observation.entity.isBall() ? Json("ball") : Json(observation.entity.playerId().value());
  json["position"] = vec2Json(observation.position);
  json["velocity"] = vec2Json(observation.velocity);
  json["confidence"] = rounded(observation.confidence);
  json["lastSeen"] = observation.lastSeen.value();
  return json;
}

[[nodiscard]] Json observationsJson(const std::span<const SimMatch::Observation> observations) {
  Json json = Json::array();
  for (const SimMatch::Observation& observation : observations) {
    json.push_back(observationJson(observation));
  }
  return json;
}

[[nodiscard]] Json playerJson(const MatchState& state, const std::size_t index) {
  const SimMatch::PlayerMatchState& player = state.players()[index];
  Json json;
  json["id"] = player.playerId.value();
  json["position"] = vec2Json(player.position);
  json["velocity"] = vec2Json(player.velocity);
  json["facing"] = vec2Json(player.facing);
  json["target"] = player.target ? vec2Json(*player.target) : Json(nullptr);
  json["observations"] = observationsJson(state.perception(index).observations);
  return json;
}

[[nodiscard]] Json ballJson(const SimMatch::BallState& ball) {
  Json json;
  json["position"] = vec2Json(ball.position);
  json["velocity"] = vec2Json(ball.velocity);
  json["owner"] = idJson(ball.owner);
  json["lastTouch"] = ball.lastTouch ? Json(ball.lastTouch->playerId.value()) : Json(nullptr);
  return json;
}

[[nodiscard]] Json pendingPassJson(const std::optional<SimMatch::PassIntent>& intent) {
  if (!intent) {
    return nullptr;
  }
  Json json;
  json["passer"] = intent->passer.value();
  json["target"] = vec2Json(intent->target);
  json["speed"] = rounded(intent->speed);
  json["receiver"] = idJson(intent->receiver);
  return json;
}

void addEventFields(Json& json, const SimMatch::PassAttempted& event) {
  json["type"] = "passAttempted";
  json["passer"] = event.passer.value();
  json["intendedReceiver"] = idJson(event.intendedReceiver);
  json["from"] = vec2Json(event.from);
  json["target"] = vec2Json(event.target);
  json["speed"] = rounded(event.speed);
}

void addEventFields(Json& json, const SimMatch::PassReceived& event) {
  json["type"] = "passReceived";
  json["receiver"] = event.receiver.value();
  json["passer"] = event.passer.value();
}

void addEventFields(Json& json, const SimMatch::PassIntercepted& event) {
  json["type"] = "passIntercepted";
  json["interceptor"] = event.interceptor.value();
  json["passer"] = event.passer.value();
}

void addEventFields(Json& json, const SimMatch::LooseBallRecovered& event) {
  json["type"] = "looseBallRecovered";
  json["player"] = event.player.value();
}

void addEventFields(Json& json, const SimMatch::PossessionChanged& event) {
  json["type"] = "possessionChanged";
  json["previousOwner"] = idJson(event.previousOwner);
  json["newOwner"] = idJson(event.newOwner);
}

[[nodiscard]] Json eventJson(const SimMatch::MatchEvent& event) {
  Json json;
  // The step's tick, one before the frame's: the event happened during the
  // step that led to the frame.
  json["tick"] = SimMatch::eventTick(event).value();
  std::visit([&json](const auto& alternative) { addEventFields(json, alternative); }, event);
  return json;
}

[[nodiscard]] Json candidateJson(const SimMatch::PassCandidate& candidate) {
  Json json;
  json["receiver"] = candidate.receiver.value();
  json["target"] = vec2Json(candidate.target);
  json["distance"] = rounded(candidate.distance);
  json["speed"] = rounded(candidate.speed);
  json["receiverConfidence"] = rounded(candidate.receiverConfidence);
  json["interceptionRisk"] = rounded(candidate.interceptionRisk);
  json["completion"] = rounded(candidate.completion);
  json["progression"] = rounded(candidate.progression);
  json["receiverPressure"] = rounded(candidate.receiverPressure);
  json["utility"] = rounded(candidate.utility);
  json["rejection"] = SimMatch::passRejectionName(candidate.rejection);
  return json;
}

[[nodiscard]] std::string_view outcomeName(const SimMatch::DecisionOutcome outcome) noexcept {
  switch (outcome) {
    case SimMatch::DecisionOutcome::kPassed:
      return "passed";
    case SimMatch::DecisionOutcome::kNoValidOption:
      return "noValidOption";
  }
  return "unknown";
}

[[nodiscard]] Json decisionJson(const SimMatch::DecisionDiagnostic& decision) {
  Json json;
  json["tick"] = decision.tick.value();
  json["player"] = decision.player.value();
  json["outcome"] = outcomeName(decision.outcome);
  json["chosen"] = decision.chosen ? Json(*decision.chosen) : Json(nullptr);
  json["candidates"] = Json::array();
  for (const SimMatch::PassCandidate& candidate : decision.candidates) {
    json["candidates"].push_back(candidateJson(candidate));
  }
  json["observations"] = observationsJson(decision.observations);
  return json;
}

[[nodiscard]] Json frameJson(const DebugFrame& frame) {
  Json json;
  json["tick"] = frame.tick.value();
  json["stateHash"] = std::format("{:016x}", SimMatch::hashMatchState(frame.state));
  json["ball"] = ballJson(frame.state.ball());
  json["pendingPass"] = pendingPassJson(frame.state.pendingPass());
  json["players"] = Json::array();
  for (std::size_t index = 0; index < frame.state.players().size(); ++index) {
    json["players"].push_back(playerJson(frame.state, index));
  }
  json["events"] = Json::array();
  for (const SimMatch::MatchEvent& event : frame.events) {
    json["events"].push_back(eventJson(event));
  }
  json["decisions"] = Json::array();
  for (const SimMatch::DecisionDiagnostic& decision : frame.decisions) {
    json["decisions"].push_back(decisionJson(decision));
  }
  return json;
}

}  // namespace

DebugFrameRecorder::DebugFrameRecorder(const SimMatch::MatchSetup& setup, std::string scenario)
    : recording_{.coreVersion = std::string(SimCore::coreVersion()),
                 .scenario = std::move(scenario),
                 .seed = setup.seed,
                 .config = setup.config,
                 .frames = {}} {
  recording_.frames.push_back(DebugFrame{
      .state = setup.initialState, .tick = SimCore::SimTick(0), .events = {}, .decisions = {}});
}

void DebugFrameRecorder::recordStep(const SimMatch::MatchSimulation& simulation) {
  recording_.frames.push_back(
      DebugFrame{.state = simulation.state(),
                 .tick = simulation.tick(),
                 .events = {simulation.events().begin(), simulation.events().end()},
                 .decisions = {simulation.diagnostics().begin(), simulation.diagnostics().end()}});
}

std::string toDebugFramesJson(const DebugRecording& recording) {
  const SimMatch::MatchConfig& config = recording.config;
  Json json;
  json["format"] = "elyverse-debug-frames";
  json["version"] = kDebugFramesVersion;
  json["coreVersion"] = recording.coreVersion;
  json["scenario"] = recording.scenario;
  // A string: JavaScript numbers cannot hold every 64-bit seed.
  json["seed"] = std::to_string(recording.seed);
  json["ticksPerSecond"] = config.ticksPerSecond;
  json["perception"] = {{"viewDistance", config.perception.viewDistance},
                        {"fieldOfViewDegrees", config.perception.fieldOfViewDegrees},
                        {"awarenessRadius", config.perception.awarenessRadius}};
  json["reception"] = {{"controlRadius", config.reception.controlRadius}};

  // The pitch and the squad never change during a match: written once.
  json["pitch"] = Json::object();
  json["players"] = Json::array();
  if (!recording.frames.empty()) {
    const MatchState& state = recording.frames.front().state;
    json["pitch"] = {{"length", state.pitch().lengthMeters()},
                     {"width", state.pitch().widthMeters()}};
    for (const SimMatch::PlayerMatchState& player : state.players()) {
      json["players"].push_back(
          {{"id", player.playerId.value()}, {"side", SimMatch::teamSideName(player.side)}});
    }
  }
  json["frames"] = Json::array();
  for (const DebugFrame& frame : recording.frames) {
    json["frames"].push_back(frameJson(frame));
  }
  return json.dump();
}

std::expected<void, std::string> saveDebugFrames(const DebugRecording& recording,
                                                 const std::filesystem::path& path) {
  std::ofstream file(path, std::ios::binary);
  if (!file) {
    return std::unexpected(path.string() + ": cannot open for writing");
  }
  file << toDebugFramesJson(recording) << '\n';
  if (!file) {
    return std::unexpected(path.string() + ": write failed");
  }
  return {};
}

}  // namespace ElyverseFootball::SimReplay
