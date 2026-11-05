#pragma once

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

#include "ballPhysics.hpp"
#include "matchSimulation.hpp"
#include "random.hpp"
#include "vec2.hpp"

namespace ElyverseFootball::SimMatch {

// How a presser and the carrier fight for the ball at the carrier's feet; see
// docs/duels.md#duels-on-the-ball. Part of MatchConfig and therefore of every
// replay.
struct DuelConfig {
  // 3 ticks at 30 Hz: a duel takes a step ten times a second.
  int intervalTicks = 3;
  // A carrier who has had the ball for less than this is not taken on: he is
  // still settling it, and a winner is not robbed straight back.
  double protectSeconds = 0.5;  // s
  // A presser this close to the carrier takes him on; they part once this
  // far apart.
  double engageRadius = 1.5;  // m
  double breakRadius = 3.0;   // m
  // A jockeying defender stays this far goal-side of the carrier.
  double jockeyDistance = 1.0;  // m
  // How far ahead of himself a carrier runs each step he turns away with the
  // ball or accelerates past; shielding it, he stands.
  double turnStep = 2.0;        // m
  double accelerateStep = 4.0;  // m
  // How far a lunge reaches from the defender, how close to the ball his foot
  // must land to meet it, and how wide the carrier's body is.
  double tackleReach = 1.8;  // m
  double footRadius = 0.3;   // m
  double bodyRadius = 0.35;  // m
  // How far from his aim a defender square on his feet lands his foot at
  // most: the half-width of a triangular spread, widened by 2 - balance.
  double footError = 0.3;  // m
  // What a met ball takes to stay with the carrier: his balance times a
  // share from minHold at a ballControl of 0 to maxHold at 1. A foot that
  // meets it better wins it cleanly, one that meets it better than half of
  // that knocks it loose, a worse one he keeps it from.
  double minHold = 0.3;
  double maxHold = 0.9;
  // How fast a ball knocked loose leaves the foot.
  double looseSpeed = 5.0;  // m/s
  // Balance: bodies this close push each other, each losing up to
  // shovePerSecond -- half of it against an equally strong opponent, all of
  // it against one stronger by half the scale or more --; a lunge costs
  // commitCost, rolling the ball round to the other side of him turnCost --
  // less for a smaller turn --; everyone regains recoveryPerSecond.
  double contactDistance = 1.2;  // m
  double shovePerSecond = 1.0;
  double commitCost = 0.3;
  double turnCost = 0.2;
  double recoveryPerSecond = 0.5;
  // A player who stumbles -- the defender whose lunge misses or fouls, the
  // carrier pushed off the ball, turned off his feet or whose ball is knocked
  // loose -- is off his feet this long.
  double stumbleSeconds = 0.5;  // s
  // The moves' utilities and the temperature they are chosen at.
  double shieldWeight = 0.8;
  double turnWeight = 0.4;
  double accelerateWeight = 0.6;
  double jockeyWeight = 0.3;
  double commitWeight = 1.0;
  double decisionTemperature = 0.2;

