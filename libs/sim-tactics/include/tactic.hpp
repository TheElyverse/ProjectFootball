#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "responsibility.hpp"
#include "tacticalPhase.hpp"

namespace ElyverseFootball::SimTactics {

// A tactic is written for seven-a-side, the P2 sandbox format. Eleven-a-side
// arrives with the P3 baseline.
inline constexpr std::size_t kSlotsPerTactic = 7;

// Where a slot stands in the base shape, as fractions of the pitch so one
// shape fits any pitch size. depth runs from the team's own goal line (0) to
// the opponent's (1), so a shape reads the same for both teams whichever way
// they attack. width runs from the touchline at y = 0 (0) to the one at
// y = width (1) in pitch coordinates, for both teams alike.
struct ShapePosition {
  double depth = 0.0;
  double width = 0.5;

  friend bool operator==(const ShapePosition&, const ShapePosition&) = default;
};

// One position of the tactic: where it stands in the base shape and what the
// player there is responsible for. The player in slot i is the i-th player of
// his side in the match.
struct TacticSlot {
  ShapePosition position;
  std::vector<SlotResponsibility> responsibilities;

  friend bool operator==(const TacticSlot&, const TacticSlot&) = default;
};

// How a team plays in one phase. Heights and lengths are fractions of the
// pitch length, widths fractions of the pitch width; the rest are
// dimensionless dials in [0, 1].
struct PhaseInstruction {
  // How far the defensive line stands from the own goal line.
  double lineHeight = 0.3;
  // How much of the pitch length the outfield block spans from its defensive
  // line to its front line: the vertical compactness.
  double blockLength = 0.45;
  // How much of the pitch width the outfield block spans: the horizontal
  // compactness.
  double blockWidth = 0.7;
  // How far the block follows the ball, from 0 (holds its place) to 1
  // (centres on the ball).
  double ballShift = 0.5;
  // How eagerly and with how many players the team presses.
  double pressingIntensity = 0.3;
  // How much risk the player on the ball accepts for a forward pass.
  double passingRisk = 0.5;
  // How often players without the ball run into space instead of holding
  // their position.
  double runFrequency = 0.3;

  friend bool operator==(const PhaseInstruction&, const PhaseInstruction&) = default;
};

// The largest weight a positioning cost component may get. Weights compare
// components with each other; beyond this, one component would drown out
// every other and the score would say nothing.
inline constexpr double kMaxPositioningWeight = 10.0;

// How a player weighs the parts of a candidate position's cost (the desired
// region, implementation plan section 7.2). Each weight lies in
// [0, kMaxPositioningWeight].
struct PositioningWeights {
  // Distance from the tactical target the shape gives him.
  double targetDistance = 1.0;
  // Standing too close to a teammate.
  double spacing = 1.0;
  // Opponents close to the position.
  double pressure = 0.5;
  // Space the opponent controls.
  double occupancy = 0.5;
  // Leaving the team open to a counterattack if the ball is lost.
  double transitionRisk = 0.5;

  friend bool operator==(const PositioningWeights&, const PositioningWeights&) = default;
};

// What holds in every phase.
struct TeamPrinciples {
  // How far up the pitch the ball must be, measured from the own goal line as
  // a fraction of the pitch length, before a team without it is in the
  // pressing phase rather than the defensive block.
  double pressingLine = 0.6;
  // The situations that start a press; each at most once.
  std::vector<PressingTrigger> pressingTriggers;
  PositioningWeights positioning;

  friend bool operator==(const TeamPrinciples&, const TeamPrinciples&) = default;
};

// Unvalidated input for Tactic::create(), in the shape tactic files have.
struct TacticSpec {
  std::string name;
  // What the tactic is for and which parameters express it. Free text for
  // people; nothing in the match reads it.
  std::string description;
  std::vector<TacticSlot> slots;
  TeamPrinciples principles;
  // Indexed by phaseIndex().
  std::array<PhaseInstruction, kPhaseCount> phases{};

  friend bool operator==(const TacticSpec&, const TacticSpec&) = default;
};

enum class TacticErrorCode : std::uint8_t {
  kEmptyName,
  kWrongSlotCount,
  kSlotOutsidePitch,
  kValueOutOfRange,
  kUnknownResponsibility,
  kDuplicateResponsibility,
  kInvalidResponsibilityWeight,
  kContradictoryResponsibilities,
  kTooManyGoalkeepers,
  kUnknownPressingTrigger,
  kDuplicatePressingTrigger,
  kContradictoryParameters,
};

// field names the offending value by its path, as a tactic file spells it --
// "slots[3].position.depth", "phases.pressing.lineHeight" -- so a loader can
// point at the line to fix. message says what is wrong with it.
struct TacticError {
  TacticErrorCode code = TacticErrorCode::kValueOutOfRange;
  std::string field;
  std::string message;

  friend bool operator==(const TacticError&, const TacticError&) = default;
};

// A validated tactic: a base shape of kSlotsPerTactic slots, team
// principles, an instruction for every phase and atomic responsibilities per
// slot (implementation plan section 7.1, docs/tactics.md). Immutable; a
// team changes tactics by getting a different one.
class Tactic {
 public:
  // The only way to obtain a Tactic, so every instance is valid. Reports every
  // rule the spec breaks, in a fixed order: name, slots by index, principles,
  // phases in kAllPhases order.
  [[nodiscard]] static std::expected<Tactic, std::vector<TacticError>> create(TacticSpec spec);

  [[nodiscard]] const std::string& name() const noexcept { return spec_.name; }
  [[nodiscard]] const std::string& description() const noexcept { return spec_.description; }
  [[nodiscard]] std::span<const TacticSlot> slots() const noexcept { return spec_.slots; }
  [[nodiscard]] const TeamPrinciples& principles() const noexcept { return spec_.principles; }
  // Throws std::out_of_range for a value outside the enumerators.
  [[nodiscard]] const PhaseInstruction& instruction(const TacticalPhase phase) const {
    return spec_.phases.at(phaseIndex(phase));
  }

  // The weight of a responsibility in a slot, 0 if the slot does not hold it.
  // Throws std::out_of_range for a slot past the end.
  [[nodiscard]] double responsibilityWeight(std::size_t slot, Responsibility responsibility) const;

  // Whether the tactic reacts to this pressing trigger.
  [[nodiscard]] bool pressesOn(PressingTrigger trigger) const noexcept;

  // The spec this tactic was created from, to write it back to a file.
  [[nodiscard]] const TacticSpec& spec() const noexcept { return spec_; }

  friend bool operator==(const Tactic&, const Tactic&) = default;

 private:
  explicit Tactic(TacticSpec spec) : spec_(std::move(spec)) {}

  TacticSpec spec_;
};

}  // namespace ElyverseFootball::SimTactics
