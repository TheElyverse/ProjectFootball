// Aerial duels and headers (docs/aerial-duels.md): the jump, who wins a high
// ball, what the winner heads it into -- a goal, a clearance --, a ball
// nobody reaches, the same delivery along the ground, and the keeper's hands.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <utility>
#include <variant>
#include <vector>

#include "aerialDuels.hpp"
#include "ballMovement.hpp"
#include "ballPhysics.hpp"
#include "goalFrame.hpp"
#include "ids.hpp"
#include "matchEvents.hpp"
#include "matchSimulation.hpp"
#include "matchState.hpp"
#include "passing.hpp"
#include "pitch.hpp"
#include "playerMovement.hpp"
#include "random.hpp"
#include "reception.hpp"
#include "referenceTactic.hpp"
#include "restart.hpp"
#include "shooting.hpp"
#include "shotStopping.hpp"
#include "simTime.hpp"
#include "tactic.hpp"
#include "vec2.hpp"

using Catch::Matchers::WithinAbs;
using ElyverseFootball::SimCore::PlayerId;
using ElyverseFootball::SimCore::RandomNumberGenerator;
using ElyverseFootball::SimCore::SimTick;
using ElyverseFootball::SimCore::Vec2;
using ElyverseFootball::SimMatch::AerialChallenger;
using ElyverseFootball::SimMatch::AerialConfig;
using ElyverseFootball::SimMatch::AerialContest;
using ElyverseFootball::SimMatch::AerialJump;
using ElyverseFootball::SimMatch::AerialPlay;
using ElyverseFootball::SimMatch::BallPhysics;
using ElyverseFootball::SimMatch::BallState;
using ElyverseFootball::SimMatch::BallTouch;
using ElyverseFootball::SimMatch::duelUtility;
using ElyverseFootball::SimMatch::executeHeader;
using ElyverseFootball::SimMatch::GoalScored;
using ElyverseFootball::SimMatch::headerErrorFactor;
using ElyverseFootball::SimMatch::HeaderIntent;
using ElyverseFootball::SimMatch::HeaderOption;
using ElyverseFootball::SimMatch::headerOptions;
using ElyverseFootball::SimMatch::holdChance;
using ElyverseFootball::SimMatch::jumpRise;
using ElyverseFootball::SimMatch::launchVerticalVelocity;
using ElyverseFootball::SimMatch::LooseBallRecovered;
using ElyverseFootball::SimMatch::makeBallMovementSystem;
using ElyverseFootball::SimMatch::makePlayerMovementSystem;
using ElyverseFootball::SimMatch::MatchEvent;
using ElyverseFootball::SimMatch::MatchSimulation;
using ElyverseFootball::SimMatch::MatchState;
using ElyverseFootball::SimMatch::mayRiseAbove;
using ElyverseFootball::SimMatch::PassAttempted;
using ElyverseFootball::SimMatch::PassConfig;
using ElyverseFootball::SimMatch::Pitch;
using ElyverseFootball::SimMatch::PlayerAttributes;
using ElyverseFootball::SimMatch::PlayerMatchState;
using ElyverseFootball::SimMatch::reachAt;
using ElyverseFootball::SimMatch::ReceptionConfig;
using ElyverseFootball::SimMatch::resolveAerialDuel;
using ElyverseFootball::SimMatch::RestartConfig;
using ElyverseFootball::SimMatch::ShotAttempted;
using ElyverseFootball::SimMatch::ShotConfig;
using ElyverseFootball::SimMatch::ShotStoppingConfig;
using ElyverseFootball::SimMatch::TeamSide;
using ElyverseFootball::SimMatch::TeamTactics;
using ElyverseFootball::SimMatch::timingSpread;
using ElyverseFootball::SimMatch::validate;
using ElyverseFootball::SimMatch::WoodworkConfig;
using ElyverseFootball::SimTactics::referenceTacticSpec;
using ElyverseFootball::SimTactics::Tactic;

