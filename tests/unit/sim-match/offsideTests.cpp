// The offside rule (docs/match-engine.md#offside): the line, a clear offside,
// a level attacker and a run from an onside position in real passes; the
// exceptions and the end of a watch with a scripted step; and a carrier who
// does not pass to a teammate he believes offside.

#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <utility>
#include <variant>
#include <vector>

#include "ids.hpp"
#include "matchCommand.hpp"
#include "matchEvents.hpp"
#include "matchFixtures.hpp"
#include "matchSetup.hpp"
#include "matchSimulation.hpp"
#include "matchState.hpp"
#include "offside.hpp"
#include "passCandidates.hpp"
#include "passing.hpp"
#include "pitch.hpp"
#include "restartKind.hpp"
#include "simTime.hpp"
#include "vec2.hpp"

using ElyverseFootball::SimCore::PlayerId;
using ElyverseFootball::SimCore::SimTick;
using ElyverseFootball::SimCore::Vec2;
using ElyverseFootball::SimMatch::BallTouch;
using ElyverseFootball::SimMatch::generatePassCandidates;
using ElyverseFootball::SimMatch::isOffsidePosition;
using ElyverseFootball::SimMatch::makeMatchSystems;
using ElyverseFootball::SimMatch::makeOffsideSystem;
using ElyverseFootball::SimMatch::MatchConfig;
using ElyverseFootball::SimMatch::MatchSimulation;
using ElyverseFootball::SimMatch::MatchState;
using ElyverseFootball::SimMatch::MatchStateWriter;
using ElyverseFootball::SimMatch::MatchStepContext;
using ElyverseFootball::SimMatch::MatchSystem;
using ElyverseFootball::SimMatch::OffsideCalled;
using ElyverseFootball::SimMatch::OffsideConfig;
using ElyverseFootball::SimMatch::offsideLine;
using ElyverseFootball::SimMatch::PassCommand;
using ElyverseFootball::SimMatch::PassReceived;
using ElyverseFootball::SimMatch::PassRecord;
using ElyverseFootball::SimMatch::PassRejection;
using ElyverseFootball::SimMatch::Pitch;
using ElyverseFootball::SimMatch::planPassSpeed;
using ElyverseFootball::SimMatch::RestartKind;
using ElyverseFootball::SimMatch::RestartTaken;
using ElyverseFootball::SimMatch::ShotRecord;
using ElyverseFootball::SimMatch::TeamSide;
using ElyverseFootball::TestSupport::playerAt;