  friend bool operator==(const DuelConfig&, const DuelConfig&) = default;
};

// Throws std::invalid_argument for an interval below one tick, a value that
// is negative or not finite, a radius, reach, step, distance or temperature
// that is not positive, an engage radius beyond the break radius, a contact
// distance not below it, or hold shares outside [0, 1] or out of order.
void validate(const DuelConfig& config);

// Where the duel's players and the ball stand at the start of a step, and the
// centre of the goal the carrier attacks.
struct DuelGeometry {
  SimCore::Vec2 carrier;
  SimCore::Vec2 defender;
  SimCore::Vec2 ball;
  SimCore::Vec2 goal;
};

// How clear of the carrier's body the defender's way to the ball is: 0 where
// it passes through it, 1 where it passes a body's width or more beside it.
[[nodiscard]] double wayClearance(const DuelGeometry& geometry, const DuelConfig& config) noexcept;

// How much the defender threatens the ball: how close he is, from 1 at
// contactDistance to 0 at breakRadius, how squarely he stands between the
// carrier and the goal, and how well balanced he is.
[[nodiscard]] double threat(const DuelGeometry& geometry, double defenderBalance,
                            const DuelConfig& config) noexcept;

// The carrier's moves -- shield, turn away, accelerate -- and their
// utilities, with both players as balanced as the duel says: shielding as
// much as the defender threatens, turning away as much as the carrier is
// balanced, accelerating as little as the defender threatens.
[[nodiscard]] std::array<double, 3> carrierUtilities(const DuelGeometry& geometry, const Duel& duel,
                                                     const DuelConfig& config) noexcept;

// The defender's moves -- jockey and, with the ball within tackleReach and
// his way to it clear of the carrier's body at all, commit -- and their
// utilities: jockeying always the same, committing as much as his way to the
// ball is clear and he is balanced. Nobody lunges through a man on purpose.
[[nodiscard]] std::vector<double> defenderUtilities(const DuelGeometry& geometry,
                                                    double defenderBalance,
                                                    const DuelConfig& config);

// The way a carrier's move plays the ball round him, as a unit vector:
// shielding straight away from the defender, turning away square to that and
// toward the goal, accelerating toward the goal and past the side of a
// defender in the way. The defender's moves play no ball: none.
[[nodiscard]] SimCore::Vec2 moveDirection(DuelMove move, const DuelGeometry& geometry) noexcept;

// Where a move takes its player, before it is kept on the pitch: a carrier
// turnStep or accelerateStep along moveDirection(), shielding where he
// stands; the defender jockeyDistance goal-side of the carrier, or lunging
// at the ball.
[[nodiscard]] SimCore::Vec2 moveTarget(DuelMove move, const DuelGeometry& geometry,
                                       const DuelConfig& config) noexcept;

// Where the foot of a lunge from `lunger` at the ball lands
// (docs/duels.md#the-lunge): off it by a triangular draw in each direction
// of half-width footError · (2 - balance), no farther than tackleReach from
// him. Always two draws from random. A tackle and a block land the same way.
[[nodiscard]] SimCore::Vec2 landLunge(SimCore::Vec2 lunger, SimCore::Vec2 ball, double balance,
                                      const DuelConfig& config,
                                      SimCore::RandomNumberGenerator& random) noexcept;

// The execution half of a lunge, from the geometry alone: the defender's foot
// goes straight from him to `foot`. Passing through the carrier's body before
// it meets the ball, it is a foul. Landing within footRadius of the ball, it
// meets it, the better the nearer -- 1 at the ball's centre, 0 at
// footRadius: better than the carrier's hold, it wins the ball cleanly;
// better than half of it, it knocks it loose; worse, the carrier keeps it,
// and it misses like a foot that does not meet the ball at all.
[[nodiscard]] LungeResult resolveLunge(const DuelGeometry& geometry, SimCore::Vec2 foot,
                                       double carrierBalance, double ballControl,
                                       const DuelConfig& config) noexcept;

inline constexpr std::string_view kDuelSystemName = "duel";

// What the duel system is configured with: the default configuration for
// whatever a caller leaves out.
struct DuelRules {
  DuelConfig duels{};
  BallPhysics physics{};
};

// Every config.intervalTicks ticks, takes the duel on the ball a step
// (docs/duels.md#duels-on-the-ball):
//
//   1. A duel ends, kept, once the carrier no longer has the ball at his
//      feet, the defender no longer presses him (ActionType::kPressCarrier)
//      or they stand more than breakRadius apart.
//   2. Both regain balance; bodies within contactDistance push each other off
//      it, by their strength -- a defender off his feet nobody. A carrier
//      pushed off his feet stumbles and loses the ball to the defender.
//   3. The defender, on his feet, decides by chooseByUtility() -- one draw
//      from the kAi stream. A lunge lands at once, on the ball where the
//      carrier's last move left it, off by a triangular error from the
//      kExecution stream -- two draws --, at most tackleReach from him, and
//      resolveLunge() says what it did. Won, the defender has the ball;
//      loose, it leaves the foot at looseSpeed, away from where the foot met
//      it, and the carrier stumbles; a foul, or a miss, and the defender
//      stumbles. Won, loose and foul end the duel.
//   4. The carrier decides -- one draw from kAi; none while he is about to
//      play the ball -- rolls the ball round him along moveDirection(),
//      carryDistance from him, and runs to the move's target; a turn that
//      takes the last of his balance leaves the ball loose there, he
//      stumbles, and the duel ends. The defender jockeys, or stands while he
//      is off his feet.
//
// Without a duel, the nearest player of the other side whose decided action
// is to press the carrier, within engageRadius and on his feet, takes on a
// carrier who has had the ball at his feet for protectSeconds -- not a
// goalkeeper holding it in his hands. In that first step he sizes the
// carrier up: he jockeys, and only the carrier decides. At most one duel is
// fought at a time. Records DuelStarted and DuelEnded, and for a won ball
// BallWon and PossessionChanged, for a loose one PossessionChanged. Writes the
// ball's position, owner, velocity and last touch, the carrier's pending
// action when he loses the ball, the players' targets and their duels and
// stumbles, nothing else. Throws std::invalid_argument for an invalid
// configuration.
[[nodiscard]] MatchSystem makeDuelSystem(const DuelRules& rules = {});

}  // namespace ElyverseFootball::SimMatch
