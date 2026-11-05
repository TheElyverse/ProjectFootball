// Every metric of MatchAnalyzer on hand-built event streams: a 60 m pitch at
// 30 Hz, home players 1 and 2 attacking +x, away players 3 and 4 attacking
// -x.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <vector>

#include "ids.hpp"
#include "matchAnalyzer.hpp"
#include "matchEvents.hpp"
#include "matchState.hpp"
#include "matchStats.hpp"
#include "simTime.hpp"
#include "teamPress.hpp"
#include "vec2.hpp"

using Catch::Approx;
using ElyverseFootball::SimAnalytics::AnalyticsConfig;
using ElyverseFootball::SimAnalytics::MatchAnalyzer;
using ElyverseFootball::SimAnalytics::MatchContext;
using ElyverseFootball::SimAnalytics::MatchStats;
using ElyverseFootball::SimAnalytics::PitchThird;
using ElyverseFootball::SimCore::PlayerId;
using ElyverseFootball::SimCore::SimTick;
using ElyverseFootball::SimCore::Vec2;
using ElyverseFootball::SimMatch::BallWon;
using ElyverseFootball::SimMatch::LooseBallRecovered;
using ElyverseFootball::SimMatch::MatchEvent;
using ElyverseFootball::SimMatch::PassAttempted;
using ElyverseFootball::SimMatch::PassIntercepted;
using ElyverseFootball::SimMatch::PassReceived;
using ElyverseFootball::SimMatch::PitchControlSampled;
using ElyverseFootball::SimMatch::PossessionChanged;
using ElyverseFootball::SimMatch::PressingEnded;
using ElyverseFootball::SimMatch::PressingStarted;
using ElyverseFootball::SimMatch::PressOutcome;
using ElyverseFootball::SimMatch::ShotAttempted;
using ElyverseFootball::SimMatch::TeamSide;

namespace {

[[nodiscard]] MatchContext context() {
  return {.pitchLengthMeters = 60.0,
          .ticksPerSecond = 30,
          .sides = {{PlayerId(1), TeamSide::kHome},
                    {PlayerId(2), TeamSide::kHome},
                    {PlayerId(3), TeamSide::kAway},
                    {PlayerId(4), TeamSide::kAway}},
          .initialPossession = std::nullopt};
}

// A step's events, fed to a fresh analyzer one step per entry, finished at
// finalTick.
[[nodiscard]] MatchStats analyze(const std::vector<std::vector<MatchEvent>>& steps,
                                 const std::int64_t finalTick = 100) {
  MatchAnalyzer analyzer(context());
  for (const auto& step : steps) {
    analyzer.observeStep(step);
  }
  return analyzer.finish(SimTick(finalTick));
}

[[nodiscard]] MatchEvent owner(const std::int64_t tick, const std::optional<std::uint32_t> previous,
                               const std::optional<std::uint32_t> next) {
  const auto playerOf = [](const std::optional<std::uint32_t> number) {
    return number ? std::optional(PlayerId(*number)) : std::nullopt;
  };
  return PossessionChanged{
      .tick = SimTick(tick), .previousOwner = playerOf(previous), .newOwner = playerOf(next)};
}

[[nodiscard]] MatchEvent pass(const std::int64_t tick, const std::uint32_t passer, const Vec2 from,
                              const Vec2 target) {
  return PassAttempted{.tick = SimTick(tick),
                       .passer = PlayerId(passer),
                       .intendedReceiver = std::nullopt,
                       .from = from,
                       .target = target,
                       .speed = 10.0};
}

[[nodiscard]] MatchEvent received(const std::int64_t tick, const std::uint32_t receiver,
                                  const std::uint32_t passer) {
  return PassReceived{
      .tick = SimTick(tick), .receiver = PlayerId(receiver), .passer = PlayerId(passer)};
}

[[nodiscard]] MatchEvent intercepted(const std::int64_t tick, const std::uint32_t interceptor,
                                     const std::uint32_t passer, const Vec2 position) {
  return PassIntercepted{.tick = SimTick(tick),
                         .interceptor = PlayerId(interceptor),
                         .passer = PlayerId(passer),
                         .position = position};
}

[[nodiscard]] MatchEvent won(const std::int64_t tick, const std::uint32_t winner,
                             const std::uint32_t loser, const Vec2 position) {
  return BallWon{.tick = SimTick(tick),
                 .winner = PlayerId(winner),
                 .loser = PlayerId(loser),
                 .position = position};
}

[[nodiscard]] MatchEvent sample(const std::int64_t tick, const double homeShare, const Vec2 ball) {
  return PitchControlSampled{.tick = SimTick(tick), .homeShare = homeShare, .ball = ball};
}

[[nodiscard]] int regainsIn(const std::array<int, 3>& byThird, const PitchThird third) {
  return byThird.at(static_cast<std::size_t>(third));
}

}  // namespace

