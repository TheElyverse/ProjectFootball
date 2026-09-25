#pragma once

#include <cstddef>
#include <optional>
#include <span>

namespace ElyverseFootball::SimAnalytics {

// A metric over a series of matches (docs/sim-benchmark.md): how many values,
// their mean, sample variance and a 95 % confidence interval of the mean.
struct SeriesSummary {
  std::size_t count = 0;
  std::optional<double> mean;
  // Empty below two values.
  std::optional<double> variance;
  std::optional<double> ciLow;
  std::optional<double> ciHigh;

  friend bool operator==(const SeriesSummary&, const SeriesSummary&) = default;
};

// The two-sided 97.5 % quantile of Student's t distribution with this many
// degrees of freedom, from a table up to 30 and 1.96, the normal quantile,
// beyond: slightly narrow for 31 to about 100 values, exact enough above.
[[nodiscard]] double tQuantile975(std::size_t degreesOfFreedom) noexcept;

// Summarizes the values in their order: the same values in the same order
// always give the same bits. The interval is mean ± t · s / √n.
[[nodiscard]] SeriesSummary summarize(std::span<const double> values);

}  // namespace ElyverseFootball::SimAnalytics
