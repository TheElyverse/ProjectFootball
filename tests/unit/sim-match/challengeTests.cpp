#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <utility>
#include <variant>
#include <vector>

#include "challenge.hpp"
#include "matchEvents.hpp"
#include "matchSimulation.hpp"
#include "matchState.hpp"

using ElyverseFootball::SimCore::PlayerId;
using ElyverseFootball::SimCore::SimTick;
using ElyverseFootball::SimCore::Vec2;
using ElyverseFootball::SimMatch::ActionType;
using ElyverseFootball::SimMatch::BallTouch;
using ElyverseFootball::SimMatch::BallWon;
using ElyverseFootball::SimMatch::ChallengeConfig;
using ElyverseFootball::SimMatch::makeChallengeSystem;
using ElyverseFootball::SimMatch::MatchEvent;
using ElyverseFootball::SimMatch::MatchSimulation;
using ElyverseFootball::SimMatch::MatchState;
using ElyverseFootball::SimMatch::MatchStateWriter;
using ElyverseFootball::SimMatch::MatchStepContext;
using ElyverseFootball::SimMatch::MatchSystem;
using ElyverseFootball::SimMatch::PassIntent;
using ElyverseFootball::SimMatch::Pitch;
using ElyverseFootball::SimMatch::PlayerAction;
using ElyverseFootball::SimMatch::PlayerMatchState;
using ElyverseFootball::SimMatch::PossessionChanged;
using ElyverseFootball::SimMatch::TeamSide;

namespace {

// One a side: home's carrier 1 at (30, 20) with the ball, touched at tick 0,
// and away's player 2 at `defender`.
[[nodiscard]] MatchState duel(const Vec2 defender) {
  const auto player = [](const PlayerId::ValueType number, const TeamSide side,
                         const Vec2 position) {
    return PlayerMatchState{.playerId = PlayerId(number),
                            .side = side,
                            .position = position,
                            .velocity = {},
                            .attributes = {},
                            .target = std::nullopt,
                            .facing = {.x = 1.0, .y = 0.0}};
  };
  auto state = MatchState::create(
      {.pitch = Pitch(60.0, 40.0),
       .players = {player(1, TeamSide::kHome, {.x = 30.0, .y = 20.0}),
                   player(2, TeamSide::kAway, defender)},
       .ball = {.position = {.x = 30.5, .y = 20.0},
                .velocity = {},
                .owner = PlayerId(1),
                .lastTouch = BallTouch{.playerId = PlayerId(1), .tick = SimTick(0)}},
       .playersPerSide = 1});
  REQUIRE(state.has_value());
  return *std::move(state);
}

// Sets player 2's decided action at tick 0, and a pending pass for the
// carrier every tick, then leaves them alone.
[[nodiscard]] MatchSystem decide(const ActionType type) {
  return {
      .name = "decide",
      .update = [type](const MatchStepContext& context, const MatchState&, MatchStateWriter& next) {
        if (context.tick() == SimTick(0)) {
          next.tactical(1).action = PlayerAction{.type = type,
                                                 .target = {.x = 30.5, .y = 20.0},
                                                 .subject = PlayerId(1),
                                                 .decidedAt = SimTick(0),
                                                 .withBall = false};
        }
        next.setPendingPass(PassIntent{.passer = PlayerId(1),
                                       .target = {.x = 50.0, .y = 20.0},
                                       .speed = 10.0,
                                       .receiver = std::nullopt});
      }};
}

struct Outcome {
  std::optional<SimTick> won;
  std::vector<MatchEvent> events;
  std::size_t attempts = 0;
};

// Ten seconds of a duel.
[[nodiscard]] Outcome play(const Vec2 defender, const ActionType type, const std::uint64_t seed,
                           const ChallengeConfig& config = {}) {
  constexpr SimTick::ValueType ticks = 300;
  MatchSimulation simulation({.initialState = duel(defender),
                              .seed = seed,
                              .ticksPerSecond = 30,
                              .systems = {decide(type), makeChallengeSystem(config)},
                              .commands = {}});
  Outcome outcome;
  std::optional<SimTick> lastAttempt;
  while (simulation.tick() < SimTick(ticks) && !outcome.won) {
    REQUIRE(simulation.step().has_value());
    const auto& attempt = simulation.state().tactical(1).lastChallenge;
    if (attempt && attempt != lastAttempt) {
      ++outcome.attempts;
      lastAttempt = attempt;
    }
    for (const MatchEvent& event : simulation.events()) {
      outcome.events.push_back(event);
      if (std::holds_alternative<BallWon>(event)) {
        outcome.won = std::get<BallWon>(event).tick;
        REQUIRE(simulation.state().ball().owner == PlayerId(2));
        REQUIRE_FALSE(simulation.state().pendingPass().has_value());
      }
    }
  }
  return outcome;
}

}  // namespace

