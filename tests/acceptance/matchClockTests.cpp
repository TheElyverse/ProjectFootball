// The match clock (docs/match-clock.md): a ten-minute tactic match, restarts
// on, plays two halves with added time by the documented rule, kicks off the
// second half the other way round, stops at full time, and replays with its
// clock through the file format.

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <optional>
#include <variant>
#include <vector>

#include "matchClock.hpp"
#include "matchEvents.hpp"
#include "matchSetup.hpp"
#include "matchSimulation.hpp"
#include "matchState.hpp"
#include "referenceTactic.hpp"
#include "replay.hpp"
#include "replayJson.hpp"
#include "scenarios.hpp"
#include "simTime.hpp"
#include "tactic.hpp"

using ElyverseFootball::SimCore::SimTick;
using ElyverseFootball::SimMatch::addedMinutes;
using ElyverseFootball::SimMatch::addedTicks;
using ElyverseFootball::SimMatch::ClockReading;
using ElyverseFootball::SimMatch::halfTicks;
using ElyverseFootball::SimMatch::lostSeconds;
using ElyverseFootball::SimMatch::makeTacticMatch;
using ElyverseFootball::SimMatch::MatchEvent;
using ElyverseFootball::SimMatch::MatchPeriod;
using ElyverseFootball::SimMatch::MatchSetup;
using ElyverseFootball::SimMatch::MatchSimulation;
using ElyverseFootball::SimMatch::MatchState;
using ElyverseFootball::SimMatch::PeriodEnded;
using ElyverseFootball::SimMatch::PlayerMatchState;
using ElyverseFootball::SimMatch::readClock;
using ElyverseFootball::SimMatch::RestartTaken;
using ElyverseFootball::SimMatch::startMatch;
using ElyverseFootball::SimMatch::TeamSide;
using ElyverseFootball::SimTactics::referenceTacticSpec;
using ElyverseFootball::SimTactics::Tactic;

namespace {

constexpr int kTicksPerSecond = 30;
// Longer than any ten-minute match with its added time.
constexpr SimTick::ValueType kMaxTicks = 30000;

[[nodiscard]] MatchSetup tenMinutes(const std::uint64_t seed) {
  const auto reference = Tactic::create(referenceTacticSpec());
  REQUIRE(reference.has_value());
  auto setup = makeTacticMatch({.home = *reference, .away = *reference}, seed);
  REQUIRE(setup.has_value());
  setup->config.restarts.enabled = true;
  setup->config.clock.durationMinutes = 10.0;
  return *std::move(setup);
}

// The tick a half ended in, and the state right after.
struct Whistle {
  PeriodEnded ended;
  MatchState after;
};

struct PlayedMatch {
  std::vector<Whistle> whistles;
  std::vector<RestartTaken> restarts;
  MatchSimulation simulation;
};

[[nodiscard]] PlayedMatch play(const MatchSetup& setup) {
  PlayedMatch played{.whistles = {}, .restarts = {}, .simulation = startMatch(setup)};
  MatchSimulation& simulation = played.simulation;
  while (simulation.state().clock().period != MatchPeriod::kFullTime &&
         simulation.tick() < SimTick(kMaxTicks)) {
    REQUIRE(simulation.step().has_value());
    for (const MatchEvent& event : simulation.events()) {
      if (const auto* restart = std::get_if<RestartTaken>(&event)) {
        played.restarts.push_back(*restart);
      } else if (const auto* ended = std::get_if<PeriodEnded>(&event)) {
        played.whistles.push_back({.ended = *ended, .after = simulation.state()});
      }
    }
  }
  return played;
}

// The real seconds the restarts after `after` and up to `upTo` lost: those of
// a half until its added time is announced. The whistle's tick belongs to the
// half it ends.
[[nodiscard]] double lostBetween(const std::vector<RestartTaken>& restarts, const SimTick after,
                                 const SimTick upTo, const MatchSetup& setup) {
  double lost = 0.0;
  for (const RestartTaken& restart : restarts) {
    if (restart.tick > after && restart.tick <= upTo) {
      lost += lostSeconds(restart.kind, setup.config.clock);
    }
  }
  return lost;
}

}  // namespace

