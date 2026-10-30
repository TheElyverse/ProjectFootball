#include "matchAnalyzer.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <utility>
#include <variant>

#include "teamFrame.hpp"

namespace ElyverseFootball::SimAnalytics {
namespace {

using SimMatch::opponentOf;
using SimMatch::TeamSide;

[[nodiscard]] std::size_t indexOf(const TeamSide side) noexcept {
  return side == TeamSide::kHome ? 0U : 1U;
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
  const ChanceQualityModel& chances = config_.chances;
  const bool validConfig = config_.progressiveMeters >= 0.0 &&
                           std::isfinite(config_.progressiveMeters) && config_.ppdaZone > 0.0 &&
                           config_.ppdaZone <= 1.0 && std::isfinite(chances.intercept) &&
                           std::isfinite(chances.perMeter) && std::isfinite(chances.perOpening) &&
                           chances.penaltyChance >= 0.0 && chances.penaltyChance <= 1.0;
  if (!validConfig) {
    throw std::invalid_argument(
        "MatchAnalyzer: progressiveMeters must be finite and non-negative, ppdaZone in (0, 1], "
        "the chance coefficients finite and penaltyChance in [0, 1]");
  }
}

void MatchAnalyzer::observeStep(const std::span<const SimMatch::MatchEvent> events) {
  // Each control event (an interception, a won duel, a loose-ball
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
    } else if (const auto* shot = std::get_if<SimMatch::ShotAttempted>(&event)) {
      onShot(*shot);
    } else if (const auto* resolved = std::get_if<SimMatch::ShotResolved>(&event)) {
      onShotResolved(*resolved);
    } else if (const auto* duel = std::get_if<SimMatch::DuelEnded>(&event)) {
      onDuel(*duel);
    } else if (const auto* aerial = std::get_if<SimMatch::AerialContest>(&event)) {
      onAerial(*aerial);
    } else if (const auto* goal = std::get_if<SimMatch::GoalScored>(&event)) {
      ++tally(goal->side).goals;
    } else if (const auto* offside = std::get_if<SimMatch::OffsideCalled>(&event)) {
      ++tally(offside->side).offsides;
    } else if (const auto* foul = std::get_if<SimMatch::FoulCalled>(&event)) {
      onFoul(*foul);
    } else if (const auto* setPiece = std::get_if<SimMatch::SetPieceTaken>(&event)) {
      onSetPiece(*setPiece);
      penaltyTaker_ = setPiece->kind == SimTactics::SetPieceKind::kPenalty
                          ? std::optional(setPiece->taker)
                          : std::nullopt;
    } else if (const auto* substitution = std::get_if<SimMatch::SubstitutionMade>(&event)) {
      // The newcomer plays for the side from now on.
      context_.sides[substitution->on] = substitution->side;
    } else if (const auto* ended = std::get_if<SimMatch::PeriodEnded>(&event)) {
      finished_ = finished_ || ended->period == SimMatch::MatchPeriod::kSecondHalf;
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

namespace {

// 3 for a win, 1 for a draw, 0 for a defeat.
[[nodiscard]] int pointsOf(const int goals, const int against) noexcept {
  if (goals == against) {
    return 1;
  }
  return goals > against ? 3 : 0;
}

}  // namespace

MatchStats MatchAnalyzer::finish(const SimCore::SimTick finalTick) const {
  std::array<std::int64_t, 2> possessed{sides_[0].possessionTicks, sides_[1].possessionTicks};
  if (possession_ && finalTick > possessionSince_) {
    possessed.at(indexOf(*possession_)) += finalTick.value() - possessionSince_.value();
  }
  std::array<std::int64_t, 2> longest{sides_[0].longestOwnershipTicks,
                                      sides_[1].longestOwnershipTicks};
  if (ownerSide_ && finalTick > ownerSince_) {
    std::int64_t& ticks = longest.at(indexOf(*ownerSide_));
    ticks = std::max(ticks, finalTick.value() - ownerSince_.value());
  }
  return {.ticks = finalTick.value(),
          .seconds =
              static_cast<double>(finalTick.value()) / static_cast<double>(context_.ticksPerSecond),
          .finished = finished_,
          .home = statsOf(TeamSide::kHome, possessed[0], possessed[0] + possessed[1], longest[0]),
          .away = statsOf(TeamSide::kAway, possessed[1], possessed[0] + possessed[1], longest[1])};
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
                                 const std::int64_t anyPossessionTicks,
                                 const std::int64_t longestOwnershipTicks) const {
  const SideTally& counts = sides_.at(indexOf(side));
  const SideTally& opponent = sides_.at(indexOf(opponentOf(side)));
  return {
      .possessionShare =
          ratio(static_cast<double>(possessedTicks), static_cast<double>(anyPossessionTicks))
              .value_or(0.0),
      .longestOwnershipSeconds =
          static_cast<double>(longestOwnershipTicks) / static_cast<double>(context_.ticksPerSecond),
      .passes = counts.passes,
      .completedPasses = counts.completedPasses,
      .passCompletion = ratio(counts.completedPasses, counts.passes),
      .meanPassMeters = ratio(counts.passMeters, counts.passes),
      .progressivePasses = counts.progressivePasses,
      .completedProgressivePasses = counts.completedProgressivePasses,
      .shots = counts.shots,
      .shotsOnTarget = counts.shotsOnTarget,
      .goals = counts.goals,
      .saves = counts.saves,
      .chanceQuality = counts.chanceQuality,
      .duelsWon = counts.duelsWon,
      .duelsLost = counts.duelsLost,
      .aerialDuels = counts.aerialDuels,
      .aerialDuelsWon = counts.aerialDuelsWon,
      .offsides = counts.offsides,
      .fouls = counts.fouls,
      .yellowCards = counts.yellowCards,
      .redCards = counts.redCards,
      .corners = counts.corners,
      .throwIns = counts.throwIns,
      .goalKicks = counts.goalKicks,
      .freeKicks = counts.freeKicks,
      .penalties = counts.penalties,
      .points = finished_ ? std::optional(pointsOf(counts.goals, opponent.goals)) : std::nullopt,
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

void MatchAnalyzer::onSetPiece(const SimMatch::SetPieceTaken& setPiece) {
  using SimTactics::SetPieceKind;
  SideTally& counts = tally(setPiece.side);
  counts.corners += setPiece.kind == SetPieceKind::kCorner ? 1 : 0;
  counts.throwIns += setPiece.kind == SetPieceKind::kThrowIn ? 1 : 0;
  counts.goalKicks += setPiece.kind == SetPieceKind::kGoalKick ? 1 : 0;
  counts.freeKicks += setPiece.kind == SetPieceKind::kFreeKick ? 1 : 0;
  counts.penalties += setPiece.kind == SetPieceKind::kPenalty ? 1 : 0;
}

void MatchAnalyzer::onShot(const SimMatch::ShotAttempted& shot) {
  const std::optional<SimCore::PlayerId> penaltyTaker = penaltyTaker_;
  penaltyTaker_.reset();
  const auto side = sideOf(shot.shooter);
  if (!side) {
    return;
  }
  SideTally& counts = tally(*side);
  ++counts.shots;
  counts.chanceQuality += penaltyTaker == shot.shooter
                              ? config_.chances.penaltyChance
                              : chanceQuality(shot.distance, shot.opening, config_.chances);
}

void MatchAnalyzer::onShotResolved(const SimMatch::ShotResolved& resolved) {
  using SimMatch::ShotOutcome;
  const auto side = sideOf(resolved.shooter);
  if (!side) {
    return;
  }
  if (resolved.outcome == ShotOutcome::kGoal || resolved.outcome == ShotOutcome::kSaved) {
    ++tally(*side).shotsOnTarget;
  }
  if (resolved.outcome == ShotOutcome::kSaved) {
    ++tally(opponentOf(*side)).saves;
  }
}

void MatchAnalyzer::onDuel(const SimMatch::DuelEnded& duel) {
  using SimMatch::DuelOutcome;
  const auto carrier = sideOf(duel.carrier);
  const auto defender = sideOf(duel.defender);
  if (!carrier || !defender ||
      (duel.outcome != DuelOutcome::kKept && duel.outcome != DuelOutcome::kWon)) {
    return;
  }
  const bool carrierWon = duel.outcome == DuelOutcome::kKept;
  ++tally(carrierWon ? *carrier : *defender).duelsWon;
  ++tally(carrierWon ? *defender : *carrier).duelsLost;
}

void MatchAnalyzer::onAerial(const SimMatch::AerialContest& contest) {
  std::array<bool, 2> upFor{};
  for (const SimMatch::AerialContestant& contestant : contest.contestants) {
    if (const auto side = sideOf(contestant.player)) {
      upFor.at(indexOf(*side)) = true;
    }
  }
  // A duel only between both sides.
  if (!upFor[0] || !upFor[1]) {
    return;
  }
  ++sides_[0].aerialDuels;
  ++sides_[1].aerialDuels;
  if (const auto winner = contest.winner ? sideOf(*contest.winner) : std::nullopt) {
    ++tally(*winner).aerialDuelsWon;
  }
}

void MatchAnalyzer::onFoul(const SimMatch::FoulCalled& foul) {
  SideTally& counts = tally(foul.side);
  ++counts.fouls;
  counts.yellowCards += foul.card == SimMatch::Card::kYellow ? 1 : 0;
  counts.redCards +=
      foul.card == SimMatch::Card::kRed || foul.card == SimMatch::Card::kSecondYellow ? 1 : 0;
}

void MatchAnalyzer::onPass(const SimMatch::PassAttempted& pass) {
  // A penalty taker who passes did not shoot it.
  if (penaltyTaker_ == pass.passer) {
    penaltyTaker_.reset();
  }
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
  // A penalty taker who lost the ball did not shoot it.
  if (penaltyTaker_ && change.previousOwner == penaltyTaker_ && change.newOwner != penaltyTaker_) {
    penaltyTaker_.reset();
  }
  if (ownerSide_) {
    std::int64_t& longest = tally(*ownerSide_).longestOwnershipTicks;
    longest = std::max(longest, change.tick.value() - ownerSince_.value());
  }
  ownerSide_ = change.newOwner ? sideOf(*change.newOwner) : std::nullopt;
  ownerSince_ = change.tick;
  // A free ball, a pass in flight, still belongs to the side that played it.
  if (!change.newOwner) {
    return;
  }
  const auto side = sideOf(*change.newOwner);
  if (!side || side == possession_) {
    return;
  }
  if (possession_) {
    SideTally& loser = tally(*possession_);
    loser.possessionTicks += change.tick.value() - possessionSince_.value();
    ++loser.turnovers;
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
