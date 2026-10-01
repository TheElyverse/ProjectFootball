#pragma once

// Symmetric draws around zero for execution and judgement errors; internal to
// sim-match.

#include "random.hpp"

namespace ElyverseFootball::SimMatch::RandomDraws {

// A uniform draw in [-1, 1).
[[nodiscard]] inline double symmetricUniform(SimCore::RandomNumberGenerator& random) noexcept {
  return (2.0 * random.nextUniform()) - 1.0;
}

// A draw in (-1, 1) from the triangle around 0: the sum of two uniform draws.
[[nodiscard]] inline double symmetricTriangular(SimCore::RandomNumberGenerator& random) noexcept {
  const double first = random.nextUniform();
  return first + random.nextUniform() - 1.0;
}

}  // namespace ElyverseFootball::SimMatch::RandomDraws
