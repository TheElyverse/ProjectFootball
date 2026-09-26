#include "matchAnalyzer.hpp"

#include <cmath>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <utility>
#include <variant>

namespace ElyverseFootball::SimAnalytics {
namespace {

using SimMatch::TeamSide;

[[nodiscard]] std::size_t indexOf(const TeamSide side) noexcept {
  return side == TeamSide::kHome ? 0U : 1U;
}

[[nodiscard]] TeamSide otherSide(const TeamSide side) noexcept {
  return side == TeamSide::kHome ? TeamSide::kAway : TeamSide::kHome;
}

[[nodiscard]] std::optional<double> ratio(const double numerator, const double denominator) {
  if (denominator == 0.0) {
    return std::nullopt;
  }
  return numerator / denominator;
}

// Where the event says a free ball was contested, if it does.
[[nodiscard]] std::optional<SimCore::Vec2> wonAtOf(const SimMatch::MatchEvent& event) {
  if (const auto* intercepted = std::get_if<SimMatch::PassIntercepted>(&event)) {
    return intercepted->position;
  }
  if (const auto* won = std::get_if<SimMatch::BallWon>(&event)) {
    return won->position;
  }
  if (const auto* recovered = std::get_if<SimMatch::LooseBallRecovered>(&event)) {
    return recovered->position;
  }
  return std::nullopt;
}

}  // namespace

MatchContext contextOf(const SimMatch::MatchState& initialState, const int ticksPerSecond) {
  MatchContext context{.pitchLengthMeters = initialState.pitch().lengthMeters(),
                       .ticksPerSecond = ticksPerSecond,
                       .sides = {},
                       .initialPossession = std::nullopt};
  for (const SimMatch::PlayerMatchState& player : initialState.players()) {
    context.sides.emplace(player.playerId, player.side);
    if (initialState.ball().owner == player.playerId) {
      context.initialPossession = player.side;
    }
  }
  return context;
}

MatchAnalyzer::MatchAnalyzer(MatchContext context, const AnalyticsConfig config)
    : context_(std::move(context)), config_(config), possession_(context_.initialPossession) {
  const bool validContext = context_.pitchLengthMeters > 0.0 &&
                            std::isfinite(context_.pitchLengthMeters) &&
                            context_.ticksPerSecond > 0;
  if (!validContext) {
    throw std::invalid_argument("MatchAnalyzer: the context needs a pitch length and tick rate");
  }
  const bool validConfig = config_.progressiveMeters >= 0.0 &&
                           std::isfinite(config_.progressiveMeters) && config_.ppdaZone > 0.0 &&
                           config_.ppdaZone <= 1.0;
  if (!validConfig) {
    throw std::invalid_argument(
        "MatchAnalyzer: progressiveMeters must be finite and non-negative, ppdaZone in (0, 1]");
  }
}

void MatchAnalyzer::observeStep(const std::span<const SimMatch::MatchEvent> events) {
  // Each control event (an interception, a won challenge, a loose-ball
  // recovery) is recorded immediately before the PossessionChanged it
  // caused; carrying its position only until that next PossessionChanged
  // keeps a step with more than one ownership change from assigning one
  // event's position to another's regain.
  std::optional<SimCore::Vec2> wonAt;
  for (const SimMatch::MatchEvent& event : events) {
    if (const auto* pass = std::get_if<SimMatch::PassAttempted>(&event)) {
      onPass(*pass);
    } else if (const auto* reception = std::get_if<SimMatch::PassReceived>(&event)) {
      onReception(*reception);
    } else if (const auto* intercepted = std::get_if<SimMatch::PassIntercepted>(&event)) {
      pendingPass_.reset();
      onDefensiveAction(intercepted->interceptor, intercepted->position);
      wonAt = wonAtOf(event);
    } else if (const auto* won = std::get_if<SimMatch::BallWon>(&event)) {
      onDefensiveAction(won->winner, won->position);
      wonAt = wonAtOf(event);
    } else if (std::holds_alternative<SimMatch::LooseBallRecovered>(event)) {
      wonAt = wonAtOf(event);
    } else if (const auto* change = std::get_if<SimMatch::PossessionChanged>(&event)) {
      onOwner(*change, wonAt);
      wonAt = std::nullopt;
    } else if (const auto* sample = std::get_if<SimMatch::PitchControlSampled>(&event)) {
      onSample(*sample);
    } else {
      onPress(event);
    }
  }
}

MatchStats MatchAnalyzer::finish(const SimCore::SimTick finalTick) const {
  std::array<std::int64_t, 2> possessed{sides_[0].possessionTicks, sides_[1].possessionTicks};
  if (possession_ && finalTick > possessionSince_) {
    possessed.at(indexOf(*possession_)) += finalTick.value() - possessionSince_.value();
  }
  return {.ticks = finalTick.value(),
          .seconds =
              static_cast<double>(finalTick.value()) / static_cast<double>(context_.ticksPerSecond),
          .home = statsOf(TeamSide::kHome, possessed[0], possessed[0] + possessed[1]),
          .away = statsOf(TeamSide::kAway, possessed[1], possessed[0] + possessed[1])};
}

std::optional<TeamSide> MatchAnalyzer::sideOf(const SimCore::PlayerId player) const {
  const auto found = context_.sides.find(player);
  if (found == context_.sides.end()) {
    return std::nullopt;
  }
  return found->second;
}

MatchAnalyzer::SideTally& MatchAnalyzer::tally(const TeamSide side) {
  return sides_.at(indexOf(side));
}

double MatchAnalyzer::depthOf(const TeamSide side, const SimCore::Vec2 position) const noexcept {
  return side == TeamSide::kHome ? position.x : context_.pitchLengthMeters - position.x;
}

PitchThird MatchAnalyzer::thirdOf(const TeamSide side,
                                  const SimCore::Vec2 position) const noexcept {
  const double third = context_.pitchLengthMeters / 3.0;
  const double depth = depthOf(side, position);
  if (depth < third) {
    return PitchThird::kDefensive;
  }
  return depth < 2.0 * third ? PitchThird::kMiddle : PitchThird::kAttacking;
}

TeamStats MatchAnalyzer::statsOf(const TeamSide side, const std::int64_t possessedTicks,
                                 const std::int64_t anyPossessionTicks) const {
  const SideTally& counts = sides_.at(indexOf(side));
  const SideTally& opponent = sides_.at(indexOf(otherSide(side)));
  return {
      .possessionShare =
          ratio(static_cast<double>(possessedTicks), static_cast<double>(anyPossessionTicks))
              .value_or(0.0),
      .passes = counts.passes,
      .completedPasses = counts.completedPasses,
      .passCompletion = ratio(counts.completedPasses, counts.passes),
      .meanPassMeters = ratio(counts.passMeters, counts.passes),
      .progressivePasses = counts.progressivePasses,
      .completedProgressivePasses = counts.completedProgressivePasses,
      .turnovers = counts.turnovers,
      .regains = counts.regains,
      .regainsByThird = counts.regainsByThird,
      .pressures = counts.pressures,
      .pressuresRegained = counts.pressuresRegained,
      .ppda = ratio(opponent.ppdaPasses, counts.ppdaActions),
      .pitchControlShare = ratio(counts.pitchControlSum, samples_),
      .attackingThirdShare = ratio(counts.attackingThirdSamples, samples_),
  };
}

void MatchAnalyzer::onPass(const SimMatch::PassAttempted& pass) {
  const auto side = sideOf(pass.passer);
  if (!side) {
    return;
  }
  SideTally& counts = tally(*side);
  const bool progressive =
      depthOf(*side, pass.target) - depthOf(*side, pass.from) >= config_.progressiveMeters;
  ++counts.passes;
  counts.passMeters += SimCore::distance(pass.from, pass.target);
  counts.progressivePasses += progressive ? 1 : 0;
  // Passes from the passer's own build-up zone, which the opponent's PPDA
  // allows.
  counts.ppdaPasses +=
      depthOf(*side, pass.from) <= config_.ppdaZone * context_.pitchLengthMeters ? 1 : 0;
  pendingPass_ = PendingPass{.passer = pass.passer, .progressive = progressive};
}

void MatchAnalyzer::onReception(const SimMatch::PassReceived& reception) {
  const auto side = sideOf(reception.passer);
  if (!side) {
    return;
  }
  SideTally& counts = tally(*side);
  ++counts.completedPasses;
  if (pendingPass_ && pendingPass_->passer == reception.passer && pendingPass_->progressive) {
    ++counts.completedProgressivePasses;
  }
  pendingPass_.reset();
}

void MatchAnalyzer::onDefensiveAction(const SimCore::PlayerId player,
                                      const SimCore::Vec2 position) {
  const auto side = sideOf(player);
  if (!side) {
    return;
  }
  // In the opponent's build-up zone: within ppdaZone of the pitch length
  // from the opponent's goal line.
  const double fromOpponentGoal = context_.pitchLengthMeters - depthOf(*side, position);
  tally(*side).ppdaActions +=
      fromOpponentGoal <= config_.ppdaZone * context_.pitchLengthMeters ? 1 : 0;
}

void MatchAnalyzer::onOwner(const SimMatch::PossessionChanged& change,
                            const std::optional<SimCore::Vec2>& wonAt) {
  // A free ball, a pass in flight, still belongs to the side that played it.
  if (!change.newOwner) {
    return;
  }
  const auto side = sideOf(*change.newOwner);
  if (!side || side == possession_) {
    return;
  }
  if (possession_) {
    tally(*possession_).possessionTicks += change.tick.value() - possessionSince_.value();
    ++tally(*possession_).turnovers;
    SideTally& winner = tally(*side);
    ++winner.regains;
    if (wonAt) {
      ++winner.regainsByThird.at(static_cast<std::size_t>(thirdOf(*side, *wonAt)));
    }
  }
  possession_ = side;
  possessionSince_ = change.tick;
}

void MatchAnalyzer::onPress(const SimMatch::MatchEvent& event) {
  if (const auto* started = std::get_if<SimMatch::PressingStarted>(&event)) {
    ++tally(started->side).pressures;
  } else if (const auto* ended = std::get_if<SimMatch::PressingEnded>(&event)) {
    tally(ended->side).pressuresRegained +=
        ended->outcome == SimMatch::PressOutcome::kBallRegained ? 1 : 0;
  }
}

void MatchAnalyzer::onSample(const SimMatch::PitchControlSampled& sample) {
  ++samples_;
  tally(TeamSide::kHome).pitchControlSum += sample.homeShare;
  tally(TeamSide::kAway).pitchControlSum += 1.0 - sample.homeShare;
  for (const TeamSide side : {TeamSide::kHome, TeamSide::kAway}) {
    tally(side).attackingThirdSamples +=
        thirdOf(side, sample.ball) == PitchThird::kAttacking ? 1 : 0;
  }
}

}  // namespace ElyverseFootball::SimAnalytics
