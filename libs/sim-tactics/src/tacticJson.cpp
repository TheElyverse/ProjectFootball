#include "tacticJson.hpp"

#include <algorithm>
#include <format>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <nlohmann/json.hpp>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

#include "rolePreset.hpp"

namespace ElyverseFootball::SimTactics {
namespace {

// ordered_json keeps keys in insertion order, so written files read in the
// order docs/tactic-format.md lists them. Key order is not part of the format.
using Json = nlohmann::ordered_json;

// ---------------------------------------------------------------------------
// Writing

[[nodiscard]] Json responsibilitiesJson(const std::vector<SlotResponsibility>& entries) {
  Json json = Json::array();
  for (const SlotResponsibility& entry : entries) {
    json.push_back(
        {{"responsibility", responsibilityName(entry.responsibility)}, {"weight", entry.weight}});
  }
  return json;
}

[[nodiscard]] Json instructionJson(const PhaseInstruction& instruction) {
  return {{"lineHeight", instruction.lineHeight},
          {"blockLength", instruction.blockLength},
          {"blockWidth", instruction.blockWidth},
          {"ballShift", instruction.ballShift},
          {"pressingIntensity", instruction.pressingIntensity},
          {"passingRisk", instruction.passingRisk},
          {"runFrequency", instruction.runFrequency}};
}

[[nodiscard]] Json principlesJson(const TeamPrinciples& principles) {
  Json triggers = Json::array();
  for (const PressingTrigger trigger : principles.pressingTriggers) {
    triggers.push_back(pressingTriggerName(trigger));
  }
  const PositioningWeights& weights = principles.positioning;
  return {{"pressingLine", principles.pressingLine},
          {"pressingTriggers", std::move(triggers)},
          {"positioning",
           {{"targetDistance", weights.targetDistance},
            {"spacing", weights.spacing},
            {"pressure", weights.pressure},
            {"occupancy", weights.occupancy},
            {"transitionRisk", weights.transitionRisk}}}};
}

// ---------------------------------------------------------------------------
// Reading

// Thrown inside this file only, and turned into a TacticFileError at the edge.
class FormatError : public std::runtime_error {
 public:
  FormatError(const TacticFileErrorCode code, const std::string& message)
      : std::runtime_error(message), code_(code) {}

  [[nodiscard]] TacticFileErrorCode code() const noexcept { return code_; }

 private:
  TacticFileErrorCode code_;
};

// A JSON value and the path that leads to it, so every error names its field.
class Field {
 public:
  Field(const Json& value, std::string path) : value_(&value), path_(std::move(path)) {}

  [[noreturn]] void fail(const std::string_view problem,
                         const TacticFileErrorCode code = TacticFileErrorCode::kMalformed) const {
    throw FormatError(code,
                      path_.empty() ? std::string(problem) : std::format("{}: {}", path_, problem));
  }

  // Rejects keys outside `known`: a misspelled key would otherwise be
  // silently ignored and its value replaced by nothing at all.
  void expectOnly(const std::initializer_list<std::string_view> known) const {
    requireObject();
    for (const auto& [key, value] : value_->items()) {
      if (std::ranges::find(known, key) == known.end()) {
        throw FormatError(TacticFileErrorCode::kMalformed,
                          std::format("{}: unknown field", childPath(key)));
      }
    }
  }

  [[nodiscard]] bool has(const std::string_view key) const {
    requireObject();
    return value_->contains(key);
  }

  [[nodiscard]] Field member(const std::string_view key) const {
    requireObject();
    const auto found = value_->find(key);
    if (found == value_->end()) {
      throw FormatError(TacticFileErrorCode::kMalformed,
                        std::format("{}: missing", childPath(key)));
    }
    return {*found, childPath(key)};
  }

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

  [[nodiscard]] std::string string() const {
    if (!value_->is_string()) {
      fail("expected a string");
    }
    return value_->get<std::string>();
  }

