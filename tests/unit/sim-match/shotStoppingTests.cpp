// How the goalkeeper stops shots (docs/shot-stopping.md): his reaction, the
// reach of his dive, his reading of the ball, and what becomes of a shot he
// meets -- caught, parried back into play or behind, or out of his reach.

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <utility>
#include <variant>
#include <vector>

#include "ballMovement.hpp"
#include "ballPhysics.hpp"
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
using ElyverseFootball::SimMatch::BallPhysics;
using ElyverseFootball::SimMatch::BallState;
using ElyverseFootball::SimMatch::BallTouch;
using ElyverseFootball::SimMatch::carriedBallPosition;
using ElyverseFootball::SimMatch::catchChance;
using ElyverseFootball::SimMatch::clampToReach;
using ElyverseFootball::SimMatch::decideDive;
using ElyverseFootball::SimMatch::Goal;
using ElyverseFootball::SimMatch::GoalEnd;
using ElyverseFootball::SimMatch::GoalScored;
using ElyverseFootball::SimMatch::handsAt;
using ElyverseFootball::SimMatch::isBusy;
using ElyverseFootball::SimMatch::isDiving;
using ElyverseFootball::SimMatch::kDefaultKeeperHandling;
using ElyverseFootball::SimMatch::KeeperDive;
using ElyverseFootball::SimMatch::LooseBallRecovered;
using ElyverseFootball::SimMatch::makeBallMovementSystem;
using ElyverseFootball::SimMatch::makePlayerMovementSystem;
using ElyverseFootball::SimMatch::makeRestartSystem;
using ElyverseFootball::SimMatch::makeShotStoppingSystem;
using ElyverseFootball::SimMatch::MatchEvent;
using ElyverseFootball::SimMatch::MatchSimulation;
using ElyverseFootball::SimMatch::MatchState;
using ElyverseFootball::SimMatch::MatchStateWriter;
using ElyverseFootball::SimMatch::MatchStepContext;
using ElyverseFootball::SimMatch::MatchSystem;
using ElyverseFootball::SimMatch::PassConfig;
using ElyverseFootball::SimMatch::PassReceived;
using ElyverseFootball::SimMatch::Pitch;
using ElyverseFootball::SimMatch::PlanePoint;
using ElyverseFootball::SimMatch::PlayerAttributes;
using ElyverseFootball::SimMatch::PlayerMatchState;
using ElyverseFootball::SimMatch::reactionSeconds;
using ElyverseFootball::SimMatch::readyHands;
using ElyverseFootball::SimMatch::ReceptionConfig;
using ElyverseFootball::SimMatch::recoverySeconds;
using ElyverseFootball::SimMatch::RestartConfig;
using ElyverseFootball::SimMatch::RestartKind;
using ElyverseFootball::SimMatch::RestartTaken;
using ElyverseFootball::SimMatch::runDistance;
using ElyverseFootball::SimMatch::runSeconds;
using ElyverseFootball::SimMatch::SaveAttempted;
using ElyverseFootball::SimMatch::SaveResult;
using ElyverseFootball::SimMatch::ShotAttempted;
using ElyverseFootball::SimMatch::ShotConfig;
using ElyverseFootball::SimMatch::ShotIntent;
using ElyverseFootball::SimMatch::ShotOutcome;
using ElyverseFootball::SimMatch::ShotRecord;
using ElyverseFootball::SimMatch::ShotResolved;
using ElyverseFootball::SimMatch::ShotStoppingConfig;
using ElyverseFootball::SimMatch::TeamSide;
using ElyverseFootball::SimMatch::TeamTactics;
using ElyverseFootball::SimMatch::touchesKeeper;
using ElyverseFootball::SimMatch::tryMargin;
using ElyverseFootball::SimMatch::validate;
using ElyverseFootball::SimMatch::WoodworkConfig;
using ElyverseFootball::SimTactics::referenceTacticSpec;
using ElyverseFootball::SimTactics::Tactic;