TEST_CASE("Possession is the share of the time a side had the ball", "[analytics]") {
  // Home from tick 10 to 40, away from 40 to the end at 100: the pass in
  // flight from tick 30 still counts for home, the ten ticks before anyone
  // had the ball count for nobody.
  const MatchStats stats = analyze(
      {{owner(10, std::nullopt, 1)},
       {pass(30, 1, {.x = 20.0, .y = 20.0}, {.x = 30.0, .y = 20.0}), owner(30, 1, std::nullopt)},
       {intercepted(40, 3, 1, {.x = 25.0, .y = 20.0}), owner(40, std::nullopt, 3)}});
  REQUIRE(stats.ticks == 100);
  REQUIRE(stats.seconds == Approx(100.0 / 30.0));
  REQUIRE(stats.home.possessionShare == Approx(30.0 / 90.0));
  REQUIRE(stats.away.possessionShare == Approx(60.0 / 90.0));
}

TEST_CASE("The longest ownership is the longest time one player kept the ball", "[analytics]") {
  // Home player 1 from tick 10 to 40, player 2 from 50 to 60; away player 3
  // from 60 to the end at 100, still on the ball.
  const MatchStats stats = analyze({{owner(10, std::nullopt, 1)},
                                    {owner(40, 1, std::nullopt)},
                                    {owner(50, std::nullopt, 2)},
                                    {owner(60, 2, 3)}});
  REQUIRE(stats.home.longestOwnershipSeconds == Approx(30.0 / 30.0));
  REQUIRE(stats.away.longestOwnershipSeconds == Approx(40.0 / 30.0));
}

TEST_CASE("Passes count attempts, completions, length and progression", "[analytics]") {
  // Home: a 10 m square pass (completed) and a 15 m forward pass (completed,
  // progressive), then a 20 m forward pass that is intercepted.
  // Away: a pass toward -x, progressive for away.
  const MatchStats stats = analyze({{pass(1, 1, {.x = 10.0, .y = 10.0}, {.x = 10.0, .y = 20.0})},
                                    {received(5, 2, 1)},
                                    {pass(10, 2, {.x = 10.0, .y = 20.0}, {.x = 22.0, .y = 29.0})},
                                    {received(15, 1, 2)},
                                    {pass(20, 1, {.x = 22.0, .y = 29.0}, {.x = 42.0, .y = 29.0})},
                                    {intercepted(25, 3, 1, {.x = 35.0, .y = 29.0})},
                                    {pass(30, 3, {.x = 35.0, .y = 29.0}, {.x = 20.0, .y = 29.0})}});
  REQUIRE(stats.home.passes == 3);
  REQUIRE(stats.home.completedPasses == 2);
  REQUIRE(stats.home.passCompletion == Approx(2.0 / 3.0));
  REQUIRE(stats.home.meanPassMeters == Approx((10.0 + 15.0 + 20.0) / 3.0));
  REQUIRE(stats.home.progressivePasses == 2);
  REQUIRE(stats.home.completedProgressivePasses == 1);
  REQUIRE(stats.away.passes == 1);
  REQUIRE(stats.away.progressivePasses == 1);
  REQUIRE(stats.away.completedPasses == 0);
  REQUIRE(stats.away.passCompletion == 0.0);
}