  [[nodiscard]] const Json& json() const noexcept { return *value_; }

 private:
  void requireObject() const {
    if (!value_->is_object()) {
      fail("expected an object");
    }
  }

  [[nodiscard]] std::string childPath(const std::string_view key) const {
    return path_.empty() ? std::string(key) : std::format("{}.{}", path_, key);
  }

  const Json* value_;
  std::string path_;
};

template <typename Enum>
[[nodiscard]] Enum readName(const Field& field, std::optional<Enum> (*parse)(std::string_view),
                            const std::string_view what) {
  const std::string name = field.string();
  const std::optional<Enum> value = parse(name);
  if (!value) {
    field.fail(std::format("unknown {} \"{}\"", what, name));
  }
  return *value;
}

[[nodiscard]] std::vector<SlotResponsibility> readResponsibilities(const Field& field) {
  std::vector<SlotResponsibility> entries;
  for (const Field& entry : field.elements()) {
    entry.expectOnly({"responsibility", "weight"});
    entries.push_back({.responsibility = readName<Responsibility>(
                           entry.member("responsibility"), &parseResponsibility, "responsibility"),
                       .weight = entry.member("weight").number()});
  }
  return entries;
}

// A slot names its duties either through a role preset or as a list, not
// both: a preset plus extra duties would hide what the slot really holds.
[[nodiscard]] TacticSlot readSlot(const Field& field) {
  field.expectOnly({"position", "role", "responsibilities"});
  const Field position = field.member("position");
  position.expectOnly({"depth", "width"});
  TacticSlot slot{.position = {.depth = position.member("depth").number(),
                               .width = position.member("width").number()},
                  .responsibilities = {}};
  const bool hasRole = field.has("role");
  if (hasRole == field.has("responsibilities")) {
    field.fail(R"(expected either "role" or "responsibilities")");
  }
  slot.responsibilities = hasRole ? presetResponsibilities(readName<RolePreset>(
                                        field.member("role"), &parseRolePreset, "role"))
                                  : readResponsibilities(field.member("responsibilities"));
  return slot;
}

[[nodiscard]] TeamPrinciples readPrinciples(const Field& field) {
  field.expectOnly({"pressingLine", "pressingTriggers", "positioning"});
  TeamPrinciples principles{.pressingLine = field.member("pressingLine").number(),
                            .pressingTriggers = {},
                            .positioning = {}};
  for (const Field& trigger : field.member("pressingTriggers").elements()) {
    principles.pressingTriggers.push_back(
        readName<PressingTrigger>(trigger, &parsePressingTrigger, "pressing trigger"));
  }
  const Field weights = field.member("positioning");
  weights.expectOnly({"targetDistance", "spacing", "pressure", "occupancy", "transitionRisk"});
  principles.positioning = {.targetDistance = weights.member("targetDistance").number(),
                            .spacing = weights.member("spacing").number(),
                            .pressure = weights.member("pressure").number(),
                            .occupancy = weights.member("occupancy").number(),
                            .transitionRisk = weights.member("transitionRisk").number()};
  return principles;
}

[[nodiscard]] PhaseInstruction readInstruction(const Field& field) {
  field.expectOnly({"lineHeight", "blockLength", "blockWidth", "ballShift", "pressingIntensity",
                    "passingRisk", "runFrequency"});
  return {.lineHeight = field.member("lineHeight").number(),
          .blockLength = field.member("blockLength").number(),
          .blockWidth = field.member("blockWidth").number(),
          .ballShift = field.member("ballShift").number(),
          .pressingIntensity = field.member("pressingIntensity").number(),
          .passingRisk = field.member("passingRisk").number(),
          .runFrequency = field.member("runFrequency").number()};
}

void checkFormat(const Field& root) {
  const Field format = root.member("format");
  if (format.string() != kTacticFormat) {
    format.fail(std::format(R"(expected "{}", got "{}")", kTacticFormat, format.string()));
  }
  const Field version = root.member("version");
  if (!version.json().is_number_integer() ||
      version.json().get<std::int64_t>() != kTacticFormatVersion) {
    version.fail(std::format("unsupported version {}, expected {}", version.json().dump(),
                             kTacticFormatVersion),
                 TacticFileErrorCode::kUnsupportedVersion);
  }
}

[[nodiscard]] TacticSpec readSpec(const Field& root) {
  checkFormat(root);
  root.expectOnly({"format", "version", "name", "description", "slots", "principles", "phases"});
  TacticSpec spec{
      .name = root.member("name").string(),
      .description = root.has("description") ? root.member("description").string() : std::string(),
      .slots = {},
      .principles = readPrinciples(root.member("principles")),
      .phases = {}};
  for (const Field& slot : root.member("slots").elements()) {
    spec.slots.push_back(readSlot(slot));
  }
  const Field phases = root.member("phases");
  phases.expectOnly({phaseName(TacticalPhase::kBuildUp), phaseName(TacticalPhase::kProgression),
                     phaseName(TacticalPhase::kFinalThird),
                     phaseName(TacticalPhase::kAttackingTransition),
                     phaseName(TacticalPhase::kDefensiveBlock), phaseName(TacticalPhase::kPressing),
                     phaseName(TacticalPhase::kDefensiveTransition)});
  for (const TacticalPhase phase : kAllPhases) {
    spec.phases.at(phaseIndex(phase)) = readInstruction(phases.member(phaseName(phase)));
  }
  return spec;
}

[[nodiscard]] std::unexpected<TacticFileError> fail(const TacticFileErrorCode code,
                                                    const std::string_view source,
                                                    const std::string_view problem) {
  return std::unexpected(
      TacticFileError{.code = code, .message = std::format("{}: {}", source, problem)});
}

}  // namespace

std::string toTacticJson(const Tactic& tactic) {
  Json json;
  json["format"] = kTacticFormat;
  json["version"] = kTacticFormatVersion;
  json["name"] = tactic.name();
  json["description"] = tactic.description();
  json["slots"] = Json::array();
  for (const TacticSlot& slot : tactic.slots()) {
    json["slots"].push_back(
        {{"position", {{"depth", slot.position.depth}, {"width", slot.position.width}}},
         {"responsibilities", responsibilitiesJson(slot.responsibilities)}});
  }
  json["principles"] = principlesJson(tactic.principles());
  json["phases"] = Json::object();
  for (const TacticalPhase phase : kAllPhases) {
    json["phases"][phaseName(phase)] = instructionJson(tactic.instruction(phase));
  }
  return json.dump(2) + "\n";
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) -- text and its name, as documented.
std::expected<Tactic, TacticFileError> parseTacticJson(const std::string_view json,
                                                       const std::string_view source) {
  const Json document = Json::parse(json, nullptr, false);
  if (document.is_discarded()) {
    return fail(TacticFileErrorCode::kMalformed, source, "not a valid JSON document");
  }
  std::optional<TacticSpec> spec;
  try {
    spec = readSpec(Field(document, ""));
  } catch (const FormatError& error) {
    return fail(error.code(), source, error.what());
  }
  auto tactic = Tactic::create(*std::move(spec));
  if (!tactic) {
    std::string problems;
    for (const TacticError& error : tactic.error()) {
      problems += std::format("{}{}: {}", problems.empty() ? "" : "; ", error.field, error.message);
    }
    return fail(TacticFileErrorCode::kInvalidTactic, source, problems);
  }
  return *std::move(tactic);
}

std::expected<Tactic, TacticFileError> loadTactic(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return fail(TacticFileErrorCode::kIoError, path.string(), "cannot read the file");
  }
  const std::string contents{std::istreambuf_iterator<char>(input),
                             std::istreambuf_iterator<char>()};
  return parseTacticJson(contents, path.string());
}

}  // namespace ElyverseFootball::SimTactics
