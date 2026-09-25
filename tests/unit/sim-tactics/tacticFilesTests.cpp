// The tactic files under data/tactics/: every one loads, and the reference
// tactic file is the tactic referenceTacticSpec() builds in code.

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <string>
#include <vector>

#include "referenceTactic.hpp"
#include "tactic.hpp"
#include "tacticHash.hpp"
#include "tacticJson.hpp"

using ElyverseFootball::SimTactics::contentHash;
using ElyverseFootball::SimTactics::loadTactic;
using ElyverseFootball::SimTactics::referenceTacticSpec;
using ElyverseFootball::SimTactics::Tactic;

namespace {

// Set by tests/unit/CMakeLists.txt to the repository's data directory.
const std::filesystem::path kTacticsDirectory = std::filesystem::path(PF_DATA_DIR) / "tactics";

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
