#pragma once

#include <string>
#include <string_view>

#include "matchStats.hpp"

namespace ElyverseFootball::SimAnalytics {

// Every statistics file starts with this format name and version; see
// docs/match-analytics.md.
inline constexpr std::string_view kStatsFormat = "elyverse-match-stats";
inline constexpr int kStatsFormatVersion = 1;

// The statistics as a JSON document with a fixed field order, empty values
// as null and numbers written exactly, so equal statistics always give the
// same bytes.
[[nodiscard]] std::string toStatsJson(const MatchStats& stats);

}  // namespace ElyverseFootball::SimAnalytics
