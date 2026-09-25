#pragma once

#include <optional>
#include <string_view>
#include <vector>

#include "matchSimulation.hpp"
#include "matchState.hpp"
#include "perception.hpp"
#include "responsibility.hpp"
#include "simTime.hpp"
#include "teamPress.hpp"

namespace ElyverseFootball::SimMatch {

// When and how a side with a tactic starts a coordinated press; see
// docs/pressing.md. Part of MatchConfig and therefore of every replay.
struct PressingConfig {
  // 6 ticks at 30 Hz: pressing is reconsidered five times a second.
  int intervalTicks = 6;
  // A defender this close to the carrier can spot a trigger.
  double triggerRadius = 20.0;  // m
  // A reception or pass this recent still counts as "just happened".
  double recentSeconds = 1.5;
  // A ball arriving at least this fast is hard to control: a poor first
  // touch.
  double heavyTouchSpeed = 11.0;  // m/s
  // A pass rolling slower than this can be pressed before it arrives.
  double slowPassSpeed = 6.0;  // m/s
  // A carrier without a teammate this close is isolated.
  double isolationRadius = 10.0;  // m
  // A carrier whose facing points this much toward his own goal -- the cosine
  // of the angle to his attacking direction below this -- faces it.
  double facingOwnGoal = -0.3;
  // A pass that ends at least this much deeper than it started went back.
  double backPassMeters = 2.0;  // m
  // The most players a press can involve, at pressing intensity 1.
  int maxJoiners = 4;
  // In the pressing phase a side presses the carrier without a trigger if its
  // pressing intensity reaches this.
  double phaseIntensity = 0.5;
  // A press that has neither won the ball nor lost it after this long has
  // been escaped.
  double maxPressSeconds = 4.0;

  friend bool operator==(const PressingConfig&, const PressingConfig&) = default;
};

// Throws std::invalid_argument for an interval below one tick, a distance,
// speed or duration that is not positive and finite, a facing threshold
// outside [-1, 1], fewer than one joiner or a phase intensity outside [0, 1].
void validate(const PressingConfig& config);

// A trigger a side has spotted, and the carrier it is about.
struct SpottedTrigger {
  SimTactics::PressingTrigger trigger = SimTactics::PressingTrigger::kPoorFirstTouch;
  SimCore::PlayerId carrier;

  friend bool operator==(const SpottedTrigger&, const SpottedTrigger&) = default;
};

// The first trigger of the side's tactic, in the tactic's order, that one of
// its outfield players within triggerRadius of the ball spots in what he
// observes -- he must have seen the carrier (or, for a slow pass, the ball)
// within the last perception interval (docs/pressing.md). Empty if none.
[[nodiscard]] std::optional<SpottedTrigger> spotTrigger(const MatchState& state,
                                                        TeamSide pressingSide, SimCore::SimTick now,
                                                        double secondsPerTick,
                                                        const PressingConfig& config,
                                                        const PerceptionConfig& perception);

// How many players join a press at a pressing intensity in [0, 1]: the
// intensity times maxJoiners, rounded to the nearest whole player.
[[nodiscard]] int pressJoiners(double intensity, const PressingConfig& config) noexcept;

// Who plays which role in a press on the carrier with this many joiners, by
// the players' positions: the outfield player nearest the carrier presses;
// with three or more joiners the one nearest the spot behind the presser
// covers; the rest block the lanes to the carrier's nearest options, each
// option taken by the free player who stands nearest his blocking spot.
// Fewer roles if the side has fewer outfield players or the carrier fewer
// options. Ties go to the lower id.
struct PressRequest {
  SimCore::PlayerId carrier;
  int joiners = 0;
  // How far behind the presser the cover stands.
  double coverDistance = 6.0;  // m
};
[[nodiscard]] std::vector<PressAssignment> assignPressRoles(const MatchState& state,
                                                            TeamSide pressingSide,
                                                            const PressRequest& request);

inline constexpr std::string_view kPressingSystemName = "team pressing";

// Every config.intervalTicks ticks, for every side with a tactic and a phase:
//
//   - A press in progress ends when the side has the ball (ball regained),
//     another player of the carrier's side has it (passed out), or after
//     maxPressSeconds (escaped), with a PressingEnded event.
//   - Without a press and without the ball, the side presses when its phase's
//     pressing intensity lets at least one player join and it spots one of
//     its tactic's triggers -- or, in the pressing phase at phaseIntensity or
//     more, whoever has the ball. The roles come from assignPressRoles(),
//     and a PressingStarted event records them.
//
// The tactical movement gives the players in a press their role's action.
// Writes presses only. Throws std::invalid_argument for an invalid
// configuration.
[[nodiscard]] MatchSystem makePressingSystem(const PressingConfig& config,
                                             const PerceptionConfig& perception,
                                             double coverDistance);

}  // namespace ElyverseFootball::SimMatch