namespace {

constexpr PlayerId kCarrier{2};
constexpr PlayerId kStriker{3};
constexpr PlayerId kDefender{9};
constexpr PlayerId kKeeper{8};

// Seven a side on the 60 x 40 m sandbox, home attacking +x: the carrier on
// the ball at the halfway line, the striker at `striker`, away's keeper at
// 58 m and its second-last player, the defender, at 45 m.
[[nodiscard]] ElyverseFootball::SimMatch::BallState carrierBall() {
  ElyverseFootball::SimMatch::BallState ball;
  ball.position = {.x = 30.5, .y = 20.0};
  ball.owner = kCarrier;
  return ball;
}

// The events of one type among a step's events, in order.
template <typename Event>
[[nodiscard]] std::vector<Event> eventsOf(
    const std::span<const ElyverseFootball::SimMatch::MatchEvent> events) {
  std::vector<Event> found;
  for (const auto& event : events) {
    if (const auto* typed = std::get_if<Event>(&event)) {
      found.push_back(*typed);
    }
  }
  return found;
}

[[nodiscard]] MatchState situation(const Vec2 striker) {
  using enum TeamSide;
  auto state = MatchState::create(
      {.pitch = Pitch(60.0, 40.0),
       .players = {playerAt(1, kHome, {.x = 2.0, .y = 20.0}),
                   playerAt(2, kHome, {.x = 30.0, .y = 20.0}), playerAt(3, kHome, striker),
                   playerAt(4, kHome, {.x = 20.0, .y = 5.0}),
                   playerAt(5, kHome, {.x = 20.0, .y = 15.0}),
                   playerAt(6, kHome, {.x = 20.0, .y = 25.0}),
                   playerAt(7, kHome, {.x = 20.0, .y = 35.0}),
                   playerAt(8, kAway, {.x = 58.0, .y = 20.0}, {.x = -1.0, .y = 0.0}),
                   playerAt(9, kAway, {.x = 45.0, .y = 8.0}, {.x = -1.0, .y = 0.0}),
                   playerAt(10, kAway, {.x = 35.0, .y = 5.0}, {.x = -1.0, .y = 0.0}),
                   playerAt(11, kAway, {.x = 35.0, .y = 35.0}, {.x = -1.0, .y = 0.0}),
                   playerAt(12, kAway, {.x = 40.0, .y = 32.0}, {.x = -1.0, .y = 0.0}),
                   playerAt(13, kAway, {.x = 25.0, .y = 5.0}, {.x = -1.0, .y = 0.0}),
                   playerAt(14, kAway, {.x = 25.0, .y = 35.0}, {.x = -1.0, .y = 0.0})},
       .ball = carrierBall(),
       .playersPerSide = 7});
  REQUIRE(state.has_value());
  return *std::move(state);
}

[[nodiscard]] MatchConfig withOffside() {
  MatchConfig config;
  config.offside.enabled = true;
  return config;
}

// Home's carrier passes to the striker's feet -- or `ahead`, into his run --
// at tick 1; the offsides called and restarts taken in the next four
// seconds, and who received the pass.
struct Played {
  std::vector<OffsideCalled> offsides;
  std::vector<RestartTaken> restarts;
  // Who received the pass.
  std::optional<PlayerId> received;
};

[[nodiscard]] Played played(const Vec2 striker, const std::optional<Vec2> run = std::nullopt) {
  const MatchConfig config = withOffside();
  const MatchState state = situation(striker);
  const Vec2 target = run.value_or(striker);
  std::vector<ElyverseFootball::SimMatch::ScheduledCommand> commands{
      {.tick = SimTick(1),
       .command = PassCommand{.playerId = kCarrier,
                              .target = target,
                              .speed = planPassSpeed(ElyverseFootball::SimCore::distance(
                                                         state.ball().position, target),
                                                     config.ball, config.passing),
                              .receiver = kStriker}}};
  if (run) {
    commands.insert(commands.begin(), {.tick = SimTick(0),
                                       .command = ElyverseFootball::SimMatch::MovePlayerCommand{
                                           .playerId = kStriker, .target = *run}});
  }
  MatchSimulation simulation({.initialState = state,
                              .seed = 1,
                              .ticksPerSecond = config.ticksPerSecond,
                              .systems = makeMatchSystems(config),
                              .commands = std::move(commands)});
  Played result;
  for (int tick = 0; tick < 120; ++tick) {
    REQUIRE(simulation.step().has_value());
    for (const auto& offside : eventsOf<OffsideCalled>(simulation.events())) {
      result.offsides.push_back(offside);
    }
    for (const auto& restart : eventsOf<RestartTaken>(simulation.events())) {
      result.restarts.push_back(restart);
    }
    for (const auto& received : eventsOf<PassReceived>(simulation.events())) {
      result.received = result.received.value_or(received.receiver);
    }
  }
  return result;
}

// A step that writes into the state, at tick `at` only.
[[nodiscard]] MatchSystem scripted(
    std::vector<std::pair<std::int64_t, std::function<void(MatchStateWriter&)>>> script) {
  return {.name = "script",
          .update =
              [script = std::move(script)](const MatchStepContext& context,
                                           const MatchState& /*current*/, MatchStateWriter& next) {
                for (const auto& [tick, write] : script) {
                  if (context.tick() == SimTick(tick)) {
                    write(next);
                  }
                }
              },
          .intervalTicks = 1,
          .phaseTicks = 0};
}

// The offsides the offside system calls after the script, from the striker
// offside at 50 m.
[[nodiscard]] std::vector<OffsideCalled> offsidesAfter(
    std::vector<std::pair<std::int64_t, std::function<void(MatchStateWriter&)>>> script) {
  MatchSimulation simulation(
      {.initialState = situation({.x = 50.0, .y = 20.0}),
       .seed = 1,
       .ticksPerSecond = 30,
       .systems = {scripted(std::move(script)),
                   makeOffsideSystem({.offside = {.enabled = true}, .physics = {}, .clock = {}})},
       .commands = {}});
  std::vector<OffsideCalled> offsides;
  for (int tick = 0; tick < 6; ++tick) {
    REQUIRE(simulation.step().has_value());
    for (const auto& offside : eventsOf<OffsideCalled>(simulation.events())) {
      offsides.push_back(offside);
    }
  }
  return offsides;
}

[[nodiscard]] std::function<void(MatchStateWriter&)> touchBy(const PlayerId player,
                                                             const std::int64_t tick) {
  return [player, tick](MatchStateWriter& next) {
    next.setBallLastTouch(BallTouch{.playerId = player, .tick = SimTick(tick)});
  };
}

[[nodiscard]] std::function<void(MatchStateWriter&)> passBy(const PlayerId player,
                                                            const std::int64_t tick) {
  return [player, tick](MatchStateWriter& next) {
    next.setLastPass(PassRecord{.passer = player,
                                .from = {.x = 30.5, .y = 20.0},
                                .tick = SimTick(tick),
                                .receiver = kStriker});
    next.setBallLastTouch(BallTouch{.playerId = player, .tick = SimTick(tick)});
  };
}

}  // namespace