namespace {

// A standard pitch: home attacks the goal at x = 105.
[[nodiscard]] Pitch pitch() {
  return Pitch(105.0, 68.0);
}

constexpr int kTicksPerSecond = 30;
constexpr double kCrossSpeed = 16.0;     // m/s along the ground
constexpr double kCrossDistance = 12.0;  // m to the first contact
// Out of everybody's way.
constexpr Vec2 kFarAway{.x = 3.0, .y = 3.0};

// Jumps timed exactly, the best utility always winning and the best header
// always chosen, and headers struck without error: a test knows what happens.
[[nodiscard]] AerialConfig exact() {
  AerialConfig config;
  config.timingError = 0.0;
  config.duelTemperature = 1e-6;
  config.decisionTemperature = 1e-6;
  config.directionError = 0.0;
  config.speedError = 0.0;
  return config;
}

[[nodiscard]] PlayerMatchState playerAt(const std::uint32_t playerId, const TeamSide side,
                                        const Vec2 position) {
  return {.playerId = PlayerId(playerId),
          .side = side,
          .position = position,
          .velocity = {},
          .attributes = {},
          .target = std::nullopt,
          // Facing the cross, which comes along growing pitch y.
          .facing = {.x = 0.0, .y = -1.0}};
}

// A delivery from home 3 along growing pitch y at x = `at.x`: it is `height`
// high where it first comes within headRadius of a player standing at `at`,
// or rolls along the grass if it is not lifted at all.
struct Delivery {
  Vec2 at;
  double height = 0.0;
  bool lifted = true;
};

// A ball lying still at `position`, played by nobody.
[[nodiscard]] BallState ballAt(const Vec2 position) {
  return {.position = position, .velocity = {}, .owner = std::nullopt, .lastTouch = std::nullopt};
}

[[nodiscard]] BallState crossTo(const Delivery& delivery) {
  constexpr double kStartHeight = 1.0;
  const double contactY = delivery.at.y - AerialConfig{}.headRadius;
  BallState ball{.position = {.x = delivery.at.x, .y = contactY - kCrossDistance},
                 .velocity = {.x = 0.0, .y = kCrossSpeed},
                 .owner = std::nullopt,
                 .lastTouch = BallTouch{.playerId = PlayerId(3), .tick = SimTick(0)}};
  if (delivery.lifted) {
    const auto lift = launchVerticalVelocity({.speed = kCrossSpeed,
                                              .distance = kCrossDistance,
                                              .height = delivery.height - kStartHeight},
                                             BallPhysics{});
    REQUIRE(lift.has_value());
    ball.height = kStartHeight;
    ball.verticalVelocity = lift.value_or(0.0);
  }
  return ball;
}

// Home 1 meets the delivery and away 8 stands beside him; home 3 crossed it
// from far away. Everyone else stands in a far corner. With a keeper, away
// plays the reference tactic, whose first slot, away 8, keeps goal.
struct Box {
  Vec2 attacker{.x = 95.0, .y = 34.0};
  std::optional<Vec2> defender = Vec2{.x = 94.4, .y = 34.3};
  PlayerAttributes attackerAttributes{};
  PlayerAttributes defenderAttributes{};
  bool keeper = false;
};

[[nodiscard]] MatchState boxState(const Box& box, const BallState& ball) {
  std::vector<PlayerMatchState> players{playerAt(1, TeamSide::kHome, box.attacker)};
  players.back().attributes = box.attackerAttributes;
  for (std::uint32_t id = 2; id <= 7; ++id) {
    players.push_back(playerAt(id, TeamSide::kHome, kFarAway));
  }
  players.push_back(playerAt(8, TeamSide::kAway, box.defender.value_or(kFarAway)));
  players.back().attributes = box.defenderAttributes;
  for (std::uint32_t id = 9; id <= 14; ++id) {
    players.push_back(playerAt(id, TeamSide::kAway, kFarAway));
  }
  TeamTactics tactics;
  if (box.keeper) {
    auto tactic = Tactic::create(referenceTacticSpec());
    REQUIRE(tactic.has_value());
    tactics.away = *std::move(tactic);
  }
  auto state = MatchState::create(
      {.pitch = pitch(), .players = std::move(players), .ball = ball, .playersPerSide = 7},
      std::move(tactics));
  REQUIRE(state.has_value());
  return *std::move(state);
}

[[nodiscard]] MatchSimulation boxMatch(const Box& box, const BallState& ball,
                                       const AerialConfig& aerial, const std::uint64_t seed = 1) {
  return MatchSimulation(
      {.initialState = boxState(box, ball),
       .seed = seed,
       .ticksPerSecond = kTicksPerSecond,
       .systems = {makePlayerMovementSystem(),
                   makeBallMovementSystem(BallPhysics{}, PassConfig{}, ReceptionConfig{},
                                          RestartConfig{}, ShotConfig{}, WoodworkConfig{},
                                          ShotStoppingConfig{}, aerial)},
       .commands = {}});
}

// Every event of this many steps, in order.
[[nodiscard]] std::vector<MatchEvent> play(MatchSimulation& simulation, const int ticks) {
  std::vector<MatchEvent> events;
  for (int tick = 0; tick < ticks; ++tick) {
    REQUIRE(simulation.step().has_value());
    events.insert(events.end(), simulation.events().begin(), simulation.events().end());
  }
  return events;
}

template <typename Event>
[[nodiscard]] std::vector<Event> eventsOf(const std::vector<MatchEvent>& events) {
  std::vector<Event> found;
  for (const MatchEvent& event : events) {
    if (const auto* typed = std::get_if<Event>(&event)) {
      found.push_back(*typed);
    }
  }
  return found;
}

[[nodiscard]] AerialContest onlyContest(const std::vector<MatchEvent>& events) {
  const auto contests = eventsOf<AerialContest>(events);
  REQUIRE(contests.size() == 1);
  return contests.front();
}

[[nodiscard]] PlayerAttributes leaping(const double jumping) {
  PlayerAttributes attributes;
  attributes.jumping = jumping;
  return attributes;
}

// A second of the delivery: it gets to the box in under one.
constexpr int kSecond = kTicksPerSecond;

}  // namespace

