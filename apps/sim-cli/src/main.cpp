#include <array>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <optional>
#include <random>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "cliOptions.hpp"
#include "debugFrames.hpp"
#include "decisionTrace.hpp"
#include "matchAnalyzer.hpp"
#include "matchSetup.hpp"
#include "referenceTactic.hpp"
#include "replay.hpp"
#include "replayJson.hpp"
#include "scenarios.hpp"
#include "simTime.hpp"
#include "statsJson.hpp"
#include "tactic.hpp"
#include "tacticJson.hpp"
#include "terminalUi.hpp"

namespace {

using ElyverseFootball::Cli::CliMode;
using ElyverseFootball::Cli::CliOptions;
using ElyverseFootball::Cli::SummaryLine;
using ElyverseFootball::SimCore::SimTick;
using ElyverseFootball::SimMatch::MatchSetup;
using ElyverseFootball::SimReplay::DebugFrameRecorder;
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

// Whether two paths name the same file, existing or not: compared after
// resolving them against the working directory, "." and ".." and links.
bool sameFile(const std::filesystem::path& first, const std::filesystem::path& second) {
  const auto resolve = [](const std::filesystem::path& path) {
    std::error_code error;
    const std::filesystem::path resolved = std::filesystem::weakly_canonical(path, error);
    return error ? std::filesystem::absolute(path).lexically_normal() : resolved;
  };
  return resolve(first) == resolve(second);
}

// Writes text to a file, replacing it.
std::expected<void, std::string> writeText(const std::filesystem::path& path,
                                           const std::string_view text) {
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  if (!file) {
    return std::unexpected(path.string() + ": cannot open for writing");
  }
  file << text;
  if (!file.flush()) {
    return std::unexpected(path.string() + ": cannot write");
  }
  return {};
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

// The tactic file at path, or the reference tactic for an empty path.
std::expected<ElyverseFootball::SimTactics::Tactic, std::string> tacticOrReference(
    const std::string& path) {
  if (path.empty()) {
    auto reference = ElyverseFootball::SimTactics::Tactic::create(
        ElyverseFootball::SimTactics::referenceTacticSpec());
    if (!reference) {
      return std::unexpected("invalid reference tactic: " + reference.error().front().message);
    }
    return *std::move(reference);
  }
  auto tactic = ElyverseFootball::SimTactics::loadTactic(path);
  if (!tactic) {
    return std::unexpected(tactic.error().message);
  }
  return *std::move(tactic);
}

std::string tacticName(const std::optional<ElyverseFootball::SimTactics::Tactic>& tactic) {
  return tactic ? tactic->name() : "scripted";
}

// The setup of the run: the scenario's, or with tactic files a tactic match.
std::expected<MatchSetup, std::string> makeSetup(
    const CliOptions& options, const ElyverseFootball::SimMatch::ScenarioDefinition& scenario,
    const std::uint64_t seed) {
  if (options.homeTactic.empty() && options.awayTactic.empty()) {
    return scenario.make(seed);
  }
  auto home = tacticOrReference(options.homeTactic);
  if (!home) {
    return std::unexpected(home.error());
  }
  auto away = tacticOrReference(options.awayTactic);
  if (!away) {
    return std::unexpected(away.error());
  }
  return ElyverseFootball::SimMatch::makeTacticMatch(
      {.home = *std::move(home), .away = *std::move(away)}, seed);
}

// Why the run's output files would overwrite each other, if they would.
std::optional<std::string> outputClash(const CliOptions& options) {
  if (!options.framesOut.empty() && sameFile(options.framesOut, options.replayOut)) {
    return "--frames-out and --replay-out name the same file '" + options.framesOut + "'";
  }
  if (!options.statsOut.empty() &&
      (sameFile(options.statsOut, options.replayOut) ||
       (!options.framesOut.empty() && sameFile(options.statsOut, options.framesOut)))) {
    return "--stats-out names the same file as another output: '" + options.statsOut + "'";
  }
  return std::nullopt;
}

// Runs a scenario, records it, writes the replay and, with --frames-out, the
// debug frames, then reports. Files are written before the terminal
// interface opens.
int runScenario(const CliOptions& options) {
  const auto* scenario = ElyverseFootball::SimMatch::findScenario(options.scenario);
  if (scenario == nullptr) {
    return fail("unknown scenario '" + options.scenario + "'; see --list-scenarios");
  }
  if (const auto clash = outputClash(options)) {
    return fail(*clash);
  }
  const std::uint64_t seed = resolveSeed(options);
  const auto setup = makeSetup(options, *scenario, seed);
  if (!setup) {
    return fail(setup.error());
  }

  // One run feeds both recorders. Collecting diagnostics draws no random
  // numbers, so it leaves the replay unchanged.
  ElyverseFootball::SimMatch::MatchSimulation simulation =
      ElyverseFootball::SimMatch::startMatch(*setup);
  ElyverseFootball::SimReplay::ReplayRecorder recorder(*setup);
  std::optional<DebugFrameRecorder> frames;
  if (!options.framesOut.empty()) {
    simulation.setCollectDiagnostics(true);
    frames.emplace(*setup, options.scenario);
  }
  std::optional<ElyverseFootball::SimAnalytics::MatchAnalyzer> analyzer;
  if (!options.statsOut.empty()) {
    analyzer.emplace(ElyverseFootball::SimAnalytics::contextOf(setup->initialState,
                                                               setup->config.ticksPerSecond));
  }
  while (simulation.tick() < SimTick(options.ticks)) {
    if (const auto stepped = simulation.step(); !stepped) {
      return fail(std::format("the simulation failed at tick {} in system '{}'",
                              stepped.error().tick.value(), stepped.error().systemName));
    }
    recorder.recordStep(simulation);
    if (frames) {
      frames->recordStep(simulation);
    }
    if (analyzer) {
      analyzer->observeStep(simulation.events());
    }
  }
  const Replay replay = recorder.finish(simulation, iso8601Now());
  if (const auto saved = ElyverseFootball::SimReplay::saveReplay(replay, options.replayOut);
      !saved) {
    return fail(saved.error().message);
  }
  if (frames) {
    if (const auto saved =
            ElyverseFootball::SimReplay::saveDebugFrames(frames->recording(), options.framesOut);
        !saved) {
      return fail(saved.error());
    }
  }

  if (analyzer) {
    if (const auto written = writeText(
            options.statsOut,
            ElyverseFootball::SimAnalytics::toStatsJson(analyzer->finish(simulation.tick())));
        !written) {
      return fail(written.error());
    }
  }

  // Elapsed time as the simulation clock computes it: one division.
  const double elapsedSeconds = static_cast<double>(replay.finalTick.value()) /
                                static_cast<double>(setup->config.ticksPerSecond);
  std::vector<SummaryLine> lines{{"scenario", options.scenario}};
  if (!options.homeTactic.empty() || !options.awayTactic.empty()) {
    const auto& tactics = setup->initialState.tactics();
    lines.emplace_back("home tactic", tacticName(tactics.home));
    lines.emplace_back("away tactic", tacticName(tactics.away));
  }
  lines.insert(lines.end(), {{"seed", std::to_string(seed)},
                             {"ticks", std::to_string(replay.finalTick.value())},
                             {"time", std::format("{} s", elapsedSeconds)},
                             {"state hash", hashText(replay.checkpoints.back().stateHash)},
                             {"event hash", hashText(replay.checkpoints.back().eventHash)},
                             {"replay", options.replayOut}});
  if (frames) {
    lines.emplace_back("frames", options.framesOut);
  }
  if (analyzer) {
    lines.emplace_back("stats", options.statsOut);
  }
  report(options, "Scenario run", lines);
  return EXIT_SUCCESS;
}

// The players and ticks --trace covers.
ElyverseFootball::SimMatch::DiagnosticsFilter traceFilter(const CliOptions& options) {
  ElyverseFootball::SimMatch::DiagnosticsFilter filter;
  for (const std::uint32_t player : options.tracePlayers) {
    filter.players.emplace_back(player);
  }
  if (options.traceFrom) {
    filter.from = SimTick(*options.traceFrom);
  }
  if (options.traceTo) {
    filter.to = SimTick(*options.traceTo);
  }
  return filter;
}

// Loads a replay, plays it back and verifies every checkpoint, and with
// --trace writes the decision trace of the playback.
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
  std::optional<ElyverseFootball::SimReplay::DecisionTracer> tracer;
  ElyverseFootball::SimReplay::PlaybackObserver observer;
  if (!options.tracePath.empty()) {
    tracer.emplace(replay->setup.config);
    observer.diagnostics = traceFilter(options);
    observer.afterStep = [&tracer](const ElyverseFootball::SimMatch::MatchSimulation& simulation) {
      tracer->recordStep(simulation);
    };
  }
  const auto playback = ElyverseFootball::SimReplay::playReplay(*replay, observer);
  if (!playback) {
    return fail(options.playPath + ": " + playback.error().message);
  }
  if (tracer) {
    if (const auto written = writeText(options.tracePath,
                                       ElyverseFootball::SimReplay::formatTrace(tracer->entries()));
        !written) {
      return fail(written.error());
    }
  }
  std::vector<SummaryLine> lines{
      {"replay", options.playPath},
      {"seed", std::to_string(replay->setup.seed)},
      {"ticks", std::to_string(playback->finalTick.value())},
      {"time", std::format("{} s", playback->elapsedSeconds)},
      {"state hash", hashText(playback->finalStateHash)},
      {"event hash", hashText(playback->finalEventHash)},
      {"checkpoints", std::format("{} verified", playback->checkpointsVerified)}};
  if (tracer) {
    lines.emplace_back(
        "trace", std::format("{} ({} decisions)", options.tracePath, tracer->entries().size()));
  }
  report(options, "Replay verified", lines);
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
