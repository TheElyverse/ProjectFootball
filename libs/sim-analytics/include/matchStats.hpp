#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace ElyverseFootball::SimAnalytics {

// The thirds of the pitch from one side's point of view: its own third, the
// middle third and the third it attacks.
enum class PitchThird : std::uint8_t {
  kDefensive,
  kMiddle,
  kAttacking,
};

inline constexpr std::size_t kThirdCount = 3;

// One side's numbers over a match (docs/tooling.md#match-analytics). Counts are
// exact; rates and means are empty where their denominator is zero.
struct TeamStats {
  // Share of the time either side had the ball that this side had it.
  double possessionShare = 0.0;
  // The longest time one player of the side kept the ball: a match that
  // stalls with a player who never plays it shows here.
  double longestOwnershipSeconds = 0.0;

  int passes = 0;
  int completedPasses = 0;
  std::optional<double> passCompletion;
  std::optional<double> meanPassMeters;
  // Passes that move the ball at least progressiveMeters toward the
  // opponent's goal line.
  int progressivePasses = 0;
  int completedProgressivePasses = 0;

  // Shots taken, and goals scored, own goals of the opponent included.
  int shots = 0;
  int goals = 0;
  // Offsides the side's players were caught in, the fouls they committed, and
  // the yellow and red cards they were shown -- a second yellow counts as a
  // red (docs/fouls.md).
  int offsides = 0;
  int fouls = 0;
  int yellowCards = 0;
  int redCards = 0;
  // 3 for a win, 1 for a draw, 0 for a defeat; empty for a match that did not
  // reach full time.
  std::optional<int> points;

  // Times the side lost the ball to the opponent, and won it from him.
  int turnovers = 0;
  int regains = 0;
  // Regains whose position is known, by third in this side's frame.
  std::array<int, kThirdCount> regainsByThird{};

  // Coordinated presses started, and those that won the ball back.
  int pressures = 0;
  int pressuresRegained = 0;
  // Opponent passes allowed per defensive action in the opponent's build-up
  // zone; lower means a more aggressive defence.
  std::optional<double> ppda;

  // Mean share of the pitch the side controls, over pitch control samples.
  std::optional<double> pitchControlShare;
  // Share of pitch control samples with the ball in the side's attacking
  // third.
  std::optional<double> attackingThirdShare;

  friend bool operator==(const TeamStats&, const TeamStats&) = default;
};

// A metric of TeamStats by name, empty where the stats leave it empty.
struct Metric {
  std::string_view name;
  std::optional<double> value;

  friend bool operator==(const Metric&, const Metric&) = default;
};

// Every metric of a side as a number, in a fixed order, regains by third as
// three metrics: what a benchmark aggregates over matches.
[[nodiscard]] std::vector<Metric> metricsOf(const TeamStats& stats);

struct MatchStats {
  std::int64_t ticks = 0;
  double seconds = 0.0;
  // Whether the match reached full time; its score is the sides' goals.
  bool finished = false;
  TeamStats home;
  TeamStats away;

  friend bool operator==(const MatchStats&, const MatchStats&) = default;
};

}  // namespace ElyverseFootball::SimAnalytics