TEST_CASE("A jump lifts a player by his jumping and times its top by his heading",
          "[aerialDuels]") {
  const AerialConfig config;
  REQUIRE_THAT(jumpRise(0.0, config), WithinAbs(config.lowestJump, 1e-12));
  REQUIRE_THAT(jumpRise(1.0, config), WithinAbs(config.highestJump, 1e-12));
  REQUIRE_THAT(jumpRise(0.5, config),
               WithinAbs((config.lowestJump + config.highestJump) / 2.0, 1e-12));

  REQUIRE_THAT(timingSpread(0.5, config), WithinAbs(config.timingError, 1e-12));
  REQUIRE(timingSpread(1.0, config) < timingSpread(0.5, config));
  REQUIRE(timingSpread(0.0, config) > timingSpread(0.5, config));

  // At the top of his jump he gets highest; mistimed, lower, and off it he
  // is back on his feet.
  REQUIRE_THAT(reachAt(1.9, 0.5, 0.0, 9.81), WithinAbs(2.4, 1e-12));
  REQUIRE(reachAt(1.9, 0.5, 0.2, 9.81) < 2.39);
  REQUIRE(reachAt(1.9, 0.5, 0.2, 9.81) == reachAt(1.9, 0.5, -0.2, 9.81));
  REQUIRE(reachAt(1.9, 0.5, 1.0, 9.81) == 1.9);
}

