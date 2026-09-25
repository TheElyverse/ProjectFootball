#include "seriesSummary.hpp"

#include <array>
#include <cmath>
#include <limits>

namespace ElyverseFootball::SimAnalytics {

double tQuantile975(const std::size_t degreesOfFreedom) noexcept {
  constexpr std::array<double, 30> kTable{12.706, 4.303, 3.182, 2.776, 2.571, 2.447, 2.365, 2.306,
                                          2.262,  2.228, 2.201, 2.179, 2.160, 2.145, 2.131, 2.120,
                                          2.110,  2.101, 2.093, 2.086, 2.080, 2.074, 2.069, 2.064,
                                          2.060,  2.056, 2.052, 2.048, 2.045, 2.042};
  if (degreesOfFreedom == 0) {
    return std::numeric_limits<double>::infinity();
  }
  return degreesOfFreedom <= kTable.size() ? kTable.at(degreesOfFreedom - 1) : 1.96;
}

SeriesSummary summarize(const std::span<const double> values) {
  SeriesSummary summary{.count = values.size(),
                        .mean = std::nullopt,
                        .variance = std::nullopt,
                        .ciLow = std::nullopt,
                        .ciHigh = std::nullopt};
  if (values.empty()) {
    return summary;
  }
  const auto count = static_cast<double>(values.size());
  double sum = 0.0;
  for (const double value : values) {
    sum += value;
  }
  const double mean = sum / count;
  summary.mean = mean;
  if (values.size() < 2) {
    return summary;
  }
  double squares = 0.0;
  for (const double value : values) {
    squares += (value - mean) * (value - mean);
  }
  const double variance = squares / (count - 1.0);
  // std::sqrt is correctly rounded on every platform.
  const double halfWidth = tQuantile975(values.size() - 1) * std::sqrt(variance / count);
  summary.variance = variance;
  summary.ciLow = mean - halfWidth;
  summary.ciHigh = mean + halfWidth;
  return summary;
}

}  // namespace ElyverseFootball::SimAnalytics
