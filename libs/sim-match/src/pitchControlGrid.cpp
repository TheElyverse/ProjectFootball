#include "pitchControlGrid.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

#include "matchState.hpp"
#include "stableMath.hpp"

namespace ElyverseFootball::SimMatch {
namespace {

[[nodiscard]] bool isPositiveFinite(const double value) noexcept {
  return value > 0.0 && value <= std::numeric_limits<double>::max();
}

// A cell index along one axis, from a coordinate in cells, clamped to
// [0, last].
[[nodiscard]] std::size_t clampedCell(const double cells, const std::size_t last) noexcept {
  const double cell = std::floor(cells);
  if (!(cell > 0.0)) {
    return 0;
  }
  return static_cast<std::size_t>(std::min(cell, static_cast<double>(last)));
}

}  // namespace

PitchControlGrid::PitchControlGrid(PitchControlGridSpec spec)
    : columns_(spec.columns),
      rows_(spec.rows),
      cellSize_(spec.cellSize),
      controlSeconds_(spec.controlSeconds),
      homeArrival_(std::move(spec.homeArrival)),
      awayArrival_(std::move(spec.awayArrival)) {
  const std::size_t cells = columns_ * rows_;
  if (cells == 0 || homeArrival_.size() != cells || awayArrival_.size() != cells ||
      !isPositiveFinite(cellSize_) || !isPositiveFinite(controlSeconds_)) {
    throw std::invalid_argument("PitchControlGrid: invalid dimensions or arrival times");
  }
}

SimCore::Vec2 PitchControlGrid::cellCenter(const GridCell cell) const noexcept {
  return {.x = (static_cast<double>(cell.column) + 0.5) * cellSize_,
          .y = (static_cast<double>(cell.row) + 0.5) * cellSize_};
}

std::optional<GridCell> PitchControlGrid::cellAt(const SimCore::Vec2 position) const noexcept {
  if (!position.isFinite()) {
    return std::nullopt;
  }
  return GridCell{.column = clampedCell(position.x / cellSize_, columns_ - 1),
                  .row = clampedCell(position.y / cellSize_, rows_ - 1)};
}

std::size_t PitchControlGrid::indexOf(const GridCell cell) const {
  if (cell.column >= columns_ || cell.row >= rows_) {
    throw std::out_of_range("PitchControlGrid: cell (" + std::to_string(cell.column) + ", " +
                            std::to_string(cell.row) + ") is outside the grid");
  }
  return (cell.column * rows_) + cell.row;
}

double PitchControlGrid::arrivalSeconds(const TeamSide side, const GridCell cell) const {
  const std::size_t index = indexOf(cell);
  return side == TeamSide::kHome ? homeArrival_.at(index) : awayArrival_.at(index);
}

double PitchControlGrid::control(const TeamSide side, const GridCell cell) const {
  const std::size_t index = indexOf(cell);
  const double own = side == TeamSide::kHome ? homeArrival_.at(index) : awayArrival_.at(index);
  const double other = side == TeamSide::kHome ? awayArrival_.at(index) : homeArrival_.at(index);
  return 1.0 / (1.0 + SimCore::stableExp((own - other) / controlSeconds_));
}

double PitchControlGrid::controlAt(const TeamSide side, const SimCore::Vec2 position) const {
  const auto cell = cellAt(position);
  return cell ? control(side, *cell) : 0.5;
}

std::optional<double> PitchControlGrid::regionControl(const TeamSide side,
                                                      const PitchRect& region) const {
  double sum = 0.0;
  std::size_t count = 0;
  for (std::size_t column = 0; column < columns_; ++column) {
    for (std::size_t row = 0; row < rows_; ++row) {
      const GridCell cell{.column = column, .row = row};
      if (region.contains(cellCenter(cell))) {
        sum += control(side, cell);
        ++count;
      }
    }
  }
  if (count == 0) {
    return std::nullopt;
  }
  return sum / static_cast<double>(count);
}

double PitchControlGrid::share(const TeamSide side) const {
  double sum = 0.0;
  for (std::size_t column = 0; column < columns_; ++column) {
    for (std::size_t row = 0; row < rows_; ++row) {
      sum += control(side, {.column = column, .row = row});
    }
  }
  return sum / static_cast<double>(columns_ * rows_);
}

}  // namespace ElyverseFootball::SimMatch