TEST_CASE("Arrival, reach, strength and body position each win a duel", "[aerialDuels]") {
  const AerialConfig config;
  const PlayerAttributes average;
  const AerialChallenger first{.playerIndex = 0, .standing = 1.9, .rise = 0.5, .lateSeconds = 0.0};
  const AerialJump jump{.reach = 2.2, .reached = true};
  const double base = duelUtility(average, first, jump, 2.0, config);

  AerialChallenger late = first;
  late.lateSeconds = 0.1;
  REQUIRE(duelUtility(average, late, jump, 2.0, config) < base);

  AerialJump higher = jump;
  higher.reach = 2.25;
  REQUIRE(duelUtility(average, first, higher, 2.0, config) > base);
  // Reach counts only so far above the ball.
  AerialJump far = jump;
  far.reach = 3.0;
  AerialJump farther = jump;
  farther.reach = 3.5;
  REQUIRE(duelUtility(average, first, far, 2.0, config) ==
          duelUtility(average, first, farther, 2.0, config));

  PlayerAttributes strong = average;
  strong.strength = 0.9;
  REQUIRE(duelUtility(strong, first, jump, 2.0, config) > base);

  AerialChallenger turned = first;
  turned.body = 0.2;
  REQUIRE(duelUtility(average, turned, jump, 2.0, config) < base);

  // A keeper with his hands brings his handling.
  AerialChallenger keeper = first;
  keeper.hands = true;
  REQUIRE(duelUtility(average, keeper, jump, 2.0, config) > base);
}

TEST_CASE("The stronger of two players who reach the ball wins it more often", "[aerialDuels]") {
  PlayerAttributes strong;
  strong.strength = 1.0;
  PlayerAttributes weak;
  weak.strength = 0.0;
  const MatchState state =
      boxState({.attackerAttributes = strong, .defenderAttributes = weak}, BallState{});
  const AerialConfig config;
  const std::vector<AerialChallenger> challengers{{.playerIndex = 0, .standing = 1.9, .rise = 0.5},
                                                  {.playerIndex = 7, .standing = 1.9, .rise = 0.5}};
  int strongWins = 0;
  constexpr int kDuels = 400;
  for (int seed = 0; seed < kDuels; ++seed) {
    RandomNumberGenerator random(static_cast<std::uint64_t>(seed));
    // A ball at head height, which both reach however they time their jump.
    const auto duel = resolveAerialDuel(state, challengers, 1.5, config, 9.81, random);
    REQUIRE(duel.jumps.size() == 2);
    REQUIRE(duel.winner.has_value());
    strongWins += duel.winner == 0 ? 1 : 0;
  }
  // exp(1 / 0.3) to 1: about 28 duels in 29.
  REQUIRE(strongWins > kDuels * 9 / 10);
}

TEST_CASE("A mistimed jump widens the header's errors", "[aerialDuels]") {
  const AerialConfig config;
  REQUIRE_THAT(headerErrorFactor(0.5, 0.0, config), WithinAbs(1.0, 1e-12));
  REQUIRE_THAT(headerErrorFactor(0.5, 1.0, config),
               WithinAbs(1.0 + config.mistimedErrorFactor, 1e-12));
  REQUIRE(headerErrorFactor(1.0, 0.0, config) < headerErrorFactor(0.5, 0.0, config));
}

TEST_CASE("No header leaves faster than maxHeaderSpeed", "[aerialDuels]") {
  AerialConfig config;
  config.speedError = 0.9;
  const PlayerMatchState player = playerAt(1, TeamSide::kHome, {.x = 50.0, .y = 34.0});
  BallState ball = ballAt({.x = 50.0, .y = 34.0});
  ball.height = 2.0;
  // A clearance as hard as a header goes, and one so far the drag stops it
  // short and it goes up at 45 degrees.
  for (const double distance : {30.0, 500.0}) {
    const HeaderIntent intent{.play = AerialPlay::kClearance,
                              .target = {.x = 50.0 + distance, .y = 34.0},
                              .height = 0.0,
                              .speed = config.maxHeaderSpeed,
                              .receiver = std::nullopt};
    for (std::uint64_t seed = 0; seed < 200; ++seed) {
      RandomNumberGenerator random(seed);
      const auto strike = executeHeader(intent, ball, player, 3.0, config, BallPhysics{}, random);
      REQUIRE(std::hypot(strike.velocity.length(), strike.verticalVelocity) <=
              config.maxHeaderSpeed + 1e-9);
    }
  }
}

