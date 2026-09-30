#pragma once

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>

#include "matchSetup.hpp"

namespace ElyverseFootball::SimMatch {

// A named, reproducible match setup: fixture, configuration and scripted
// commands. The seed is the caller's; everything else is fixed, so a scenario
// name plus a seed identifies a match.
//
// Scenarios are versioned like fixtures: changing one changes every match
// played from it, so the change must be deliberate and documented in
// docs/scenarios.md.
struct ScenarioDefinition {
  std::string_view name;
  std::string_view description;
  std::expected<MatchSetup, std::string> (*make)(std::uint64_t seed);
};

// Every scenario, in a fixed order.
[[nodiscard]] std::span<const ScenarioDefinition> scenarios() noexcept;

// The scenario with this name, or nullptr.
[[nodiscard]] const ScenarioDefinition* findScenario(std::string_view name) noexcept;

// A seven-a-side match between two tactics: the players of the kickoff
// fixture on the sandbox pitch with the sides' tactics, lined up for a
// kickoff by home (lineUpForKickoff(), docs/restarts.md), the standard
// configuration, and home's taker -- the striker of a preset, player 7 --
// given the ball at kickoff. Any two tactics that fit seven a side can face
// each other; a side without a tactic is scripted and lines up from the
// fixture's layout. The "tactic-match" scenario is this fixture with the
// reference tactic on both sides.
[[nodiscard]] std::expected<MatchSetup, std::string> makeTacticMatch(TeamTactics tactics,
                                                                     std::uint64_t seed);

}  // namespace ElyverseFootball::SimMatch
