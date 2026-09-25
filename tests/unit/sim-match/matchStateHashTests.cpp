#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "kickoffScenario.hpp"
#include "matchState.hpp"
#include "matchStateHash.hpp"
#include "vec2.hpp"

using ElyverseFootball::SimCore::PlayerId;
using ElyverseFootball::SimCore::Vec2;
using ElyverseFootball::SimMatch::hashMatchState;
using ElyverseFootball::SimMatch::makeSevenASideKickoff;
using ElyverseFootball::SimMatch::MatchState;
using ElyverseFootball::SimMatch::MatchStateSpec;
using ElyverseFootball::SimMatch::Pitch;
using ElyverseFootball::SimMatch::PlayerMatchState;
using ElyverseFootball::SimMatch::TeamSide;

namespace {

[[nodiscard]] MatchStateSpec kickoffSpec() {
  const auto state = makeSevenASideKickoff(Pitch(60.0, 40.0));
  REQUIRE(state.has_value());
  return {.pitch = state->pitch(),
          .players = {state->players().begin(), state->players().end()},
          .ball = state->ball(),
          .playersPerSide = state->playersPerSide()};
}

[[nodiscard]] std::uint64_t hashOf(const MatchStateSpec& spec) {
  const auto state = MatchState::create(spec);
  REQUIRE(state.has_value());
  return hashMatchState(*state);
}

// A field's name and how to change it.
using Change = std::pair<std::string, std::function<void(MatchStateSpec&)>>;

}  // namespace

TEST_CASE("Equal states hash equally", "[matchStateHash]") {
  REQUIRE(hashOf(kickoffSpec()) == hashOf(kickoffSpec()));
}

TEST_CASE("The kickoff hash is pinned", "[matchStateHash]") {
  // Changes when the fixture, a state field or the hash encoding changes;
  // each of those invalidates recorded replays, so update it deliberately.
  REQUIRE(hashOf(kickoffSpec()) == 0xc22d772ab92fca0cULL);
}

// Guards against a field that is added to the state but forgotten here.
TEST_CASE("Every field of the state changes the hash", "[matchStateHash]") {
  const std::uint64_t original = hashOf(kickoffSpec());
  const std::vector<Change> changes{
      {"pitch length", [](auto& spec) { spec.pitch = Pitch(61.0, 40.0); }},
      {"pitch width", [](auto& spec) { spec.pitch = Pitch(60.0, 41.0); }},
      {"player id", [](auto& spec) { spec.players.at(3).playerId = PlayerId(99); }},
      {"player side",
       [](auto& spec) {
         spec.players.at(0).side = TeamSide::kAway;
         spec.players.at(13).side = TeamSide::kHome;
       }},
      {"player order", [](auto& spec) { std::swap(spec.players.at(0), spec.players.at(1)); }},
      {"player position", [](auto& spec) { spec.players.at(5).position.x += 0.001; }},
      {"player velocity", [](auto& spec) { spec.players.at(5).velocity.y = 1.0; }},
      {"max speed", [](auto& spec) { spec.players.at(2).attributes.maxSpeed = 8.0; }},
      {"acceleration", [](auto& spec) { spec.players.at(2).attributes.acceleration = 3.0; }},
      {"target", [](auto& spec) { spec.players.at(9).target = Vec2{}; }},
      {"ball position", [](auto& spec) { spec.ball.position.y = 1.0; }},
      {"ball velocity", [](auto& spec) { spec.ball.velocity.x = -1.0; }},
  };

  for (const auto& [field, change] : changes) {
    CAPTURE(field);
    MatchStateSpec spec = kickoffSpec();
    change(spec);
    REQUIRE(hashOf(spec) != original);
  }
}