TEST_CASE("A presser within reach wins the ball about as often as the win chance", "[challenge]") {
  // First attempts, over many seeds, are won about a fifth of the time; over
  // ten seconds -- 19 attempts -- almost every duel is won.
  std::size_t firstAttemptsWon = 0;
  std::size_t won = 0;
  constexpr std::size_t kSeeds = 400;
  for (std::uint64_t seed = 1; seed <= kSeeds; ++seed) {
    const Outcome outcome = play({.x = 30.8, .y = 20.0}, ActionType::kPressCarrier, seed);
    if (!outcome.won) {
      continue;
    }
    ++won;
    // Protected for half a second after the carrier's touch at tick 0: the
    // first attempt comes at tick 15, then one every 15 ticks.
    REQUIRE(outcome.won.value_or(SimTick(0)).value() % 15 == 0);
    REQUIRE(outcome.won.value_or(SimTick(0)) >= SimTick(15));
    firstAttemptsWon += outcome.won == SimTick(15) ? 1U : 0U;
    const auto& events = outcome.events;
    REQUIRE(events.size() == 2);
    REQUIRE(std::get<BallWon>(events.at(0)) == BallWon{.tick = outcome.won.value_or(SimTick(0)),
                                                       .winner = PlayerId(2),
                                                       .loser = PlayerId(1),
                                                       .position = {.x = 30.5, .y = 20.0}});
    REQUIRE(std::get<PossessionChanged>(events.at(1)).newOwner == PlayerId(2));
  }
  REQUIRE(won > kSeeds * 95 / 100);
  const double share = static_cast<double>(firstAttemptsWon) / static_cast<double>(kSeeds);
  REQUIRE(share > 0.14);
  REQUIRE(share < 0.26);
}

TEST_CASE("Only a presser within reach challenges", "[challenge]") {
  // Out of reach, or near but not pressing: no challenge in ten seconds.
  const Outcome far = play({.x = 31.5, .y = 20.0}, ActionType::kPressCarrier, 1);
  REQUIRE(far.attempts == 0);
  REQUIRE_FALSE(far.won.has_value());
  const Outcome marking = play({.x = 30.8, .y = 20.0}, ActionType::kMarkOpponent, 1);
  REQUIRE(marking.attempts == 0);
  REQUIRE_FALSE(marking.won.has_value());
}

TEST_CASE("A losing presser challenges again only after the attempt time", "[challenge]") {
  ChallengeConfig never;
  never.winChance = 0.0;
  const Outcome outcome = play({.x = 30.8, .y = 20.0}, ActionType::kPressCarrier, 1, never);
  // Ticks 15, 30, ..., 285: 19 attempts in ten seconds.
  REQUIRE(outcome.attempts == 19);
  REQUIRE_FALSE(outcome.won.has_value());
}

TEST_CASE("The challenge system rejects an invalid configuration", "[challenge]") {
  REQUIRE_THROWS_AS(makeChallengeSystem({.intervalTicks = 0}), std::invalid_argument);
  ChallengeConfig config;
  config.winChance = 1.5;
  REQUIRE_THROWS_AS(makeChallengeSystem(config), std::invalid_argument);
  config = {};
  config.radius = 0.0;
  REQUIRE_THROWS_AS(makeChallengeSystem(config), std::invalid_argument);
  REQUIRE_NOTHROW(makeChallengeSystem({}));
}