TEST_CASE("Shots count per side and are no passes", "[analytics]") {
  const auto shot = [](const std::int64_t tick, const std::uint32_t shooter) -> MatchEvent {
    return ShotAttempted{.tick = SimTick(tick),
                         .shooter = PlayerId(shooter),
                         .from = {.x = 50.0, .y = 20.0},
                         .target = {.x = 60.0, .y = 20.0},
                         .height = 0.5,
                         .struckAt = {.x = 60.0, .y = 20.2},
                         .struckHeight = 0.6,
                         .speed = 25.0,
                         .distance = 10.0,
                         .opening = 0.4};
  };
  const MatchStats stats = analyze({{shot(1, 1)}, {shot(5, 2)}, {shot(9, 3)}});
  REQUIRE(stats.home.shots == 2);
  REQUIRE(stats.away.shots == 1);
  REQUIRE(stats.home.passes == 0);
}

TEST_CASE("Goals make the score, and full time the points", "[analytics]") {
  using ElyverseFootball::SimMatch::GoalScored;
  using ElyverseFootball::SimMatch::MatchPeriod;
  using ElyverseFootball::SimMatch::PeriodEnded;
  const auto goal = [](const std::int64_t tick, const TeamSide side) -> MatchEvent {
    return GoalScored{.tick = SimTick(tick),
                      .side = side,
                      .scorer = std::nullopt,
                      .assist = std::nullopt,
                      .ownGoal = false,
                      .score = {}};
  };
  const auto ended = [](const std::int64_t tick, const MatchPeriod period) -> MatchEvent {
    return PeriodEnded{.tick = SimTick(tick), .period = period, .addedMinutes = 1};
  };
  // Home's second is an own goal by away's player 3: home's all the same.
  const MatchEvent ownGoal = GoalScored{.tick = SimTick(60),
                                        .side = TeamSide::kHome,
                                        .scorer = PlayerId(3),
                                        .assist = std::nullopt,
                                        .ownGoal = true,
                                        .score = {}};
  const std::vector<std::vector<MatchEvent>> halfTime{
      {goal(3, TeamSide::kAway)}, {ended(40, MatchPeriod::kFirstHalf)}, {ownGoal}};
  const MatchStats open = analyze(halfTime);
  REQUIRE(open.home.goals == 1);
  REQUIRE(open.away.goals == 1);
  REQUIRE_FALSE(open.finished);
  REQUIRE_FALSE(open.home.points.has_value());

  std::vector<std::vector<MatchEvent>> won = halfTime;
  won.push_back({goal(70, TeamSide::kHome)});
  won.push_back({ended(90, MatchPeriod::kSecondHalf)});
  const MatchStats final = analyze(won);
  REQUIRE(final.finished);
  REQUIRE(final.home.goals == 2);
  REQUIRE(final.home.points == 3);
  REQUIRE(final.away.points == 0);

  std::vector<std::vector<MatchEvent>> drawn = halfTime;
  drawn.push_back({ended(90, MatchPeriod::kSecondHalf)});
  REQUIRE(analyze(drawn).home.points == 1);
  REQUIRE(analyze(drawn).away.points == 1);
}

TEST_CASE("Fouls and cards count against the offender's side", "[analytics]") {
  using ElyverseFootball::SimMatch::Card;
  using ElyverseFootball::SimMatch::FoulCalled;
  using ElyverseFootball::SimMatch::FoulRestart;
  const auto foul = [](const std::int64_t tick, const Card card) -> MatchEvent {
    return FoulCalled{.tick = SimTick(tick),
                      .side = TeamSide::kAway,
                      .offender = PlayerId(3),
                      .victim = PlayerId(1),
                      .position = {.x = 30.0, .y = 20.0},
                      .restart = FoulRestart::kFreeKick,
                      .card = card,
                      .fromBehind = false,
                      .denyingGoal = false};
  };
  const MatchStats stats =
      analyze({{foul(1, Card::kNone)}, {foul(2, Card::kYellow)}, {foul(3, Card::kSecondYellow)}});
  REQUIRE(stats.away.fouls == 3);
  REQUIRE(stats.away.yellowCards == 1);
  REQUIRE(stats.away.redCards == 1);
  REQUIRE(stats.home.fouls == 0);
}

