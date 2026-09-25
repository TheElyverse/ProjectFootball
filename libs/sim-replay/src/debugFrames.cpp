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
#include "zones.hpp"

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

[[nodiscard]] Json regionJson(const SimMatch::DesiredRegion& region) {
  return {{"tacticalTarget", vec2Json(region.tacticalTarget)},
          {"center", vec2Json(region.center)},
          {"cost", rounded(region.cost.total)}};
}

// A player's decided action: his assignment.
[[nodiscard]] Json actionJson(const SimMatch::PlayerAction& action) {
  return {{"type", SimMatch::actionName(action.type)},
          {"target", vec2Json(action.target)},
          {"subject", idJson(action.subject)}};
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
  const SimMatch::PlayerTacticalState& tactical = state.tactical(index);
  json["region"] = tactical.region ? regionJson(*tactical.region) : Json(nullptr);
  json["action"] = tactical.action ? actionJson(*tactical.action) : Json(nullptr);
  return json;
}

// Each team's phase and shape: its lines, compactness and width.
[[nodiscard]] Json teamsJson(const MatchState& state) {
  Json json = Json::array();
  for (const SimMatch::TeamSide side : {SimMatch::TeamSide::kHome, SimMatch::TeamSide::kAway}) {
    Json team;
    team["side"] = SimMatch::teamSideName(side);
    const auto& phase = state.phase(side);
    team["phase"] = phase ? Json(SimTactics::phaseName(phase->phase)) : Json(nullptr);
    const auto shape = SimMatch::measureTeamShape(state, side);
    team["shape"] = shape ? Json{{"defensiveLine", rounded(shape->defensiveLine)},
                                 {"midfieldLine", rounded(shape->midfieldLine)},
                                 {"frontLine", rounded(shape->frontLine)},
                                 {"length", rounded(shape->length)},
                                 {"width", rounded(shape->width)},
                                 {"centroid", vec2Json(shape->centroid)}}
                          : Json(nullptr);
    json.push_back(std::move(team));
  }
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
  json["position"] = vec2Json(event.position);
}

void addEventFields(Json& json, const SimMatch::LooseBallRecovered& event) {
  json["type"] = "looseBallRecovered";
  json["player"] = event.player.value();
  json["position"] = vec2Json(event.position);
}

void addEventFields(Json& json, const SimMatch::PossessionChanged& event) {
  json["type"] = "possessionChanged";
  json["previousOwner"] = idJson(event.previousOwner);
  json["newOwner"] = idJson(event.newOwner);
}

void addEventFields(Json& json, const SimMatch::PhaseChanged& event) {
  json["type"] = "phaseChanged";
  json["side"] = SimMatch::teamSideName(event.side);
  json["previous"] = event.previous ? Json(SimTactics::phaseName(*event.previous)) : Json(nullptr);
  json["phase"] = SimTactics::phaseName(event.phase);
}

void addEventFields(Json& json, const SimMatch::BallWon& event) {
  json["type"] = "ballWon";
  json["winner"] = event.winner.value();
  json["loser"] = event.loser.value();
  json["position"] = vec2Json(event.position);
}

void addEventFields(Json& json, const SimMatch::PressingStarted& event) {
  json["type"] = "pressingStarted";
  json["side"] = SimMatch::teamSideName(event.side);
  json["carrier"] = event.carrier.value();
  json["trigger"] =
      event.trigger ? Json(SimTactics::pressingTriggerName(*event.trigger)) : Json(nullptr);
  json["assignments"] = Json::array();
  for (const SimMatch::PressAssignment& assignment : event.assignments) {
    json["assignments"].push_back({{"player", assignment.player.value()},
                                   {"role", SimMatch::pressRoleName(assignment.role)},
                                   {"subject", assignment.subject.value()}});
  }
}

void addEventFields(Json& json, const SimMatch::PressingEnded& event) {
  json["type"] = "pressingEnded";
  json["side"] = SimMatch::teamSideName(event.side);
  json["outcome"] = SimMatch::pressOutcomeName(event.outcome);
}

void addEventFields(Json& json, const SimMatch::TacticChanged& event) {
  json["type"] = "tacticChanged";
  json["side"] = SimMatch::teamSideName(event.side);
  json["tactic"] = event.tactic;
  json["contentHash"] = std::format("{:016x}", event.contentHash);
}

void addEventFields(Json& json, const SimMatch::PitchControlSampled& event) {
  json["type"] = "pitchControlSampled";
  json["homeShare"] = rounded(event.homeShare);
  json["ball"] = vec2Json(event.ball);
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

[[nodiscard]] Json actionCandidateJson(const SimMatch::ActionCandidate& candidate) {
  const SimMatch::ActionScores& scores = candidate.scores;
  Json json;
  json["type"] = SimMatch::actionName(candidate.type);
  json["target"] = vec2Json(candidate.target);
  json["subject"] = idJson(candidate.subject);
  json["scores"] = {{"responsibility", rounded(scores.responsibility)},
                    {"region", rounded(scores.region)},
                    {"space", rounded(scores.space)},
                    {"lane", rounded(scores.lane)},
                    {"urgency", rounded(scores.urgency)},
                    {"effort", rounded(scores.effort)}};
  json["utility"] = rounded(candidate.utility);
  json["dominant"] = SimMatch::dominantScore(scores);
  return json;
}

[[nodiscard]] Json actionDecisionJson(const SimMatch::ActionDiagnostic& decision) {
  Json json;
  json["tick"] = decision.tick.value();
  json["player"] = decision.player.value();
  json["chosen"] = decision.chosen ? Json(*decision.chosen) : Json(nullptr);
  json["assigned"] = decision.assigned;
  json["candidates"] = Json::array();
  for (const SimMatch::ActionCandidate& candidate : decision.candidates) {
    json["candidates"].push_back(actionCandidateJson(candidate));
  }
  return json;
}

[[nodiscard]] Json frameJson(const DebugFrame& frame) {
  Json json;
  json["tick"] = frame.tick.value();
  json["stateHash"] = std::format("{:016x}", SimMatch::hashMatchState(frame.state));
  json["ball"] = ballJson(frame.state.ball());
  json["pendingPass"] = pendingPassJson(frame.state.pendingPass());
  json["teams"] = teamsJson(frame.state);
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
  json["actions"] = Json::array();
  for (const SimMatch::ActionDiagnostic& action : frame.actions) {
    json["actions"].push_back(actionDecisionJson(action));
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
  recording_.frames.push_back(DebugFrame{.state = setup.initialState,
                                         .tick = SimCore::SimTick(0),
                                         .events = {},
                                         .decisions = {},
                                         .actions = {}});
}

void DebugFrameRecorder::recordStep(const SimMatch::MatchSimulation& simulation) {
  recording_.frames.push_back(DebugFrame{
      .state = simulation.state(),
      .tick = simulation.tick(),
      .events = {simulation.events().begin(), simulation.events().end()},
      .decisions = {simulation.diagnostics().begin(), simulation.diagnostics().end()},
      .actions = {simulation.actionDiagnostics().begin(), simulation.actionDiagnostics().end()}});
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
  // Closed explicitly: the stream buffers, so the last write -- onto a full
  // disk, say -- may only fail while flushing on close.
  file.close();
  if (!file) {
    return std::unexpected(path.string() + ": write failed");
  }
  return {};
}

}  // namespace ElyverseFootball::SimReplay
