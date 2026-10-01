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

void addFields(StableHasher& hasher, const SaveAttempted& event) noexcept {
  hasher.addU64(event.keeper.value());
  hasher.addU64(event.shooter.value());
  hasher.addI64(event.shotTick.value());
  hasher.addU64(static_cast<std::uint64_t>(event.result));
  hasher.addDouble(event.position.x);
  hasher.addDouble(event.position.y);
  hasher.addDouble(event.height);
}

}  // namespace

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