namespace {

// A standard pitch, so the goal is full size: 7.32 m by 2.44 m at x = 105.
[[nodiscard]] Pitch pitch() {
  return Pitch(105.0, 68.0);
}

[[nodiscard]] Goal goal() {
  return pitch().goal(GoalEnd::kMaxX);
}

// Sixteen meters out, straight in front of the goal; the keeper stands
// 1.5 m off his line.
constexpr Vec2 kShooter{.x = 89.0, .y = 34.0};
constexpr Vec2 kKeeper{.x = 103.5, .y = 34.0};
// Out of everybody's way.
constexpr Vec2 kFarAway{.x = 3.0, .y = 3.0};

constexpr int kTicksPerSecond = 30;

// Fast enough along the ground that he reaches a ball inside the post only
// at full stretch.
constexpr double kParrySpeed = 18.0;

// Execution without error, so a test can predict the ball exactly.
[[nodiscard]] ShotConfig exact() {
  ShotConfig config;
  config.spreadAtZero = 0.0;
  config.spreadPerMeter = 0.0;
  config.speedError = 0.0;
  config.spinError = 0.0;
  return config;
}

// A keeper who holds every ball he touches, and one who holds none.
[[nodiscard]] ShotStoppingConfig safeHands() {
  ShotStoppingConfig config;
  config.catchableSpeed = 1e9;
  return config;
}

[[nodiscard]] ShotStoppingConfig noHands() {
  ShotStoppingConfig config;
  config.catchableSpeed = 1e-6;
  config.parrySpread = 0.0;
  config.minParrySpeed = 0.3;
  config.maxParrySpeed = 0.3;
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
          .facing = {.x = side == TeamSide::kHome ? 1.0 : -1.0, .y = 0.0}};
}

// What the shot is up against.
struct Chance {
  Vec2 shooter = kShooter;
  Vec2 keeper = kKeeper;
  Vec2 teammate = kFarAway;
  // A perfect reader by default, so a test knows where he goes.
  double anticipation = 1.0;
  double handling = kDefaultKeeperHandling;
  ShotStoppingConfig saves;
  bool restarts = false;
  std::uint64_t seed = 1;
};

// Home 1 has the ball and home 2 is his teammate; away plays the reference
// tactic, whose first slot, away 8, keeps goal. Everyone else stands in a far
// corner.
[[nodiscard]] MatchState chanceState(const Chance& chance) {
  std::vector<PlayerMatchState> players{playerAt(1, TeamSide::kHome, chance.shooter),
                                        playerAt(2, TeamSide::kHome, chance.teammate)};
  for (std::uint32_t id = 3; id <= 7; ++id) {
    players.push_back(playerAt(id, TeamSide::kHome, kFarAway));
  }
  players.push_back(playerAt(8, TeamSide::kAway, chance.keeper));
  players.back().attributes.keeperAnticipation = chance.anticipation;
  players.back().attributes.keeperHandling = chance.handling;
  for (std::uint32_t id = 9; id <= 14; ++id) {
    players.push_back(playerAt(id, TeamSide::kAway, kFarAway));
  }
  auto tactic = Tactic::create(referenceTacticSpec());
  REQUIRE(tactic.has_value());
  auto state = MatchState::create({.pitch = pitch(),
                                   .players = std::move(players),
                                   .ball = {.position = chance.shooter + Vec2{.x = 0.5, .y = 0.0},
                                            .velocity = {},
                                            .owner = PlayerId(1),
                                            .lastTouch = std::nullopt},
                                   .playersPerSide = 7},
                                  TeamTactics{.home = std::nullopt, .away = *std::move(tactic)});
  REQUIRE(state.has_value());
  return *std::move(state);
}

// A match in which home 1 decides on this shot in the first step, so the ball
// system strikes it in the second, and the keeper answers it.
[[nodiscard]] MatchSimulation shotMatch(const Chance& chance, const ShotIntent& intent) {
  const RestartConfig restarts{.enabled = chance.restarts};
  std::vector<MatchSystem> systems{
      {.name = "shot decision",
       .update =
           [intent](const MatchStepContext& context, const MatchState& /*state*/,
                    MatchStateWriter& next) {
             if (context.tick() == SimTick(0)) {
               next.setPendingAction(0, intent);
             }
           }},
      makeShotStoppingSystem(chance.saves, BallPhysics{}),
      makePlayerMovementSystem(chance.saves),
      makeBallMovementSystem(BallPhysics{}, PassConfig{}, ReceptionConfig{}, restarts, exact(),
                             WoodworkConfig{}, chance.saves)};
  if (chance.restarts) {
    systems.push_back(makeRestartSystem(restarts, BallPhysics{}));
  }
  return MatchSimulation({.initialState = chanceState(chance),
                          .seed = chance.seed,
                          .ticksPerSecond = kTicksPerSecond,
                          .systems = std::move(systems),
                          .commands = {}});
}

