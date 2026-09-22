#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <cmath>
#include <cstddef>
#include <limits>
#include <string>
#include <vector>

#include "matchState.hpp"

using ElyverseFootball::SimCore::PlayerId;
using ElyverseFootball::SimCore::Vec2;
using ElyverseFootball::SimMatch::BallState;
using ElyverseFootball::SimMatch::kDefaultPlayersPerSide;
using ElyverseFootball::SimMatch::MatchState;
using ElyverseFootball::SimMatch::MatchStateError;
using ElyverseFootball::SimMatch::MatchStateErrorCode;
using ElyverseFootball::SimMatch::MatchStateSpec;
using ElyverseFootball::SimMatch::Pitch;
using ElyverseFootball::SimMatch::PlayerMatchState;
using ElyverseFootball::SimMatch::TeamSide;

namespace {

constexpr double kLengthMeters = 60.0;
constexpr double kWidthMeters = 40.0;

// A spec that breaks no rule: both sides fully staffed, everyone inside the
// pitch with unique ids, the ball on the center spot. Tests break exactly one
// thing about it at a time.
[[nodiscard]] MatchStateSpec validSpec(const int playersPerSide = kDefaultPlayersPerSide) {
  std::vector<PlayerMatchState> players;
  PlayerId::ValueType nextId = 1;
  for (const TeamSide side : {TeamSide::kHome, TeamSide::kAway}) {
    const double lineX = side == TeamSide::kHome ? 15.0 : 45.0;
    for (int slot = 0; slot < playersPerSide; ++slot) {
      players.push_back({.playerId = PlayerId(nextId++),
                         .side = side,
                         .position = {.x = lineX, .y = 2.0 + (2.0 * static_cast<double>(slot))},
                         .velocity = {}});
    }
  }
  return {.pitch = Pitch(kLengthMeters, kWidthMeters),
          .players = std::move(players),
          .ball = {.position = {.x = kLengthMeters / 2.0, .y = kWidthMeters / 2.0}, .velocity = {}},
          .playersPerSide = playersPerSide};
}

[[nodiscard]] std::vector<MatchStateErrorCode> codesOf(const std::vector<MatchStateError>& errors) {
  std::vector<MatchStateErrorCode> codes;
  codes.reserve(errors.size());
  for (const MatchStateError& error : errors) {
    codes.push_back(error.code);
  }
  return codes;
}

[[nodiscard]] bool mentions(const std::string& message, const std::string& fragment) {
  return message.find(fragment) != std::string::npos;
}

}  // namespace

TEST_CASE("MatchState::create accepts a valid seven-a-side spec", "[matchState]") {
  const MatchStateSpec spec = validSpec();
  const auto state = MatchState::create(spec);

  REQUIRE(state.has_value());
  REQUIRE(state->players().size() == 14);
  REQUIRE(state->playersPerSide() == kDefaultPlayersPerSide);
  REQUIRE(state->pitch() == Pitch(kLengthMeters, kWidthMeters));
  REQUIRE(state->ball() == spec.ball);
  REQUIRE(state->players().front() == spec.players.front());
}

TEST_CASE("MatchState::create accepts squad sizes other than seven", "[matchState]") {
  const auto state = MatchState::create(validSpec(11));

  REQUIRE(state.has_value());
  REQUIRE(state->players().size() == 22);
  REQUIRE(state->playersPerSide() == 11);
}

TEST_CASE("MatchState::create accepts players on the pitch boundary", "[matchState]") {
  MatchStateSpec spec = validSpec();
  spec.players.at(0).position = {.x = 0.0, .y = 0.0};
  spec.players.at(1).position = {.x = kLengthMeters, .y = kWidthMeters};

  REQUIRE(MatchState::create(spec).has_value());
}

