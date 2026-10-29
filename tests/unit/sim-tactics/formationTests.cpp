#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <optional>
#include <utility>

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
    // The back four, then midfield and attack, each line from the touchline
    // at y = 0 to the other.
    const int midfieldEnd = formation == ElyverseFootball::SimTactics::Formation::k442 ? 8 : 7;
    for (const auto& [first, last] :
         {std::pair{1, 4}, std::pair{5, midfieldEnd}, std::pair{midfieldEnd + 1, 10}}) {
      for (int index = first; index < last; ++index) {
        CAPTURE(index);
        REQUIRE(spec.slots.at(static_cast<std::size_t>(index)).position.width <
                spec.slots.at(static_cast<std::size_t>(index) + 1).position.width);
      }
    }
  }
  REQUIRE_FALSE(parseFormation("2-3-5").has_value());
}
