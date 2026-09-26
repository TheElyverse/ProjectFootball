#include "offBallActions.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <optional>
#include <stdexcept>

#include "actionGeometry.hpp"
#include "passCandidates.hpp"
#include "responsibility.hpp"
#include "tactic.hpp"
#include "tacticalPhases.hpp"
#include "teamFrame.hpp"

namespace ElyverseFootball::SimMatch {
namespace {

using SimCore::Vec2;
using SimTactics::Responsibility;

using ActionGeometry::distanceBetween;
using ActionGeometry::distanceToSegment;
using ActionGeometry::kDirections;

// Wing and halfspace centre lines as fractions of the width from the
// touchline on the slot's side (the same as the tactical target's).
constexpr double kWingCentre = 0.1;
constexpr double kHalfspaceCentre = 0.3;

// A run in behind needs the line at least this far ahead of the runner.
constexpr double kMinimumRun = 3.0;  // m

// What an off-ball decision is scored against: the settings of the decision.
struct OffBallRules {
  SimCore::SimTick now;
  double secondsPerTick = 0.0;
  const OffBallConfig* config = nullptr;
  const PositioningConfig* positioning = nullptr;
  const PerceptionConfig* perception = nullptr;
};

// Everything scoring needs about the deciding player and his view: whom he
// remembers, and where he believes the carrier is -- the ball itself if he
// does not remember him, since everyone watches the ball.
class Scorer {
 public:
  Scorer(const MatchState& state, const std::size_t playerIndex, const DesiredRegion& region,
         const OffBallRules& rules)
      : state_(&state),
        playerIndex_(playerIndex),
        region_(&region),
        now_(rules.now),
        secondsPerTick_(rules.secondsPerTick),
        config_(rules.config),
        positioning_(rules.positioning),
        perception_(rules.perception),
        remembered_(rememberedPlayers(state, playerIndex, rules.now, rules.secondsPerTick,
                                      *rules.perception, rules.positioning->minConfidence)),
        carrier_(state.ball().position),
        carrierId_(state.ball().owner) {
    for (const RememberedPlayer& other : remembered_) {
      if (other.playerId == carrierId_) {
        carrier_ = other.position;
      }
    }
  }

  [[nodiscard]] const MatchState& state() const noexcept { return *state_; }
  [[nodiscard]] const DesiredRegion& region() const noexcept { return *region_; }
  [[nodiscard]] const OffBallConfig& config() const noexcept { return *config_; }
  [[nodiscard]] const PositioningConfig& positioning() const noexcept { return *positioning_; }
  [[nodiscard]] const PerceptionConfig& perception() const noexcept { return *perception_; }
  [[nodiscard]] std::optional<SimCore::PlayerId> carrierId() const noexcept { return carrierId_; }

  [[nodiscard]] const SimTactics::Tactic& tactic() const {
    return ActionGeometry::tacticOf(state(), player().side);
  }

  [[nodiscard]] const PlayerMatchState& player() const { return state().players()[playerIndex_]; }

  [[nodiscard]] double weight(const Responsibility responsibility) const {
    const SimTactics::Tactic& tactic = this->tactic();
    return tactic.responsibilityWeight(slotIndex(state(), playerIndex_), responsibility);
  }

  [[nodiscard]] double runFrequency() const {
    const auto& phase = state().phase(player().side);
    const SimTactics::Tactic& tactic = this->tactic();
    return phase ? tactic.instruction(phase->phase).runFrequency : 0.0;
  }

  [[nodiscard]] double ownControl(const Vec2 target) const {
    const auto& grid = state().pitchControl();
    return grid ? grid->controlAt(player().side, target) : 0.5;
  }

  [[nodiscard]] double lane(const Vec2 target) const {
    return laneOpenness(carrier_, target, remembered_, config().laneRadius);
  }

  // A candidate with every weighted part of its utility.
  [[nodiscard]] ActionCandidate score(const ActionType type, const Vec2 target,
                                      const double responsibility, const double urgency,
                                      const std::optional<SimCore::PlayerId> subject) const {
    const double penalty = ActionGeometry::regionPenalty(state(), playerIndex_, target, region(),
                                                         {.now = now_,
                                                          .secondsPerTick = secondsPerTick_,
                                                          .positioning = positioning_,
                                                          .perception = perception_});
    ActionScores scores{
        .responsibility = config().responsibilityWeight * responsibility,
        .region = -config().regionWeight * penalty,
        .space = config().spaceWeight * ownControl(target),
        .lane = config().laneWeight * lane(target),
        .urgency = config().urgencyWeight * urgency,
        .effort = -config().effortWeight *
                  (distanceBetween(player().position, target) / config().effortScale)};
    return {.type = type,
            .target = target,
            .subject = subject,
            .scores = scores,
            .utility = scores.total()};
  }