[[nodiscard]] ShotIntent shotAt(const Vec2 target, const double height, const double speed) {
  return {.shooter = PlayerId(1), .target = target, .height = height, .speed = speed};
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

// The one save attempt of the one shot the events hold; it names the shot.
[[nodiscard]] SaveAttempted saveOf(const std::vector<MatchEvent>& events) {
  const auto attempts = eventsOf<ShotAttempted>(events);
  const auto saves = eventsOf<SaveAttempted>(events);
  REQUIRE(attempts.size() == 1);
  REQUIRE(saves.size() == 1);
  REQUIRE(saves.front().keeper == PlayerId(8));
  REQUIRE(saves.front().shooter == attempts.front().shooter);
  REQUIRE(saves.front().shotTick == attempts.front().tick);
  return saves.front();
}

// The one outcome of the one shot the events hold.
[[nodiscard]] ShotOutcome outcomeOf(const std::vector<MatchEvent>& events) {
  const auto resolved = eventsOf<ShotResolved>(events);
  REQUIRE(resolved.size() == 1);
  return resolved.front().outcome;
}

// The chance's state with this free ball in play instead, struck by home 1
// in tick 0.
[[nodiscard]] MatchState withBall(const Chance& chance, const Vec2 position, const Vec2 velocity,
                                  const double height = 0.0) {
  const MatchState base = chanceState(chance);
  auto state = MatchState::create(
      {.pitch = base.pitch(),
       .players = {base.players().begin(), base.players().end()},
       .ball = {.position = position,
                .velocity = velocity,
                .owner = std::nullopt,
                .lastTouch = BallTouch{.playerId = PlayerId(1), .tick = SimTick(0)},
                .height = height},
       .playersPerSide = 7},
      base.tactics());
  REQUIRE(state.has_value());
  return *std::move(state);
}

// The keeper's decision against the ball of `state`, which must make one.
[[nodiscard]] KeeperDive decide(const MatchState& state, const std::uint64_t seed = 1) {
  RandomNumberGenerator random(seed);
  const auto dive = decideDive(state, 7, SimTick(6), ShotStoppingConfig{}, BallPhysics{}, random);
  REQUIRE(dive.has_value());
  return dive.value_or(KeeperDive{});
}

// A keeper of these reflexes.
[[nodiscard]] PlayerAttributes reflexes(const double value) {
  PlayerAttributes attributes;
  attributes.keeperReflexes = value;
  return attributes;
}

// The point a dive sends his hands to, across from where he stood.
[[nodiscard]] PlanePoint aimOf(const KeeperDive& dive) {
  return {.across = dive.feet + dive.target.across, .up = dive.target.up};
}

// A dive from where the keeper stands, square to a ball along growing x,
// with his hands sent to this point and no run first.
[[nodiscard]] KeeperDive diveTo(const PlanePoint target) {
  return {.touchedBy = PlayerId(1),
          .touchedAt = SimTick(0),
          .tick = SimTick(0),
          .origin = kKeeper,
          .normal = {.x = 1.0, .y = 0.0},
          .target = target};
}

// The first moment, in hundredths of a second up to two seconds, a keeper
// diving from where he stands for a ball at this point of his plane touches
// it; empty if he never does.
[[nodiscard]] std::optional<double> secondsToTouch(const PlanePoint ball,
                                                   const ShotStoppingConfig& config) {
  const KeeperDive dive = diveTo(clampToReach(ball, config));
  for (int step = 0; step <= 200; ++step) {
    const double seconds = step / 100.0;
    if (touchesKeeper(0.0, handsAt(dive, seconds, PlayerAttributes{}, config), ball, config)) {
      return seconds;
    }
  }
  return std::nullopt;
}

// The keeper's dive, if he has one.
[[nodiscard]] std::optional<KeeperDive> diveOf(const MatchSimulation& simulation) {
  return simulation.state().tactical(7).dive;
}

}  // namespace

