#include <array>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <expected>
#include <format>
#include <iostream>
#include <random>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "cliOptions.hpp"
#include "matchSetup.hpp"
#include "replay.hpp"
#include "replayJson.hpp"
#include "scenarios.hpp"
#include "simTime.hpp"
#include "terminalUi.hpp"

namespace {

using ElyverseFootball::Cli::CliMode;
using ElyverseFootball::Cli::CliOptions;
using ElyverseFootball::Cli::SummaryLine;
using ElyverseFootball::SimCore::SimTick;
using ElyverseFootball::SimMatch::MatchSetup;
using ElyverseFootball::SimReplay::Replay;

// Only the CLI (an adapter, not the simulation core) may pull entropy from a
// non-deterministic source, and only to pick the *master* seed when the user
// didn't supply one -- it is recorded in the output so the run stays
// reproducible from that point on.
std::uint64_t resolveSeed(const CliOptions& options) {
  if (options.seed) {
    return *options.seed;
  }
  std::random_device randomDevice;
  return (static_cast<std::uint64_t>(randomDevice()) << 32U) | randomDevice();
}

std::string iso8601Now() {
  const std::time_t now = std::time(nullptr);
  std::tm utcTm{};
#ifdef _WIN32
  gmtime_s(&utcTm, &now);
#else
  gmtime_r(&now, &utcTm);
#endif
  std::array<char, 32> buffer{};
  std::strftime(buffer.data(), buffer.size(), "%Y-%m-%dT%H:%M:%SZ", &utcTm);
  return buffer.data();
}

std::string hashText(const std::uint64_t hash) {
  return std::format("{:016x}", hash);
}

// Prints aligned "label: value" lines to stdout, or shows them in the terminal
// interface with --tui.
void report(const CliOptions& options, const std::string& title,
            const std::vector<SummaryLine>& lines) {
  if (options.tui) {
    ElyverseFootball::Cli::showSummary(title, lines);
    return;
  }
  for (const auto& [label, value] : lines) {
    std::cout << std::format("{:<14}{}\n", label + ":", value);
  }
}

int fail(const std::string& message) {
  std::cerr << "sim-cli: " << message << "\n";
  return EXIT_FAILURE;
}

void listScenarios() {
  for (const auto& scenario : ElyverseFootball::SimMatch::scenarios()) {
    std::cout << std::format("{:<16}{}\n", scenario.name, scenario.description);
  }
}

// Runs a scenario, records it, writes the replay, then reports. The replay is
// written before the terminal interface opens.
int runScenario(const CliOptions& options) {
  const auto* scenario = ElyverseFootball::SimMatch::findScenario(options.scenario);
  if (scenario == nullptr) {
    return fail("unknown scenario '" + options.scenario + "'; see --list-scenarios");
  }
  const std::uint64_t seed = resolveSeed(options);
  const auto setup = scenario->make(seed);
  if (!setup) {
    return fail(setup.error());
  }
  const auto replay = ElyverseFootball::SimReplay::recordMatch(
      *setup, SimTick(options.ticks), ElyverseFootball::SimReplay::kDefaultCheckpointIntervalTicks,
      iso8601Now());
  if (!replay) {
    return fail(std::format("the simulation failed at tick {} in system '{}'",
                            replay.error().tick.value(), replay.error().systemName));
  }
  if (const auto saved = ElyverseFootball::SimReplay::saveReplay(*replay, options.replayOut);
      !saved) {
    return fail(saved.error().message);
  }

  // Elapsed time as the simulation clock computes it: one division.
  const double elapsedSeconds = static_cast<double>(replay->finalTick.value()) /
                                static_cast<double>(setup->config.ticksPerSecond);
  report(options, "Scenario run",
         {{"scenario", options.scenario},
          {"seed", std::to_string(seed)},
          {"ticks", std::to_string(replay->finalTick.value())},
          {"time", std::format("{} s", elapsedSeconds)},
          {"state hash", hashText(replay->checkpoints.back().stateHash)},
          {"replay", options.replayOut}});
  return EXIT_SUCCESS;
}

// Loads a replay, plays it back and verifies every checkpoint.
int playReplayFile(const CliOptions& options) {
  const auto replay = ElyverseFootball::SimReplay::loadReplay(options.playPath);
  if (!replay) {
    return fail(replay.error().message);
  }
  // A replay file may claim any length the format allows; playback steps
  // tick by tick, so hold it to the limit of a new run.
  if (replay->finalTick.value() > ElyverseFootball::Cli::kMaxTicks) {
    return fail(std::format("{}: the replay runs {} ticks, sim-cli plays at most {}",
                            options.playPath, replay->finalTick.value(),
                            ElyverseFootball::Cli::kMaxTicks));
  }
  const auto playback = ElyverseFootball::SimReplay::playReplay(*replay);
  if (!playback) {
    return fail(options.playPath + ": " + playback.error().message);
  }
  report(options, "Replay verified",
         {{"replay", options.playPath},
          {"seed", std::to_string(replay->setup.seed)},
          {"ticks", std::to_string(playback->finalTick.value())},
          {"time", std::format("{} s", playback->elapsedSeconds)},
          {"state hash", hashText(playback->finalStateHash)},
          {"checkpoints", std::format("{} verified", playback->checkpointsVerified)}});
  return EXIT_SUCCESS;
}

}  // namespace

int main(int argc, char** argv) {
  std::expected<CliOptions, std::string> parsed;
  try {
    parsed = ElyverseFootball::Cli::parseCliOptions(
        std::span<char* const>(argv, static_cast<std::size_t>(argc)));
  } catch (const std::out_of_range&) {
    // checkedAt()'s bounds check should be unreachable given the parser's own
    // guards; if it ever fires (e.g. a future refactor drops a guard), report
    // it the same way as any other malformed input instead of letting the
    // exception escape main() and std::terminate().
    parsed = std::unexpected("internal error: argument index out of range");
  }
  if (!parsed) {
    std::cerr << "sim-cli: " << parsed.error() << "\n" << ElyverseFootball::Cli::kUsage << "\n";
    return EXIT_FAILURE;
  }
  const CliOptions& options = *parsed;
  if (options.tui && !ElyverseFootball::Cli::hasInteractiveTerminal()) {
    return fail("--tui requires an interactive terminal on stdin and stdout");
  }

  try {
    switch (options.mode) {
      case CliMode::kHelp:
        std::cout << ElyverseFootball::Cli::kUsage << "\n";
        return EXIT_SUCCESS;
      case CliMode::kListScenarios:
        listScenarios();
        return EXIT_SUCCESS;
      case CliMode::kPlay:
        return playReplayFile(options);
      case CliMode::kRun:
        return runScenario(options);
    }
  } catch (const std::exception& error) {
    return fail(std::string("unexpected error: ") + error.what());
  }
  return EXIT_FAILURE;
}
