#pragma once

#include <optional>
#include <string_view>

#include "matchSimulation.hpp"
#include "matchState.hpp"
#include "simTime.hpp"
#include "tactic.hpp"
#include "tacticalPhase.hpp"

namespace ElyverseFootball::SimMatch {

// How the match decides which tactical phase each team is in; see
// docs/match-phases.md. Part of MatchConfig and therefore of every replay.
struct PhaseConfig {
  // 10 ticks at 30 Hz: phases are re-evaluated three times a second, the
  // tactical evaluation rate of implementation plan section 6.3.
  int intervalTicks = 10;
  // How long a transition lasts after the ball changes teams.
  double transitionSeconds = 4.0;
  // How far the ball must cross a phase boundary before a team switches
  // phase, so a ball rolling along the boundary does not make it flicker.
  double hysteresisMeters = 3.0;

  friend bool operator==(const PhaseConfig&, const PhaseConfig&) = default;
};

// The team whose ball it is: the owner's side, or while the ball is free the
// side of its last touch. Empty while nobody has touched it.
[[nodiscard]] std::optional<TeamSide> teamOnTheBall(const MatchState& state) noexcept;

// possession brought up to date with the ball: unchanged while the same team
// has it; otherwise the new team, since the tick of the ball's last touch --
// when the team won it -- and fromOpponent if the other team had it before.
[[nodiscard]] TeamPossession updatePossession(const MatchState& state,
                                              const TeamPossession& possession,
                                              SimCore::SimTick now) noexcept;

// What the phase of a team depends on.
struct PhaseSituation {
  TeamSide side = TeamSide::kHome;
  const SimTactics::Tactic* tactic = nullptr;
  TeamPossession possession;
  // How far the ball is from this team's own goal line, in meters.
  double ballDepth = 0.0;
  double pitchLength = 0.0;
  // Seconds since possession.since.
  double possessionSeconds = 0.0;
  // The phase the team is in now; empty before its first evaluation.
  std::optional<SimTactics::TacticalPhase> previous;
};

// The phase a team is in:
//
//   - with the ball: attacking transition for transitionSeconds after
//     winning it from the opponent, then build-up, progression or final third
//     by the third the ball is in;
//   - without it: defensive transition for transitionSeconds after losing it
//     to the opponent, then pressing while the ball is at least the tactic's
//     pressingLine up the pitch, the defensive block otherwise;
//   - while nobody has had the ball: as without it, with no transition.
//
// A team leaves a third, or switches between pressing and block, only once
// the ball is hysteresisMeters past the boundary; a ball within that margin
// keeps the phase it had. Transitions end on time, with no margin.
[[nodiscard]] SimTactics::TacticalPhase classifyPhase(const PhaseSituation& situation,
                                                      const PhaseConfig& config) noexcept;

inline constexpr std::string_view kPhaseSystemName = "tactical phase";

// Every config.intervalTicks ticks: brings team possession up to date and
// classifies the phase of every side with a tactic, recording a
// PhaseChanged event when it changes. Writes team possession and phases
// only. Throws std::invalid_argument for an interval below one tick or a
// negative or non-finite duration or margin.
[[nodiscard]] MatchSystem makePhaseSystem(const PhaseConfig& config);

}  // namespace ElyverseFootball::SimMatch
