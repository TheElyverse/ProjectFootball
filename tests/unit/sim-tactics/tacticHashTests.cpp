#include <catch2/catch_test_macros.hpp>
#include <cstdint>

#include "referenceTactic.hpp"
#include "responsibility.hpp"
#include "tactic.hpp"
#include "tacticHash.hpp"
#include "tacticalPhase.hpp"

using ElyverseFootball::SimTactics::contentHash;
using ElyverseFootball::SimTactics::phaseIndex;
using ElyverseFootball::SimTactics::PressingTrigger;
using ElyverseFootball::SimTactics::referenceTacticSpec;
using ElyverseFootball::SimTactics::Tactic;
using ElyverseFootball::SimTactics::TacticalPhase;
using ElyverseFootball::SimTactics::TacticSpec;

namespace {

[[nodiscard]] std::uint64_t hashOf(TacticSpec spec) {
  const auto tactic = Tactic::create(std::move(spec));
  REQUIRE(tactic.has_value());
  return tactic ? contentHash(*tactic) : 0;
}

}  // namespace

TEST_CASE("Equal tactics hash alike", "[tacticHash]") {
  REQUIRE(hashOf(referenceTacticSpec()) == hashOf(referenceTacticSpec()));
}

TEST_CASE("Signed zero hashes the same as positive zero", "[tacticHash]") {
  auto negativeZero = referenceTacticSpec();
  negativeZero.principles.pressingLine = -0.0;
  auto positiveZero = referenceTacticSpec();
  positiveZero.principles.pressingLine = 0.0;
  REQUIRE(hashOf(negativeZero) == hashOf(positiveZero));
}

TEST_CASE("Every part of a tactic changes its hash", "[tacticHash]") {
  const std::uint64_t reference = hashOf(referenceTacticSpec());
  auto spec = referenceTacticSpec();

  SECTION("name") {
    spec.name = "other";
  }
  SECTION("description") {
    spec.description += ".";
  }
  SECTION("shape") {
    spec.slots.at(3).position.depth = 0.36;
  }
  SECTION("responsibility weight") {
    spec.slots.at(6).responsibilities.at(1).weight = 0.9;
  }
  SECTION("responsibility order") {
    std::swap(spec.slots.at(6).responsibilities.at(0), spec.slots.at(6).responsibilities.at(1));
  }
  SECTION("pressing trigger") {
    spec.principles.pressingTriggers = {PressingTrigger::kSlowPass};
  }
  SECTION("positioning weight") {
    spec.principles.positioning.transitionRisk = 0.6;
  }
  SECTION("phase instruction") {
    spec.phases.at(phaseIndex(TacticalPhase::kDefensiveTransition)).runFrequency = 0.31;
  }
  REQUIRE(hashOf(spec) != reference);
}

TEST_CASE("The reference tactic hash is pinned", "[tacticHash]") {
  // Changes when the reference tactic or the hash layout changes; both must
  // be deliberate, since replays record tactic hashes.
  REQUIRE(hashOf(referenceTacticSpec()) == 0xd1f008d25b46aabfULL);
}
