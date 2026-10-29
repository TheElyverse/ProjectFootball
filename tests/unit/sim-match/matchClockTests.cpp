// The match clock (docs/match-clock.md): halves, the ninety-minute equivalent,
// added time, and how players tire (docs/players.md#fitness).

#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>

#include "fitness.hpp"
#include "matchClock.hpp"
#include "matchState.hpp"
#include "restartKind.hpp"
#include "simTime.hpp"

using ElyverseFootball::SimCore::SimTick;
using ElyverseFootball::SimMatch::addedMinutes;
using ElyverseFootball::SimMatch::addedTicks;
using ElyverseFootball::SimMatch::ClockReading;
using ElyverseFootball::SimMatch::endsChanged;
using ElyverseFootball::SimMatch::FitnessConfig;
using ElyverseFootball::SimMatch::formatClock;
using ElyverseFootball::SimMatch::halfTicks;
using ElyverseFootball::SimMatch::lostSeconds;
using ElyverseFootball::SimMatch::MatchClock;
using ElyverseFootball::SimMatch::MatchClockConfig;
using ElyverseFootball::SimMatch::MatchPeriod;
using ElyverseFootball::SimMatch::PlayerFitness;
using ElyverseFootball::SimMatch::PlayerMatchState;
using ElyverseFootball::SimMatch::readClock;
using ElyverseFootball::SimMatch::RestartKind;
using ElyverseFootball::SimMatch::stepFitness;

namespace {

constexpr int kTicksPerSecond = 30;
constexpr double kSecondsPerTick = 1.0 / kTicksPerSecond;

[[nodiscard]] MatchClockConfig lasting(const double minutes) {
  MatchClockConfig config;
  config.durationMinutes = minutes;
  return config;
}

[[nodiscard]] std::string shownAt(const MatchClock& clock, const std::int64_t tick,
                                  const MatchClockConfig& config) {
  return formatClock(readClock(clock, SimTick(tick), config, kTicksPerSecond));
}

// A run: so many ticks at a speed, in a match of halves of `half` ticks.
struct Run {
  std::int64_t ticks = 0;
  double speed = 0.0;
  std::int64_t half = 1;
};

// A player's fitness after the run, at a top speed of 7.5 m/s.
[[nodiscard]] PlayerFitness after(const PlayerFitness& fitness, const Run& run) {
  PlayerMatchState player;
  player.attributes.maxSpeed = 7.5;
  player.velocity = {.x = run.speed, .y = 0.0};
  player.fitness = fitness;
  for (std::int64_t tick = 0; tick < run.ticks; ++tick) {
    player.fitness = stepFitness(player, {.secondsPerTick = kSecondsPerTick, .halfTicks = run.half},
                                 FitnessConfig{});
  }
  return player.fitness;
}

// Seconds as ticks.
[[nodiscard]] constexpr std::int64_t ticks(const std::int64_t seconds) {
  return seconds * kTicksPerSecond;
}

}  // namespace

TEST_CASE("A half lasts half the duration in ticks", "[matchClock]") {
  REQUIRE(halfTicks(lasting(90.0), kTicksPerSecond) == 81000);
  REQUIRE(halfTicks(lasting(10.0), kTicksPerSecond) == 9000);
  REQUIRE(halfTicks(lasting(10.0), 60) == 18000);
}

TEST_CASE("A ten-minute and a ninety-minute match show the same clock", "[matchClock]") {
  const MatchClockConfig ten = lasting(10.0);
  const MatchClockConfig ninety = lasting(90.0);
  const MatchClock first{};
  // Every tick of the ten-minute half, and the same moment nine ticks later
  // in the ninety-minute one.
  for (std::int64_t tick = 0; tick <= 9300; ++tick) {
    CAPTURE(tick);
    REQUIRE(readClock(first, SimTick(tick), ten, kTicksPerSecond) ==
            readClock(first, SimTick(9 * tick), ninety, kTicksPerSecond));
  }
  // A real minute of the ten-minute match is nine on the clock.
  REQUIRE(shownAt(first, 1800, ten) == "09:00");
  REQUIRE(shownAt(first, 8100, ten) == "40:30");
  REQUIRE(shownAt(first, 9000, ten) == "45:00");
  REQUIRE(shownAt(first, 9200, ten) == "45+01:00");
  REQUIRE(shownAt(first, 81000 + 1800, ninety) == "45+01:00");

  const MatchClock second{.period = MatchPeriod::kSecondHalf, .periodStart = SimTick(9200)};
  REQUIRE(shownAt(second, 9200, ten) == "45:00");
  REQUIRE(shownAt(second, 9200 + 4500, ten) == "67:30");
  REQUIRE(shownAt(second, 9200 + 9000, ten) == "90:00");
  REQUIRE(shownAt(second, 9200 + 9000 + 650, ten) == "90+03:15");

  const MatchClock fullTime{.period = MatchPeriod::kFullTime,
                            .periodStart = SimTick(9200),
                            .lostSeconds = 25.0,
                            .addedMinutes = 4};
  REQUIRE(readClock(fullTime, SimTick(100000), ten, kTicksPerSecond) ==
          ClockReading{.period = MatchPeriod::kFullTime, .seconds = 5400, .addedSeconds = 240});
  REQUIRE(shownAt(fullTime, 100000, ten) == "90+04:00");
}

