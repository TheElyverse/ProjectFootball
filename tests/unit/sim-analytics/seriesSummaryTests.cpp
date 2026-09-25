#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <vector>

#include "matchStats.hpp"
#include "seriesSummary.hpp"

using Catch::Approx;
using ElyverseFootball::SimAnalytics::metricsOf;
using ElyverseFootball::SimAnalytics::summarize;
using ElyverseFootball::SimAnalytics::TeamStats;
using ElyverseFootball::SimAnalytics::tQuantile975;

TEST_CASE("A series summary gives mean, sample variance and a t interval", "[seriesSummary]") {
  const std::vector<double> values{2.0, 4.0, 4.0, 4.0, 5.0, 5.0, 7.0, 9.0};
  const auto summary = summarize(values);
  REQUIRE(summary.count == 8);
  REQUIRE(summary.mean == 5.0);
  REQUIRE(summary.variance == Approx(32.0 / 7.0));
  const double halfWidth = 2.365 * std::sqrt((32.0 / 7.0) / 8.0);
  REQUIRE(summary.ciLow == Approx(5.0 - halfWidth));
  REQUIRE(summary.ciHigh == Approx(5.0 + halfWidth));
}

TEST_CASE("Short series have no variance or interval", "[seriesSummary]") {
  const auto none = summarize({});
  REQUIRE(none.count == 0);
  REQUIRE_FALSE(none.mean.has_value());
  const std::vector<double> one{3.0};
  const auto single = summarize(one);
  REQUIRE(single.mean == 3.0);
  REQUIRE_FALSE(single.variance.has_value());
  REQUIRE_FALSE(single.ciLow.has_value());
}

TEST_CASE("The t quantile comes from the table, then the normal quantile", "[seriesSummary]") {
  REQUIRE(tQuantile975(1) == 12.706);
  REQUIRE(tQuantile975(9) == 2.262);
  REQUIRE(tQuantile975(30) == 2.042);
  REQUIRE(tQuantile975(31) == 1.96);
}

TEST_CASE("Every metric of a side is named, in a fixed order", "[seriesSummary]") {
  TeamStats stats;
  stats.passes = 12;
  stats.regainsByThird = {1, 2, 3};
  const auto metrics = metricsOf(stats);
  REQUIRE(metrics.size() == 17);
  REQUIRE(metrics.at(0).name == "possessionShare");
  REQUIRE(metrics.at(1).value == 12.0);
  REQUIRE(metrics.at(3).name == "passCompletion");
  REQUIRE_FALSE(metrics.at(3).value.has_value());
  REQUIRE(metrics.at(11).name == "regainsAttackingThird");
  REQUIRE(metrics.at(11).value == 3.0);
}