TEST_CASE("A pass just short of the progressive distance is not progressive", "[analytics]") {
  MatchAnalyzer analyzer(
      context(), AnalyticsConfig{.progressiveMeters = 12.0, .ppdaZone = 0.6, .chances = {}});
  analyzer.observeStep(std::vector{pass(1, 1, {.x = 10.0, .y = 10.0}, {.x = 21.9, .y = 10.0})});
  analyzer.observeStep(std::vector{pass(2, 1, {.x = 10.0, .y = 10.0}, {.x = 22.0, .y = 10.0})});
  REQUIRE(analyzer.finish(SimTick(3)).home.progressivePasses == 1);
}

TEST_CASE("A change of side is a turnover and a regain, by third", "[analytics]") {
  // Away intercepts at x = 50, 10 m from its own goal line: its defensive
  // third. Home wins the ball back at x = 45: its attacking third. Then away
  // gets the ball without an event saying where -- a command, say.
  const MatchStats stats =
      analyze({{owner(0, std::nullopt, 1)},
               {owner(10, 1, std::nullopt)},
               {intercepted(20, 3, 1, {.x = 50.0, .y = 20.0}), owner(20, std::nullopt, 3)},
               {won(30, 2, 3, {.x = 45.0, .y = 20.0}), owner(30, 3, 2)},
               {owner(40, 2, 4)}});
  REQUIRE(stats.home.turnovers == 2);
  REQUIRE(stats.away.turnovers == 1);
  REQUIRE(stats.away.regains == 2);
  REQUIRE(regainsIn(stats.away.regainsByThird, PitchThird::kDefensive) == 1);
  REQUIRE(regainsIn(stats.away.regainsByThird, PitchThird::kMiddle) == 0);
  REQUIRE(regainsIn(stats.away.regainsByThird, PitchThird::kAttacking) == 0);
  REQUIRE(stats.home.regains == 1);
  REQUIRE(regainsIn(stats.home.regainsByThird, PitchThird::kAttacking) == 1);
}

TEST_CASE("Initial possession is credited from tick zero and its loss is a turnover",
          "[analytics]") {
  // Home already owns the ball in the initial state -- no PossessionChanged
  // announces that -- and loses it to an interception in away's defensive
  // third at tick 20.
  MatchContext initial = context();
  initial.initialPossession = TeamSide::kHome;
  MatchAnalyzer analyzer(initial);
  const std::vector<MatchEvent> step{intercepted(20, 3, 1, {.x = 50.0, .y = 20.0}),
                                     owner(20, std::nullopt, 3)};
  analyzer.observeStep(step);
  const MatchStats stats = analyzer.finish(SimTick(100));
  REQUIRE(stats.home.possessionShare == Approx(20.0 / 100.0));
  REQUIRE(stats.away.possessionShare == Approx(80.0 / 100.0));
  REQUIRE(stats.home.turnovers == 1);
  REQUIRE(stats.away.regains == 1);
  REQUIRE(regainsIn(stats.away.regainsByThird, PitchThird::kDefensive) == 1);
}

