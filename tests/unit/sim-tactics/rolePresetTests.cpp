#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>

#include "responsibility.hpp"
#include "rolePreset.hpp"

using ElyverseFootball::SimTactics::contradicts;
using ElyverseFootball::SimTactics::isKnown;
using ElyverseFootball::SimTactics::kAllRolePresets;
using ElyverseFootball::SimTactics::parseRolePreset;
using ElyverseFootball::SimTactics::presetResponsibilities;
using ElyverseFootball::SimTactics::Responsibility;
using ElyverseFootball::SimTactics::RolePreset;
using ElyverseFootball::SimTactics::rolePresetName;

TEST_CASE("Role preset names round-trip", "[rolePreset]") {
  for (const RolePreset preset : kAllRolePresets) {
    CAPTURE(rolePresetName(preset));
    REQUIRE(parseRolePreset(rolePresetName(preset)) == preset);
  }
  REQUIRE_FALSE(parseRolePreset("libero").has_value());
  REQUIRE(presetResponsibilities(static_cast<RolePreset>(std::uint8_t{77})).empty());
}

TEST_CASE("Every role preset is a consistent list of duties", "[rolePreset]") {
  for (const RolePreset preset : kAllRolePresets) {
    CAPTURE(rolePresetName(preset));
    const auto duties = presetResponsibilities(preset);
    REQUIRE_FALSE(duties.empty());
    REQUIRE(duties.front().weight == 1.0);
    for (std::size_t index = 0; index < duties.size(); ++index) {
      REQUIRE(isKnown(duties[index].responsibility));
      REQUIRE(duties[index].weight > 0.0);
      REQUIRE(duties[index].weight <= 1.0);
      for (std::size_t earlier = 0; earlier < index; ++earlier) {
        REQUIRE(duties[earlier].responsibility != duties[index].responsibility);
        REQUIRE_FALSE(contradicts(duties[earlier].responsibility, duties[index].responsibility));
      }
    }
  }
  REQUIRE(presetResponsibilities(RolePreset::kWinger).front().responsibility ==
          Responsibility::kProvideWidth);
}
