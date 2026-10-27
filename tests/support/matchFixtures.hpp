#pragma once

// Fixtures the sim-match unit tests and the acceptance scenarios share.

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "ids.hpp"
#include "matchEvents.hpp"
#include "matchSetup.hpp"
#include "matchSimulation.hpp"
#include "matchState.hpp"
#include "scenarios.hpp"
#include "vec2.hpp"

namespace ElyverseFootball::TestSupport {

// A player standing at `position` with default attributes, facing `facing`.
[[nodiscard]] inline SimMatch::PlayerMatchState playerAt(
    const std::uint32_t playerId, const SimMatch::TeamSide side, const SimCore::Vec2 position,
    const SimCore::Vec2 facing = {.x = 1.0, .y = 0.0}) {
  return {.playerId = SimCore::PlayerId(playerId),
          .side = side,
          .position = position,
          .velocity = {},
          .attributes = {},
          .target = std::nullopt,
          .facing = facing};
}

// Steps the simulation `steps` times; every step must succeed.
inline void stepTimes(SimMatch::MatchSimulation& simulation, const int steps) {
  for (int step = 0; step < steps; ++step) {
    REQUIRE(simulation.step().has_value());
  }
}

// The events of one type among `events`, in order.
template <typename Event>
[[nodiscard]] std::vector<Event> eventsOf(const std::vector<SimMatch::MatchEvent>& events) {
  std::vector<Event> found;
  for (const SimMatch::MatchEvent& event : events) {
    if (const auto* typed = std::get_if<Event>(&event)) {
      found.push_back(*typed);
    }
  }
  return found;
}

// The setup of a registered scenario for `seed`; it must exist and build.
[[nodiscard]] inline SimMatch::MatchSetup setupOf(const std::string_view scenario,
                                                  const std::uint64_t seed) {
  const auto* definition = SimMatch::findScenario(scenario);
  REQUIRE(definition != nullptr);
  auto setup = definition->make(seed);
  REQUIRE(setup.has_value());
  return *std::move(setup);
}

}  // namespace ElyverseFootball::TestSupport
