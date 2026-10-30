#include "matchEvents.hpp"

namespace ElyverseFootball::SimMatch {
namespace {

using SimCore::StableHasher;

void addPlayer(StableHasher& hasher, const std::optional<SimCore::PlayerId>& player) noexcept {
  hasher.addBool(player.has_value());
  hasher.addU64(player.value_or(SimCore::PlayerId::invalid()).value());
}

void addFields(StableHasher& hasher, const PassAttempted& event) noexcept {
  hasher.addU64(event.passer.value());
  addPlayer(hasher, event.intendedReceiver);
  hasher.addDouble(event.from.x);
  hasher.addDouble(event.from.y);
  hasher.addDouble(event.target.x);
  hasher.addDouble(event.target.y);
  hasher.addDouble(event.speed);
}

void addFields(StableHasher& hasher, const PassReceived& event) noexcept {
  hasher.addU64(event.receiver.value());
  hasher.addU64(event.passer.value());
}

void addFields(StableHasher& hasher, const PassIntercepted& event) noexcept {
  hasher.addU64(event.interceptor.value());
  hasher.addU64(event.passer.value());
  hasher.addDouble(event.position.x);
  hasher.addDouble(event.position.y);
}

void addFields(StableHasher& hasher, const LooseBallRecovered& event) noexcept {
  hasher.addU64(event.player.value());
  hasher.addDouble(event.position.x);
  hasher.addDouble(event.position.y);
}

void addFields(StableHasher& hasher, const PitchControlSampled& event) noexcept {
  hasher.addDouble(event.homeShare);
  hasher.addDouble(event.ball.x);
  hasher.addDouble(event.ball.y);
}

void addFields(StableHasher& hasher, const PossessionChanged& event) noexcept {
  addPlayer(hasher, event.previousOwner);
  addPlayer(hasher, event.newOwner);
}

void addFields(StableHasher& hasher, const BallWon& event) noexcept {
  hasher.addU64(event.winner.value());
  hasher.addU64(event.loser.value());
  hasher.addDouble(event.position.x);
  hasher.addDouble(event.position.y);
  hasher.addU64(static_cast<std::uint64_t>(event.cause));
}

void addFields(StableHasher& hasher, const DribbleAttempted& event) noexcept {
  hasher.addU64(event.carrier.value());
  hasher.addU64(event.opponent.value());
  hasher.addDouble(event.from.x);
  hasher.addDouble(event.from.y);
  hasher.addDouble(event.target.x);
  hasher.addDouble(event.target.y);
}

void addFields(StableHasher& hasher, const DribbleEnded& event) noexcept {
  hasher.addU64(event.carrier.value());
  hasher.addU64(event.opponent.value());
  hasher.addU64(static_cast<std::uint64_t>(event.outcome));
  hasher.addDouble(event.position.x);
  hasher.addDouble(event.position.y);
}

void addFields(StableHasher& hasher, const DuelStarted& event) noexcept {
  hasher.addU64(event.carrier.value());
  hasher.addU64(event.defender.value());
  hasher.addDouble(event.position.x);
  hasher.addDouble(event.position.y);
}

void addFields(StableHasher& hasher, const DuelEnded& event) noexcept {
  hasher.addU64(event.carrier.value());
  hasher.addU64(event.defender.value());
  hasher.addU64(static_cast<std::uint64_t>(event.outcome));
  hasher.addDouble(event.position.x);
  hasher.addDouble(event.position.y);
}

void addFields(StableHasher& hasher, const CrossAttempted& event) noexcept {
  hasher.addU64(event.crosser.value());
  hasher.addByte(static_cast<std::uint8_t>(event.area));
  hasher.addDouble(event.from.x);
  hasher.addDouble(event.from.y);
  hasher.addDouble(event.target.x);
  hasher.addDouble(event.target.y);
  hasher.addDouble(event.height);
  hasher.addBool(event.runner.has_value());
  hasher.addU64(event.runner.value_or(SimCore::PlayerId::invalid()).value());
  hasher.addDouble(event.speed);
}

void addFields(StableHasher& hasher, const TackleAttempted& event) noexcept {
  hasher.addU64(event.tackler.value());
  hasher.addU64(event.carrier.value());
  hasher.addBool(event.block);
  hasher.addU64(static_cast<std::uint64_t>(event.result));
  hasher.addDouble(event.position.x);
  hasher.addDouble(event.position.y);
}

void addFields(StableHasher& hasher, const PressingStarted& event) noexcept {
  hasher.addU64(static_cast<std::uint64_t>(event.side));
  hasher.addU64(event.carrier.value());
  hasher.addBool(event.trigger.has_value());
  hasher.addU64(static_cast<std::uint64_t>(
      event.trigger.value_or(SimTactics::PressingTrigger::kPoorFirstTouch)));
  hasher.addU64(event.assignments.size());
  for (const PressAssignment& assignment : event.assignments) {
    hasher.addU64(assignment.player.value());
    hasher.addU64(static_cast<std::uint64_t>(assignment.role));
    hasher.addU64(assignment.subject.value());
  }
}

void addFields(StableHasher& hasher, const PressingEnded& event) noexcept {
  hasher.addU64(static_cast<std::uint64_t>(event.side));
  hasher.addU64(static_cast<std::uint64_t>(event.outcome));
}

void addFields(StableHasher& hasher, const PhaseChanged& event) noexcept {
  hasher.addU64(static_cast<std::uint64_t>(event.side));
  hasher.addBool(event.previous.has_value());
  hasher.addU64(
      static_cast<std::uint64_t>(event.previous.value_or(SimTactics::TacticalPhase::kBuildUp)));
  hasher.addU64(static_cast<std::uint64_t>(event.phase));
}

void addFields(StableHasher& hasher, const TacticChanged& event) noexcept {
  hasher.addU64(static_cast<std::uint64_t>(event.side));
  hasher.addString(event.tactic);
  hasher.addU64(event.contentHash);
  hasher.addI64(event.given.value());
}

void addFields(StableHasher& hasher, const InstructionGiven& event) noexcept {
  hasher.addU64(static_cast<std::uint64_t>(event.side));
  hasher.addString(event.tactic);
  hasher.addU64(event.contentHash);
  hasher.addI64(event.effective.value());
}

void addFields(StableHasher& hasher, const SubstitutionMade& event) noexcept {
  hasher.addU64(static_cast<std::uint64_t>(event.side));
  hasher.addU64(event.off.value());
  hasher.addU64(event.on.value());
  hasher.addI64(event.ordered.value());
}

void addFields(StableHasher& hasher, const SubstitutionRefused& event) noexcept {
  hasher.addU64(static_cast<std::uint64_t>(event.side));
  hasher.addU64(event.off.value());
  hasher.addU64(event.on.value());
  hasher.addI64(event.ordered.value());
  hasher.addByte(static_cast<std::uint8_t>(event.reason));
}

void addFields(StableHasher& hasher, const RestartTaken& event) noexcept {
  hasher.addU64(static_cast<std::uint64_t>(event.kind));
  hasher.addU64(event.player.value());
  hasher.addDouble(event.position.x);
  hasher.addDouble(event.position.y);
}

void addFields(StableHasher& hasher, const ShotAttempted& event) noexcept {
  hasher.addU64(event.shooter.value());
  hasher.addDouble(event.from.x);
  hasher.addDouble(event.from.y);
  hasher.addDouble(event.target.x);
  hasher.addDouble(event.target.y);
  hasher.addDouble(event.height);
  hasher.addDouble(event.struckAt.x);
  hasher.addDouble(event.struckAt.y);
  hasher.addDouble(event.struckHeight);
  hasher.addDouble(event.speed);
  hasher.addDouble(event.distance);
  hasher.addDouble(event.opening);
  hasher.addByte(static_cast<std::uint8_t>(event.type));
}

void addFields(StableHasher& hasher, const ShotDeflected& event) noexcept {
  hasher.addU64(event.shooter.value());
  hasher.addI64(event.shotTick.value());
  hasher.addU64(event.player.value());
  hasher.addDouble(event.position.x);
  hasher.addDouble(event.position.y);
  hasher.addDouble(event.height);
  hasher.addBool(event.blocked);
}

void addFields(StableHasher& hasher, const ShotHitWoodwork& event) noexcept {
  hasher.addU64(event.shooter.value());
  hasher.addI64(event.shotTick.value());
  hasher.addU64(static_cast<std::uint64_t>(event.part));
  hasher.addDouble(event.position.x);
  hasher.addDouble(event.position.y);
  hasher.addDouble(event.height);
}

void addFields(StableHasher& hasher, const ShotResolved& event) noexcept {
  hasher.addU64(event.shooter.value());
  hasher.addI64(event.shotTick.value());
  hasher.addU64(static_cast<std::uint64_t>(event.outcome));
}

void addFields(StableHasher& hasher, const GoalScored& event) noexcept {
  hasher.addU64(static_cast<std::uint64_t>(event.side));
  addPlayer(hasher, event.scorer);
  addPlayer(hasher, event.assist);
  hasher.addBool(event.ownGoal);
  hasher.addI64(event.score.home);
  hasher.addI64(event.score.away);
}

void addFields(StableHasher& hasher, const SetPieceTaken& event) noexcept {
  hasher.addByte(static_cast<std::uint8_t>(event.kind));
  hasher.addU64(static_cast<std::uint64_t>(event.side));
  hasher.addU64(event.taker.value());
  hasher.addByte(static_cast<std::uint8_t>(event.delivery));
  hasher.addDouble(event.target.x);
  hasher.addDouble(event.target.y);
  addPlayer(hasher, event.receiver);
}

void addFields(StableHasher& hasher, const FoulCalled& event) noexcept {
  hasher.addU64(static_cast<std::uint64_t>(event.side));
  hasher.addU64(event.offender.value());
  hasher.addU64(event.victim.value());
  hasher.addDouble(event.position.x);
  hasher.addDouble(event.position.y);
  hasher.addU64(static_cast<std::uint64_t>(event.restart));
  hasher.addU64(static_cast<std::uint64_t>(event.card));
  hasher.addBool(event.fromBehind);
  hasher.addBool(event.denyingGoal);
}

void addFields(StableHasher& hasher, const OffsideCalled& event) noexcept {
  hasher.addU64(static_cast<std::uint64_t>(event.side));
  hasher.addU64(event.player.value());
  hasher.addU64(event.passer.value());
  hasher.addDouble(event.lineX);
  hasher.addDouble(event.position.x);
  hasher.addDouble(event.position.y);
}

void addFields(StableHasher& hasher, const PeriodEnded& event) noexcept {
  hasher.addU64(static_cast<std::uint64_t>(event.period));
  hasher.addI64(event.addedMinutes);
}

void addFields(StableHasher& hasher, const SaveAttempted& event) noexcept {
  hasher.addU64(event.keeper.value());
  hasher.addU64(event.shooter.value());
  hasher.addI64(event.shotTick.value());
  hasher.addU64(static_cast<std::uint64_t>(event.result));
  hasher.addDouble(event.position.x);
  hasher.addDouble(event.position.y);
  hasher.addDouble(event.height);
}

void addFields(StableHasher& hasher, const AerialContest& event) noexcept {
  hasher.addDouble(event.position.x);
  hasher.addDouble(event.position.y);
  hasher.addDouble(event.height);
  hasher.addU64(event.contestants.size());
  for (const AerialContestant& contestant : event.contestants) {
    hasher.addU64(contestant.player.value());
    hasher.addDouble(contestant.reach);
    hasher.addBool(contestant.reached);
  }
  addPlayer(hasher, event.winner);
  hasher.addBool(event.play.has_value());
  hasher.addU64(static_cast<std::uint64_t>(event.play.value_or(AerialPlay::kShot)));
}

}  // namespace

std::string_view woodworkPartName(const WoodworkPart part) noexcept {
  switch (part) {
    case WoodworkPart::kPostAtMinY:
      return "postAtMinY";
    case WoodworkPart::kPostAtMaxY:
      return "postAtMaxY";
    case WoodworkPart::kCrossbar:
      return "crossbar";
  }
  return "unknown";
}

std::string_view aerialPlayName(const AerialPlay play) noexcept {
  switch (play) {
    case AerialPlay::kShot:
      return "shot";
    case AerialPlay::kPass:
      return "pass";
    case AerialPlay::kKnockDown:
      return "knockDown";
    case AerialPlay::kClearance:
      return "clearance";
    case AerialPlay::kCaught:
      return "caught";
    case AerialPlay::kPunched:
      return "punched";
  }
  return "unknown";
}

std::string_view saveResultName(const SaveResult result) noexcept {
  switch (result) {
    case SaveResult::kCaught:
      return "caught";
    case SaveResult::kParriedIntoPlay:
      return "parriedIntoPlay";
    case SaveResult::kParriedBehind:
      return "parriedBehind";
    case SaveResult::kOutOfReach:
      return "outOfReach";
  }
  return "unknown";
}

std::string_view shotOutcomeName(const ShotOutcome outcome) noexcept {
  switch (outcome) {
    case ShotOutcome::kGoal:
      return "goal";
    case ShotOutcome::kSaved:
      return "saved";
    case ShotOutcome::kOffTarget:
      return "offTarget";
    case ShotOutcome::kBlocked:
      return "blocked";
  }
  return "unknown";
}

std::string_view onBallReasonName(const OnBallReason reason) noexcept {
  switch (reason) {
    case OnBallReason::kChoice:
      return "choice";
    case OnBallReason::kScanFirstSide:
      return "scanFirstSide";
    case OnBallReason::kScanOtherSide:
      return "scanOtherSide";
    case OnBallReason::kForcedRejected:
      return "forcedRejected";
    case OnBallReason::kForcedClear:
      return "forcedClear";
  }
  return "unknown";
}

std::string_view ballWonCauseName(const BallWonCause cause) noexcept {
  switch (cause) {
    case BallWonCause::kDuel:
      return "duel";
    case BallWonCause::kLooseTouch:
      return "looseTouch";
  }
  return "unknown";
}

std::string_view dribbleOutcomeName(const DribbleOutcome outcome) noexcept {
  switch (outcome) {
    case DribbleOutcome::kBeaten:
      return "beaten";
    case DribbleOutcome::kKept:
      return "kept";
    case DribbleOutcome::kLost:
      return "lost";
  }
  return "unknown";
}

std::string_view duelOutcomeName(const DuelOutcome outcome) noexcept {
  switch (outcome) {
    case DuelOutcome::kKept:
      return "kept";
    case DuelOutcome::kWon:
      return "won";
    case DuelOutcome::kLoose:
      return "loose";
    case DuelOutcome::kFoul:
      return "foul";
  }
  return "unknown";
}

std::string_view lungeResultName(const LungeResult result) noexcept {
  switch (result) {
    case LungeResult::kMissed:
      return "missed";
    case LungeResult::kWon:
      return "won";
    case LungeResult::kLoose:
      return "loose";
    case LungeResult::kFoul:
      return "foul";
    case LungeResult::kBlocked:
      return "blocked";
  }
  return "unknown";
}

std::string_view decisionOutcomeName(const DecisionOutcome outcome) noexcept {
  switch (outcome) {
    case DecisionOutcome::kPassed:
      return "passed";
    case DecisionOutcome::kNoValidOption:
      return "noValidOption";
    case DecisionOutcome::kShot:
      return "shot";
    case DecisionOutcome::kHeld:
      return "held";
    case DecisionOutcome::kTurned:
      return "turned";
    case DecisionOutcome::kCarried:
      return "carried";
    case DecisionOutcome::kDribbled:
      return "dribbled";
    case DecisionOutcome::kCrossed:
      return "crossed";
  }
  return "unknown";
}

bool isTouch(const DecisionOutcome outcome) noexcept {
  return outcome == DecisionOutcome::kTurned || outcome == DecisionOutcome::kCarried ||
         outcome == DecisionOutcome::kDribbled;
}

std::string_view foulRestartName(const FoulRestart restart) noexcept {
  switch (restart) {
    case FoulRestart::kFreeKick:
      return "freeKick";
    case FoulRestart::kPenalty:
      return "penalty";
    case FoulRestart::kAdvantage:
      return "advantage";
  }
  return "unknown";
}

std::string_view cardName(const Card card) noexcept {
  switch (card) {
    case Card::kNone:
      return "none";
    case Card::kYellow:
      return "yellow";
    case Card::kSecondYellow:
      return "secondYellow";
    case Card::kRed:
      return "red";
  }
  return "unknown";
}

std::string_view diveCallName(const DiveCall call) noexcept {
  switch (call) {
    case DiveCall::kDive:
      return "dive";
    case DiveCall::kLeaveWide:
      return "leaveWide";
    case DiveCall::kLeaveOutOfReach:
      return "leaveOutOfReach";
  }
  return "unknown";
}

std::string_view substitutionRefusalName(const SubstitutionRefusal refusal) noexcept {
  switch (refusal) {
    case SubstitutionRefusal::kLimitReached:
      return "limitReached";
    case SubstitutionRefusal::kPlayerOff:
      return "playerOff";
    case SubstitutionRefusal::kPlayerUsed:
      return "playerUsed";
  }
  return "unknown";
}

std::string_view eventName(const MatchEvent& event) {
  struct Names {
    std::string_view operator()(const PassAttempted& /*event*/) const noexcept {
      return "pass attempted";
    }
    std::string_view operator()(const PassReceived& /*event*/) const noexcept {
      return "pass received";
    }
    std::string_view operator()(const PassIntercepted& /*event*/) const noexcept {
      return "pass intercepted";
    }
    std::string_view operator()(const LooseBallRecovered& /*event*/) const noexcept {
      return "loose ball recovered";
    }
    std::string_view operator()(const PossessionChanged& /*event*/) const noexcept {
      return "possession changed";
    }
    std::string_view operator()(const PhaseChanged& /*event*/) const noexcept {
      return "phase changed";
    }
    std::string_view operator()(const BallWon& /*event*/) const noexcept { return "ball won"; }
    std::string_view operator()(const PressingStarted& /*event*/) const noexcept {
      return "pressing started";
    }
    std::string_view operator()(const PressingEnded& /*event*/) const noexcept {
      return "pressing ended";
    }
    std::string_view operator()(const TacticChanged& /*event*/) const noexcept {
      return "tactic changed";
    }
    std::string_view operator()(const PitchControlSampled& /*event*/) const noexcept {
      return "pitch control sampled";
    }
    std::string_view operator()(const RestartTaken& /*event*/) const noexcept {
      return "restart taken";
    }
    std::string_view operator()(const ShotAttempted& /*event*/) const noexcept {
      return "shot attempted";
    }
    std::string_view operator()(const ShotDeflected& /*event*/) const noexcept {
      return "shot deflected";
    }
    std::string_view operator()(const ShotHitWoodwork& /*event*/) const noexcept {
      return "shot hit woodwork";
    }
    std::string_view operator()(const ShotResolved& /*event*/) const noexcept {
      return "shot resolved";
    }
    std::string_view operator()(const GoalScored& /*event*/) const noexcept {
      return "goal scored";
    }
    std::string_view operator()(const SaveAttempted& /*event*/) const noexcept {
      return "save attempted";
    }
    std::string_view operator()(const AerialContest& /*event*/) const noexcept {
      return "aerial contest";
    }
    std::string_view operator()(const DribbleAttempted& /*event*/) const noexcept {
      return "dribble attempted";
    }
    std::string_view operator()(const DribbleEnded& /*event*/) const noexcept {
      return "dribble ended";
    }
    std::string_view operator()(const DuelStarted& /*event*/) const noexcept {
      return "duel started";
    }
    std::string_view operator()(const DuelEnded& /*event*/) const noexcept { return "duel ended"; }
    std::string_view operator()(const TackleAttempted& /*event*/) const noexcept {
      return "tackle attempted";
    }
    std::string_view operator()(const CrossAttempted& /*event*/) const noexcept {
      return "cross attempted";
    }
    std::string_view operator()(const PeriodEnded& /*event*/) const noexcept {
      return "period ended";
    }
    std::string_view operator()(const OffsideCalled& /*event*/) const noexcept {
      return "offside called";
    }
    std::string_view operator()(const FoulCalled& /*event*/) const noexcept {
      return "foul called";
    }
    std::string_view operator()(const SetPieceTaken& /*event*/) const noexcept {
      return "set piece taken";
    }
    std::string_view operator()(const InstructionGiven& /*event*/) const noexcept {
      return "instruction given";
    }
    std::string_view operator()(const SubstitutionMade& /*event*/) const noexcept {
      return "substitution made";
    }
    std::string_view operator()(const SubstitutionRefused& /*event*/) const noexcept {
      return "substitution refused";
    }
  };
  return std::visit(Names{}, event);
}

SimCore::SimTick eventTick(const MatchEvent& event) {
  return std::visit([](const auto& alternative) { return alternative.tick; }, event);
}

void addEvent(StableHasher& hasher, const MatchEvent& event) {
  hasher.addU64(event.index());
  hasher.addI64(eventTick(event).value());
  std::visit([&hasher](const auto& alternative) { addFields(hasher, alternative); }, event);
}

}  // namespace ElyverseFootball::SimMatch