TEST_CASE("A keeper reacts as quickly as his reflexes allow", "[shotStopping]") {
  const ShotStoppingConfig config;
  REQUIRE(reactionSeconds(0.0, config) == config.slowestReaction);
  REQUIRE(reactionSeconds(1.0, config) == config.quickestReaction);
  REQUIRE_THAT(reactionSeconds(0.5, config),
               WithinAbs((config.slowestReaction + config.quickestReaction) / 2.0, 1e-12));
}

TEST_CASE("A keeper who anticipates well tries less for the hopeless", "[shotStopping]") {
  const ShotStoppingConfig config;
  REQUIRE(tryMargin(0.0, config) == config.carelessTryMargin);
  REQUIRE(tryMargin(1.0, config) == config.carefulTryMargin);
  REQUIRE(tryMargin(0.5, config) < tryMargin(0.2, config));
}

TEST_CASE("A keeper is down the longer the farther he stretched", "[shotStopping]") {
  const ShotStoppingConfig config;
  REQUIRE(recoverySeconds(0.0, reflexes(0.0), config) == config.standingRecovery);
  REQUIRE(recoverySeconds(1.0, reflexes(0.0), config) == config.stretchRecovery);
  REQUIRE(recoverySeconds(0.5, reflexes(0.0), config) >
          recoverySeconds(0.2, reflexes(0.0), config));
  // Quick reflexes get him up a third sooner.
  REQUIRE_THAT(recoverySeconds(1.0, reflexes(1.0), config),
               WithinAbs(config.stretchRecovery * 2.0 / 3.0, 1e-12));
}

TEST_CASE("A keeper runs as far as his legs allow", "[shotStopping]") {
  const PlayerAttributes attributes;
  REQUIRE(runDistance(0.0, attributes) == 0.0);
  for (const double meters : {0.5, 2.0, 7.0, 20.0}) {
    CAPTURE(meters);
    REQUIRE_THAT(runDistance(runSeconds(meters, attributes), attributes), WithinAbs(meters, 1e-9));
  }
  // Up to speed after maxSpeed / acceleration, flat out from there.
  const double flatOut = attributes.maxSpeed / attributes.acceleration;
  REQUIRE_THAT(runDistance(flatOut + 1.0, attributes) - runDistance(flatOut, attributes),
               WithinAbs(attributes.maxSpeed, 1e-9));
}

TEST_CASE("A near corner is easier to reach than the far one", "[shotStopping]") {
  const ShotStoppingConfig config;
  const auto nearCorner = secondsToTouch({.across = 1.8, .up = 0.3}, config);
  const auto farCorner = secondsToTouch({.across = 3.0, .up = 0.3}, config);
  REQUIRE(nearCorner.has_value());
  REQUIRE(farCorner.has_value());
  REQUIRE(nearCorner.value_or(0.0) < farCorner.value_or(0.0));
  // Either side alike.
  REQUIRE(secondsToTouch({.across = -1.8, .up = 0.3}, config) == nearCorner);
}

TEST_CASE("A low ball is easier to reach than the top corner", "[shotStopping]") {
  const ShotStoppingConfig config;
  REQUIRE(secondsToTouch({.across = 2.5, .up = 0.3}, config).has_value());
  // As far across, under the crossbar: his reach narrows the higher he goes.
  REQUIRE_FALSE(secondsToTouch({.across = 2.5, .up = 2.3}, config).has_value());
  // High and close to him he still gets.
  REQUIRE(secondsToTouch({.across = 1.0, .up = 2.3}, config).has_value());
}

TEST_CASE("Before he moves a keeper reaches only what comes at him", "[shotStopping]") {
  const ShotStoppingConfig config;
  const PlanePoint ready =
      handsAt(diveTo({.across = 2.0, .up = 0.5}), 0.0, PlayerAttributes{}, config);
  REQUIRE(ready == readyHands(config));
  REQUIRE(touchesKeeper(0.0, ready, {.across = 0.3, .up = 1.0}, config));
  REQUIRE(touchesKeeper(0.0, ready, {.across = 0.0, .up = 0.1}, config));
  REQUIRE_FALSE(touchesKeeper(0.0, ready, {.across = 1.2, .up = 1.0}, config));
}

