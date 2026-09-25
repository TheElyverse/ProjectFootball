#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>
#include <vector>

#include "matchEvents.hpp"
#include "matchSetup.hpp"
#include "matchSimulation.hpp"
#include "matchState.hpp"
#include "simTime.hpp"

namespace ElyverseFootball::SimReplay {

// The version of the debug frame format written by this build; see
// docs/debug-viewer.md.
inline constexpr int kDebugFramesVersion = 2;

// Everything the debug viewer shows for one tick: the state after the step
// that reached it, and what that step recorded. Tick 0 is the initial state,
// with no events.
struct DebugFrame {
  SimMatch::MatchState state;
  SimCore::SimTick tick;
  std::vector<SimMatch::MatchEvent> events;
  std::vector<SimMatch::DecisionDiagnostic> decisions;
  std::vector<SimMatch::ActionDiagnostic> actions;
};

// A match as a sequence of frames, one per tick, for the debug viewer.
// Read-only output: nothing reads it back into a simulation, and a replay,
// not this, is what reproduces a match.
struct DebugRecording {
  std::string coreVersion;
  std::string scenario;
  std::uint64_t seed = 0;
  SimMatch::MatchConfig config;
  std::vector<DebugFrame> frames;
};

// Records a frame after every step. The simulation must collect diagnostics
// (MatchSimulation::setCollectDiagnostics), or frames have no decisions.
class DebugFrameRecorder {
 public:
  // Records the initial state as the frame of tick 0.
  DebugFrameRecorder(const SimMatch::MatchSetup& setup, std::string scenario);

  void recordStep(const SimMatch::MatchSimulation& simulation);

  [[nodiscard]] const DebugRecording& recording() const noexcept { return recording_; }

 private:
  DebugRecording recording_;
};

// The recording as a JSON document in the format of docs/debug-viewer.md.
// Positions, velocities and scores are rounded to three decimals, so the
// file is smaller; it is for looking at, not for verifying a match.
[[nodiscard]] std::string toDebugFramesJson(const DebugRecording& recording);

// toDebugFramesJson() written to a file. Fails with a message naming the path.
[[nodiscard]] std::expected<void, std::string> saveDebugFrames(const DebugRecording& recording,
                                                               const std::filesystem::path& path);

}  // namespace ElyverseFootball::SimReplay