TEST_CASE("Lost real seconds become added minutes on the clock", "[matchClock]") {
  const MatchClockConfig ten = lasting(10.0);
  const MatchClockConfig ninety = lasting(90.0);
  REQUIRE(addedMinutes(0.0, ninety) == 0);
  // The same twenty real seconds: a third of a shown minute in a ninety-minute
  // match, three in a ten-minute one.
  REQUIRE(addedMinutes(20.0, ninety) == 1);
  REQUIRE(addedMinutes(20.0, ten) == 3);
  REQUIRE(addedMinutes(120.0, ninety) == 2);
  REQUIRE(addedMinutes(121.0, ninety) == 3);
  // Played in real time again: twenty seconds of the ten-minute match.
  REQUIRE(addedTicks(3, ten, kTicksPerSecond) == 600);
  REQUIRE(addedTicks(1, ninety, kTicksPerSecond) == 1800);

  const MatchClockConfig config;
  REQUIRE(lostSeconds(RestartKind::kThrowIn, config) == config.throwInLostSeconds);
  REQUIRE(lostSeconds(RestartKind::kGoalKick, config) == config.goalKickLostSeconds);
  REQUIRE(lostSeconds(RestartKind::kCorner, config) == config.cornerLostSeconds);
  REQUIRE(lostSeconds(RestartKind::kKickoff, config) == config.kickoffLostSeconds);
}

TEST_CASE("The teams change ends at half time", "[matchClock]") {
  REQUIRE_FALSE(endsChanged(MatchPeriod::kFirstHalf));
  REQUIRE(endsChanged(MatchPeriod::kSecondHalf));
  REQUIRE(endsChanged(MatchPeriod::kFullTime));
}

TEST_CASE("An invalid clock configuration is rejected", "[matchClock]") {
  REQUIRE_NOTHROW(validate(MatchClockConfig{}, kTicksPerSecond));
  for (const double minutes : {0.0, -10.0, std::numeric_limits<double>::quiet_NaN(),
                               std::numeric_limits<double>::infinity(), 1e-6, 2000.0}) {
    CAPTURE(minutes);
    REQUIRE_THROWS_AS(validate(lasting(minutes), kTicksPerSecond), std::invalid_argument);
  }
  MatchClockConfig lost;
  lost.cornerLostSeconds = -1.0;
  REQUIRE_THROWS_AS(validate(lost, kTicksPerSecond), std::invalid_argument);
  REQUIRE_THROWS_AS(validate(MatchClockConfig{}, 0), std::invalid_argument);
}

TEST_CASE("Fatigue follows the share of the match a player has played", "[fitness]") {
  // Nine of ten minutes tire as much as eighty-one of ninety: the share of
  // the duration, not the time.
  const PlayerFitness ten =
      after({}, {.ticks = ticks(std::int64_t{9} * 60), .speed = 2.0, .half = 9000});
  const PlayerFitness ninety =
      after({}, {.ticks = ticks(std::int64_t{81} * 60), .speed = 2.0, .half = 81000});
  REQUIRE(ten.topSpeedShare == ninety.topSpeedShare);
  REQUIRE(std::abs(ten.topSpeedShare - (1.0 - (0.06 * 0.9))) < 1e-12);
  // Added time tires no further.
  REQUIRE(after({}, {.ticks = 20000, .speed = 2.0, .half = 9000}).topSpeedShare ==
          after({}, {.ticks = 18000, .speed = 2.0, .half = 9000}).topSpeedShare);
}

TEST_CASE("A sprint drains and recovers in real time, whatever the duration", "[fitness]") {
  for (const std::int64_t half : {std::int64_t{9000}, std::int64_t{81000}}) {
    CAPTURE(half);
    // Five seconds at full speed use a third of a full reserve.
    const PlayerFitness sprinted = after({}, {.ticks = ticks(5), .speed = 7.5, .half = half});
    REQUIRE(std::abs(sprinted.sprintReserve - (2.0 / 3.0)) < 1e-9);
    REQUIRE(sprinted.topSpeedShare > 0.85);
    // Fifteen seconds use all of it: no faster than sprint speed after that.
    const PlayerFitness empty = after({}, {.ticks = ticks(15), .speed = 7.5, .half = half});
    REQUIRE(empty.sprintReserve == 0.0);
    REQUIRE(empty.topSpeedShare <= 0.85);
    // Jogging, forty-five seconds fill it again, to rounding.
    const PlayerFitness recovered = after(empty, {.ticks = ticks(45), .speed = 4.0, .half = half});
    REQUIRE(recovered.sprintReserve > 1.0 - 1e-9);
    REQUIRE(after(empty, {.ticks = ticks(45) + 1, .speed = 4.0, .half = half}).sprintReserve ==
            1.0);
    REQUIRE(recovered.topSpeedShare > 0.85);
  }
}

TEST_CASE("An invalid fitness configuration is rejected", "[fitness]") {
  REQUIRE_NOTHROW(validate(FitnessConfig{}));
  FitnessConfig share;
  share.sprintSpeedShare = 0.0;
  REQUIRE_THROWS_AS(validate(share), std::invalid_argument);
  FitnessConfig seconds;
  seconds.recoverySeconds = std::numeric_limits<double>::infinity();
  REQUIRE_THROWS_AS(validate(seconds), std::invalid_argument);
  FitnessConfig loss;
  loss.fullMatchSpeedLoss = 1.0;
  REQUIRE_THROWS_AS(validate(loss), std::invalid_argument);
}