TEST_CASE("A keeper reads the ball as well as his anticipation allows", "[shotStopping]") {
  const ShotStoppingConfig config;
  // The ball on its way at him, at 1 m, a little to his left.
  const auto stateWith = [](const double anticipation) {
    Chance chance;
    chance.anticipation = anticipation;
    return withBall(chance, {.x = 95.0, .y = 34.5}, {.x = 20.0, .y = 0.0}, 1.0);
  };

  const KeeperDive perfect = decide(stateWith(1.0));
  REQUIRE(perfect.touchedBy == PlayerId(1));
  REQUIRE(perfect.touchedAt == SimTick(0));
  REQUIRE(perfect.tick == SimTick(6));
  REQUIRE(perfect.origin == kKeeper);
  REQUIRE_THAT(aimOf(perfect).across, WithinAbs(0.5, 1e-9));
  REQUIRE(aimOf(perfect).up < 1.0 + 0.11);

  int misread = 0;
  for (std::uint64_t seed = 1; seed <= 200; ++seed) {
    const PlanePoint poor = aimOf(decide(stateWith(0.0), seed));
    const double off = std::max(std::abs(poor.across - aimOf(perfect).across),
                                std::abs(poor.up - aimOf(perfect).up));
    REQUIRE(off <= config.readError + 1e-9);
    misread += off > 0.1 ? 1 : 0;
  }
  REQUIRE(misread > 100);
}

TEST_CASE("With time to spare a keeper runs before he dives", "[shotStopping]") {
  // Along the ground at 20 m/s toward the corner, 3 m to his left where it
  // passes him: from 25 m he has time to get across first, from 8 m he dives
  // from where he stands.
  const auto toCorner = [](const double out) {
    const Vec2 from{.x = kKeeper.x - out, .y = kKeeper.y};
    const Vec2 way = Vec2{.x = kKeeper.x, .y = kKeeper.y + 3.0} - from;
    return decide(withBall({}, from, way * (20.0 / way.length())));
  };
  const KeeperDive far = toCorner(25.0);
  const KeeperDive near = toCorner(8.0);
  REQUIRE(far.feet > 1.5);
  REQUIRE(far.runSeconds > 0.0);
  REQUIRE(near.feet < far.feet);
  // Both go for the same point; the one who ran stretches less for it.
  REQUIRE(std::abs(far.target.across) < std::abs(near.target.across));
}

TEST_CASE("A keeper leaves a ball he cannot get near", "[shotStopping]") {
  // Inside the far post but 3.4 m to his left, and only 6 m out.
  Chance chance;
  const MatchState state =
      withBall(chance, {.x = kKeeper.x - 6.0, .y = 37.4}, {.x = 30.0, .y = 0.0});
  const KeeperDive dive = decide(state);
  REQUIRE(dive.feet == 0.0);
  REQUIRE(dive.target == readyHands(ShotStoppingConfig{}));
  REQUIRE_FALSE(isBusy(dive, 0.0));
}

TEST_CASE("A keeper leaves a ball he reads going clearly wide", "[shotStopping]") {
  // Near the post, the ball 1 m to his left: wide of it by 0.8 m, or inside.
  Chance chance;
  chance.keeper = {.x = 103.5, .y = 37.0};
  const auto toward = [&](const double goalLineY) {
    const Vec2 from{.x = 93.0, .y = 36.5};
    const Vec2 way = Vec2{.x = 105.0, .y = goalLineY} - from;
    return decide(withBall(chance, from, way * (20.0 / way.length()), 0.5));
  };
  REQUIRE(goal().postAtMaxY().y + 0.8 > goal().postAtMaxY().y + ShotStoppingConfig{}.wideMargin);
  REQUIRE_FALSE(isBusy(toward(goal().postAtMaxY().y + 0.8), 0.0));
  REQUIRE(isBusy(toward(goal().postAtMaxY().y - 0.3), 0.0));
}

