#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <span>

#include "ids.hpp"
#include "matchEvents.hpp"
#include "matchState.hpp"
#include "matchStats.hpp"
#include "simTime.hpp"
#include "vec2.hpp"

namespace ElyverseFootball::SimAnalytics {

// What an analysis knows besides the events, fixed for the whole match: the
// pitch, the clock and who plays for which side.
struct MatchContext {
  double pitchLengthMeters = 0.0;
  int ticksPerSecond = 0;
  std::map<SimCore::PlayerId, SimMatch::TeamSide> sides;
};

// The context of a match starting from this state.
[[nodiscard]] MatchContext contextOf(const SimMatch::MatchState& initialState, int ticksPerSecond);

// The definitions behind the metrics (docs/match-analytics.md).
struct AnalyticsConfig {
  // A pass is progressive if it moves the ball at least this far toward the
  // opponent's goal line.
  double progressiveMeters = 10.0;
  // PPDA counts opponent passes played from, and own defensive actions made
  // in, the part of the pitch this far from the opponent's goal line, as a
  // fraction of the length: the opponent's build-up zone.
  double ppdaZone = 0.6;

  friend bool operator==(const AnalyticsConfig&, const AnalyticsConfig&) = default;
};

// Match statistics from domain events only: fed the events of every step in
// order, it never reads a match state. The same events always give the same
// statistics, bit for bit.
//
//   MatchAnalyzer analyzer(contextOf(setup.initialState, ticksPerSecond));
//   while (...) { simulation.step(); analyzer.observeStep(simulation.events()); }
//   const MatchStats stats = analyzer.finish(simulation.tick());
//
// Throws std::invalid_argument for a context without a pitch length or tick
// rate, or an invalid configuration.
class MatchAnalyzer {
 public:
  explicit MatchAnalyzer(MatchContext context, AnalyticsConfig config = {});

  // The events of one step, in the order it recorded them. Events of players
  // the context does not know are ignored.
  void observeStep(std::span<const SimMatch::MatchEvent> events);

  // The statistics of the match up to finalTick, the tick after its last
  // step.
  [[nodiscard]] MatchStats finish(SimCore::SimTick finalTick) const;

 private:
  // Counts kept per side, indexed by kHome and kAway.
  struct SideTally {
    std::int64_t possessionTicks = 0;
    int passes = 0;
    int completedPasses = 0;
    double passMeters = 0.0;
    int progressivePasses = 0;
    int completedProgressivePasses = 0;
    int turnovers = 0;
    int regains = 0;
    std::array<int, kThirdCount> regainsByThird{};
    int pressures = 0;
    int pressuresRegained = 0;
    int ppdaPasses = 0;
    int ppdaActions = 0;
    double pitchControlSum = 0.0;
    int attackingThirdSamples = 0;
  };

  // The pass in flight, so a reception can be credited to it.
  struct PendingPass {
    SimCore::PlayerId passer;
    bool progressive = false;
  };

  [[nodiscard]] std::optional<SimMatch::TeamSide> sideOf(SimCore::PlayerId player) const;
  [[nodiscard]] SideTally& tally(SimMatch::TeamSide side);
  [[nodiscard]] double depthOf(SimMatch::TeamSide side, SimCore::Vec2 position) const noexcept;
  [[nodiscard]] PitchThird thirdOf(SimMatch::TeamSide side, SimCore::Vec2 position) const noexcept;
  [[nodiscard]] TeamStats statsOf(SimMatch::TeamSide side, std::int64_t possessedTicks,
                                  std::int64_t anyPossessionTicks) const;

  void onPass(const SimMatch::PassAttempted& pass);
  void onReception(const SimMatch::PassReceived& reception);
  void onDefensiveAction(SimCore::PlayerId player, SimCore::Vec2 position);
  void onOwner(const SimMatch::PossessionChanged& change,
               const std::optional<SimCore::Vec2>& wonAt);
  void onPress(const SimMatch::MatchEvent& event);
  void onSample(const SimMatch::PitchControlSampled& sample);

  MatchContext context_;
  AnalyticsConfig config_;
  std::array<SideTally, 2> sides_{};
  std::optional<SimMatch::TeamSide> possession_;
  SimCore::SimTick possessionSince_;
  std::optional<PendingPass> pendingPass_;
  int samples_ = 0;
};

}  // namespace ElyverseFootball::SimAnalytics
