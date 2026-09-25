#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "matchState.hpp"
#include "referenceTactic.hpp"
#include "tactic.hpp"

using ElyverseFootball::SimCore::PlayerId;
using ElyverseFootball::SimCore::Vec2;
using ElyverseFootball::SimMatch::BallState;
using ElyverseFootball::SimMatch::checkStartingPositions;
using ElyverseFootball::SimMatch::kDefaultAcceleration;
using ElyverseFootball::SimMatch::kDefaultMaxSpeed;
using ElyverseFootball::SimMatch::kDefaultPlayersPerSide;
using ElyverseFootball::SimMatch::kMaxBallSpeed;
using ElyverseFootball::SimMatch::MatchState;
using ElyverseFootball::SimMatch::MatchStateError;
using ElyverseFootball::SimMatch::MatchStateErrorCode;
using ElyverseFootball::SimMatch::MatchStateSpec;
using ElyverseFootball::SimMatch::ObservedEntity;
using ElyverseFootball::SimMatch::Pitch;
using ElyverseFootball::SimMatch::PlayerAttributes;
using ElyverseFootball::SimMatch::PlayerMatchState;
using ElyverseFootball::SimMatch::TeamSide;
using ElyverseFootball::SimMatch::teamSideName;

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
                         .velocity = {},
                         .attributes = {},
                         .target = std::nullopt,
                         .facing = {.x = 1.0, .y = 0.0}});
    }
  }
  return {.pitch = Pitch(kLengthMeters, kWidthMeters),
          .players = std::move(players),
          .ball = {.position = {.x = kLengthMeters / 2.0, .y = kWidthMeters / 2.0},
                   .velocity = {},
                   .owner = std::nullopt,
                   .lastTouch = std::nullopt},
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

TEST_CASE("Every player starts with an empty memory", "[matchState]") {
  const auto state = MatchState::create(validSpec());

  REQUIRE(state.has_value());
  for (std::size_t index = 0; index < state->players().size(); ++index) {
    REQUIRE(state->perception(index).observations.empty());
  }
  REQUIRE_THROWS_AS(state->perception(state->players().size()), std::out_of_range);
}

TEST_CASE("Observed entities order the ball first, then players by id", "[matchState]") {
  REQUIRE(ObservedEntity::ball() < ObservedEntity::player(PlayerId(1)));
  REQUIRE(ObservedEntity::player(PlayerId(1)) < ObservedEntity::player(PlayerId(2)));
  REQUIRE(ObservedEntity::ball().isBall());
  REQUIRE_FALSE(ObservedEntity::ball().playerId().isValid());
  REQUIRE(ObservedEntity::player(PlayerId(4)).playerId() == PlayerId(4));
}

TEST_CASE("MatchState::create accepts squad sizes other than seven", "[matchState]") {
  const auto state = MatchState::create(validSpec(11));

  REQUIRE(state.has_value());
  REQUIRE(state->players().size() == 22);
  REQUIRE(state->playersPerSide() == 11);
}

TEST_CASE("MatchState::create accepts players and a ball outside the pitch", "[matchState]") {
  // A ball over the touchline or a player behind the goal line is football,
  // not a broken state; only starting states must be on the pitch.
  MatchStateSpec spec = validSpec();
  spec.players.at(0).position = {.x = -3.0, .y = 20.0};
  spec.players.at(8).position = {.x = 30.0, .y = kWidthMeters + 2.0};
  spec.ball.position = {.x = kLengthMeters + 1.5, .y = 18.0};

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

TEST_CASE("MatchState::create rejects a team side outside the declared enumerators",
          "[matchState]") {
  MatchStateSpec spec = validSpec();
  spec.players.at(9).side = static_cast<TeamSide>(2);

  const auto state = MatchState::create(spec);

  // The squad could still be seven per side if that player were away, so the
  // unknown side is the only defect reported.
  REQUIRE_FALSE(state.has_value());
  REQUIRE(codesOf(state.error()) == std::vector{MatchStateErrorCode::kInvalidTeamSide});
  CAPTURE(state.error().front().message);
  REQUIRE(mentions(state.error().front().message, "index 9"));
  REQUIRE(mentions(state.error().front().message, "team side value 2"));
}

TEST_CASE("Players with an unknown side are not counted as away", "[matchState]") {
  // Seven home players plus seven with an undeclared side: no away team.
  MatchStateSpec spec = validSpec();
  for (PlayerMatchState& player : spec.players) {
    if (player.side == TeamSide::kAway) {
      player.side = static_cast<TeamSide>(2);
    }
  }

  const auto state = MatchState::create(spec);

  REQUIRE_FALSE(state.has_value());
  const std::vector<MatchStateErrorCode> codes = codesOf(state.error());
  REQUIRE(codes.size() == 7);
  for (const MatchStateErrorCode code : codes) {
    REQUIRE(code == MatchStateErrorCode::kInvalidTeamSide);
  }
}

TEST_CASE("Unknown sides do not hide a squad that is too large", "[matchState]") {
  MatchStateSpec spec = validSpec();
  PlayerMatchState extra = spec.players.front();
  extra.playerId = PlayerId(100);
  extra.side = static_cast<TeamSide>(7);
  spec.players.push_back(extra);

  const auto state = MatchState::create(spec);

  REQUIRE_FALSE(state.has_value());
  REQUIRE(codesOf(state.error()) == std::vector{MatchStateErrorCode::kWrongPlayerCountPerSide,
                                                MatchStateErrorCode::kInvalidTeamSide});
  CAPTURE(state.error().front().message);
  REQUIRE(mentions(state.error().front().message, "1 more with an unknown side"));
}

TEST_CASE("teamSideName does not label undeclared values as a side", "[matchState]") {
  REQUIRE(teamSideName(TeamSide::kHome) == "home");
  REQUIRE(teamSideName(TeamSide::kAway) == "away");
  REQUIRE(teamSideName(static_cast<TeamSide>(2)) == "unknown");
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

TEST_CASE("MatchState::create rejects a non-finite player position", "[matchState]") {
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

TEST_CASE("MatchState::create rejects a player faster than his max speed", "[matchState]") {
  MatchStateSpec spec = validSpec();
  spec.players.at(6).velocity = {.x = 10.0, .y = 0.0};

  const auto state = MatchState::create(spec);

  REQUIRE_FALSE(state.has_value());
  REQUIRE(codesOf(state.error()) == std::vector{MatchStateErrorCode::kPlayerTooFast});
  CAPTURE(state.error().front().message);
  REQUIRE(mentions(state.error().front().message, "index 6"));

  // Exactly at the limit is fine.
  spec.players.at(6).velocity = {.x = 0.0, .y = spec.players.at(6).attributes.maxSpeed};
  REQUIRE(MatchState::create(spec).has_value());
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
  CAPTURE(state.error().front().message);
  REQUIRE(mentions(state.error().front().message, "index 6"));
  REQUIRE(mentions(state.error().front().message, ") m/s"));
}

TEST_CASE("Players default to the documented movement limits and no target", "[matchState]") {
  const PlayerMatchState player{};

  REQUIRE(player.attributes.maxSpeed == kDefaultMaxSpeed);
  REQUIRE(player.attributes.acceleration == kDefaultAcceleration);
  REQUIRE_FALSE(player.target.has_value());
}

TEST_CASE("MatchState::create accepts a target outside the pitch", "[matchState]") {
  // Commands keep targets on the pitch; the state itself only requires them
  // to be finite, like positions.
  MatchStateSpec spec = validSpec();
  spec.players.at(3).target = Vec2{.x = -5.0, .y = 50.0};

  const auto state = MatchState::create(spec);

  REQUIRE(state.has_value());
  REQUIRE(state->players()[3].target == Vec2{.x = -5.0, .y = 50.0});
}

TEST_CASE("MatchState::create rejects attributes that are not positive and finite",
          "[matchState]") {
  const double invalidValue = GENERATE(0.0, -1.0, std::numeric_limits<double>::quiet_NaN(),
                                       std::numeric_limits<double>::infinity());
  const bool breakSpeed = GENERATE(true, false);
  CAPTURE(invalidValue, breakSpeed);
  MatchStateSpec spec = validSpec();
  PlayerAttributes& attributes = spec.players.at(5).attributes;
  (breakSpeed ? attributes.maxSpeed : attributes.acceleration) = invalidValue;

  const auto state = MatchState::create(spec);

  REQUIRE_FALSE(state.has_value());
  REQUIRE(codesOf(state.error()) == std::vector{MatchStateErrorCode::kInvalidPlayerAttributes});
  CAPTURE(state.error().front().message);
  REQUIRE(mentions(state.error().front().message, "index 5"));
  REQUIRE(mentions(state.error().front().message, "expected both positive and finite"));
}

TEST_CASE("MatchState::create rejects a non-finite target", "[matchState]") {
  MatchStateSpec spec = validSpec();
  spec.players.at(10).target = Vec2{.x = std::numeric_limits<double>::quiet_NaN(), .y = 3.0};

  const auto state = MatchState::create(spec);

  REQUIRE_FALSE(state.has_value());
  REQUIRE(codesOf(state.error()) == std::vector{MatchStateErrorCode::kNonFinitePlayerTarget});
  CAPTURE(state.error().front().message);
  REQUIRE(mentions(state.error().front().message, "index 10"));
  REQUIRE(mentions(state.error().front().message, "non-finite target"));
}

TEST_CASE("MatchState::create rejects a facing that is not a unit vector", "[matchState]") {
  const Vec2 facing = GENERATE(Vec2{}, Vec2{.x = 0.5, .y = 0.0}, Vec2{.x = 1.0, .y = 1.0},
                               Vec2{.x = std::numeric_limits<double>::quiet_NaN(), .y = 0.0});
  CAPTURE(facing.x, facing.y);
  MatchStateSpec spec = validSpec();
  spec.players.at(12).facing = facing;

  const auto state = MatchState::create(spec);

  REQUIRE_FALSE(state.has_value());
  REQUIRE(codesOf(state.error()) == std::vector{MatchStateErrorCode::kInvalidPlayerFacing});
  CAPTURE(state.error().front().message);
  REQUIRE(mentions(state.error().front().message, "index 12"));
  REQUIRE(mentions(state.error().front().message, "not a finite unit vector"));
}

TEST_CASE("MatchState::create accepts any unit facing", "[matchState]") {
  MatchStateSpec spec = validSpec();
  spec.players.at(0).facing = {.x = 0.6, .y = -0.8};
  spec.players.at(1).facing = {.x = 0.0, .y = 1.0};

  REQUIRE(MatchState::create(spec).has_value());
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

TEST_CASE("MatchState::create rejects a ball faster than any kick", "[matchState]") {
  MatchStateSpec spec = validSpec();
  // Squaring this speed would overflow.
  spec.ball.velocity = {.x = 1e155, .y = 0.0};

  const auto state = MatchState::create(spec);

  REQUIRE_FALSE(state.has_value());
  REQUIRE(codesOf(state.error()) == std::vector{MatchStateErrorCode::kBallTooFast});

  spec.ball.velocity = {.x = 0.0, .y = -kMaxBallSpeed};
  REQUIRE(MatchState::create(spec).has_value());
}

TEST_CASE("MatchState::create reports every broken rule in a fixed order", "[matchState]") {
  MatchStateSpec spec = validSpec();
  spec.players.pop_back();
  spec.players.at(9).playerId = spec.players.at(2).playerId;
  spec.players.at(11).position = {.x = std::numeric_limits<double>::infinity(), .y = 20.0};
  spec.ball.velocity = {.x = std::numeric_limits<double>::quiet_NaN(), .y = 0.0};

  const auto state = MatchState::create(spec);

  REQUIRE_FALSE(state.has_value());
  REQUIRE(codesOf(state.error()) == std::vector{MatchStateErrorCode::kWrongPlayerCountPerSide,
                                                MatchStateErrorCode::kDuplicatePlayerId,
                                                MatchStateErrorCode::kNonFinitePlayerPosition,
                                                MatchStateErrorCode::kNonFiniteBallVelocity});
  for (const MatchStateError& error : state.error()) {
    CAPTURE(error.message);
    REQUIRE_FALSE(error.message.empty());
  }
}

TEST_CASE("checkStartingPositions accepts positions on the pitch boundary", "[matchState]") {
  MatchStateSpec spec = validSpec();
  spec.players.at(0).position = {.x = 0.0, .y = 0.0};
  spec.players.at(1).position = {.x = kLengthMeters, .y = kWidthMeters};
  spec.ball.position = {.x = kLengthMeters, .y = 0.0};
  const auto state = MatchState::create(spec);
  REQUIRE(state.has_value());

  REQUIRE(checkStartingPositions(*state).empty());
}

TEST_CASE("checkStartingPositions rejects a player outside the pitch", "[matchState]") {
  MatchStateSpec spec = validSpec();
  spec.players.at(4).position = {.x = std::nextafter(kLengthMeters, kLengthMeters + 1.0),
                                 .y = 20.0};
  const auto state = MatchState::create(spec);
  REQUIRE(state.has_value());

  const std::vector<MatchStateError> errors = checkStartingPositions(*state);

  REQUIRE(codesOf(errors) == std::vector{MatchStateErrorCode::kPlayerOutsidePitch});
  CAPTURE(errors.front().message);
  REQUIRE(mentions(errors.front().message, "index 4"));
  REQUIRE(mentions(errors.front().message, "outside the 60 x 40 m pitch"));
  // One ulp past the touchline must not print as a point on it.
  REQUIRE(mentions(errors.front().message, "at (60.00000000000001, 20) m"));
}

TEST_CASE("checkStartingPositions rejects a ball outside the pitch", "[matchState]") {
  MatchStateSpec spec = validSpec();
  spec.ball.position = {.x = -0.5, .y = 20.0};
  const auto state = MatchState::create(spec);
  REQUIRE(state.has_value());

  const std::vector<MatchStateError> errors = checkStartingPositions(*state);

  REQUIRE(codesOf(errors) == std::vector{MatchStateErrorCode::kBallOutsidePitch});
  CAPTURE(errors.front().message);
  REQUIRE(mentions(errors.front().message, "the ball is outside the 60 x 40 m pitch"));
  REQUIRE(mentions(errors.front().message, "at (-0.5, 20) m"));
}

TEST_CASE("checkStartingPositions reports players by index, then the ball", "[matchState]") {
  MatchStateSpec spec = validSpec();
  spec.players.at(11).position = {.x = 200.0, .y = 20.0};
  spec.players.at(3).position = {.x = 20.0, .y = -1.0};
  spec.ball.position = {.x = 20.0, .y = 100.0};
  const auto state = MatchState::create(spec);
  REQUIRE(state.has_value());

  const std::vector<MatchStateError> errors = checkStartingPositions(*state);

  REQUIRE(codesOf(errors) == std::vector{MatchStateErrorCode::kPlayerOutsidePitch,
                                         MatchStateErrorCode::kPlayerOutsidePitch,
                                         MatchStateErrorCode::kBallOutsidePitch});
  REQUIRE(mentions(errors.at(0).message, "index 3"));
  REQUIRE(mentions(errors.at(1).message, "index 11"));
}

namespace {

[[nodiscard]] ElyverseFootball::SimTactics::Tactic referenceTactic() {
  auto tactic = ElyverseFootball::SimTactics::Tactic::create(
      ElyverseFootball::SimTactics::referenceTacticSpec());
  REQUIRE(tactic.has_value());
  return *std::move(tactic);
}

}  // namespace

TEST_CASE("A side plays a tactic or is scripted", "[matchState]") {
  const auto untactical = MatchState::create(validSpec());
  REQUIRE(untactical.has_value());
  REQUIRE_FALSE(untactical->tactics().home.has_value());
  REQUIRE_FALSE(untactical->tactics().away.has_value());

  const auto state = MatchState::create(validSpec(), {.home = referenceTactic(), .away = {}});
  REQUIRE(state.has_value());
  REQUIRE(state->tactics().of(TeamSide::kHome) == referenceTactic());
  REQUIRE_FALSE(state->tactics().of(TeamSide::kAway).has_value());
  REQUIRE(*state != *untactical);
}

TEST_CASE("MatchState::create rejects a tactic that does not fit the squad", "[matchState]") {
  const auto state = MatchState::create(validSpec(6), {.home = {}, .away = referenceTactic()});
  REQUIRE_FALSE(state.has_value());
  REQUIRE(codesOf(state.error()) == std::vector{MatchStateErrorCode::kTacticDoesNotFitSquad});
  REQUIRE(state.error().front().message == "away's tactic 'reference' has 7 slots for 6 players");
}

TEST_CASE("A player's slot is his place within his side", "[matchState]") {
  auto spec = validSpec();
  // Interleave the sides: home, away, home, away, ...
  std::vector<PlayerMatchState> interleaved;
  for (std::size_t index = 0; index < 7; ++index) {
    interleaved.push_back(spec.players.at(index));
    interleaved.push_back(spec.players.at(index + 7));
  }
  spec.players = interleaved;
  const auto state = MatchState::create(spec);
  REQUIRE(state.has_value());
  REQUIRE(ElyverseFootball::SimMatch::slotIndex(*state, 0) == 0);
  REQUIRE(ElyverseFootball::SimMatch::slotIndex(*state, 1) == 0);
  REQUIRE(ElyverseFootball::SimMatch::slotIndex(*state, 4) == 2);
  REQUIRE(ElyverseFootball::SimMatch::slotIndex(*state, 13) == 6);
  REQUIRE_THROWS_AS(ElyverseFootball::SimMatch::slotIndex(*state, 14), std::out_of_range);
}
