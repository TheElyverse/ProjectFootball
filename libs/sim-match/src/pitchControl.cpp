#include "pitchControl.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "spatialQueries.hpp"

namespace ElyverseFootball::SimMatch {
namespace {

void validate(const PitchControlConfig& config) {
  constexpr double kMax = std::numeric_limits<double>::max();
  const bool valid = config.intervalTicks >= 1 && config.cellSize >= kMinPitchControlCellSize &&
                     config.cellSize <= kMax && config.controlSeconds > 0.0 &&
                     config.controlSeconds <= kMax;
  if (!valid) {
    throw std::invalid_argument("pitch control: invalid configuration");
  }
}

// Cells along one pitch dimension: enough to cover it, at least one.
[[nodiscard]] std::size_t cellsAlong(const double meters, const double cellSize) noexcept {
  return std::max<std::size_t>(1, static_cast<std::size_t>(std::ceil(meters / cellSize)));
}

}  // namespace

PitchControlGrid computePitchControl(const MatchState& state, const PitchControlConfig& config) {
  const Pitch& pitch = state.pitch();
  const std::size_t columns = cellsAlong(pitch.lengthMeters(), config.cellSize);
  const std::size_t rows = cellsAlong(pitch.widthMeters(), config.cellSize);
  std::vector<double> home(columns * rows, std::numeric_limits<double>::infinity());
  std::vector<double> away(columns * rows, std::numeric_limits<double>::infinity());
  for (std::size_t column = 0; column < columns; ++column) {
    for (std::size_t row = 0; row < rows; ++row) {
      const SimCore::Vec2 center{.x = (static_cast<double>(column) + 0.5) * config.cellSize,
                                 .y = (static_cast<double>(row) + 0.5) * config.cellSize};
      const SimCore::Vec2 point = pitch.clamp(center);
      const std::size_t index = (column * rows) + row;
      for (const PlayerMatchState& player : state.players()) {
        // point is on the pitch, so an arrival time exists.
        const double seconds = estimateArrivalSeconds(player, point, pitch).value_or(0.0);
        double& earliest = player.side == TeamSide::kHome ? home.at(index) : away.at(index);
        earliest = std::min(earliest, seconds);
      }
    }
  }
  return PitchControlGrid({.columns = columns,
                           .rows = rows,
                           .cellSize = config.cellSize,
                           .controlSeconds = config.controlSeconds,
                           .homeArrival = std::move(home),
                           .awayArrival = std::move(away)});
}

MatchSystem makePitchControlSystem(const PitchControlConfig& config) {
  validate(config);
  return {.name = std::string(kPitchControlSystemName),
          .update =
              [config](const MatchStepContext& /*context*/, const MatchState& current,
                       MatchStateWriter& next) {
                next.setPitchControl(computePitchControl(current, config));
              },
          .intervalTicks = config.intervalTicks,
          .phaseTicks = 0};
}

}  // namespace ElyverseFootball::SimMatch
