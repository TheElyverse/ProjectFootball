#pragma once

#include <cstddef>
#include <optional>
#include <span>

#include "random.hpp"

namespace ElyverseFootball::SimMatch {

// The seeded softmax every decision in the match chooses with
// (docs/pass-decisions.md): option i with probability proportional to
// exp((u_i - u_max) / temperature). The best option is the most likely, not a
// certainty.
//
// utilities must be finite and temperature positive and finite. Draws exactly
// one number from random if there is an option and none otherwise; empty
// without an option. The exponential is stableExp(), so the choice is the
// same on every platform.
[[nodiscard]] std::optional<std::size_t> chooseByUtility(std::span<const double> utilities,
                                                         double temperature,
                                                         SimCore::RandomNumberGenerator& random);

}  // namespace ElyverseFootball::SimMatch
