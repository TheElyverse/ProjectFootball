#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "matchSetup.hpp"
#include "matchSimulation.hpp"
#include "simTime.hpp"
#include "stableHash.hpp"

namespace ElyverseFootball::SimReplay {

// The version of the replay file format written by this build; see
// docs/replay-format.md.
inline constexpr int kReplaySchemaVersion = 3;

// One checkpoint per simulated second at the default 30 Hz.
inline constexpr int kDefaultCheckpointIntervalTicks = 30;

// The state hash (SimMatch::hashMatchState) after the step that reached tick,
// and the event hash: every event published up to that step, in order, fed to
// one SimCore::StableHasher with SimMatch::addEvent(). Tick 0 is the initial
// state, before any event.
struct ReplayCheckpoint {
  SimCore::SimTick tick;
  std::uint64_t stateHash = 0;
  std::uint64_t eventHash = 0;

  friend bool operator==(const ReplayCheckpoint&, const ReplayCheckpoint&) = default;
};

// A recorded match: the replay contract InitialSnapshot + OrderedCommands +
// Seed + CoreVersion (docs/implementation-plan.md section 5.3), the
// configuration of the standard systems, how far the match ran, and state
// hashes to verify playback against.
//
// setup.commands holds the commands in execution order: by tick, and in
// scheduling order within a tick.
struct Replay {
  std::string coreVersion;
  // When the file was written, UTC; metadata only, never part of a check.
  std::string createdAt;
  SimMatch::MatchSetup setup;
  SimCore::SimTick finalTick;
  // Ascending by tick, each tick at most once, none past finalTick.
  std::vector<ReplayCheckpoint> checkpoints;

  friend bool operator==(const Replay&, const Replay&) = default;
};

enum class ReplayErrorCode : std::uint8_t {
  kIoError,
  kMalformed,
  kUnsupportedSchemaVersion,
  kIncompatibleCoreVersion,
  kInvalidSetup,
  kSimulationFailed,
  kCheckpointMismatch,
};

struct ReplayError {
  ReplayErrorCode code = ReplayErrorCode::kMalformed;
  std::string message;

  friend bool operator==(const ReplayError&, const ReplayError&) = default;
};

// Records a replay of a simulation started with SimMatch::startMatch(setup).
// Call recordStep() after every successful step and finish() at the end. The
// recorder takes a checkpoint of the initial state, one every
// checkpointIntervalTicks ticks, and one of the final state.
class ReplayRecorder {
 public:
  // Throws std::invalid_argument for an interval below one tick and for an
  // initial state with perception memories, which the replay format does not
  // record: record from a state built with MatchState::create().
  explicit ReplayRecorder(SimMatch::MatchSetup setup,
                          int checkpointIntervalTicks = kDefaultCheckpointIntervalTicks);

  void recordStep(const SimMatch::MatchSimulation& simulation);

  // The replay of everything the simulation has done: its applied commands
  // replace the setup's, so commands scheduled during the run are recorded
  // and commands never reached are not.
  [[nodiscard]] Replay finish(const SimMatch::MatchSimulation& simulation,
                              std::string createdAt) const;

 private:
  SimMatch::MatchSetup setup_;
  int checkpointIntervalTicks_;
  std::vector<ReplayCheckpoint> checkpoints_;
  SimCore::StableHasher events_;
};

// Runs the setup with the standard systems up to finalTick and records the
// replay. Fails with the step error if a step fails.
[[nodiscard]] std::expected<Replay, SimMatch::MatchStepError> recordMatch(
    const SimMatch::MatchSetup& setup, SimCore::SimTick finalTick, int checkpointIntervalTicks,
    std::string createdAt);

// What playing a replay back produced.
struct ReplayPlayback {
  SimCore::SimTick finalTick;
  double elapsedSeconds = 0.0;
  std::uint64_t finalStateHash = 0;
  std::uint64_t finalEventHash = 0;
  std::size_t checkpointsVerified = 0;
};

// Rebuilds the recorded match, runs it to the final tick, and compares the
// state and event hashes at every checkpoint. Rejects a replay from another core version,
// an invalid setup, a failing step, and the first checkpoint with a differing
// hash.
//
// An observer watches the playback step by step -- the decision tracer, for
// instance -- and may turn diagnostics on for it. Diagnostics change nothing
// in the match, so a watched playback verifies exactly like an unwatched one.
struct PlaybackObserver {
  // Collect diagnostics, narrowed by this filter; empty collects none.
  std::optional<SimMatch::DiagnosticsFilter> diagnostics;
  // Called after every successful step.
  std::function<void(const SimMatch::MatchSimulation&)> afterStep;
};

[[nodiscard]] std::expected<ReplayPlayback, ReplayError> playReplay(
    const Replay& replay, const PlaybackObserver& observer = {});

}  // namespace ElyverseFootball::SimReplay
