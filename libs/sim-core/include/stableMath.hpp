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

namespace Detail {

// arctan for 0 <= value <= 1, the core of stableArcTangent().
//
// Halving with atan(x) = 2·atan(x / (1 + sqrt(1 + x²))) brings the argument
// below a tenth -- three halvings take even 1 down to tan(pi / 32), about
// 0.0985 -- and std::sqrt is correctly rounded in IEEE 754, so every step is
// exact to the last bit. An argument already that small is left alone, which
// keeps the smallest subnormals from halving away into zero.
//
// The Taylor series x - x³/3 + x⁵/5 - ... then converges fast: from a tenth
// its first omitted term is below 1e-22, and the result is within a few units
// in the last place of atan(value).
[[nodiscard]] inline double arcTangentAtMostOne(const double value) noexcept {
  constexpr double kSeriesLimit = 0.1;
  constexpr int kMaxHalvings = 3;
  double reduced = value;
  int halvings = 0;
  while (reduced > kSeriesLimit && halvings < kMaxHalvings) {
    reduced = reduced / (1.0 + std::sqrt(1.0 + (reduced * reduced)));
    ++halvings;
  }
  // 1/21, 1/19, ..., 1/3, 1 in Horner form over x²:
  // x·(1 - x²·(1/3 - x²·(1/5 - ...))).
  const double square = reduced * reduced;
  double sum = 1.0 / 21.0;
  for (int denominator = 19; denominator >= 1; denominator -= 2) {
    sum = (1.0 / static_cast<double>(denominator)) - (square * sum);
  }
  return static_cast<double>(1 << halvings) * reduced * sum;
}

}  // namespace Detail

// arctan(value) in radians, from basic arithmetic and std::sqrt only, so the
// result is bit-identical on every platform. std::atan and std::atan2 are not:
// like std::exp (see stableExp) they need not be correctly rounded, and a
// last-bit difference is enough to make two machines rate the same shooting
// angle differently and a replay diverge.
//
// The result is in (-pi/2, pi/2) and has the sign of the argument, including
// for a negative zero. An argument beyond 1 in magnitude folds to
// pi/2 - arctan(1/|value|), infinity gives +-pi/2, and NaN gives NaN.
[[nodiscard]] inline double stableArcTangent(const double value) noexcept {
  if (std::isnan(value)) {
    return value;
  }
  const double sign = std::signbit(value) ? -1.0 : 1.0;
  const double magnitude = std::abs(value);
  constexpr double kRightAngle = std::numbers::pi / 2.0;
  if (std::isinf(magnitude)) {
    return sign * kRightAngle;
  }
  if (magnitude > 1.0) {
    return sign * (kRightAngle - Detail::arcTangentAtMostOne(1.0 / magnitude));
  }
  return sign * Detail::arcTangentAtMostOne(magnitude);
}

}  // namespace ElyverseFootball::SimCore