  // The support position with the most open lane from the carrier, counting
  // how far it takes the player from his region: on a ring around the
  // carrier at supportDistance, or a step or two from his region's centre.
  // A small shift that opens the lane beats a long run around the carrier.
  [[nodiscard]] Vec2 supportPosition() const {
    const Pitch& pitch = state().pitch();
    Vec2 best = region().center;
    double bestScore = -std::numeric_limits<double>::infinity();
    const auto consider = [&](const Vec2 point) {
      const Vec2 candidate = pitch.clamp(point);
      const double detour =
          distanceBetween(candidate, region().center) / (2.0 * config().supportDistance);
      if (const double score = lane(candidate) - detour; score > bestScore) {
        best = candidate;
        bestScore = score;
      }
    };
    for (const Vec2 direction : kDirections) {
      consider(carrier_ + (direction * config().supportDistance));
    }
    for (const double radius :
         {config().supportDistance / 3.0, 2.0 * config().supportDistance / 3.0}) {
      for (const Vec2 direction : kDirections) {
        consider(region().center + (direction * radius));
      }
    }
    return best;
  }

  // How closely the carrier is pressed, in [0, 1]: 1 with an opponent at his
  // feet, 0 with none within the pressure radius, as the player remembers it.
  [[nodiscard]] double carrierPressure() const {
    double pressure = 0.0;
    for (const RememberedPlayer& other : remembered_) {
      if (!other.teammate) {
        const double distance = distanceBetween(other.position, carrier_);
        pressure = std::max(pressure, 1.0 - (distance / config().pressedRadius));
      }
    }
    return pressure;
  }

  // The nearby point his team controls most, looking forward first.
  [[nodiscard]] Vec2 spacePosition() const {
    const Pitch& pitch = state().pitch();
    const Vec2 from = player().position;
    Vec2 best = from;
    double bestControl = -1.0;
    for (const double radius : {config().spaceSearchRadius / 2.0, config().spaceSearchRadius}) {
      for (const Vec2 direction : kDirections) {
        const Vec2 forward{.x = direction.x * attackingDirection(player().side), .y = direction.y};
        const Vec2 candidate = pitch.clamp(from + (forward * radius));
        if (const double control = ownControl(candidate); control > bestControl) {
          best = candidate;
          bestControl = control;
        }
      }
    }
    return best;
  }

  // The depth of the opponent's defensive line as the player remembers it:
  // the second deepest opponent, the deepest being the goalkeeper. Empty if
  // he remembers fewer than two opponents.
  [[nodiscard]] std::optional<double> believedLine() const {
    std::vector<double> depths;
    for (const RememberedPlayer& other : remembered_) {
      if (!other.teammate) {
        depths.push_back(depthOf(player().side, other.position, state().pitch()));
      }
    }
    if (depths.size() < 2) {
      return std::nullopt;
    }
    std::ranges::sort(depths, std::greater<>());
    return depths.at(1);
  }

  // The y of a lane centre on the slot's side of the pitch.
  [[nodiscard]] double laneY(const double fromTouchline) const {
    const SimTactics::Tactic& tactic = this->tactic();
    const double width = state().pitch().widthMeters();
    const bool lowSide = tactic.slots()[slotIndex(state(), playerIndex_)].position.width <= 0.5;
    return lowSide ? fromTouchline * width : (1.0 - fromTouchline) * width;
  }