TEST_CASE("Each regain in a step matches its own control event", "[analytics]") {
  // Two ownership changes in the same step: away wins a duel in its
  // defensive third, then home immediately regains it from a command, whose
  // location analytics cannot know. The command's regain must not be
  // assigned the duel's position.
  const MatchStats stats =
      analyze({{owner(0, std::nullopt, 1)},
               {won(10, 3, 1, {.x = 50.0, .y = 20.0}), owner(10, 1, 3), owner(10, 3, 2)}});
  REQUIRE(stats.away.regains == 1);
  REQUIRE(regainsIn(stats.away.regainsByThird, PitchThird::kDefensive) == 1);
  REQUIRE(stats.home.regains == 1);
  REQUIRE(regainsIn(stats.home.regainsByThird, PitchThird::kDefensive) == 0);
  REQUIRE(regainsIn(stats.home.regainsByThird, PitchThird::kMiddle) == 0);
  REQUIRE(regainsIn(stats.home.regainsByThird, PitchThird::kAttacking) == 0);
}

TEST_CASE("The first owner of the match is no regain", "[analytics]") {
  const MatchStats stats =
      analyze({{LooseBallRecovered{.tick = SimTick(5), .player = PlayerId(3), .position = {}},
                owner(5, std::nullopt, 3)}});
  REQUIRE(stats.away.regains == 0);
  REQUIRE(stats.home.turnovers == 0);
}

TEST_CASE("Presses count as pressures and regains", "[analytics]") {
  const auto started = [](const std::int64_t tick) {
    return PressingStarted{.tick = SimTick(tick),
                           .side = TeamSide::kHome,
                           .carrier = PlayerId(3),
                           .trigger = std::nullopt,
                           .assignments = {}};
  };
  const auto ended = [](const std::int64_t tick, const PressOutcome outcome) {
    return PressingEnded{.tick = SimTick(tick), .side = TeamSide::kHome, .outcome = outcome};
  };
  const MatchStats stats = analyze({{started(1)},
                                    {ended(20, PressOutcome::kPassedOut)},
                                    {started(30)},
                                    {ended(40, PressOutcome::kBallRegained)}});
  REQUIRE(stats.home.pressures == 2);
  REQUIRE(stats.home.pressuresRegained == 1);
  REQUIRE(stats.away.pressures == 0);
}

TEST_CASE("PPDA divides opponent build-up passes by defensive actions there", "[analytics]") {
  // Away's build-up zone is x >= 24 (its 60 % from its own goal line at
  // x = 60). Three away passes start there, one does not. Home defends there
  // twice -- a won duel and an interception -- and once outside it.
  const MatchStats stats = analyze({{pass(1, 3, {.x = 50.0, .y = 20.0}, {.x = 40.0, .y = 20.0})},
                                    {pass(2, 4, {.x = 30.0, .y = 20.0}, {.x = 20.0, .y = 20.0})},
                                    {pass(3, 3, {.x = 24.0, .y = 20.0}, {.x = 20.0, .y = 20.0})},
                                    {pass(4, 3, {.x = 10.0, .y = 20.0}, {.x = 5.0, .y = 20.0})},
                                    {won(5, 1, 3, {.x = 40.0, .y = 20.0})},
                                    {intercepted(6, 2, 4, {.x = 30.0, .y = 20.0})},
                                    {won(7, 1, 3, {.x = 10.0, .y = 20.0})}});
  REQUIRE(stats.home.ppda == Approx(3.0 / 2.0));
  // Away made no defensive action: no PPDA.
  REQUIRE_FALSE(stats.away.ppda.has_value());
}

TEST_CASE("Pitch control and territory come from the samples", "[analytics]") {
  // The ball once in away's third (home attacks it), once in the middle, once
  // in home's third.
  const MatchStats stats = analyze({{sample(0, 0.6, {.x = 50.0, .y = 20.0})},
                                    {sample(10, 0.5, {.x = 30.0, .y = 20.0})},
                                    {sample(20, 0.4, {.x = 5.0, .y = 20.0})}});
  REQUIRE(stats.home.pitchControlShare == Approx(0.5));
  REQUIRE(stats.away.pitchControlShare == Approx(0.5));
  REQUIRE(stats.home.attackingThirdShare == Approx(1.0 / 3.0));
  REQUIRE(stats.away.attackingThirdShare == Approx(1.0 / 3.0));
}