TEST_CASE("Only a ball that may rise above a height is searched for in the air", "[aerialDuels]") {
  const BallPhysics physics;
  BallState ball = ballAt({.x = 50.0, .y = 34.0});
  ball.velocity = {.x = 10.0, .y = 0.0};
  // Rolling, or coming down from below it: never.
  REQUIRE_FALSE(mayRiseAbove(ball, 1.0, physics));
  ball.height = 0.9;
  ball.verticalVelocity = -2.0;
  REQUIRE_FALSE(mayRiseAbove(ball, 1.0, physics));
  // Rising, but not that far: never.
  ball.height = 0.5;
  ball.verticalVelocity = 1.0;
  REQUIRE_FALSE(mayRiseAbove(ball, 1.0, physics));
  // Rising far enough, or already above it.
  ball.verticalVelocity = 5.0;
  REQUIRE(mayRiseAbove(ball, 1.0, physics));
  ball.height = 1.5;
  ball.verticalVelocity = -2.0;
  REQUIRE(mayRiseAbove(ball, 1.0, physics));
}

TEST_CASE("A keeper holds a ball nobody challenged him for, and drops some he was",
          "[aerialDuels]") {
  const AerialConfig config;
  PlayerAttributes keeper;
  REQUIRE(holdChance(keeper, false, config) == 1.0);
  REQUIRE_THAT(holdChance(keeper, true, config), WithinAbs(1.0 - config.contestedDrop, 1e-12));
  keeper.keeperHandling = 1.0;
  REQUIRE(holdChance(keeper, true, config) > 1.0 - config.contestedDrop);
}

TEST_CASE("A header goes at goal near it, and clear near his own", "[aerialDuels]") {
  const AerialConfig config;
  const auto playsOf = [&](const MatchState& state, const Vec2 ball) {
    std::vector<AerialPlay> plays;
    for (const HeaderOption& option : headerOptions(state, 0, ballAt(ball), config)) {
      plays.push_back(option.intent.play);
    }
    return plays;
  };
  // Ten meters out, alone: a shot or a clearance.
  const MatchState alone = boxState({}, BallState{});
  REQUIRE(playsOf(alone, {.x = 95.0, .y = 34.0}) ==
          std::vector{AerialPlay::kClearance, AerialPlay::kShot});
  // Thirty meters out: no shot.
  REQUIRE(playsOf(alone, {.x = 75.0, .y = 34.0}) == std::vector{AerialPlay::kClearance});

  // Near his own goal a clearance is worth the most.
  const MatchState back = boxState({.attacker = {.x = 5.0, .y = 34.0}}, BallState{});
  const auto backOptions = headerOptions(back, 0, ballAt({.x = 5.0, .y = 34.0}), config);
  REQUIRE(backOptions.front().intent.play == AerialPlay::kClearance);
  REQUIRE(backOptions.front().utility > 0.8);
  REQUIRE(backOptions.front().intent.target.x > 30.0);
}

TEST_CASE("A header is played to the closer teammate as a knock-down, the farther as a pass",
          "[aerialDuels]") {
  const AerialConfig config;
  // Home 1 at the halfway line, home 2 four meters away, home 3 twelve.
  std::vector<PlayerMatchState> players{playerAt(1, TeamSide::kHome, {.x = 52.0, .y = 34.0}),
                                        playerAt(2, TeamSide::kHome, {.x = 56.0, .y = 34.0}),
                                        playerAt(3, TeamSide::kHome, {.x = 52.0, .y = 46.0})};
  for (std::uint32_t id = 4; id <= 7; ++id) {
    players.push_back(playerAt(id, TeamSide::kHome, kFarAway));
  }
  for (std::uint32_t id = 8; id <= 14; ++id) {
    players.push_back(playerAt(id, TeamSide::kAway, {.x = 100.0, .y = 60.0}));
  }
  const auto state = MatchState::create(
      {.pitch = pitch(), .players = std::move(players), .ball = {}, .playersPerSide = 7});
  REQUIRE(state.has_value());

  const auto options = headerOptions(*state, 0, ballAt({.x = 52.0, .y = 34.0}), config);
  REQUIRE(options.size() == 3);
  REQUIRE(options.at(1).intent.play == AerialPlay::kKnockDown);
  REQUIRE(options.at(1).intent.receiver == PlayerId(2));
  REQUIRE(options.at(2).intent.play == AerialPlay::kPass);
  REQUIRE(options.at(2).intent.receiver == PlayerId(3));
  // Both open, so the closer is worth more.
  REQUIRE(options.at(1).utility > options.at(2).utility);
}