TEST_CASE("The keeper decides his dive before the ball gets to him", "[shotStopping]") {
  MatchSimulation simulation = shotMatch({}, shotAt(goal().center, 1.0, 25.0));
  REQUIRE(simulation.step().has_value());
  REQUIRE(simulation.step().has_value());
  REQUIRE(simulation.state().lastShot().has_value());
  const ShotRecord shot = simulation.state().lastShot().value_or(ShotRecord{});
  REQUIRE_FALSE(diveOf(simulation).has_value());

  const double reaction = reactionSeconds(0.5, ShotStoppingConfig{});
  int ticks = 0;
  while (!diveOf(simulation) && ticks < kTicksPerSecond) {
    REQUIRE(simulation.step().has_value());
    ++ticks;
  }
  const KeeperDive dive = diveOf(simulation).value_or(KeeperDive{});
  REQUIRE(diveOf(simulation).has_value());
  REQUIRE(static_cast<double>(dive.tick.value() - shot.tick.value()) / kTicksPerSecond >= reaction);
  REQUIRE(dive.touchedBy == PlayerId(1));
  REQUIRE(dive.touchedAt == shot.tick);
  REQUIRE(simulation.state().ball().position.x < kKeeper.x);
  REQUIRE_THAT(aimOf(dive).across, WithinAbs(0.0, 1e-9));
}

TEST_CASE("A diving keeper moves tick by tick and is down once the ball passed", "[shotStopping]") {
  // Low, 2 m to his left: he goes across for it.
  MatchSimulation simulation =
      shotMatch({}, shotAt(goal().center + Vec2{.x = 0.0, .y = 2.2}, 0.2, 22.0));
  std::vector<double> across;
  bool wasDown = false;
  bool upAgain = false;
  for (int tick = 0; tick < 3 * kTicksPerSecond; ++tick) {
    REQUIRE(simulation.step().has_value());
    across.push_back(simulation.state().players()[7].position.y - kKeeper.y);
    const bool down = isDiving(simulation.state(), 7, simulation.tick(), 1.0 / kTicksPerSecond);
    wasDown = wasDown || down;
    upAgain = upAgain || (wasDown && !down);
  }
  REQUIRE(wasDown);
  REQUIRE(upAgain);
  REQUIRE(across.back() > 1.0);
  // Never a jump: a few tenths of a meter a tick at most.
  for (std::size_t tick = 1; tick < across.size(); ++tick) {
    REQUIRE(std::abs(across[tick] - across[tick - 1]) < 0.3);
  }
}

TEST_CASE("A central shot is saved", "[shotStopping]") {
  for (std::uint64_t seed = 1; seed <= 20; ++seed) {
    CAPTURE(seed);
    Chance chance;
    chance.seed = seed;
    chance.anticipation = 0.5;
    MatchSimulation simulation = shotMatch(chance, shotAt(goal().center, 1.0, 25.0));
    const auto events = play(simulation, 3 * kTicksPerSecond);

    const SaveAttempted save = saveOf(events);
    REQUIRE(save.result != SaveResult::kOutOfReach);
    REQUIRE(eventsOf<GoalScored>(events).empty());
  }
}

TEST_CASE("A shot into the top corner is out of the keeper's reach", "[shotStopping]") {
  MatchSimulation simulation =
      shotMatch({}, shotAt(goal().postAtMaxY() - Vec2{.x = 0.0, .y = 0.3}, 2.1, 30.0));
  const auto events = play(simulation, 2 * kTicksPerSecond);

  REQUIRE(saveOf(events).result == SaveResult::kOutOfReach);
  REQUIRE(outcomeOf(events) == ShotOutcome::kGoal);
  REQUIRE(eventsOf<GoalScored>(events).size() == 1);
}

TEST_CASE("A shot from distance into the corner is reached after a run", "[shotStopping]") {
  // Along the ground, inside the post, at a pace that beats him from 16 m;
  // from 30 m he has the time to cross first.
  const auto saveFrom = [](const Vec2 shooter) {
    Chance chance;
    chance.shooter = shooter;
    MatchSimulation simulation =
        shotMatch(chance, shotAt(goal().postAtMaxY() - Vec2{.x = 0.0, .y = 0.3}, 0.0, 24.0));
    std::optional<KeeperDive> dive;
    std::vector<MatchEvent> events;
    for (int tick = 0; tick < 3 * kTicksPerSecond; ++tick) {
      REQUIRE(simulation.step().has_value());
      dive = dive ? dive : diveOf(simulation);
      events.insert(events.end(), simulation.events().begin(), simulation.events().end());
    }
    REQUIRE(dive.has_value());
    return std::pair{dive.value_or(KeeperDive{}), saveOf(events).result};
  };
  REQUIRE(saveFrom(kShooter).second == SaveResult::kOutOfReach);
  const auto [run, result] = saveFrom({.x = 75.0, .y = 34.0});
  REQUIRE(result != SaveResult::kOutOfReach);
  REQUIRE(run.feet > 0.5);
}

