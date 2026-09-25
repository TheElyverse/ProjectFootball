#include <catch2/catch_test_macros.hpp>
#include <cstdint>

#include "responsibility.hpp"
#include "tacticalPhase.hpp"

using ElyverseFootball::SimTactics::contradicts;
using ElyverseFootball::SimTactics::isInPossession;
using ElyverseFootball::SimTactics::isKnown;
using ElyverseFootball::SimTactics::kAllPhases;
using ElyverseFootball::SimTactics::kAllPressingTriggers;
using ElyverseFootball::SimTactics::kAllResponsibilities;
using ElyverseFootball::SimTactics::parsePhase;
using ElyverseFootball::SimTactics::parsePressingTrigger;
using ElyverseFootball::SimTactics::parseResponsibility;
using ElyverseFootball::SimTactics::phaseIndex;
using ElyverseFootball::SimTactics::phaseName;
using ElyverseFootball::SimTactics::PressingTrigger;
using ElyverseFootball::SimTactics::pressingTriggerName;
using ElyverseFootball::SimTactics::Responsibility;
using ElyverseFootball::SimTactics::responsibilityName;
using ElyverseFootball::SimTactics::TacticalPhase;

TEST_CASE("Phase names round-trip and index kAllPhases", "[tacticalPhase]") {
  for (const TacticalPhase phase : kAllPhases) {
    CAPTURE(phaseName(phase));
    REQUIRE(parsePhase(phaseName(phase)) == phase);
    REQUIRE(kAllPhases.at(phaseIndex(phase)) == phase);
  }
  REQUIRE_FALSE(parsePhase("BuildUp").has_value());
  REQUIRE(phaseName(static_cast<TacticalPhase>(std::uint8_t{200})) == "unknown");
}

TEST_CASE("Four phases have the ball, three do not", "[tacticalPhase]") {
  REQUIRE(isInPossession(TacticalPhase::kBuildUp));
  REQUIRE(isInPossession(TacticalPhase::kProgression));
  REQUIRE(isInPossession(TacticalPhase::kFinalThird));
  REQUIRE(isInPossession(TacticalPhase::kAttackingTransition));
  REQUIRE_FALSE(isInPossession(TacticalPhase::kDefensiveBlock));
  REQUIRE_FALSE(isInPossession(TacticalPhase::kPressing));
  REQUIRE_FALSE(isInPossession(TacticalPhase::kDefensiveTransition));
}

TEST_CASE("Responsibility and trigger names round-trip", "[responsibility]") {
  for (const Responsibility responsibility : kAllResponsibilities) {
    CAPTURE(responsibilityName(responsibility));
    REQUIRE(parseResponsibility(responsibilityName(responsibility)) == responsibility);
    REQUIRE(isKnown(responsibility));
  }
  for (const PressingTrigger trigger : kAllPressingTriggers) {
    CAPTURE(pressingTriggerName(trigger));
    REQUIRE(parsePressingTrigger(pressingTriggerName(trigger)) == trigger);
    REQUIRE(isKnown(trigger));
  }
  REQUIRE_FALSE(parseResponsibility("striker").has_value());
  REQUIRE_FALSE(isKnown(static_cast<Responsibility>(std::uint8_t{99})));
  REQUIRE_FALSE(isKnown(static_cast<PressingTrigger>(std::uint8_t{99})));
}

TEST_CASE("Contradicting responsibilities are symmetric", "[responsibility]") {
  using enum Responsibility;
  REQUIRE(contradicts(kProvideWidth, kOccupyHalfspace));
  REQUIRE(contradicts(kOccupyHalfspace, kProvideWidth));
  REQUIRE(contradicts(kRunInBehind, kHoldRestDefence));
  REQUIRE(contradicts(kHoldDefensiveLine, kRunInBehind));
  REQUIRE(contradicts(kGuardGoal, kCover));
  REQUIRE_FALSE(contradicts(kCover, kMarkOpponent));
  REQUIRE_FALSE(contradicts(kGuardGoal, kGuardGoal));
  for (const Responsibility left : kAllResponsibilities) {
    for (const Responsibility right : kAllResponsibilities) {
      // NOLINTNEXTLINE(readability-suspicious-call-argument) -- swapped on purpose.
      REQUIRE(contradicts(left, right) == contradicts(right, left));
    }
  }
}