TEST_CASE("An attacker wins a high ball and heads it into the goal", "[aerialDuels]") {
  // He jumps higher than the defender beside him, who does not get to it.
  MatchSimulation simulation =
      boxMatch({.attackerAttributes = leaping(1.0), .defenderAttributes = leaping(0.0)},
               crossTo({.at = {.x = 95.0, .y = 34.0}, .height = 2.35}), exact());

  const auto events = play(simulation, 2 * kSecond);

  const AerialContest contest = onlyContest(events);
  REQUIRE(contest.height > 2.3);
  REQUIRE(contest.height < 2.4);
  REQUIRE(contest.contestants.size() == 2);
  REQUIRE(contest.contestants.at(0).player == PlayerId(1));
  REQUIRE(contest.contestants.at(0).reached);
  REQUIRE(contest.contestants.at(1).player == PlayerId(8));
  REQUIRE_FALSE(contest.contestants.at(1).reached);
  REQUIRE(contest.winner == PlayerId(1));
  REQUIRE(contest.play == AerialPlay::kShot);

  const auto shots = eventsOf<ShotAttempted>(events);
  REQUIRE(shots.size() == 1);
  REQUIRE(shots.front().shooter == PlayerId(1));
  const auto goals = eventsOf<GoalScored>(events);
  REQUIRE(goals.size() == 1);
  REQUIRE(goals.front().side == TeamSide::kHome);
  REQUIRE(goals.front().scorer == PlayerId(1));
  REQUIRE(simulation.state().score().home == 1);
}

TEST_CASE("A defender wins a high ball in his own box and heads it clear", "[aerialDuels]") {
  MatchSimulation simulation =
      boxMatch({.attackerAttributes = leaping(0.0), .defenderAttributes = leaping(1.0)},
               crossTo({.at = {.x = 94.4, .y = 34.3}, .height = 2.35}), exact());

  const auto events = play(simulation, 3 * kSecond);

  const AerialContest contest = onlyContest(events);
  REQUIRE(contest.winner == PlayerId(8));
  REQUIRE(contest.play == AerialPlay::kClearance);
  // A clearance is no pass.
  REQUIRE(eventsOf<PassAttempted>(events).empty());
  REQUIRE(eventsOf<ShotAttempted>(events).empty());
  // Two seconds after the header it is far up the pitch, away from his goal.
  const BallState& ball = simulation.state().ball();
  REQUIRE(ball.lastTouch.value_or(BallTouch{}).playerId == PlayerId(8));
  REQUIRE(ball.position.x < 80.0);
}

TEST_CASE("A ball neither player reaches flies on over them", "[aerialDuels]") {
  // Both stand 2.2 m high at best; the ball is within attemptMargin of that,
  // so both go up, but higher.
  const BallState cross = crossTo({.at = {.x = 95.0, .y = 34.0}, .height = 2.35});
  MatchSimulation simulation = boxMatch(
      {.attackerAttributes = leaping(0.0), .defenderAttributes = leaping(0.0)}, cross, exact());
  Box empty;
  empty.attacker = kFarAway;
  empty.defender = std::nullopt;
  MatchSimulation unopposed = boxMatch(empty, cross, exact());

  const auto events = play(simulation, kSecond);
  (void)play(unopposed, kSecond);

  // One contest: neither goes up again before he has landed.
  const AerialContest contest = onlyContest(events);
  REQUIRE(contest.contestants.size() == 2);
  REQUIRE_FALSE(contest.contestants.at(0).reached);
  REQUIRE_FALSE(contest.contestants.at(1).reached);
  REQUIRE_FALSE(contest.winner.has_value());
  REQUIRE_FALSE(contest.play.has_value());
  // The ball went on as if they were not there.
  REQUIRE(simulation.state().ball() == unopposed.state().ball());
}

