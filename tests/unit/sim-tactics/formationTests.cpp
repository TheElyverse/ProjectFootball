#include <catch2/catch_test_macros.hpp>
#include <optional>

#include "formation.hpp"
#include "referenceTactic.hpp"
#include "rolePreset.hpp"
#include "tactic.hpp"

using ElyverseFootball::SimTactics::formationName;
using ElyverseFootball::SimTactics::formationSlots;
using ElyverseFootball::SimTactics::kAllFormations;
using ElyverseFootball::SimTactics::kElevenASideSlots;
using ElyverseFootball::SimTactics::parseFormation;
using ElyverseFootball::SimTactics::presetResponsibilities;
using ElyverseFootball::SimTactics::referenceTacticSpec;
using ElyverseFootball::SimTactics::RolePreset;
using ElyverseFootball::SimTactics::Tactic;

TEST_CASE("Every formation preset fills eleven slots of a valid tactic", "[formation]") {
  for (const auto formation : kAllFormations) {
    CAPTURE(formationName(formation));
    REQUIRE(parseFormation(formationName(formation)) == formation);
    auto spec = referenceTacticSpec();
    spec.slots = formationSlots(formation);
    REQUIRE(spec.slots.size() == kElevenASideSlots);
    // The goalkeeper first, as the line-up order expects.
    REQUIRE(spec.slots.front().responsibilities == presetResponsibilities(RolePreset::kGoalkeeper));
    const auto tactic = Tactic::create(spec);
    REQUIRE(tactic.has_value());
  }
  REQUIRE_FALSE(parseFormation("2-3-5").has_value());
}