TEST_CASE("Equal specs produce equal states", "[matchState]") {
  const auto first = MatchState::create(validSpec());
  const auto second = MatchState::create(validSpec());

  REQUIRE(first.has_value());
  REQUIRE(second.has_value());
  REQUIRE(*first == *second);
}

TEST_CASE("Player order is part of the state", "[matchState]") {
  MatchStateSpec reordered = validSpec();
  std::swap(reordered.players.at(0), reordered.players.at(1));

  const auto original = MatchState::create(validSpec());
  const auto swapped = MatchState::create(reordered);

  REQUIRE(original.has_value());
  REQUIRE(swapped.has_value());
  REQUIRE_FALSE(*original == *swapped);
}

TEST_CASE("MatchState::create rejects a squad that is not seven per side", "[matchState]") {
  MatchStateSpec spec = validSpec();
  spec.players.pop_back();

  const auto state = MatchState::create(spec);

  REQUIRE_FALSE(state.has_value());
  REQUIRE(codesOf(state.error()) == std::vector{MatchStateErrorCode::kWrongPlayerCountPerSide});
  CAPTURE(state.error().front().message);
  REQUIRE(mentions(state.error().front().message, "home has 7 players and away has 6"));
  REQUIRE(mentions(state.error().front().message, "expected 7 per side"));
}

TEST_CASE("MatchState::create rejects a nonpositive squad size", "[matchState]") {
  const int playersPerSide = GENERATE(0, -1);
  MatchStateSpec spec = validSpec();
  spec.playersPerSide = playersPerSide;

  const auto state = MatchState::create(spec);

  REQUIRE_FALSE(state.has_value());
  REQUIRE(codesOf(state.error()) == std::vector{MatchStateErrorCode::kInvalidPlayersPerSide});
  CAPTURE(state.error().front().message);
  REQUIRE(mentions(state.error().front().message, "playersPerSide must be at least 1"));
  REQUIRE(mentions(state.error().front().message, std::to_string(playersPerSide)));
}

TEST_CASE("MatchState::create rejects duplicate player ids", "[matchState]") {
  MatchStateSpec spec = validSpec();
  spec.players.at(8).playerId = spec.players.at(3).playerId;

  const auto state = MatchState::create(spec);

  REQUIRE_FALSE(state.has_value());
  REQUIRE(codesOf(state.error()) == std::vector{MatchStateErrorCode::kDuplicatePlayerId});
  CAPTURE(state.error().front().message);
  REQUIRE(mentions(state.error().front().message, "duplicate player id 4"));
  REQUIRE(mentions(state.error().front().message, "at index 8"));
  REQUIRE(mentions(state.error().front().message, "first seen at index 3"));
}

TEST_CASE("MatchState::create rejects an invalid player id", "[matchState]") {
  MatchStateSpec spec = validSpec();
  spec.players.at(2).playerId = PlayerId::invalid();

  const auto state = MatchState::create(spec);

  REQUIRE_FALSE(state.has_value());
  REQUIRE(codesOf(state.error()) == std::vector{MatchStateErrorCode::kInvalidPlayerId});
  CAPTURE(state.error().front().message);
  REQUIRE(mentions(state.error().front().message, "index 2"));
  REQUIRE(mentions(state.error().front().message, "no valid player id"));
}

TEST_CASE("Two invalid ids are not reported as duplicates of each other", "[matchState]") {
  MatchStateSpec spec = validSpec();
  spec.players.at(2).playerId = PlayerId::invalid();
  spec.players.at(5).playerId = PlayerId::invalid();

  const auto state = MatchState::create(spec);

  REQUIRE_FALSE(state.has_value());
  REQUIRE(codesOf(state.error()) == std::vector{MatchStateErrorCode::kInvalidPlayerId,
                                                MatchStateErrorCode::kInvalidPlayerId});
}