TEST_CASE("The same delivery along the ground is no aerial contest", "[aerialDuels]") {
  MatchSimulation simulation =
      boxMatch({}, crossTo({.at = {.x = 95.0, .y = 34.0}, .lifted = false}), exact());

  const auto events = play(simulation, 2 * kSecond);

  REQUIRE(eventsOf<AerialContest>(events).empty());
  // The first to reach it takes it at his feet: the attacker, whom it comes
  // within controlRadius of before the defender.
  const auto recovered = eventsOf<LooseBallRecovered>(events);
  REQUIRE(recovered.size() == 1);
  REQUIRE(recovered.front().player == PlayerId(1));
  REQUIRE(simulation.state().ball().owner == PlayerId(1));
}

TEST_CASE("A keeper in his area reaches higher with his hands and holds the ball",
          "[aerialDuels]") {
  // Too high for an attacker of average jumping, who reaches 2.4 m, within
  // the keeper's hands, which reach 2.7 m -- and from 1.2 m away, so he gets
  // to it first, a little higher than where it is 2.5 m high.
  const Box box{
      .attacker = {.x = 100.6, .y = 33.6}, .defender = Vec2{.x = 101.0, .y = 34.0}, .keeper = true};
  AerialConfig config = exact();
  config.contestedDrop = 0.0;
  MatchSimulation simulation =
      boxMatch(box, crossTo({.at = {.x = 101.0, .y = 34.0}, .height = 2.5}), config);

  const auto events = play(simulation, 2 * kSecond);

  const AerialContest contest = onlyContest(events);
  REQUIRE(contest.contestants.size() == 2);
  REQUIRE(contest.winner == PlayerId(8));
  REQUIRE(contest.play == AerialPlay::kCaught);
  REQUIRE(simulation.state().ball().owner == PlayerId(8));
}

TEST_CASE("A keeper who drops a contested ball punches it clear", "[aerialDuels]") {
  const Box box{
      .attacker = {.x = 100.6, .y = 33.6}, .defender = Vec2{.x = 101.0, .y = 34.0}, .keeper = true};
  AerialConfig config = exact();
  config.contestedDrop = 1.0;
  MatchSimulation simulation =
      boxMatch(box, crossTo({.at = {.x = 101.0, .y = 34.0}, .height = 2.5}), config);

  const auto events = play(simulation, 2 * kSecond);

  const AerialContest contest = onlyContest(events);
  REQUIRE(contest.winner == PlayerId(8));
  REQUIRE(contest.play == AerialPlay::kPunched);
  REQUIRE_FALSE(simulation.state().ball().owner.has_value());
  // Away from his goal.
  REQUIRE(simulation.state().ball().position.x < 95.0);
}

TEST_CASE("Aerial duels reject an invalid configuration", "[aerialDuels]") {
  REQUIRE_NOTHROW(validate(AerialConfig{}));
  AerialConfig config;
  config.lowestJump = 0.8;
  REQUIRE_THROWS_AS(validate(config), std::invalid_argument);
  config = {};
  config.contestedDrop = 1.5;
  REQUIRE_THROWS_AS(validate(config), std::invalid_argument);
  config = {};
  config.duelTemperature = 0.0;
  REQUIRE_THROWS_AS(validate(config), std::invalid_argument);
  config = {};
  config.knockDownRange = 30.0;
  REQUIRE_THROWS_AS(validate(config), std::invalid_argument);
}