 private:
  const MatchState* state_;
  std::size_t playerIndex_;
  const DesiredRegion* region_;
  SimCore::SimTick now_;
  double secondsPerTick_;
  const OffBallConfig* config_;
  const PositioningConfig* positioning_;
  const PerceptionConfig* perception_;
  std::vector<RememberedPlayer> remembered_;
  Vec2 carrier_;
  std::optional<SimCore::PlayerId> carrierId_;
};

}  // namespace

void validate(const OffBallConfig& config) {
  constexpr double kMax = std::numeric_limits<double>::max();
  const auto positive = [](const double value) { return value > 0.0 && value <= kMax; };
  const auto weight = [](const double value) { return value >= 0.0 && value <= kMax; };
  const bool valid = positive(config.nearBallRadius) && config.nearIntervalTicks >= 1 &&
                     config.farIntervalTicks >= 1 && positive(config.supportDistance) &&
                     positive(config.spaceSearchRadius) && positive(config.runDepth) &&
                     positive(config.laneRadius) && positive(config.pressedRadius) &&
                     positive(config.effortScale) && config.holdResponsibility >= 0.0 &&
                     config.holdResponsibility <= 1.0 && positive(config.temperature) &&
                     weight(config.responsibilityWeight) && weight(config.regionWeight) &&
                     weight(config.spaceWeight) && weight(config.laneWeight) &&
                     weight(config.urgencyWeight) && weight(config.effortWeight);
  if (!valid) {
    throw std::invalid_argument("off-ball decisions: invalid configuration");
  }
}

double laneOpenness(const Vec2 from, const Vec2 until,
                    const std::vector<RememberedPlayer>& remembered,
                    const double laneRadius) noexcept {
  double nearest = std::numeric_limits<double>::infinity();
  for (const RememberedPlayer& other : remembered) {
    if (!other.teammate) {
      nearest = std::min(nearest, distanceToSegment(other.position, from, until));
    }
  }
  return std::min(1.0, nearest / laneRadius);
}

std::vector<ActionCandidate> generateOffBallCandidates(
    const MatchState& state, const std::size_t playerIndex, const DesiredRegion& region,
    const SimCore::SimTick now, const double secondsPerTick, const OffBallConfig& config,
    const PositioningConfig& positioning, const PerceptionConfig& perception) {
  const Scorer scorer(state, playerIndex, region,
                      {.now = now,
                       .secondsPerTick = secondsPerTick,
                       .config = &config,
                       .positioning = &positioning,
                       .perception = &perception});

  const PlayerMatchState& player = state.players()[playerIndex];
  const Pitch& pitch = state.pitch();
  const double runs = scorer.runFrequency();
  std::vector<ActionCandidate> candidates;
  candidates.push_back(scorer.score(ActionType::kHoldPosition, region.center,
                                    config.holdResponsibility, 0.0, std::nullopt));
  candidates.push_back(scorer.score(ActionType::kSupportCarrier, scorer.supportPosition(),
                                    scorer.weight(Responsibility::kSupportCarrier),
                                    scorer.carrierPressure(), scorer.carrierId()));
  candidates.push_back(
      scorer.score(ActionType::kMoveIntoSpace, scorer.spacePosition(), 0.0, runs, std::nullopt));
  if (const auto line = scorer.believedLine()) {
    const double depth = std::min(*line + config.runDepth, pitch.lengthMeters() - 1.0);
    if (depth - depthOf(player.side, player.position, pitch) >= kMinimumRun) {
      const Vec2 target =
          pitch.clamp({.x = xAtDepth(player.side, depth, pitch), .y = player.position.y});
      candidates.push_back(scorer.score(ActionType::kRunInBehind, target,
                                        scorer.weight(Responsibility::kRunInBehind), runs,
                                        std::nullopt));
    }
  }
  candidates.push_back(
      scorer.score(ActionType::kCreateWidth,
                   pitch.clamp({.x = player.position.x, .y = scorer.laneY(kWingCentre)}),
                   scorer.weight(Responsibility::kProvideWidth), 0.0, std::nullopt));
  candidates.push_back(
      scorer.score(ActionType::kOccupyHalfspace,
                   pitch.clamp({.x = player.position.x, .y = scorer.laneY(kHalfspaceCentre)}),
                   scorer.weight(Responsibility::kOccupyHalfspace), 0.0, std::nullopt));
  return candidates;
}

bool isActionDecisionDue(const MatchState& state, const std::size_t playerIndex,
                         const SimCore::SimTick now, const OffBallConfig& config) {
  const auto& action = state.tactical(playerIndex).action;
  const PlayerMatchState& player = state.players()[playerIndex];
  // Not possession().team: it only updates when the phase system next
  // runs, so it can still name the losing side right after a turnover the
  // ball itself already shows.
  const bool withBall = teamOnTheBall(state) == player.side;
  if (!action || action->withBall != withBall) {
    return true;
  }
  const bool nearBall =
      distanceBetween(player.position, state.ball().position) <= config.nearBallRadius;
  const int interval = nearBall ? config.nearIntervalTicks : config.farIntervalTicks;
  return now.value() - action->decidedAt.value() >= interval;
}

}  // namespace ElyverseFootball::SimMatch