TEST_CASE("MatchState::create rejects a player outside the pitch", "[matchState]") {
  MatchStateSpec spec = validSpec();
  spec.players.at(4).position = {.x = std::nextafter(kLengthMeters, kLengthMeters + 1.0),
                                 .y = 20.0};

  const auto state = MatchState::create(spec);

  REQUIRE_FALSE(state.has_value());
  REQUIRE(codesOf(state.error()) == std::vector{MatchStateErrorCode::kPlayerOutsidePitch});
  CAPTURE(state.error().front().message);
  REQUIRE(mentions(state.error().front().message, "index 4"));
  REQUIRE(mentions(state.error().front().message, "outside the 60.00 x 40.00 m pitch"));
}

TEST_CASE("A non-finite player position is reported once, not twice", "[matchState]") {
  const double invalidValue =
      GENERATE(std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(),
               -std::numeric_limits<double>::infinity());
  CAPTURE(invalidValue);
  MatchStateSpec spec = validSpec();
  spec.players.at(1).position = {.x = invalidValue, .y = 20.0};

  const auto state = MatchState::create(spec);

  REQUIRE_FALSE(state.has_value());
  REQUIRE(codesOf(state.error()) == std::vector{MatchStateErrorCode::kNonFinitePlayerPosition});
  REQUIRE(mentions(state.error().front().message, "non-finite position"));
}

TEST_CASE("MatchState::create rejects a non-finite player velocity", "[matchState]") {
  const double invalidValue =
      GENERATE(std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity());
  CAPTURE(invalidValue);
  MatchStateSpec spec = validSpec();
  spec.players.at(6).velocity = {.x = 1.0, .y = invalidValue};

  const auto state = MatchState::create(spec);

  REQUIRE_FALSE(state.has_value());
  REQUIRE(codesOf(state.error()) == std::vector{MatchStateErrorCode::kNonFinitePlayerVelocity});
  REQUIRE(mentions(state.error().front().message, "index 6"));
}

TEST_CASE("MatchState::create rejects a ball outside the pitch", "[matchState]") {
  MatchStateSpec spec = validSpec();
  spec.ball.position = {.x = -0.5, .y = 20.0};

  const auto state = MatchState::create(spec);

  REQUIRE_FALSE(state.has_value());
  REQUIRE(codesOf(state.error()) == std::vector{MatchStateErrorCode::kBallOutsidePitch});
  REQUIRE(mentions(state.error().front().message, "the ball is outside"));
}

TEST_CASE("MatchState::create rejects a non-finite ball state", "[matchState]") {
  MatchStateSpec spec = validSpec();
  spec.ball.position = {.x = std::numeric_limits<double>::quiet_NaN(), .y = 20.0};
  spec.ball.velocity = {.x = 0.0, .y = std::numeric_limits<double>::infinity()};

  const auto state = MatchState::create(spec);

  REQUIRE_FALSE(state.has_value());
  REQUIRE(codesOf(state.error()) == std::vector{MatchStateErrorCode::kNonFiniteBallPosition,
                                                MatchStateErrorCode::kNonFiniteBallVelocity});
}

TEST_CASE("MatchState::create reports every broken rule in a fixed order", "[matchState]") {
  MatchStateSpec spec = validSpec();
  spec.players.pop_back();
  spec.players.at(9).playerId = spec.players.at(2).playerId;
  spec.players.at(11).position = {.x = 200.0, .y = 20.0};
  spec.ball.position = {.x = 20.0, .y = 100.0};

  const auto state = MatchState::create(spec);

  REQUIRE_FALSE(state.has_value());
  REQUIRE(codesOf(state.error()) == std::vector{MatchStateErrorCode::kWrongPlayerCountPerSide,
                                                MatchStateErrorCode::kDuplicatePlayerId,
                                                MatchStateErrorCode::kPlayerOutsidePitch,
                                                MatchStateErrorCode::kBallOutsidePitch});
  for (const MatchStateError& error : state.error()) {
    CAPTURE(error.message);
    REQUIRE_FALSE(error.message.empty());
  }
}