TEST_CASE("The offside line is the second-last opponent, the ball or halfway", "[offside]") {
  const Pitch pitch(60.0, 40.0);
  const std::vector<Vec2> opponents{
      {.x = 58.0, .y = 20.0}, {.x = 45.0, .y = 8.0}, {.x = 35.0, .y = 5.0}};
  REQUIRE(offsideLine(TeamSide::kHome, opponents, {.x = 30.0, .y = 20.0}, pitch) == 45.0);
  // The ball ahead of the second-last opponent is the line.
  REQUIRE(offsideLine(TeamSide::kHome, opponents, {.x = 50.0, .y = 20.0}, pitch) == 50.0);
  // Nobody is offside in his own half.
  const std::vector<Vec2> deep{{.x = 58.0, .y = 20.0}, {.x = 20.0, .y = 8.0}};
  REQUIRE(offsideLine(TeamSide::kHome, deep, {.x = 10.0, .y = 20.0}, pitch) == 30.0);
  // Away attacks -x: depths from its own goal line at x = 60.
  REQUIRE(offsideLine(TeamSide::kAway,
                      std::vector<Vec2>{{.x = 2.0, .y = 20.0}, {.x = 15.0, .y = 8.0}},
                      {.x = 40.0, .y = 20.0}, pitch) == 45.0);
  // With one opponent the second-last is the goal line.
  REQUIRE(offsideLine(TeamSide::kHome, std::vector<Vec2>{{.x = 58.0, .y = 20.0}},
                      {.x = 30.0, .y = 20.0}, pitch) == 60.0);

  // Level is onside, within the documented tolerance.
  const OffsideConfig config;
  REQUIRE_FALSE(isOffsidePosition(TeamSide::kHome, {.x = 45.0, .y = 0.0}, 45.0, pitch, config));
  REQUIRE_FALSE(isOffsidePosition(TeamSide::kHome, {.x = 45.1, .y = 0.0}, 45.0, pitch, config));
  REQUIRE(isOffsidePosition(TeamSide::kHome, {.x = 45.11, .y = 0.0}, 45.0, pitch, config));
}

