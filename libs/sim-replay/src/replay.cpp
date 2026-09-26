#include "replay.hpp"

#include <cstddef>
#include <format>
#include <stdexcept>
#include <string>
#include <utility>

#include "matchEvents.hpp"
#include "matchStateHash.hpp"
#include "stableHash.hpp"
#include "version.hpp"

namespace ElyverseFootball::SimReplay {
namespace {

using SimCore::SimTick;
using SimMatch::hashMatchState;
using SimMatch::MatchSimulation;

[[nodiscard]] ReplayCheckpoint checkpointOf(const MatchSimulation& simulation,
                                            const SimCore::StableHasher& events) noexcept {
  return {.tick = simulation.tick(),
          .stateHash = hashMatchState(simulation.state()),
          .eventHash = events.value()};
}

void addEvents(SimCore::StableHasher& hasher, const MatchSimulation& simulation) {
  for (const SimMatch::MatchEvent& event : simulation.events()) {
    SimMatch::addEvent(hasher, event);
  }
}

[[nodiscard]] std::string hex(const std::uint64_t hash) {
  return std::format("{:016x}", hash);
}

[[nodiscard]] std::unexpected<ReplayError> fail(const ReplayErrorCode code, std::string message) {
  return std::unexpected(
      ReplayError{.code = code, .message = std::move(message), .divergence = std::nullopt});
}

// "diverged after tick 30, at or before tick 60: state hash at tick 60 is
// ..., the replay recorded ...".
[[nodiscard]] std::string divergenceMessage(const ReplayDivergence& divergence,
                                            const std::uint64_t stateHash,
                                            const std::uint64_t eventHash,
                                            const ReplayCheckpoint& recorded) {
  const std::int64_t tick = divergence.firstDiverging.value();
  std::string message = divergence.lastMatching
                            ? std::format("diverged after tick {}, at or before tick {}",
                                          divergence.lastMatching->value(), tick)
                            : std::format("diverged at tick {}", tick);
  if (divergence.stateDiffers) {
    message += std::format(": state hash at tick {} is {}, the replay recorded {}", tick,
                           hex(stateHash), hex(recorded.stateHash));
  }
  if (divergence.eventsDiffer) {
    message += std::format("{} event hash at tick {} is {}, the replay recorded {}",
                           divergence.stateDiffers ? ";" : ":", tick, hex(eventHash),
                           hex(recorded.eventHash));
  }
  return message;
}

// The recorded checkpoints must be usable before anything runs: ascending,
// unique, within the recorded ticks, and including the initial and the final
// state, so that playback always compares where the match starts and ends.
// Every command must run before the final tick; one that never runs is not
// part of the recorded match.
[[nodiscard]] std::expected<void, ReplayError> checkRecording(const Replay& replay) {
  const SimTick::ValueType finalTick = replay.finalTick.value();
  SimTick::ValueType previous = -1;
  for (const ReplayCheckpoint& checkpoint : replay.checkpoints) {
    const SimTick::ValueType tick = checkpoint.tick.value();
    if (tick <= previous || tick > finalTick) {
      return fail(ReplayErrorCode::kInvalidSetup,
                  std::format("checkpoint at tick {} is out of order or past the final tick {}",
                              tick, finalTick));
    }
    previous = tick;
  }
  if (replay.checkpoints.empty() || replay.checkpoints.front().tick != SimTick(0) ||
      replay.checkpoints.back().tick != replay.finalTick) {
    return fail(ReplayErrorCode::kInvalidSetup,
                std::format("checkpoints must include tick 0 and the final tick {}", finalTick));
  }
  for (const SimMatch::ScheduledCommand& command : replay.setup.commands) {
    if (command.tick.value() >= finalTick) {
      return fail(ReplayErrorCode::kInvalidSetup,
                  std::format("command at tick {} never runs before the final tick {}",
                              command.tick.value(), finalTick));
    }
  }
  return {};
}

}  // namespace

ReplayRecorder::ReplayRecorder(SimMatch::MatchSetup setup, const int checkpointIntervalTicks)
    : setup_(std::move(setup)), checkpointIntervalTicks_(checkpointIntervalTicks) {
  if (checkpointIntervalTicks < 1) {
    throw std::invalid_argument("ReplayRecorder: checkpoint interval must be at least one tick");
  }
  // The replay format has no place for perception memories, possession,
  // phases, the pitch-control cache, players' tactical state, chasers or
  // presses: stateJson() does not serialize them and readState() recreates
  // them at their defaults, so a state where any of them is already
  // populated would silently change on a JSON round trip and fail its
  // tick-0 checkpoint on playback.
  const SimMatch::MatchState& initial = setup_.initialState;
  bool pristine = !initial.possession().team.has_value() &&
                  !initial.phase(SimMatch::TeamSide::kHome).has_value() &&
                  !initial.phase(SimMatch::TeamSide::kAway).has_value() &&
                  !initial.pitchControl().has_value() &&
                  !initial.chaser(SimMatch::TeamSide::kHome).has_value() &&
                  !initial.chaser(SimMatch::TeamSide::kAway).has_value() &&
                  !initial.press(SimMatch::TeamSide::kHome).has_value() &&
                  !initial.press(SimMatch::TeamSide::kAway).has_value();
  for (std::size_t index = 0; pristine && index < initial.players().size(); ++index) {
    pristine = initial.perception(index).observations.empty() &&
               initial.tactical(index) == SimMatch::PlayerTacticalState{};
  }
  if (!pristine) {
    throw std::invalid_argument(
        "ReplayRecorder: the initial state must not hold perception memories, possession, "
        "phases, pitch control, tactical state, chasers or presses; record from a state built "
        "with MatchState::create()");
  }
  checkpoints_.push_back({.tick = SimTick(0),
                          .stateHash = hashMatchState(setup_.initialState),
                          .eventHash = events_.value()});
}

void ReplayRecorder::recordStep(const MatchSimulation& simulation) {
  addEvents(events_, simulation);
  if (simulation.tick().value() % checkpointIntervalTicks_ == 0) {
    checkpoints_.push_back(checkpointOf(simulation, events_));
  }
}

Replay ReplayRecorder::finish(const MatchSimulation& simulation, std::string createdAt) const {
  Replay replay{.coreVersion = std::string(SimCore::coreVersion()),
                .createdAt = std::move(createdAt),
                .setup = setup_,
                .finalTick = simulation.tick(),
                .checkpointIntervalTicks = checkpointIntervalTicks_,
                .checkpoints = checkpoints_};
  replay.setup.commands.assign(simulation.appliedCommands().begin(),
                               simulation.appliedCommands().end());
  if (replay.checkpoints.back().tick != simulation.tick()) {
    replay.checkpoints.push_back(checkpointOf(simulation, events_));
  }
  return replay;
}

std::expected<Replay, SimMatch::MatchStepError> recordMatch(const SimMatch::MatchSetup& setup,
                                                            const SimTick finalTick,
                                                            const int checkpointIntervalTicks,
                                                            std::string createdAt) {
  MatchSimulation simulation = SimMatch::startMatch(setup);
  ReplayRecorder recorder(setup, checkpointIntervalTicks);
  while (simulation.tick() < finalTick) {
    if (auto stepped = simulation.step(); !stepped) {
      return std::unexpected(std::move(stepped.error()));
    }
    recorder.recordStep(simulation);
  }
  return recorder.finish(simulation, std::move(createdAt));
}

std::expected<ReplayPlayback, ReplayError> playReplay(const Replay& replay,
                                                      const PlaybackObserver& observer) {
  if (replay.coreVersion != SimCore::coreVersion()) {
    return fail(ReplayErrorCode::kIncompatibleCoreVersion,
                std::format("replay was recorded with core version {}, this build is {}",
                            replay.coreVersion, SimCore::coreVersion()));
  }
  if (auto checked = checkRecording(replay); !checked) {
    return std::unexpected(std::move(checked.error()));
  }

  std::expected<MatchSimulation, ReplayError> started = [&replay]() {
    try {
      return std::expected<MatchSimulation, ReplayError>(SimMatch::startMatch(replay.setup));
    } catch (const std::invalid_argument& error) {
      return std::expected<MatchSimulation, ReplayError>(
          std::unexpected(ReplayError{.code = ReplayErrorCode::kInvalidSetup,
                                      .message = error.what(),
                                      .divergence = std::nullopt}));
    }
  }();
  if (!started) {
    return std::unexpected(std::move(started.error()));
  }
  MatchSimulation& simulation = *started;
  if (observer.diagnostics) {
    simulation.setCollectDiagnostics(true);
    simulation.setDiagnosticsFilter(*observer.diagnostics);
  }

  ReplayPlayback playback{.finalTick = SimTick(0)};
  SimCore::StableHasher events;
  auto checkpoint = replay.checkpoints.begin();
  const auto verify = [&]() -> std::expected<void, ReplayError> {
    if (checkpoint == replay.checkpoints.end() || checkpoint->tick != simulation.tick()) {
      return {};
    }
    const std::uint64_t actual = hashMatchState(simulation.state());
    const ReplayDivergence divergence{
        .lastMatching = checkpoint == replay.checkpoints.begin()
                            ? std::nullopt
                            : std::optional<SimCore::SimTick>(std::prev(checkpoint)->tick),
        .firstDiverging = checkpoint->tick,
        .stateDiffers = actual != checkpoint->stateHash,
        .eventsDiffer = events.value() != checkpoint->eventHash};
    if (divergence.stateDiffers || divergence.eventsDiffer) {
      return std::unexpected(
          ReplayError{.code = ReplayErrorCode::kCheckpointMismatch,
                      .message = divergenceMessage(divergence, actual, events.value(), *checkpoint),
                      .divergence = divergence});
    }
    ++checkpoint;
    ++playback.checkpointsVerified;
    return {};
  };

  if (auto verified = verify(); !verified) {
    return std::unexpected(std::move(verified.error()));
  }
  while (simulation.tick() < replay.finalTick) {
    if (const auto stepped = simulation.step(); !stepped) {
      return fail(ReplayErrorCode::kSimulationFailed,
                  std::format("step from tick {} failed in system '{}'",
                              stepped.error().tick.value(), stepped.error().systemName));
    }
    addEvents(events, simulation);
    if (observer.afterStep) {
      observer.afterStep(simulation);
    }
    if (auto verified = verify(); !verified) {
      return std::unexpected(std::move(verified.error()));
    }
  }

  playback.finalTick = simulation.tick();
  playback.elapsedSeconds = simulation.elapsedSeconds();
  playback.finalStateHash = hashMatchState(simulation.state());
  playback.finalEventHash = events.value();
  return playback;
}

}  // namespace ElyverseFootball::SimReplay