TEST_CASE("A save that keeps possession", "[shotStopping]") {
  Chance chance;
  chance.saves = safeHands();
  MatchSimulation simulation = shotMatch(chance, shotAt(goal().center, 1.0, 25.0));
  const auto events = play(simulation, 2 * kTicksPerSecond);

  const SaveAttempted save = saveOf(events);
  REQUIRE(save.result == SaveResult::kCaught);
  REQUIRE(outcomeOf(events) == ShotOutcome::kSaved);
  REQUIRE(simulation.state().ball().owner == PlayerId(8));
  // The ball stays where he met it, at his feet, rather than coming to him;
  // and he is up at once.
  REQUIRE(ElyverseFootball::SimCore::distance(simulation.state().ball().position, save.position) <
          1.0);
  REQUIRE_FALSE(diveOf(simulation).has_value());
}

TEST_CASE("A ball caught diving lies at the keeper's feet where he lands", "[shotStopping]") {
  Chance chance;
  chance.saves = safeHands();
  chance.handling = 1.0;
  // Low, 2 m to his left: he goes across for it.
  MatchSimulation simulation =
      shotMatch(chance, shotAt(goal().center + Vec2{.x = 0.0, .y = 2.2}, 0.2, 22.0));
  for (int tick = 0; tick < 2 * kTicksPerSecond && simulation.state().ball().owner != PlayerId(8);
       ++tick) {
    REQUIRE(simulation.step().has_value());
  }
  REQUIRE(simulation.state().ball().owner == PlayerId(8));
  const PlayerMatchState& keeper = simulation.state().players()[7];
  REQUIRE(keeper.position.y > kKeeper.y + 1.0);
  REQUIRE(simulation.state().ball().position ==
          carriedBallPosition(keeper, BallPhysics{}, pitch()));
}

TEST_CASE("A ball at catchable speed is never held", "[shotStopping]") {
  const ShotStoppingConfig config;
  PlayerAttributes sure;
  sure.keeperHandling = 1.0;
  BallState ball;
  ball.velocity = {.x = config.catchableSpeed, .y = 0.0};
  REQUIRE(catchChance(ball, 0.0, sure, config) == 0.0);
  ball.velocity = {.x = config.catchableSpeed / 2.0, .y = 0.0};
  REQUIRE(catchChance(ball, 0.0, sure, config) > 0.9);
  REQUIRE(catchChance(ball, 0.0, PlayerAttributes{}, config) == 0.5);
}

TEST_CASE("A parried shot rebounds into play and stays saved", "[shotStopping]") {
  Chance chance;
  chance.saves = noHands();
  // Beside the shot's way, out of reach of its body, and in the way of the
  // rebound.
  chance.teammate = {.x = 97.0, .y = 34.8};
  MatchSimulation simulation = shotMatch(chance, shotAt(goal().center, 1.0, 25.0));
  const auto events = play(simulation, 4 * kTicksPerSecond);

  REQUIRE(saveOf(events).result == SaveResult::kParriedIntoPlay);
  // The rebound is a loose ball, whoever gets to it, and the shot was saved.
  const auto recovered = eventsOf<LooseBallRecovered>(events);
  REQUIRE(recovered.size() == 1);
  REQUIRE(recovered.front().player == PlayerId(2));
  REQUIRE(eventsOf<PassReceived>(events).empty());
  REQUIRE(outcomeOf(events) == ShotOutcome::kSaved);
  REQUIRE(eventsOf<GoalScored>(events).empty());
}