TEST_CASE("A striker beyond the line who receives the pass is offside", "[offside]") {
  const Played clear = played({.x = 50.0, .y = 20.0});
  REQUIRE(clear.offsides.size() == 1);
  const OffsideCalled& call = clear.offsides.front();
  REQUIRE(call.player == kStriker);
  REQUIRE(call.passer == kCarrier);
  REQUIRE(call.side == TeamSide::kHome);
  REQUIRE(call.lineX == 45.0);
  // An indirect free kick for away where he received it, its nearest player on
  // the ball.
  REQUIRE(clear.restarts.size() == 1);
  REQUIRE(clear.restarts.front().kind == RestartKind::kIndirectFreeKick);
  REQUIRE(clear.restarts.front().position == call.position);
  REQUIRE(clear.restarts.front().player == kKeeper);
}

TEST_CASE("A striker level with the line is onside", "[offside]") {
  const Played level = played({.x = 45.05, .y = 20.0});
  REQUIRE(level.offsides.empty());
  REQUIRE(level.restarts.empty());
  REQUIRE(level.received == kStriker);
}

TEST_CASE("A run from an onside position beats the line", "[offside]") {
  // Onside when the ball is played, beyond the line when it reaches him.
  const Played run = played({.x = 43.0, .y = 20.0}, Vec2{.x = 53.0, .y = 20.0});
  REQUIRE(run.offsides.empty());
  REQUIRE(run.received == kStriker);
}

TEST_CASE("Deliberate play, restarts and parried shots decide what the rule watches", "[offside]") {
  // The pass at tick 0 finds the striker at tick 2: offside.
  REQUIRE(offsidesAfter({{0, passBy(kCarrier, 0)}, {2, touchBy(kStriker, 2)}}).size() == 1);
  // A defender plays the ball deliberately in between: a new phase.
  REQUIRE(offsidesAfter(
              {{0, passBy(kCarrier, 0)}, {1, touchBy(kDefender, 1)}, {2, touchBy(kStriker, 2)}})
              .empty());
  // A teammate onside touches it first: the play moves on.
  REQUIRE(offsidesAfter(
              {{0, passBy(kCarrier, 0)}, {1, touchBy(PlayerId(5), 1)}, {2, touchBy(kStriker, 2)}})
              .empty());
  // A keeper's parry is no deliberate play: the striker on the rebound is
  // offside.
  const auto shot = [](MatchStateWriter& next) {
    next.setLastShot(
        ShotRecord{.shooter = kCarrier, .from = {.x = 30.5, .y = 20.0}, .tick = SimTick(0)});
    next.setBallLastTouch(BallTouch{.playerId = kCarrier, .tick = SimTick(0)});
  };
  const auto parry = [](MatchStateWriter& next) {
    ShotRecord parried{.shooter = kCarrier, .from = {.x = 30.5, .y = 20.0}, .tick = SimTick(0)};
    parried.parry = BallTouch{.playerId = kKeeper, .tick = SimTick(1)};
    next.setLastShot(parried);
    next.setBallLastTouch(parried.parry);
  };
  REQUIRE(offsidesAfter({{0, shot}, {1, parry}, {2, touchBy(kStriker, 2)}}).size() == 1);
  // No offside from a throw-in, a goal kick or a corner: the taker's first play
  // is free of it.
  const auto restart = [](MatchStateWriter& next) {
    next.setBallLastTouch(BallTouch{.playerId = kCarrier, .tick = SimTick(0)});
    next.setOffsideExemption(BallTouch{.playerId = kCarrier, .tick = SimTick(0)});
  };
  REQUIRE(
      offsidesAfter({{0, restart}, {1, passBy(kCarrier, 1)}, {3, touchBy(kStriker, 3)}}).empty());
}

