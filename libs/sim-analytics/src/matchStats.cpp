#include "matchStats.hpp"

#include <cstddef>

namespace ElyverseFootball::SimAnalytics {
namespace {

[[nodiscard]] std::optional<double> count(const int value) {
  return static_cast<double>(value);
}

[[nodiscard]] std::optional<double> regainsIn(const TeamStats& stats, const PitchThird third) {
  return count(stats.regainsByThird.at(static_cast<std::size_t>(third)));
}

}  // namespace

std::vector<Metric> metricsOf(const TeamStats& stats) {
  return {{.name = "possessionShare", .value = stats.possessionShare},
          {.name = "passes", .value = count(stats.passes)},
          {.name = "completedPasses", .value = count(stats.completedPasses)},
          {.name = "passCompletion", .value = stats.passCompletion},
          {.name = "meanPassMeters", .value = stats.meanPassMeters},
          {.name = "progressivePasses", .value = count(stats.progressivePasses)},
          {.name = "completedProgressivePasses", .value = count(stats.completedProgressivePasses)},
          {.name = "turnovers", .value = count(stats.turnovers)},
          {.name = "regains", .value = count(stats.regains)},
          {.name = "regainsDefensiveThird", .value = regainsIn(stats, PitchThird::kDefensive)},
          {.name = "regainsMiddleThird", .value = regainsIn(stats, PitchThird::kMiddle)},
          {.name = "regainsAttackingThird", .value = regainsIn(stats, PitchThird::kAttacking)},
          {.name = "pressures", .value = count(stats.pressures)},
          {.name = "pressuresRegained", .value = count(stats.pressuresRegained)},
          {.name = "ppda", .value = stats.ppda},
          {.name = "pitchControlShare", .value = stats.pitchControlShare},
          {.name = "attackingThirdShare", .value = stats.attackingThirdShare}};
}

}  // namespace ElyverseFootball::SimAnalytics
