#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>

#include "referenceTactic.hpp"
#include "responsibility.hpp"
#include "rolePreset.hpp"
#include "tactic.hpp"

using ElyverseFootball::SimTactics::contradicts;
using ElyverseFootball::SimTactics::isKnown;
using ElyverseFootball::SimTactics::kAllRolePresets;
using ElyverseFootball::SimTactics::parseRolePreset;
using ElyverseFootball::SimTactics::presetResponsibilities;
using ElyverseFootball::SimTactics::referenceTacticSpec;
using ElyverseFootball::SimTactics::Responsibility;
using ElyverseFootball::SimTactics::RolePreset;
using ElyverseFootball::SimTactics::rolePresetName;
using ElyverseFootball::SimTactics::Tactic;

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

TEST_CASE("Every role preset fills a valid slot", "[rolePreset]") {
  for (const RolePreset preset : kAllRolePresets) {
    CAPTURE(rolePresetName(preset));
    auto spec = referenceTacticSpec();
    // Slot 1 is a centre back in the reference tactic; the goalkeeper preset
    // would be a second keeper there, so it goes into slot 0 instead.
    const std::size_t slot = preset == RolePreset::kGoalkeeper ? 0 : 1;
    spec.slots.at(slot).responsibilities = presetResponsibilities(preset);
    REQUIRE(Tactic::create(spec).has_value());
  }
}

TEST_CASE("A tactic keeps the responsibilities, not the preset", "[rolePreset]") {
  // A slot filled by hand with the same list is the same slot.
  auto byPreset = referenceTacticSpec();
  auto byHand = referenceTacticSpec();
  byPreset.slots.at(4).responsibilities = presetResponsibilities(RolePreset::kWinger);
  byHand.slots.at(4).responsibilities = {
      {.responsibility = Responsibility::kProvideWidth, .weight = 1.0},
      {.responsibility = Responsibility::kRunInBehind, .weight = 0.5},
      {.responsibility = Responsibility::kClosePressingLine, .weight = 0.5}};
  REQUIRE(Tactic::create(byPreset) == Tactic::create(byHand));
}
