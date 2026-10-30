// The set-piece scenarios (docs/scenarios.md#the-set-piece-scenarios): a
// corner and a throw-in of the eleven-a-side reference, and the sandbox's goal
// kick. The ball lies dead until the taker plays it as his plan says, and
// both sides take up their places first (docs/set-pieces.md).

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <optional>
#include <string_view>
#include <variant>

#include "ids.hpp"
#include "matchEvents.hpp"
#include "matchFixtures.hpp"
#include "matchSetup.hpp"
#include "matchSimulation.hpp"
#include "matchState.hpp"
#include "pitch.hpp"
#include "setPieces.hpp"
#include "simTime.hpp"
#include "teamFrame.hpp"
#include "zones.hpp"

using ElyverseFootball::SimCore::SimTick;
using ElyverseFootball::SimCore::Vec2;
using ElyverseFootball::SimMatch::GoalEnd;
using ElyverseFootball::SimMatch::keepsGoal;
using ElyverseFootball::SimMatch::MatchEvent;
using ElyverseFootball::SimMatch::MatchSimulation;
using ElyverseFootball::SimMatch::MatchState;
using ElyverseFootball::SimMatch::PlayerMatchState;
using ElyverseFootball::SimMatch::PossessionChanged;
using ElyverseFootball::SimMatch::RestartTaken;
using ElyverseFootball::SimMatch::SetPiece;
using ElyverseFootball::SimMatch::SetPieceTaken;
using ElyverseFootball::SimMatch::setPieceTargets;
using ElyverseFootball::SimMatch::startMatch;
using ElyverseFootball::SimMatch::TeamSide;
using ElyverseFootball::SimTactics::SetPieceKind;
using ElyverseFootball::TestSupport::setupOf;

namespace {

// The scenario up to the set piece being taken: what was set up, the state
// the taker played it from, the event, and when it was awarded.
struct Taken {
  SetPiece setPiece;
  MatchState before;
  SetPieceTaken taken;
  SimTick awarded;
  bool touchedDead = false;
};

[[nodiscard]] Taken played(const std::string_view scenario, const std::uint64_t seed) {
  MatchSimulation simulation = startMatch(setupOf(scenario, seed));
  std::optional<Taken> result;
  std::optional<SetPiece> setPiece;
  bool touchedDead = false;
  while (!result && simulation.tick() < SimTick(900)) {
    const MatchState before = simulation.state();
    REQUIRE(simulation.step().has_value());
    if (!setPiece && simulation.state().setPiece()) {
      setPiece = simulation.state().setPiece();
    }
    for (const MatchEvent& event : simulation.events()) {
      if (const auto* taken = std::get_if<SetPieceTaken>(&event)) {
        REQUIRE(setPiece.has_value());
        result = Taken{.setPiece = setPiece.value_or(SetPiece{}),
                       .before = before,
                       .taken = *taken,
                       .awarded = setPiece.value_or(SetPiece{}).awarded,
                       .touchedDead = touchedDead};
      } else if (setPiece && std::holds_alternative<PossessionChanged>(event) &&
                 !std::holds_alternative<RestartTaken>(event)) {
        // Only the taker's own touch gives the dead ball to anyone.
        const auto& change = std::get<PossessionChanged>(event);
        touchedDead = touchedDead || (change.newOwner && change.newOwner != setPiece->taker);
      }
    }
  }
  REQUIRE(result.has_value());
  return *std::move(result);  // NOLINT(bugprone-unchecked-optional-access) -- required above.
}

// How many players of `side` stand within `radius` of where the set piece
// sends them.
[[nodiscard]] int inPlace(const Taken& taken, const TeamSide side, const double radius) {
  const auto targets = setPieceTargets(taken.before, taken.setPiece, {}, {});
  int count = 0;
  for (std::size_t index = 0; index < targets.size(); ++index) {
    const PlayerMatchState& player = taken.before.players()[index];
    const auto& target = targets[index];
    if (player.side == side && target &&
        ElyverseFootball::SimCore::distance(player.position, target.value_or(Vec2{})) <= radius) {
      ++count;
    }
  }
  return count;
}

}  // namespace

