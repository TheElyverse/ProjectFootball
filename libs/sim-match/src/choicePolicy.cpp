#include "choicePolicy.hpp"

#include <algorithm>

#include "stableMath.hpp"

namespace ElyverseFootball::SimMatch {

std::optional<std::size_t> chooseByUtility(const std::span<const double> utilities,
                                           const double temperature,
                                           SimCore::RandomNumberGenerator& random) {
  if (utilities.empty()) {
    return std::nullopt;
  }
  // Weights relative to the best utility, so the largest is exactly 1 and no
  // weight overflows.
  const double best = *std::ranges::max_element(utilities);
  double total = 0.0;
  for (const double utility : utilities) {
    total += SimCore::stableExp((utility - best) / temperature);
  }
  const double draw = random.nextUniform() * total;
  double cumulative = 0.0;
  for (std::size_t index = 0; index < utilities.size(); ++index) {
    cumulative += SimCore::stableExp((utilities[index] - best) / temperature);
    if (draw < cumulative) {
      return index;
    }
  }
  // Only reachable through rounding of the sum: the last option.
  return utilities.size() - 1;
}

}  // namespace ElyverseFootball::SimMatch