TEST_CASE("A carrier does not pass to a teammate he believes offside", "[offside]") {
  for (const bool enabled : {true, false}) {
    CAPTURE(enabled);
    MatchConfig config;
    config.offside.enabled = enabled;
    MatchSimulation simulation({.initialState = situation({.x = 50.0, .y = 20.0}),
                                .seed = 1,
                                .ticksPerSecond = config.ticksPerSecond,
                                .systems = makeMatchSystems(config),
                                .commands = {}});
    // Long enough for the carrier to see everyone.
    for (int tick = 0; tick < 30; ++tick) {
      REQUIRE(simulation.step().has_value());
    }
    const auto candidates = generatePassCandidates(
        simulation.state(), 1, {.tick = simulation.tick(), .secondsPerTick = 1.0 / 30.0},
        {.scoring = config.decisions.scoring,
         .ball = config.ball,
         .passing = config.passing,
         .perception = config.perception,
         .reception = config.reception,
         .offside = config.offside});
    std::optional<PassRejection> striker;
    for (const auto& candidate : candidates) {
      if (candidate.receiver == kStriker) {
        striker = candidate.rejection;
      }
    }
    REQUIRE(striker.has_value());
    REQUIRE((striker == PassRejection::kOffside) == enabled);
  }
}

TEST_CASE("The whistle ends what the rule watches and the shot it stopped", "[offside]") {
  using ElyverseFootball::SimMatch::MatchPeriod;
  using ElyverseFootball::SimMatch::OffsideWatch;
  using ElyverseFootball::SimMatch::ShotOutcome;
  using ElyverseFootball::SimMatch::ShotResolved;
  // A shot by the carrier, deflected by the striker standing offside: the free
  // kick resolves the shot.
  const auto shot = [](MatchStateWriter& next) {
    next.setLastShot(
        ShotRecord{.shooter = kCarrier, .from = {.x = 30.5, .y = 20.0}, .tick = SimTick(0)});
    next.setBallLastTouch(BallTouch{.playerId = kCarrier, .tick = SimTick(0)});
  };
  const auto deflection = [](MatchStateWriter& next) {
    ShotRecord deflected{.shooter = kCarrier, .from = {.x = 30.5, .y = 20.0}, .tick = SimTick(0)};
    deflected.deflection = BallTouch{.playerId = kStriker, .tick = SimTick(1)};
    next.setLastShot(deflected);
    next.setBallLastTouch(deflected.deflection);
  };
  MatchSimulation simulation(
      {.initialState = situation({.x = 50.0, .y = 20.0}),
       .seed = 1,
       .ticksPerSecond = 30,
       .systems = {scripted({{0, shot}, {1, deflection}}),
                   makeOffsideSystem({.offside = {.enabled = true}, .physics = {}, .clock = {}})},
       .commands = {}});
  std::vector<ShotResolved> resolved;
  for (int tick = 0; tick < 3; ++tick) {
    REQUIRE(simulation.step().has_value());
    for (const auto& event : eventsOf<ShotResolved>(simulation.events())) {
      resolved.push_back(event);
    }
  }
  REQUIRE(resolved.size() == 1);
  REQUIRE(resolved.front().outcome == ShotOutcome::kBlocked);
  REQUIRE(simulation.state().lastShot().value_or(ShotRecord{}).resolved);

  // A watch still open at half time ends with the second half's kickoff.
  MatchConfig config = withOffside();
  config.clock.durationMinutes = 1.0 / 60.0;
  const auto watch = [](MatchStateWriter& next) {
    next.setOffside(OffsideWatch{.side = TeamSide::kAway,
                                 .passer = kDefender,
                                 .tick = SimTick(0),
                                 .line = 30.0,
                                 .offsidePlayers = ~std::uint64_t{0}});
  };
  std::vector<MatchSystem> systems{scripted({{0, watch}})};
  for (MatchSystem& system : makeMatchSystems(config)) {
    systems.push_back(std::move(system));
  }
  MatchSimulation halves({.initialState = situation({.x = 50.0, .y = 20.0}),
                          .seed = 1,
                          .ticksPerSecond = config.ticksPerSecond,
                          .systems = std::move(systems),
                          .commands = {}});
  REQUIRE(halves.step().has_value());
  REQUIRE(halves.state().offside().has_value());
  while (halves.state().clock().period == MatchPeriod::kFirstHalf) {
    REQUIRE(halves.step().has_value());
  }
  REQUIRE_FALSE(halves.state().offside().has_value());
}
