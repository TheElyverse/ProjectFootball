#pragma once

#include <array>
#include <cmath>
#include <limits>
#include <numbers>

namespace ElyverseFootball::SimCore {

// e^x from basic arithmetic only, so the result is bit-identical on every
// platform. std::exp is not: the standard does not require it to be
// correctly rounded, and libstdc++, MSVC and libc++ may return different last
// bits -- enough to flip a probabilistic decision and make a replay diverge
// between machines.
//
// Cody-Waite range reduction x = k·ln 2 + r with |r| <= ln 2 / 2, a degree-13
// Taylor polynomial for e^r in Horner form, and an exact scaling by 2^k with
// std::ldexp. Every step is a correctly rounded IEEE 754 operation; the
// result is within a few units in the last place of e^x. Returns 0 below -745 and infinity
// above 709.78, like std::exp, and NaN for NaN.
[[nodiscard]] inline double stableExp(const double value) noexcept {
  constexpr double kUpper = 709.782712893384;
  constexpr double kLower = -745.1332191019411;
  if (std::isnan(value)) {
    return value;
  }
  if (value > kUpper) {
    return std::numeric_limits<double>::infinity();
  }
  if (value < kLower) {
    return 0.0;
  }
  // ln 2 split into a high part with trailing zero bits, so k · kLn2High is
  // exact for every power in range, and the remainder.
  constexpr double kLog2E = std::numbers::log2e;
  // NOLINTNEXTLINE(modernize-use-std-numbers) -- deliberately not ln 2, see above.
  constexpr double kLn2High = 6.93147180369123816490e-01;
  constexpr double kLn2Low = 1.90821492927058770002e-10;
  const double powerOfTwo = std::floor((value * kLog2E) + 0.5);
  const double remainder = (value - (powerOfTwo * kLn2High)) - (powerOfTwo * kLn2Low);

  // 1/12!, 1/11!, ..., 1/1!, 1/0!: the Horner coefficients after 1/13!.
  constexpr std::array<double, 13> kInverseFactorials{1.0 / 479001600.0,
                                                      1.0 / 39916800.0,
                                                      1.0 / 3628800.0,
                                                      1.0 / 362880.0,
                                                      1.0 / 40320.0,
                                                      1.0 / 5040.0,
                                                      1.0 / 720.0,
                                                      1.0 / 120.0,
                                                      1.0 / 24.0,
                                                      1.0 / 6.0,
                                                      1.0 / 2.0,
                                                      1.0,
                                                      1.0};
  double sum = 1.0 / 6227020800.0;  // 1/13!
  for (const double inverseFactorial : kInverseFactorials) {
    sum = (sum * remainder) + inverseFactorial;
  }
  return std::ldexp(sum, static_cast<int>(powerOfTwo));
}

}  // namespace ElyverseFootball::SimCore
