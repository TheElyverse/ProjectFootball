// The tactic files under data/tactics/: every one loads, and the reference
// tactic file is the tactic referenceTacticSpec() builds in code. The three
// tactical identities differ only in data, in the directions their intent
// promises (docs/tactical-identities.md).

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include "referenceTactic.hpp"
#include "tactic.hpp"
#include "tacticHash.hpp"
#include "tacticJson.hpp"
#include "tacticalPhase.hpp"

using ElyverseFootball::SimTactics::contentHash;
using ElyverseFootball::SimTactics::loadTactic;
using ElyverseFootball::SimTactics::referenceTacticSpec;
using ElyverseFootball::SimTactics::Tactic;
using ElyverseFootball::SimTactics::TacticalPhase;

namespace {

// Set by tests/unit/CMakeLists.txt to the repository's data directory.
const std::filesystem::path kTacticsDirectory = std::filesystem::path(PF_DATA_DIR) / "tactics";

[[nodiscard]] Tactic preset(const std::string& name) {
  auto tactic = loadTactic(kTacticsDirectory / (name + ".json"));
  INFO(name);
  REQUIRE(tactic.has_value());
  return *std::move(tactic);
}

}  // namespace

TEST_CASE("Every tactic file loads", "[tacticFiles]") {
  std::vector<std::filesystem::path> files;
  for (const auto& entry : std::filesystem::directory_iterator(kTacticsDirectory)) {
    if (entry.path().extension() == ".json") {
      files.push_back(entry.path());
    }
  }
  std::ranges::sort(files);
  REQUIRE_FALSE(files.empty());
  for (const auto& file : files) {
    const auto tactic = loadTactic(file);
    INFO((tactic ? std::string() : tactic.error().message));
    REQUIRE(tactic.has_value());
    // A file is named after the tactic it holds.
    REQUIRE(tactic->name() == file.stem().string());
  }
}

TEST_CASE("The reference tactic file equals the reference tactic in code", "[tacticFiles]") {
  const auto loaded = loadTactic(kTacticsDirectory / "reference.json");
  REQUIRE(loaded.has_value());
  const auto built = Tactic::create(referenceTacticSpec());
  REQUIRE(built.has_value());
  REQUIRE(*loaded == *built);
  REQUIRE(contentHash(*loaded) == contentHash(*built));
}

TEST_CASE("The three tactical identities differ where their intent says", "[tacticFiles]") {
  const Tactic possession = preset("possession");
  const Tactic counter = preset("counter");
  const Tactic pressing = preset("pressing");
  REQUIRE(contentHash(possession) != contentHash(counter));
  REQUIRE(contentHash(counter) != contentHash(pressing));
  REQUIRE(contentHash(pressing) != contentHash(possession));

  // Pressing presses earliest, hardest and on every trigger.
  REQUIRE(pressing.principles().pressingLine < possession.principles().pressingLine);
  REQUIRE(possession.principles().pressingLine < counter.principles().pressingLine);
  REQUIRE(pressing.principles().pressingTriggers.size() == 5);
  REQUIRE(counter.principles().pressingTriggers.empty());
  REQUIRE(pressing.instruction(TacticalPhase::kPressing).pressingIntensity >
          possession.instruction(TacticalPhase::kPressing).pressingIntensity);

  // Counter defends deepest and goes vertical after a regain.
  REQUIRE(counter.instruction(TacticalPhase::kDefensiveBlock).lineHeight <
          possession.instruction(TacticalPhase::kDefensiveBlock).lineHeight);
  REQUIRE(counter.instruction(TacticalPhase::kAttackingTransition).passingRisk >
          pressing.instruction(TacticalPhase::kAttackingTransition).passingRisk);
  REQUIRE(counter.instruction(TacticalPhase::kAttackingTransition).runFrequency >
          possession.instruction(TacticalPhase::kAttackingTransition).runFrequency);

  // Possession circulates safely and counterpresses right after a loss.
  REQUIRE(possession.instruction(TacticalPhase::kBuildUp).passingRisk <
          counter.instruction(TacticalPhase::kBuildUp).passingRisk);
  REQUIRE(possession.instruction(TacticalPhase::kBuildUp).blockWidth >
          counter.instruction(TacticalPhase::kBuildUp).blockWidth);
  REQUIRE(possession.instruction(TacticalPhase::kDefensiveTransition).pressingIntensity >
          possession.instruction(TacticalPhase::kDefensiveBlock).pressingIntensity);
}
