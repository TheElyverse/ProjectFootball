#include "actionCandidate.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <utility>

namespace ElyverseFootball::SimMatch {

std::string_view dominantScore(const ActionScores& scores) noexcept {
  const std::array<std::pair<std::string_view, double>, 6> parts{{
      {"responsibility", scores.responsibility},
      {"region", scores.region},
      {"space", scores.space},
      {"lane", scores.lane},
      {"urgency", scores.urgency},
      {"effort", scores.effort},
  }};
  std::size_t dominant = 0;
  for (std::size_t index = 1; index < parts.size(); ++index) {
    if (std::abs(parts.at(index).second) > std::abs(parts.at(dominant).second)) {
      dominant = index;
    }
  }
  return parts.at(dominant).first;
}

}  // namespace ElyverseFootball::SimMatch
