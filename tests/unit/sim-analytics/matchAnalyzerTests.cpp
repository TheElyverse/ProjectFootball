// Every metric of MatchAnalyzer on hand-built event streams: a 60 m pitch at
// 30 Hz, home players 1 and 2 attacking +x, away players 3 and 4 attacking
// -x.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
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
using ElyverseFootball::SimMatch::TeamSide;

namespace {

[[nodiscard]] MatchContext context() {
  return {.pitchLengthMeters = 60.0,
          .ticksPerSecond = 30,
          .sides = {{PlayerId(1), TeamSide::kHome},
                    {PlayerId(2), TeamSide::kHome},
                    {PlayerId(3), TeamSide::kAway},
                    {PlayerId(4), TeamSide::kAway}}};
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

TEST_CASE("A pass just short of the progressive distance is not progressive", "[analytics]") {
  MatchAnalyzer analyzer(context(), AnalyticsConfig{.progressiveMeters = 12.0, .ppdaZone = 0.6});
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
  // twice -- a won challenge and an interception -- and once outside it.
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
  REQUIRE_THROWS_AS(MatchAnalyzer(context(), {.progressiveMeters = -1.0, .ppdaZone = 0.6}),
                    std::invalid_argument);
  REQUIRE_THROWS_AS(MatchAnalyzer(context(), {.progressiveMeters = 10.0, .ppdaZone = 0.0}),
                    std::invalid_argument);
}
