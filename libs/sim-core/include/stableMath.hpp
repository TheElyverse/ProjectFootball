#pragma once

#include <algorithm>
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

// sqrt(x² + y²) -- the length of a vector (x, y) -- from basic arithmetic and
// std::sqrt only, so the result is bit-identical on every platform. std::hypot
// is not: like std::exp (see stableExp) it need not be correctly rounded, and
// a last-bit difference in a distance is enough to flip a decision.
//
// Where squaring is safe it is exactly std::sqrt(x * x + y * y), the length the
// movement code already uses. Beyond that range both components are scaled by a
// power of two first and the result scaled back, all exactly, so the squares
// cannot overflow to infinity or underflow to zero. A result beyond the largest
// double is infinity; an infinite component gives infinity even beside a NaN,
// like std::hypot, and otherwise NaN gives NaN.
[[nodiscard]] inline double stableHypot(const double x, const double y) noexcept {
  const double absX = std::abs(x);
  const double absY = std::abs(y);
  if (std::isinf(absX) || std::isinf(absY)) {
    return std::numeric_limits<double>::infinity();
  }
  if (std::isnan(absX) || std::isnan(absY)) {
    return std::numeric_limits<double>::quiet_NaN();
  }
  const double largest = std::max(absX, absY);
  // Squares of components within these bounds stay normal and finite, and a
  // smaller component whose square underflows loses less than the sum's rounding.
  constexpr double kSafeMin = 0x1p-500;
  constexpr double kSafeMax = 0x1p500;
  if (largest == 0.0 || (largest >= kSafeMin && largest <= kSafeMax)) {
    return std::sqrt((absX * absX) + (absY * absY));
  }
  const int exponent = std::ilogb(largest);
  const double scaledX = std::ldexp(absX, -exponent);
  const double scaledY = std::ldexp(absY, -exponent);
  return std::ldexp(std::sqrt((scaledX * scaledX) + (scaledY * scaledY)), exponent);
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

// Sine and cosine of one angle, from one range reduction.
struct SineCosine {
  double sine = 0.0;
  double cosine = 1.0;
};

namespace Detail {

// sin(r) for |r| <= pi/4: the Taylor series up to r^17 in Horner form over r²,
// r·(1 - r²/3!·(...)). The first omitted term is below 1e-19 there.
[[nodiscard]] inline double sineAtMostQuarterPi(const double reduced) noexcept {
  // 1/17!, -1/15!, ..., -1/3!: the Horner coefficients, the highest power first.
  constexpr std::array<double, 8> kCoefficients{
      1.0 / 355687428096000.0, -1.0 / 1307674368000.0, 1.0 / 6227020800.0, -1.0 / 39916800.0,
      1.0 / 362880.0,          -1.0 / 5040.0,          1.0 / 120.0,        -1.0 / 6.0};
  const double square = reduced * reduced;
  double sum = 0.0;
  for (const double coefficient : kCoefficients) {
    sum = (sum * square) + coefficient;
  }
  return reduced + (reduced * (square * sum));
}

// cos(r) for |r| <= pi/4: the Taylor series up to r^18 in Horner form over r².
// The first omitted term is below 1e-20 there.
[[nodiscard]] inline double cosineAtMostQuarterPi(const double reduced) noexcept {
  // -1/18!, 1/16!, ..., -1/2!: the Horner coefficients, the highest power first.
  constexpr std::array<double, 9> kCoefficients{-1.0 / 6402373705728000.0,
                                                1.0 / 20922789888000.0,
                                                -1.0 / 87178291200.0,
                                                1.0 / 479001600.0,
                                                -1.0 / 3628800.0,
                                                1.0 / 40320.0,
                                                -1.0 / 720.0,
                                                1.0 / 24.0,
                                                -1.0 / 2.0};
  const double square = reduced * reduced;
  double sum = 0.0;
  for (const double coefficient : kCoefficients) {
    sum = (sum * square) + coefficient;
  }
  return 1.0 + (square * sum);
}

}  // namespace Detail

// sin(angle) and cos(angle), angle in radians, from basic arithmetic only, so
// the result is bit-identical on every platform. std::sin and std::cos are not:
// like std::exp (see stableExp) they need not be correctly rounded, and a
// last-bit difference in a turned velocity is enough to make a replay diverge.
//
// Cody-Waite range reduction angle = k·pi/2 + r with |r| <= pi/4, pi/2 split
// into three parts of 33 bits and a tail, so every product k·part is exact for
// |k| < 2^20, then the Taylor series of sin(r) and cos(r) and the quadrant k
// mod 4. The magnitude is reduced and the sine takes the angle's sign, so the
// sine is exactly odd and the cosine exactly even. Within
// |angle| <= kMaxStableAngle the result is within two units in the last place
// of 1 of the true value (docs/match-engine.md#stable-math). Beyond it, and for
// a non-finite angle, both are NaN.
inline constexpr double kMaxStableAngle = 0x1p20 * (std::numbers::pi / 2.0);

[[nodiscard]] inline SineCosine stableSineCosine(const double angle) noexcept {
  const double magnitude = std::abs(angle);
  if (!(magnitude <= kMaxStableAngle)) {
    const double notANumber = std::numeric_limits<double>::quiet_NaN();
    return {.sine = notANumber, .cosine = notANumber};
  }
  constexpr double kTwoOverPi = 2.0 * std::numbers::inv_pi;
  // pi/2 = kHalfPi1 + kHalfPi2 + kHalfPi3 + kHalfPi3Tail, the first three with
  // 33 significant bits each (fdlibm's split).
  constexpr double kHalfPi1 = 1.57079632673412561417e+00;
  constexpr double kHalfPi2 = 6.07710050630396597660e-11;
  constexpr double kHalfPi3 = 2.02226624871116645580e-21;
  constexpr double kHalfPi3Tail = 8.47842766036889956997e-32;
  const double quarterTurns = std::floor((magnitude * kTwoOverPi) + 0.5);
  double reduced = magnitude - (quarterTurns * kHalfPi1);
  reduced -= quarterTurns * kHalfPi2;
  reduced -= quarterTurns * kHalfPi3;
  reduced -= quarterTurns * kHalfPi3Tail;

  const double sine = Detail::sineAtMostQuarterPi(reduced);
  const double cosine = Detail::cosineAtMostQuarterPi(reduced);
  // 0 <= k <= 2^20, so the quadrant k mod 4 is exact in an int.
  SineCosine result;
  switch (static_cast<int>(quarterTurns) % 4) {
    case 0:
      result = {.sine = sine, .cosine = cosine};
      break;
    case 1:
      result = {.sine = cosine, .cosine = -sine};
      break;
    case 2:
      result = {.sine = -sine, .cosine = -cosine};
      break;
    default:
      result = {.sine = -cosine, .cosine = sine};
      break;
  }
  if (std::signbit(angle)) {
    result.sine = -result.sine;
  }
  return result;
}

// sin(angle) in radians, bit-identical on every platform; see stableSineCosine().
[[nodiscard]] inline double stableSine(const double angle) noexcept {
  return stableSineCosine(angle).sine;
}

// cos(angle) in radians, bit-identical on every platform; see stableSineCosine().
[[nodiscard]] inline double stableCosine(const double angle) noexcept {
  return stableSineCosine(angle).cosine;
}

// The value `amount` of the way from `from` toward `toward`: from + amount ×
// (toward − from), always this one expression. std::lerp is not: it picks
// among formulas to be exact at the ends and monotonic, and libstdc++, MSVC
// and libc++ need not pick alike.
[[nodiscard]] inline double stableLerp(const double from, const double toward,
                                       const double amount) noexcept {
  return from + (amount * (toward - from));
}

}  // namespace ElyverseFootball::SimCore
