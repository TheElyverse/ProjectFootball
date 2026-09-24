#include "scenarios.hpp"

#include <array>
#include <cstddef>
#include <utility>

#include "ids.hpp"
#include "kickoffScenario.hpp"
#include "matchCommand.hpp"
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

// Player index i (id i + 1) visits waypoint k at the pitch position given by
// a fixed arithmetic pattern: spread over the whole pitch, different for every
// player, no randomness. Waypoints change every 4 to 8 seconds, often before a
// player has arrived, so runs are interrupted and redirected mid-stride.
[[nodiscard]] SimCore::Vec2 m0Waypoint(const std::size_t playerIndex, const std::size_t waypoint) {
  const std::size_t column = ((playerIndex * 7U) + (waypoint * 13U)) % 11U;
  const std::size_t row = ((playerIndex * 5U) + (waypoint * 3U)) % 9U;
  return {.x = 5.0 + (5.0 * static_cast<double>(column)),
          .y = 4.0 + (4.0 * static_cast<double>(row))};
}

// The M0 acceptance scenario of docs/scenarios.md: every player moving, with
// several target changes each, and a rolling ball.
[[nodiscard]] std::expected<MatchSetup, std::string> m0Acceptance(const std::uint64_t seed) {
  auto setup = kickoffWith(seed, {.x = 9.0, .y = 4.0});
  if (!setup) {
    return setup;
  }
  constexpr std::size_t kWaypoints = 6;
  const std::size_t playerCount = setup->initialState.players().size();
  for (std::size_t waypoint = 0; waypoint < kWaypoints; ++waypoint) {
    for (std::size_t index = 0; index < playerCount; ++index) {
      // Waypoint k starts at 6 s · k, staggered by player so changes spread
      // over two seconds instead of landing on one tick.
      const auto tick = static_cast<SimCore::SimTick::ValueType>((waypoint * 180U) + (index * 4U));
      setup->commands.push_back(
          {.tick = SimCore::SimTick(tick),
           .command = MovePlayerCommand{.playerId = setup->initialState.players()[index].playerId,
                                        .target = m0Waypoint(index, waypoint)}});
    }
  }
  // Targets off the pitch are moved onto it: one behind a goal line, one past
  // a corner.
  setup->commands.push_back({.tick = SimCore::SimTick(600),
                             .command = MovePlayerCommand{.playerId = SimCore::PlayerId(1),
                                                          .target = {.x = -8.0, .y = 20.0}}});
  setup->commands.push_back({.tick = SimCore::SimTick(600),
                             .command = MovePlayerCommand{.playerId = SimCore::PlayerId(14),
                                                          .target = {.x = 75.0, .y = 55.0}}});
  return setup;
}

constexpr std::array kScenarios{
    ScenarioDefinition{
        .name = "kickoff",
        .description = "seven-a-side kickoff fixture, the ball free on the center spot",
        .make = &kickoff},
    ScenarioDefinition{.name = "rolling-ball",
                       .description = "kickoff fixture with the ball rolling at (8, 3) m/s",
                       .make = &rollingBall},
    ScenarioDefinition{.name = "m0-acceptance",
                       .description = "all 14 players on scripted runs with target changes, "
                                      "rolling ball",
                       .make = &m0Acceptance},
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