TEST_CASE("Match clock: a ten-minute match plays two halves and ends at full time",
          "[acceptance][clock]") {
  for (const std::uint64_t seed : {std::uint64_t{1}, std::uint64_t{2}}) {
    CAPTURE(seed);
    const MatchSetup setup = tenMinutes(seed);
    const std::int64_t half = halfTicks(setup.config.clock, kTicksPerSecond);
    PlayedMatch played = play(setup);
    REQUIRE(played.whistles.size() == 2);
    const Whistle& first = played.whistles[0];
    const Whistle& second = played.whistles[1];
    REQUIRE(first.ended.period == MatchPeriod::kFirstHalf);
    REQUIRE(second.ended.period == MatchPeriod::kSecondHalf);

    // Each half lasts its regulation time and the minutes its restarts lost,
    // rounded up on the clock -- or a little longer while a shot is on its way.
    const SimTick secondStart = first.ended.tick;
    CAPTURE(played.restarts.size(), first.ended.addedMinutes, second.ended.addedMinutes);
    REQUIRE(first.ended.addedMinutes ==
            addedMinutes(lostBetween(played.restarts, SimTick(-1), SimTick(half), setup),
                         setup.config.clock));
    REQUIRE(second.ended.addedMinutes ==
            addedMinutes(lostBetween(played.restarts, secondStart,
                                     SimTick(secondStart.value() + half), setup),
                         setup.config.clock));
    REQUIRE(first.ended.addedMinutes > 0);
    const std::int64_t firstEnd =
        half + addedTicks(first.ended.addedMinutes, setup.config.clock, kTicksPerSecond);
    REQUIRE(first.ended.tick.value() >= firstEnd);
    REQUIRE(first.ended.tick.value() < firstEnd + (std::int64_t{5} * kTicksPerSecond));
    const std::int64_t secondEnd =
        secondStart.value() + half +
        addedTicks(second.ended.addedMinutes, setup.config.clock, kTicksPerSecond);
    REQUIRE(second.ended.tick.value() >= secondEnd);
    REQUIRE(second.ended.tick.value() < secondEnd + (std::int64_t{5} * kTicksPerSecond));

    // The clock shows the ninety-minute equivalent at each whistle.
    REQUIRE(readClock(first.after.clock(), first.ended.tick, setup.config.clock, kTicksPerSecond) ==
            ClockReading{.period = MatchPeriod::kSecondHalf, .seconds = 2700, .addedSeconds = 0});
    REQUIRE(
        readClock(second.after.clock(), second.ended.tick, setup.config.clock, kTicksPerSecond) ==
        ClockReading{.period = MatchPeriod::kFullTime,
                     .seconds = 5400,
                     .addedSeconds = std::int64_t{second.ended.addedMinutes} * 60});

    // Away kicks off the second half: everybody in his own half, the ball at
    // the feet of an away player on the centre spot.
    const MatchState& kickoff = first.after;
    REQUIRE(kickoff.ball().owner.has_value());
    const double halfway = kickoff.pitch().lengthMeters() / 2.0;
    for (const PlayerMatchState& player : kickoff.players()) {
      CAPTURE(player.playerId.value());
      if (kickoff.ball().owner == player.playerId) {
        REQUIRE(player.side == TeamSide::kAway);
      }
      REQUIRE((player.side == TeamSide::kHome ? player.position.x <= halfway
                                              : player.position.x >= halfway));
    }
    REQUIRE(kickoff.ball().position == kickoff.pitch().center());

    // After the final whistle nothing moves.
    const MatchState frozen = played.simulation.state();
    for (int tick = 0; tick < 30; ++tick) {
      REQUIRE(played.simulation.step().has_value());
      REQUIRE(played.simulation.events().empty());
    }
    REQUIRE(played.simulation.state() == frozen);
  }
}

TEST_CASE("Match clock: the duration is part of the replay", "[acceptance][clock]") {
  using ElyverseFootball::SimReplay::parseReplayJson;
  using ElyverseFootball::SimReplay::playReplay;
  using ElyverseFootball::SimReplay::recordMatch;
  using ElyverseFootball::SimReplay::toReplayJson;
  const MatchSetup setup = tenMinutes(1);
  const auto replay = recordMatch(setup, SimTick(20000), 30, "2026-10-29T12:00:00Z");
  REQUIRE(replay.has_value());
  const auto parsed = parseReplayJson(toReplayJson(*replay));
  REQUIRE(parsed.has_value());
  REQUIRE(parsed->setup.config.clock == setup.config.clock);
  REQUIRE(parsed->setup.config.fitness == setup.config.fitness);
  const auto playback = playReplay(*parsed);
  REQUIRE(playback.has_value());
  REQUIRE(playback->checkpointsVerified == replay->checkpoints.size());
}