TEST_CASE("Without events every rate is empty", "[analytics]") {
  const MatchStats stats = analyze({}, 0);
  REQUIRE(stats.home.possessionShare == 0.0);
  REQUIRE_FALSE(stats.home.passCompletion.has_value());
  REQUIRE_FALSE(stats.home.meanPassMeters.has_value());
  REQUIRE_FALSE(stats.home.ppda.has_value());
  REQUIRE_FALSE(stats.home.pitchControlShare.has_value());
  REQUIRE_FALSE(stats.away.attackingThirdShare.has_value());
}

TEST_CASE("Events of unknown players are ignored", "[analytics]") {
  const MatchStats stats = analyze({{pass(1, 99, {.x = 10.0, .y = 10.0}, {.x = 40.0, .y = 10.0})},
                                    {owner(2, std::nullopt, 99)},
                                    {won(3, 98, 99, {.x = 30.0, .y = 20.0})}});
  REQUIRE(stats == analyze({}));
}

TEST_CASE("MatchAnalyzer rejects an unusable context or configuration", "[analytics]") {
  MatchContext noPitch = context();
  noPitch.pitchLengthMeters = 0.0;
  REQUIRE_THROWS_AS(MatchAnalyzer(noPitch), std::invalid_argument);
  MatchContext noClock = context();
  noClock.ticksPerSecond = 0;
  REQUIRE_THROWS_AS(MatchAnalyzer(noClock), std::invalid_argument);
  REQUIRE_THROWS_AS(
      MatchAnalyzer(context(), {.progressiveMeters = -1.0, .ppdaZone = 0.6, .chances = {}}),
      std::invalid_argument);
  REQUIRE_THROWS_AS(
      MatchAnalyzer(context(), {.progressiveMeters = 10.0, .ppdaZone = 0.0, .chances = {}}),
      std::invalid_argument);
}

TEST_CASE("Corners, throw-ins and goal kicks count for the side taking them", "[analytics]") {
  using ElyverseFootball::SimMatch::SetPieceTaken;
  using ElyverseFootball::SimTactics::SetPieceDelivery;
  using ElyverseFootball::SimTactics::SetPieceKind;
  const auto taken = [](const std::int64_t tick, const SetPieceKind kind,
                        const TeamSide side) -> MatchEvent {
    return SetPieceTaken{.tick = SimTick(tick),
                         .kind = kind,
                         .side = side,
                         .taker = PlayerId(side == TeamSide::kHome ? 1 : 3),
                         .delivery = SetPieceDelivery::kShort,
                         .target = {.x = 30.0, .y = 20.0},
                         .receiver = std::nullopt};
  };
  const MatchStats stats = analyze({{taken(1, SetPieceKind::kCorner, TeamSide::kHome)},
                                    {taken(2, SetPieceKind::kCorner, TeamSide::kHome)},
                                    {taken(3, SetPieceKind::kThrowIn, TeamSide::kAway)},
                                    {taken(4, SetPieceKind::kGoalKick, TeamSide::kAway)},
                                    {taken(5, SetPieceKind::kKickoff, TeamSide::kHome)}});
  REQUIRE(stats.home.corners == 2);
  REQUIRE(stats.home.throwIns == 0);
  REQUIRE(stats.home.goalKicks == 0);
  REQUIRE(stats.away.corners == 0);
  REQUIRE(stats.away.throwIns == 1);
  REQUIRE(stats.away.goalKicks == 1);
}

TEST_CASE("A substitute counts for his side from his substitution on", "[analytics]") {
  using ElyverseFootball::SimMatch::SubstitutionMade;
  const MatchEvent substitution = SubstitutionMade{.tick = SimTick(1),
                                                   .side = TeamSide::kAway,
                                                   .off = PlayerId(3),
                                                   .on = PlayerId(30),
                                                   .ordered = SimTick(0)};
  const MatchEvent shot = ShotAttempted{.tick = SimTick(2),
                                        .shooter = PlayerId(30),
                                        .from = {.x = 20.0, .y = 20.0},
                                        .target = {.x = 0.0, .y = 20.0},
                                        .height = 0.5,
                                        .struckAt = {.x = 0.0, .y = 20.2},
                                        .struckHeight = 0.6,
                                        .speed = 20.0,
                                        .distance = 20.0,
                                        .opening = 0.4};
  REQUIRE(analyze({{shot}}).away.shots == 0);
  REQUIRE(analyze({{substitution}, {shot}}).away.shots == 1);
}

