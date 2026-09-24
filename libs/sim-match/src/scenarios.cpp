#include "scenarios.hpp"

#include <array>
#include <utility>

#include "kickoffScenario.hpp"
#include "pitch.hpp"
#include "vec2.hpp"

namespace ElyverseFootball::SimMatch {
namespace {

// The sandbox pitch: an example 7v7 size, not a mandated one.
constexpr double kPitchLength = 60.0;
constexpr double kPitchWidth = 40.0;

[[nodiscard]] std::expected<MatchSetup, std::string> kickoffWith(const std::uint64_t seed,
                                                                 const SimCore::Vec2 ballVelocity) {
  auto state = makeSevenASideKickoff(Pitch(kPitchLength, kPitchWidth), ballVelocity);
  if (!state) {
    return std::unexpected("invalid kickoff fixture: " + state.error().front().message);
  }
  return MatchSetup{.initialState = *std::move(state), .config = {}, .seed = seed, .commands = {}};
}

[[nodiscard]] std::expected<MatchSetup, std::string> kickoff(const std::uint64_t seed) {
  return kickoffWith(seed, {});
}

[[nodiscard]] std::expected<MatchSetup, std::string> rollingBall(const std::uint64_t seed) {
  return kickoffWith(seed, {.x = 8.0, .y = 3.0});
}

constexpr std::array kScenarios{
    ScenarioDefinition{.name = "kickoff",
                       .description = "seven-a-side kickoff fixture, everyone at rest",
                       .make = &kickoff},
    ScenarioDefinition{.name = "rolling-ball",
                       .description = "kickoff fixture with the ball rolling at (8, 3) m/s",
                       .make = &rollingBall},
};

}  // namespace

std::span<const ScenarioDefinition> scenarios() noexcept {
  return kScenarios;
}

const ScenarioDefinition* findScenario(const std::string_view name) noexcept {
  for (const ScenarioDefinition& scenario : kScenarios) {
    if (scenario.name == name) {
      return &scenario;
    }
  }
  return nullptr;
}

}  // namespace ElyverseFootball::SimMatch