TEST_CASE("A keeper gathers his own parry once he is up again", "[shotStopping]") {
  Chance chance;
  chance.saves = noHands();
  // The ball drops dead off him.
  chance.saves.minParrySpeed = 0.02;
  chance.saves.maxParrySpeed = 0.02;
  MatchSimulation simulation = shotMatch(chance, shotAt(goal().center, 1.0, 25.0));
  std::vector<MatchEvent> events;
  std::optional<int> parried;
  std::optional<int> gathered;
  for (int tick = 0; tick < 3 * kTicksPerSecond; ++tick) {
    REQUIRE(simulation.step().has_value());
    for (const MatchEvent& event : simulation.events()) {
      events.push_back(event);
      if (std::holds_alternative<SaveAttempted>(event)) {
        parried = tick;
      }
      if (const auto* recovered = std::get_if<LooseBallRecovered>(&event);
          recovered != nullptr && recovered->player == PlayerId(8)) {
        gathered = tick;
      }
    }
  }
  REQUIRE(saveOf(events).result == SaveResult::kParriedIntoPlay);
  REQUIRE(parried.has_value());
  REQUIRE(gathered.has_value());
  // Not before he is up: at least standing set's recovery, shortened by an
  // average keeper's reflexes.
  const double down = recoverySeconds(0.0, PlayerAttributes{}, ShotStoppingConfig{});
  REQUIRE(static_cast<double>(gathered.value_or(0) - parried.value_or(0)) / kTicksPerSecond >=
          down - (1.0 / kTicksPerSecond));
  REQUIRE(outcomeOf(events) == ShotOutcome::kSaved);
}

TEST_CASE("A shot parried at full stretch goes behind for a corner", "[shotStopping]") {
  Chance chance;
  chance.saves = noHands();
  chance.restarts = true;
  // Along the ground, fast and inside the post: he gets a hand to it.
  MatchSimulation simulation =
      shotMatch(chance, shotAt(goal().postAtMaxY() - Vec2{.x = 0.0, .y = 0.3}, 0.0, kParrySpeed));
  const auto events = play(simulation, 3 * kTicksPerSecond);

  REQUIRE(saveOf(events).result == SaveResult::kParriedBehind);
  REQUIRE(outcomeOf(events) == ShotOutcome::kSaved);
  const auto restarts = eventsOf<RestartTaken>(events);
  REQUIRE(restarts.size() == 1);
  REQUIRE(restarts.front().kind == RestartKind::kCorner);
}

TEST_CASE("A slow ball along the ground is picked up, not dived for", "[shotStopping]") {
  MatchSimulation simulation = shotMatch({}, shotAt(goal().center, 0.0, 7.0));
  const auto events = play(simulation, 4 * kTicksPerSecond);

  REQUIRE(eventsOf<SaveAttempted>(events).empty());
  REQUIRE_FALSE(diveOf(simulation).has_value());
  REQUIRE(simulation.state().ball().owner == PlayerId(8));
  REQUIRE(outcomeOf(events) == ShotOutcome::kSaved);
}

TEST_CASE("Shot stopping configurations are validated", "[shotStopping]") {
  REQUIRE_NOTHROW(validate(ShotStoppingConfig{}));
  const auto rejects = [](auto change) {
    ShotStoppingConfig config;
    change(config);
    REQUIRE_THROWS_AS(validate(config), std::invalid_argument);
  };
  rejects([](ShotStoppingConfig& config) { config.quickestReaction = 0.5; });
  rejects([](ShotStoppingConfig& config) { config.readError = -0.1; });
  rejects([](ShotStoppingConfig& config) { config.diveReach = 0.0; });
  rejects([](ShotStoppingConfig& config) { config.diveSpeed = 0.0; });
  rejects([](ShotStoppingConfig& config) { config.catchableSpeed = 0.0; });
  rejects([](ShotStoppingConfig& config) { config.minParrySpeed = 0.6; });
  rejects([](ShotStoppingConfig& config) { config.maxParrySpeed = 1.5; });
  rejects([](ShotStoppingConfig& config) { config.parryTilt = -1.0; });
  rejects([](ShotStoppingConfig& config) { config.carefulTryMargin = 2.0; });
  rejects([](ShotStoppingConfig& config) { config.standingRecovery = 2.0; });
  rejects([](ShotStoppingConfig& config) { config.reflexRecoveryShare = 1.5; });
}