namespace {

// A shot by `shooter` at tick `tick` from `distance` metres with this opening.
[[nodiscard]] MatchEvent shotFrom(const std::int64_t tick, const std::uint32_t shooter,
                                  const double distance, const double opening) {
  return ShotAttempted{.tick = SimTick(tick),
                       .shooter = PlayerId(shooter),
                       .from = {.x = 50.0, .y = 20.0},
                       .target = {.x = 60.0, .y = 20.0},
                       .height = 0.5,
                       .struckAt = {.x = 60.0, .y = 20.2},
                       .struckHeight = 0.6,
                       .speed = 25.0,
                       .distance = distance,
                       .opening = opening};
}

[[nodiscard]] MatchEvent resolved(const std::int64_t tick, const std::uint32_t shooter,
                                  const ElyverseFootball::SimMatch::ShotOutcome outcome) {
  return ElyverseFootball::SimMatch::ShotResolved{.tick = SimTick(tick + 5),
                                                  .shooter = PlayerId(shooter),
                                                  .shotTick = SimTick(tick),
                                                  .outcome = outcome};
}

}  // namespace

TEST_CASE("Shots on target and saves come from what became of each shot", "[analytics]") {
  using enum ElyverseFootball::SimMatch::ShotOutcome;
  const MatchStats stats = analyze({{shotFrom(1, 1, 11.0, 0.63)},
                                    {resolved(1, 1, kGoal)},
                                    {shotFrom(10, 1, 20.0, 0.36)},
                                    {resolved(10, 1, kSaved)},
                                    {shotFrom(20, 2, 25.0, 0.29)},
                                    {resolved(20, 2, kOffTarget)},
                                    {shotFrom(30, 3, 16.0, 0.45)},
                                    {resolved(30, 3, kBlocked)}});
  REQUIRE(stats.home.shots == 3);
  REQUIRE(stats.home.shotsOnTarget == 2);
  REQUIRE(stats.away.saves == 1);
  REQUIRE(stats.home.saves == 0);
  REQUIRE(stats.away.shots == 1);
  REQUIRE(stats.away.shotsOnTarget == 0);
}

TEST_CASE("Chance quality sums each shot's worth, a penalty at its fixed value", "[analytics]") {
  using ElyverseFootball::SimAnalytics::chanceQuality;
  using ElyverseFootball::SimMatch::SetPieceTaken;
  using ElyverseFootball::SimTactics::SetPieceDelivery;
  using ElyverseFootball::SimTactics::SetPieceKind;
  // The documented reference values.
  REQUIRE(chanceQuality(6.0, 1.042) == Approx(0.455).margin(0.001));
  REQUIRE(chanceQuality(25.0, 0.29) == Approx(0.038).margin(0.005));
  REQUIRE(chanceQuality(6.0, 1.042) > chanceQuality(11.0, 0.63));
  REQUIRE(chanceQuality(11.0, 0.63) > chanceQuality(11.0, 0.2));

  const MatchEvent penalty = SetPieceTaken{.tick = SimTick(40),
                                           .kind = SetPieceKind::kPenalty,
                                           .side = TeamSide::kHome,
                                           .taker = PlayerId(2),
                                           .delivery = SetPieceDelivery::kShort,
                                           .target = {.x = 60.0, .y = 20.0},
                                           .receiver = std::nullopt};
  // The penalty is taken in one step and shot in the next, as the set piece
  // system and the ball system play it.
  const MatchStats stats = analyze({{shotFrom(1, 1, 11.0, 0.63)},
                                    {penalty},
                                    {shotFrom(41, 2, 11.0, 0.63)},
                                    // The same taker's next shot is open play again.
                                    {shotFrom(60, 2, 11.0, 0.63)}});
  REQUIRE(stats.home.chanceQuality ==
          Approx((2.0 * chanceQuality(11.0, 0.63)) + 0.76).epsilon(1e-12));
  REQUIRE(stats.home.penalties == 1);
  REQUIRE(stats.away.chanceQuality == 0.0);

  // A taker who passes instead did not shoot the penalty.
  const MatchStats passed = analyze({{penalty},
                                     {pass(41, 2, {.x = 49.0, .y = 20.0}, {.x = 45.0, .y = 25.0})},
                                     {shotFrom(60, 2, 11.0, 0.63)}});
  REQUIRE(passed.home.chanceQuality == Approx(chanceQuality(11.0, 0.63)).epsilon(1e-12));
}

