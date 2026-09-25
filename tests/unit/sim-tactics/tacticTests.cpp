#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

#include "referenceTactic.hpp"
#include "responsibility.hpp"
#include "tactic.hpp"
#include "tacticalPhase.hpp"

using ElyverseFootball::SimTactics::kAllPhases;
using ElyverseFootball::SimTactics::kSlotsPerTactic;
using ElyverseFootball::SimTactics::phaseIndex;
using ElyverseFootball::SimTactics::PressingTrigger;
using ElyverseFootball::SimTactics::referenceTacticSpec;
using ElyverseFootball::SimTactics::Responsibility;
using ElyverseFootball::SimTactics::Tactic;
using ElyverseFootball::SimTactics::TacticalPhase;
using ElyverseFootball::SimTactics::TacticError;
using ElyverseFootball::SimTactics::TacticErrorCode;
using ElyverseFootball::SimTactics::TacticSpec;

namespace {

// The errors of a spec that must be rejected.
[[nodiscard]] std::vector<TacticError> errorsOf(TacticSpec spec) {
  auto tactic = Tactic::create(std::move(spec));
  REQUIRE_FALSE(tactic.has_value());
  return tactic.has_value() ? std::vector<TacticError>{} : tactic.error();
}

// The one error a spec that breaks one rule is rejected with.
[[nodiscard]] TacticError onlyErrorOf(TacticSpec spec) {
  const auto errors = errorsOf(std::move(spec));
  REQUIRE(errors.size() == 1);
  return errors.front();
}

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

}  // namespace

TEST_CASE("The reference tactic is valid", "[tactic]") {
  const auto tactic = Tactic::create(referenceTacticSpec());
  REQUIRE(tactic.has_value());
  REQUIRE(tactic->name() == "reference");
  REQUIRE(tactic->slots().size() == kSlotsPerTactic);
  REQUIRE(tactic->spec() == referenceTacticSpec());
}

TEST_CASE("A tactic answers per slot and per phase", "[tactic]") {
  auto spec = referenceTacticSpec();
  spec.phases.at(phaseIndex(TacticalPhase::kPressing)).lineHeight = 0.45;
  spec.principles.pressingTriggers = {PressingTrigger::kBackPass};
  const auto tactic = Tactic::create(spec);
  REQUIRE(tactic.has_value());

  REQUIRE(tactic->instruction(TacticalPhase::kPressing).lineHeight == 0.45);
  REQUIRE(tactic->instruction(TacticalPhase::kDefensiveBlock).lineHeight == 0.25);
  REQUIRE(tactic->responsibilityWeight(0, Responsibility::kGuardGoal) == 1.0);
  REQUIRE(tactic->responsibilityWeight(0, Responsibility::kProvideWidth) == 0.0);
  REQUIRE(tactic->responsibilityWeight(4, Responsibility::kRunInBehind) == 0.5);
  REQUIRE_THROWS_AS(tactic->responsibilityWeight(kSlotsPerTactic, Responsibility::kCover),
                    std::out_of_range);
  REQUIRE(tactic->pressesOn(PressingTrigger::kBackPass));
  REQUIRE_FALSE(tactic->pressesOn(PressingTrigger::kSlowPass));
}

TEST_CASE("A tactic needs a name and seven slots", "[tactic]") {
  auto unnamed = referenceTacticSpec();
  unnamed.name.clear();
  const auto nameError = onlyErrorOf(unnamed);
  REQUIRE(nameError.code == TacticErrorCode::kEmptyName);
  REQUIRE(nameError.field == "name");

  auto missing = referenceTacticSpec();
  missing.slots.pop_back();
  const auto slotError = onlyErrorOf(missing);
  REQUIRE(slotError.code == TacticErrorCode::kWrongSlotCount);
  REQUIRE(slotError.field == "slots");
  REQUIRE(slotError.message == "has 6 slots, a seven-a-side tactic needs 7");
}

TEST_CASE("Shape positions must lie on the pitch", "[tactic]") {
  auto spec = referenceTacticSpec();
  spec.slots.at(2).position.depth = 1.2;
  spec.slots.at(5).position.width = kNaN;
  const auto errors = errorsOf(spec);
  REQUIRE(errors.size() == 2);
  REQUIRE(errors[0].code == TacticErrorCode::kSlotOutsidePitch);
  REQUIRE(errors[0].field == "slots[2].position.depth");
  REQUIRE(errors[0].message == "1.2 lies off the pitch; expected a fraction in [0, 1]");
  REQUIRE(errors[1].field == "slots[5].position.width");
}

