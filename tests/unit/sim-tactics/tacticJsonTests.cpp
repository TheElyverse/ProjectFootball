#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>

#include "referenceTactic.hpp"
#include "tactic.hpp"
#include "tacticHash.hpp"
#include "tacticJson.hpp"

using ElyverseFootball::SimTactics::contentHash;
using ElyverseFootball::SimTactics::loadTactic;
using ElyverseFootball::SimTactics::parseTacticJson;
using ElyverseFootball::SimTactics::referenceTacticSpec;
using ElyverseFootball::SimTactics::Tactic;
using ElyverseFootball::SimTactics::TacticFileError;
using ElyverseFootball::SimTactics::TacticFileErrorCode;
using ElyverseFootball::SimTactics::toTacticJson;

namespace {

using Json = nlohmann::json;

[[nodiscard]] Tactic referenceTactic() {
  auto tactic = Tactic::create(referenceTacticSpec());
  REQUIRE(tactic.has_value());
  return *std::move(tactic);
}

// The reference tactic as a document, to break one thing at a time.
[[nodiscard]] Json referenceDocument() {
  return Json::parse(toTacticJson(referenceTactic()));
}

[[nodiscard]] TacticFileError errorOf(const Json& document) {
  const auto tactic = parseTacticJson(document.dump(), "test.json");
  REQUIRE_FALSE(tactic.has_value());
  return tactic.has_value() ? TacticFileError{} : tactic.error();
}

}  // namespace

TEST_CASE("A tactic survives a round trip through its file format", "[tacticJson]") {
  const Tactic tactic = referenceTactic();
  const std::string text = toTacticJson(tactic);
  const auto parsed = parseTacticJson(text, "reference.json");
  REQUIRE(parsed.has_value());
  REQUIRE(*parsed == tactic);
  REQUIRE(contentHash(*parsed) == contentHash(tactic));
  // Written again, the file is identical: nothing is lost or reordered.
  REQUIRE(toTacticJson(*parsed) == text);
}

TEST_CASE("Loading is deterministic", "[tacticJson]") {
  const std::string text = toTacticJson(referenceTactic());
  const auto first = parseTacticJson(text, "a.json");
  const auto second = parseTacticJson(text, "a.json");
  REQUIRE(first.has_value());
  REQUIRE(first == second);
  REQUIRE(contentHash(*first) == contentHash(*second));
}

TEST_CASE("A role preset in a file stands for its responsibilities", "[tacticJson]") {
  Json document = referenceDocument();
  auto& slot = document["slots"][4];
  slot.erase("responsibilities");
  slot["role"] = "winger";
  const auto parsed = parseTacticJson(document.dump(), "roles.json");
  REQUIRE(parsed.has_value());
  // The reference tactic's slot 4 is a winger built from the same preset.
  REQUIRE(*parsed == referenceTactic());
}

TEST_CASE("Files that are not tactic files are rejected", "[tacticJson]") {
  SECTION("not JSON") {
    const auto tactic = parseTacticJson("{ nope", "broken.json");
    REQUIRE_FALSE(tactic.has_value());
    REQUIRE(tactic.error().code == TacticFileErrorCode::kMalformed);
    REQUIRE(tactic.error().message == "broken.json: not a valid JSON document");
  }
  SECTION("another format") {
    Json document = referenceDocument();
    document["format"] = "elyverse-replay";
    const auto error = errorOf(document);
    REQUIRE(error.code == TacticFileErrorCode::kMalformed);
    REQUIRE(error.message ==
            R"(test.json: format: expected "elyverse-tactic", got "elyverse-replay")");
  }
  SECTION("unsupported version") {
    Json document = referenceDocument();
    document["version"] = 2;
    const auto error = errorOf(document);
    REQUIRE(error.code == TacticFileErrorCode::kUnsupportedVersion);
    REQUIRE(error.message == "test.json: version: unsupported version 2, expected 1");
    document["version"] = "1";
    REQUIRE(errorOf(document).code == TacticFileErrorCode::kUnsupportedVersion);
  }
  SECTION("not an object") {
    REQUIRE(errorOf(Json::array()).message == "test.json: expected an object");
  }
}

TEST_CASE("Malformed fields are named in the error", "[tacticJson]") {
  Json document = referenceDocument();
  std::string expected;
  SECTION("missing") {
    document["phases"]["pressing"].erase("ballShift");
    expected = "test.json: phases.pressing.ballShift: missing";
  }
  SECTION("mistyped") {
    document["slots"][2]["position"]["depth"] = "deep";
    expected = "test.json: slots[2].position.depth: expected a number";
  }
  SECTION("unknown field") {
    document["principles"]["pressingLin"] = 0.5;
    expected = "test.json: principles.pressingLin: unknown field";
  }
  SECTION("unknown phase") {
    document["phases"]["counter"] = document["phases"]["pressing"];
    expected = "test.json: phases.counter: unknown field";
  }
  SECTION("unknown responsibility") {
    document["slots"][1]["responsibilities"][0]["responsibility"] = "sweep";
    expected =
        R"(test.json: slots[1].responsibilities[0].responsibility: unknown responsibility "sweep")";
  }
  SECTION("unknown role") {
    document["slots"][1].erase("responsibilities");
    document["slots"][1]["role"] = "libero";
    expected = R"(test.json: slots[1].role: unknown role "libero")";
  }
  SECTION("role and responsibilities") {
    document["slots"][1]["role"] = "centreBack";
    expected = R"(test.json: slots[1]: expected either "role" or "responsibilities")";
  }
  SECTION("unknown pressing trigger") {
    document["principles"]["pressingTriggers"] = {"panic"};
    expected = R"(test.json: principles.pressingTriggers[0]: unknown pressing trigger "panic")";
  }
  const auto error = errorOf(document);
  REQUIRE(error.code == TacticFileErrorCode::kMalformed);
  REQUIRE(error.message == expected);
}

TEST_CASE("A file loads only what Tactic::create accepts", "[tacticJson]") {
  Json document = referenceDocument();
  document["phases"]["pressing"]["lineHeight"] = 1.3;
  document["slots"][0]["position"]["width"] = -0.2;
  const auto error = errorOf(document);
  REQUIRE(error.code == TacticFileErrorCode::kInvalidTactic);
  REQUIRE(
      error.message ==
      "test.json: slots[0].position.width: -0.2 lies off the pitch; expected a fraction in "
      "[0, 1]; phases.pressing.lineHeight: 1.3 must lie in [0, 1]; phases.pressing.blockLength: "
      "a block of length 0.4 from a line at 1.3 reaches past the opponent's goal line");
}

TEST_CASE("The description is optional", "[tacticJson]") {
  Json document = referenceDocument();
  document.erase("description");
  const auto parsed = parseTacticJson(document.dump(), "plain.json");
  REQUIRE(parsed.has_value());
  REQUIRE(parsed->description().empty());
}

TEST_CASE("A missing file is an I/O error", "[tacticJson]") {
  const auto tactic = loadTactic("no/such/tactic.json");
  REQUIRE_FALSE(tactic.has_value());
  REQUIRE(tactic.error().code == TacticFileErrorCode::kIoError);
  REQUIRE(tactic.error().message.starts_with("no/such/tactic.json: "));
}