TEST_CASE("Set pieces: a corner is taken from its plan, both sides in place",
          "[acceptance][setPieces]") {
  for (std::uint64_t seed = 1; seed <= 3; ++seed) {
    CAPTURE(seed);
    const Taken taken = played("corner", seed);
    REQUIRE(taken.setPiece.kind == SetPieceKind::kCorner);
    REQUIRE(taken.setPiece.side == TeamSide::kHome);
    REQUIRE_FALSE(taken.touchedDead);
    // Not before its cornerSeconds.
    REQUIRE(taken.taken.tick.value() - taken.awarded.value() >=
            static_cast<SimTick::ValueType>(
                ElyverseFootball::SimMatch::SetPieceConfig{}.cornerSeconds * 30));
    // The reference plans a far-post delivery to its first runner.
    REQUIRE(taken.taken.delivery == ElyverseFootball::SimTactics::SetPieceDelivery::kFarPost);
    REQUIRE(taken.taken.receiver.has_value());
    // Home's runners in the box, away's markers and zones with them.
    const MatchState& state = taken.before;
    const auto area = state.pitch().penaltyArea(GoalEnd::kMaxX);
    int attackers = 0;
    for (std::size_t index = 0; index < state.players().size(); ++index) {
      const PlayerMatchState& player = state.players()[index];
      attackers += player.side == TeamSide::kHome && area.contains(player.position) ? 1 : 0;
    }
    REQUIRE(attackers >= 3);
    REQUIRE(inPlace(taken, TeamSide::kHome, 2.0) >= 4);
    CAPTURE(taken.taken.tick.value() - taken.awarded.value(), inPlace(taken, TeamSide::kAway, 2.0),
            inPlace(taken, TeamSide::kAway, 5.0), inPlace(taken, TeamSide::kHome, 2.0));
    REQUIRE(inPlace(taken, TeamSide::kAway, 2.0) >= 5);
    // Nobody of away within the distance the Laws ask of the ball.
    for (const PlayerMatchState& player : state.players()) {
      if (player.side == TeamSide::kAway) {
        REQUIRE(ElyverseFootball::SimCore::distance(player.position, taken.setPiece.spot) >=
                state.pitch().markings().centerCircleRadiusMeters - 0.5);
      }
    }
  }
}

TEST_CASE("Set pieces: a throw-in finds a near option, marked", "[acceptance][setPieces]") {
  for (std::uint64_t seed = 1; seed <= 3; ++seed) {
    CAPTURE(seed);
    const Taken taken = played("throw-in", seed);
    REQUIRE(taken.setPiece.kind == SetPieceKind::kThrowIn);
    REQUIRE_FALSE(taken.touchedDead);
    REQUIRE(taken.taken.tick.value() - taken.awarded.value() >=
            static_cast<SimTick::ValueType>(
                ElyverseFootball::SimMatch::SetPieceConfig{}.throwInSeconds * 30));
    REQUIRE(taken.taken.delivery == ElyverseFootball::SimTactics::SetPieceDelivery::kShort);
    REQUIRE(taken.taken.receiver.has_value());
    // The receiver is one of the two options, near the thrower.
    REQUIRE(ElyverseFootball::SimCore::distance(taken.taken.target, taken.setPiece.spot) < 15.0);
    REQUIRE(inPlace(taken, TeamSide::kHome, 2.0) >= 2);
    REQUIRE(inPlace(taken, TeamSide::kAway, 2.0) >= 2);
  }
}

TEST_CASE("Set pieces: a goal kick waits until the opponents have left the area",
          "[acceptance][setPieces]") {
  for (std::uint64_t seed = 1; seed <= 3; ++seed) {
    CAPTURE(seed);
    const Taken taken = played("goal-kick", seed);
    REQUIRE(taken.setPiece.kind == SetPieceKind::kGoalKick);
    REQUIRE(taken.taken.tick.value() - taken.awarded.value() >=
            static_cast<SimTick::ValueType>(
                ElyverseFootball::SimMatch::SetPieceConfig{}.goalKickSeconds * 30));
    // Home, the opponents, have left away's penalty area.
    const auto area = taken.before.pitch().penaltyArea(GoalEnd::kMaxX);
    for (const PlayerMatchState& player : taken.before.players()) {
      if (player.side == TeamSide::kHome) {
        REQUIRE_FALSE(area.contains(player.position));
      }
    }
  }
}
