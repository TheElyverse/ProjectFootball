#include "replay.hpp"

#include <format>
#include <stdexcept>
#include <string>
#include <utility>

#include "matchStateHash.hpp"
#include "version.hpp"

namespace ElyverseFootball::SimReplay {
namespace {

using SimCore::SimTick;
using SimMatch::hashMatchState;
using SimMatch::MatchSimulation;

[[nodiscard]] ReplayCheckpoint checkpointOf(const MatchSimulation& simulation) noexcept {
  return {.tick = simulation.tick(), .stateHash = hashMatchState(simulation.state())};
}

[[nodiscard]] std::string hex(const std::uint64_t hash) {
  return std::format("{:016x}", hash);
}

[[nodiscard]] std::unexpected<ReplayError> fail(const ReplayErrorCode code, std::string message) {
  return std::unexpected(ReplayError{.code = code, .message = std::move(message)});
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
  checkpoints_.push_back({.tick = SimTick(0), .stateHash = hashMatchState(setup_.initialState)});
}

void ReplayRecorder::recordStep(const MatchSimulation& simulation) {
  if (simulation.tick().value() % checkpointIntervalTicks_ == 0) {
    checkpoints_.push_back(checkpointOf(simulation));
  }
}

Replay ReplayRecorder::finish(const MatchSimulation& simulation, std::string createdAt) const {
  Replay replay{.coreVersion = std::string(SimCore::coreVersion()),
                .createdAt = std::move(createdAt),
                .setup = setup_,
                .finalTick = simulation.tick(),
                .checkpoints = checkpoints_};
  replay.setup.commands.assign(simulation.appliedCommands().begin(),
                               simulation.appliedCommands().end());
  if (replay.checkpoints.back().tick != simulation.tick()) {
    replay.checkpoints.push_back(checkpointOf(simulation));
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

std::expected<ReplayPlayback, ReplayError> playReplay(const Replay& replay) {
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
      return std::expected<MatchSimulation, ReplayError>(std::unexpected(
          ReplayError{.code = ReplayErrorCode::kInvalidSetup, .message = error.what()}));
    }
  }();
  if (!started) {
    return std::unexpected(std::move(started.error()));
  }
  MatchSimulation& simulation = *started;

  ReplayPlayback playback{.finalTick = SimTick(0)};
  auto checkpoint = replay.checkpoints.begin();
  const auto verify = [&]() -> std::expected<void, ReplayError> {
    if (checkpoint == replay.checkpoints.end() || checkpoint->tick != simulation.tick()) {
      return {};
    }
    const std::uint64_t actual = hashMatchState(simulation.state());
    if (actual != checkpoint->stateHash) {
      return fail(ReplayErrorCode::kCheckpointMismatch,
                  std::format("state hash at tick {} is {}, the replay recorded {}",
                              simulation.tick().value(), hex(actual), hex(checkpoint->stateHash)));
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
    if (auto verified = verify(); !verified) {
      return std::unexpected(std::move(verified.error()));
    }
  }

  playback.finalTick = simulation.tick();
  playback.elapsedSeconds = simulation.elapsedSeconds();
  playback.finalStateHash = hashMatchState(simulation.state());
  return playback;
}

}  // namespace ElyverseFootball::SimReplay