TEST_CASE("The chance model is checked", "[analytics]") {
  using ElyverseFootball::SimAnalytics::ChanceQualityModel;
  const auto analyzerWith = [](const ChanceQualityModel& chances) {
    return MatchAnalyzer(context(),
                         {.progressiveMeters = 10.0, .ppdaZone = 0.6, .chances = chances});
  };
  REQUIRE_THROWS_AS(analyzerWith({.penaltyChance = 1.5}), std::invalid_argument);
  REQUIRE_THROWS_AS(analyzerWith({.perMeter = std::numeric_limits<double>::quiet_NaN()}),
                    std::invalid_argument);
}

TEST_CASE("Duels count for the side that won and the side that lost them", "[analytics]") {
  using ElyverseFootball::SimMatch::DuelEnded;
  using enum ElyverseFootball::SimMatch::DuelOutcome;
  const auto duel = [](const std::int64_t tick,
                       const ElyverseFootball::SimMatch::DuelOutcome outcome) -> MatchEvent {
    return DuelEnded{.tick = SimTick(tick),
                     .carrier = PlayerId(1),
                     .defender = PlayerId(3),
                     .outcome = outcome,
                     .position = {.x = 30.0, .y = 20.0}};
  };
  const MatchStats stats = analyze(
      {{duel(1, kKept)}, {duel(2, kWon)}, {duel(3, kWon)}, {duel(4, kLoose)}, {duel(5, kFoul)}});
  REQUIRE(stats.home.duelsWon == 1);
  REQUIRE(stats.home.duelsLost == 2);
  REQUIRE(stats.away.duelsWon == 2);
  REQUIRE(stats.away.duelsLost == 1);
}

TEST_CASE("Aerial duels are contests of both sides; their winner's side won them", "[analytics]") {
  using ElyverseFootball::SimMatch::AerialContest;
  using ElyverseFootball::SimMatch::AerialContestant;
  const auto contest = [](const std::int64_t tick, const std::vector<std::uint32_t>& players,
                          const std::optional<std::uint32_t> winner) -> MatchEvent {
    std::vector<AerialContestant> contestants;
    contestants.reserve(players.size());
    for (const std::uint32_t player : players) {
      contestants.push_back(
          {.player = PlayerId(player), .reach = 2.5, .reached = winner == player});
    }
    return AerialContest{.tick = SimTick(tick),
                         .position = {.x = 50.0, .y = 20.0},
                         .height = 2.4,
                         .contestants = std::move(contestants),
                         .winner = winner ? std::optional(PlayerId(*winner)) : std::nullopt,
                         .play = std::nullopt};
  };
  const MatchStats stats = analyze({{contest(1, {1, 3}, 3)},
                                    {contest(2, {2, 4, 3}, 2)},
                                    {contest(3, {1, 3}, std::nullopt)},
                                    // One side alone is no duel.
                                    {contest(4, {1, 2}, 1)}});
  REQUIRE(stats.home.aerialDuels == 3);
  REQUIRE(stats.away.aerialDuels == 3);
  REQUIRE(stats.home.aerialDuelsWon == 1);
  REQUIRE(stats.away.aerialDuelsWon == 1);
}
