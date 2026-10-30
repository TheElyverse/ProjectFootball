#include "matchStats.hpp"

#include <cmath>
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

double chanceQuality(const double distance, const double opening,
                     const ChanceQualityModel& model) noexcept {
  const double logit = model.intercept + (model.perMeter * distance) + (model.perOpening * opening);
  return 1.0 / (1.0 + std::exp(-logit));
}

std::vector<Metric> metricsOf(const TeamStats& stats) {
  return {
      {.name = "possessionShare", .value = stats.possessionShare},
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
      {.name = "attackingThirdShare", .value = stats.attackingThirdShare},
      {.name = "shots", .value = count(stats.shots)},
      {.name = "shotsOnTarget", .value = count(stats.shotsOnTarget)},
      {.name = "goals", .value = count(stats.goals)},
      {.name = "saves", .value = count(stats.saves)},
      {.name = "chanceQuality", .value = stats.chanceQuality},
      {.name = "duelsWon", .value = count(stats.duelsWon)},
      {.name = "duelsLost", .value = count(stats.duelsLost)},
      {.name = "aerialDuels", .value = count(stats.aerialDuels)},
      {.name = "aerialDuelsWon", .value = count(stats.aerialDuelsWon)},
      {.name = "offsides", .value = count(stats.offsides)},
      {.name = "fouls", .value = count(stats.fouls)},
      {.name = "yellowCards", .value = count(stats.yellowCards)},
      {.name = "redCards", .value = count(stats.redCards)},
      {.name = "corners", .value = count(stats.corners)},
      {.name = "throwIns", .value = count(stats.throwIns)},
      {.name = "goalKicks", .value = count(stats.goalKicks)},
      {.name = "freeKicks", .value = count(stats.freeKicks)},
      {.name = "penalties", .value = count(stats.penalties)},
      {.name = "points", .value = stats.points ? count(*stats.points) : std::optional<double>()},
      {.name = "longestOwnershipSeconds", .value = stats.longestOwnershipSeconds}};
}

}  // namespace ElyverseFootball::SimAnalytics
