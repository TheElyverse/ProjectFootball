#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "pitch.hpp"
#include "vec2.hpp"

namespace ElyverseFootball::SimMatch {

enum class TeamSide : std::uint8_t;

// A cell of a PitchControlGrid by column (along the length) and row (across
// the width).
struct GridCell {
  std::size_t column = 0;
  std::size_t row = 0;

  friend bool operator==(const GridCell&, const GridCell&) = default;
};

// Unvalidated input for a PitchControlGrid: columns x rows cells of cellSize
// meters, and each side's arrival time per cell, column by column, each
// column row by row.
struct PitchControlGridSpec {
  std::size_t columns = 0;
  std::size_t rows = 0;
  double cellSize = 0.0;
  double controlSeconds = 0.0;
  std::vector<double> homeArrival;
  std::vector<double> awayArrival;
};

// Which team can reach which part of the pitch first, and how clearly
// (docs/pitch-control.md): a grid of square cells over the pitch, each with
// both teams' earliest arrival time at its centre. The control of a cell
// follows from the two arrival times; see control().
//
// Built by computePitchControl() (pitchControl.hpp) and cached in the match
// state between refreshes. A value type: two grids are equal when every
// number is.
class PitchControlGrid {
 public:
  // Throws std::invalid_argument unless the grid has a cell, both arrival
  // vectors hold columns * rows values, and the cell size and controlSeconds
  // are positive and finite.
  explicit PitchControlGrid(PitchControlGridSpec spec);

  [[nodiscard]] std::size_t columns() const noexcept { return columns_; }
  [[nodiscard]] std::size_t rows() const noexcept { return rows_; }
  [[nodiscard]] double cellSize() const noexcept { return cellSize_; }
  [[nodiscard]] double controlSeconds() const noexcept { return controlSeconds_; }

  // The centre of a cell. Cells are laid out from the corner (0, 0); the last
  // column and row may extend past the pitch when its size is not a whole
  // number of cells.
  [[nodiscard]] SimCore::Vec2 cellCenter(GridCell cell) const noexcept;

  // The cell a position lies in; positions off the grid map to the nearest
  // cell on its edge. Empty for a non-finite position.
  [[nodiscard]] std::optional<GridCell> cellAt(SimCore::Vec2 position) const noexcept;

  // Seconds the side's fastest player needs to reach the cell's centre.
  // Throws std::out_of_range for a cell outside the grid.
  [[nodiscard]] double arrivalSeconds(TeamSide side, GridCell cell) const;

  // How much of the cell the side controls, in [0, 1]: a logistic of the
  // arrival advantage, 1 / (1 + e^((own - other) / controlSeconds)). 1/2
  // when both arrive together; a side that is controlSeconds earlier holds
  // about 0.73. Computed with stableExp(), so the same on every platform;
  // one side's control mirrors the other's for mirrored arrival times.
  // Throws std::out_of_range for a cell outside the grid.
  [[nodiscard]] double control(TeamSide side, GridCell cell) const;

  // control() of the cell a position lies in; 0.5 for a non-finite position,
  // which no one controls.
  [[nodiscard]] double controlAt(TeamSide side, SimCore::Vec2 position) const;

  // The mean control of the cells whose centres lie in the rectangle; empty
  // if none does.
  [[nodiscard]] std::optional<double> regionControl(TeamSide side, const PitchRect& region) const;

  // The side's mean control over the whole grid: its share of the pitch.
  [[nodiscard]] double share(TeamSide side) const;

  friend bool operator==(const PitchControlGrid&, const PitchControlGrid&) = default;

  // Moves out this grid's two arrival-time vectors, leaving it otherwise
  // unused; computePitchControl() calls this to recycle a previous refresh's
  // storage for one of the same size instead of allocating a fresh one.
  [[nodiscard]] std::pair<std::vector<double>, std::vector<double>>
  extractArrivalStorage() && noexcept {
    return {std::move(homeArrival_), std::move(awayArrival_)};
  }

 private:
  [[nodiscard]] std::size_t indexOf(GridCell cell) const;

  std::size_t columns_;
  std::size_t rows_;
  double cellSize_;
  double controlSeconds_;
  std::vector<double> homeArrival_;
  std::vector<double> awayArrival_;
};

}  // namespace ElyverseFootball::SimMatch
