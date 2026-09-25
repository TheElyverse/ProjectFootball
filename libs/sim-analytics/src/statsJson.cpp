#include "statsJson.hpp"

#include <nlohmann/json.hpp>
#include <optional>

namespace ElyverseFootball::SimAnalytics {
namespace {

// Insertion-ordered, so the file reads in the order of the documentation.
using Json = nlohmann::ordered_json;

[[nodiscard]] Json optionalJson(const std::optional<double>& value) {
  return value ? Json(*value) : Json(nullptr);
}

[[nodiscard]] Json teamJson(const TeamStats& stats) {
  Json json;
  json["possessionShare"] = stats.possessionShare;
  json["passes"] = stats.passes;
  json["completedPasses"] = stats.completedPasses;
  json["passCompletion"] = optionalJson(stats.passCompletion);
  json["meanPassMeters"] = optionalJson(stats.meanPassMeters);
  json["progressivePasses"] = stats.progressivePasses;
  json["completedProgressivePasses"] = stats.completedProgressivePasses;
  json["turnovers"] = stats.turnovers;
  json["regains"] = stats.regains;
  json["regainsByThird"] = {
      {"defensive", stats.regainsByThird.at(static_cast<std::size_t>(PitchThird::kDefensive))},
      {"middle", stats.regainsByThird.at(static_cast<std::size_t>(PitchThird::kMiddle))},
      {"attacking", stats.regainsByThird.at(static_cast<std::size_t>(PitchThird::kAttacking))}};
  json["pressures"] = stats.pressures;
  json["pressuresRegained"] = stats.pressuresRegained;
  json["ppda"] = optionalJson(stats.ppda);
  json["pitchControlShare"] = optionalJson(stats.pitchControlShare);
  json["attackingThirdShare"] = optionalJson(stats.attackingThirdShare);
  return json;
}

}  // namespace

std::string toStatsJson(const MatchStats& stats) {
  Json json;
  json["format"] = kStatsFormat;
  json["version"] = kStatsFormatVersion;
  json["ticks"] = stats.ticks;
  json["seconds"] = stats.seconds;
  json["home"] = teamJson(stats.home);
  json["away"] = teamJson(stats.away);
  return json.dump(2) + "\n";
}

}  // namespace ElyverseFootball::SimAnalytics
