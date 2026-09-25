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
}

void addFields(StableHasher& hasher, const LooseBallRecovered& event) noexcept {
  hasher.addU64(event.player.value());
}

void addFields(StableHasher& hasher, const PossessionChanged& event) noexcept {
  addPlayer(hasher, event.previousOwner);
  addPlayer(hasher, event.newOwner);
}

void addFields(StableHasher& hasher, const PhaseChanged& event) noexcept {
  hasher.addU64(static_cast<std::uint64_t>(event.side));
  hasher.addBool(event.previous.has_value());
  hasher.addU64(
      static_cast<std::uint64_t>(event.previous.value_or(SimTactics::TacticalPhase::kBuildUp)));
  hasher.addU64(static_cast<std::uint64_t>(event.phase));
}

}  // namespace

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