TEST_CASE("Responsibilities must be known, weighted and consistent", "[tactic]") {
  SECTION("unknown") {
    auto spec = referenceTacticSpec();
    spec.slots.at(3).responsibilities.push_back(
        {.responsibility = static_cast<Responsibility>(std::uint8_t{42}), .weight = 1.0});
    const auto error = onlyErrorOf(spec);
    REQUIRE(error.code == TacticErrorCode::kUnknownResponsibility);
    REQUIRE(error.field == "slots[3].responsibilities[3]");
  }
  SECTION("weight") {
    auto spec = referenceTacticSpec();
    spec.slots.at(1).responsibilities.at(0).weight = 0.0;
    const auto error = onlyErrorOf(spec);
    REQUIRE(error.code == TacticErrorCode::kInvalidResponsibilityWeight);
    REQUIRE(error.field == "slots[1].responsibilities[0].weight");
    REQUIRE(error.message == "weight 0 of holdDefensiveLine must lie in (0, 1]");
  }
  SECTION("duplicate") {
    auto spec = referenceTacticSpec();
    spec.slots.at(6).responsibilities.push_back(
        {.responsibility = Responsibility::kRunInBehind, .weight = 0.2});
    const auto error = onlyErrorOf(spec);
    REQUIRE(error.code == TacticErrorCode::kDuplicateResponsibility);
    REQUIRE(error.message == "runInBehind is listed twice");
  }
  SECTION("contradictory") {
    auto spec = referenceTacticSpec();
    // Winger: provideWidth first.
    spec.slots.at(4).responsibilities.push_back(
        {.responsibility = Responsibility::kOccupyHalfspace, .weight = 0.5});
    const auto error = onlyErrorOf(spec);
    REQUIRE(error.code == TacticErrorCode::kContradictoryResponsibilities);
    REQUIRE(error.field == "slots[4].responsibilities[3]");
    REQUIRE(error.message == "occupyHalfspace contradicts provideWidth");
  }
  SECTION("two goalkeepers") {
    auto spec = referenceTacticSpec();
    spec.slots.at(1).responsibilities = {{.responsibility = Responsibility::kGuardGoal}};
    const auto error = onlyErrorOf(spec);
    REQUIRE(error.code == TacticErrorCode::kTooManyGoalkeepers);
    REQUIRE(error.field == "slots[1].responsibilities");
  }
  SECTION("a slot without duties just holds the shape") {
    auto spec = referenceTacticSpec();
    spec.slots.at(3).responsibilities.clear();
    REQUIRE(Tactic::create(spec).has_value());
  }
}

TEST_CASE("Principles are range-checked", "[tactic]") {
  auto spec = referenceTacticSpec();
  spec.principles.pressingLine = -0.1;
  spec.principles.pressingTriggers = {PressingTrigger::kSlowPass, PressingTrigger::kSlowPass,
                                      static_cast<PressingTrigger>(std::uint8_t{9})};
  spec.principles.positioning.occupancy = 11.0;
  const auto errors = errorsOf(spec);
  REQUIRE(errors.size() == 4);
  REQUIRE(errors[0].field == "principles.pressingLine");
  REQUIRE(errors[0].code == TacticErrorCode::kValueOutOfRange);
  REQUIRE(errors[1].code == TacticErrorCode::kDuplicatePressingTrigger);
  REQUIRE(errors[1].field == "principles.pressingTriggers[1]");
  REQUIRE(errors[2].code == TacticErrorCode::kUnknownPressingTrigger);
  REQUIRE(errors[3].field == "principles.positioning.occupancy");
  REQUIRE(errors[3].message == "11 must lie in [0, 10]");
}

TEST_CASE("Phase instructions are range-checked", "[tactic]") {
  for (const TacticalPhase phase : kAllPhases) {
    auto spec = referenceTacticSpec();
    auto& instruction = spec.phases.at(phaseIndex(phase));
    instruction.blockWidth = 0.0;
    instruction.passingRisk = kNaN;
    const auto errors = errorsOf(spec);
    REQUIRE(errors.size() == 2);
    REQUIRE(errors[0].code == TacticErrorCode::kValueOutOfRange);
    REQUIRE(errors[0].message == "0 must lie in (0, 1]");
    REQUIRE(errors[1].code == TacticErrorCode::kValueOutOfRange);
  }
  auto spec = referenceTacticSpec();
  spec.phases.at(phaseIndex(TacticalPhase::kFinalThird)).runFrequency = 1.5;
  REQUIRE(onlyErrorOf(spec).field == "phases.finalThird.runFrequency");
}

TEST_CASE("A block must fit between the goal lines", "[tactic]") {
  auto spec = referenceTacticSpec();
  auto& instruction = spec.phases.at(phaseIndex(TacticalPhase::kPressing));
  instruction.lineHeight = 0.7;
  instruction.blockLength = 0.4;
  const auto error = onlyErrorOf(spec);
  REQUIRE(error.code == TacticErrorCode::kContradictoryParameters);
  REQUIRE(error.field == "phases.pressing.blockLength");

  instruction.blockLength = 0.3;
  REQUIRE(Tactic::create(spec).has_value());
}

TEST_CASE("Every broken rule is reported in a fixed order", "[tactic]") {
  auto spec = referenceTacticSpec();
  spec.name.clear();
  spec.slots.at(0).position.depth = -1.0;
  spec.principles.pressingLine = 2.0;
  spec.phases.at(phaseIndex(TacticalPhase::kBuildUp)).ballShift = 3.0;
  const auto errors = errorsOf(spec);
  REQUIRE(errors.size() == 4);
  REQUIRE(errors[0].field == "name");
  REQUIRE(errors[1].field == "slots[0].position.depth");
  REQUIRE(errors[2].field == "principles.pressingLine");
  REQUIRE(errors[3].field == "phases.buildUp.ballShift");
}
