// sim-benchmark: plays a series of seven-a-side matches between two tactic
// files and writes their statistics (docs/sim-benchmark.md).

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "benchmark.hpp"
#include "seriesSummary.hpp"
#include "tacticJson.hpp"

namespace {

using ElyverseFootball::Benchmark::BenchmarkSpec;
using ElyverseFootball::Benchmark::MatchResult;

constexpr std::string_view kUsage =
    "usage: sim-benchmark --home <tactic.json> --away <tactic.json> [options]\n"
    "       sim-benchmark --style <tactic.json> --style <tactic.json> [--style ...] [options]\n"
    "options: [--matches <n>] [--seed <u64>] [--minutes <n>] [--jobs <n>] [--out <path>]\n"
    "         [--no-restarts] [--verify-replays <n>] [--max-seconds-per-match <s>]\n"
    "         [--max-seconds-total <s>]";

struct Options {
  std::string home;
  std::string away;
  int matches = 10;
  std::uint64_t seed = 1;
  int minutes = 10;
  int jobs = 1;
  std::string out;
  bool restarts = true;
  // Round robin: every ordered pairing of these, --matches each.
  std::vector<std::string> styles;
  // Matches per series to record as a replay and play back.
  int verifyReplays = 0;
  std::optional<double> maxSecondsPerMatch;
  std::optional<double> maxSecondsTotal;
};

int fail(const std::string& message) {
  std::cerr << "sim-benchmark: " << message << "\n";
  return EXIT_FAILURE;
}

template <typename Number>
std::expected<Number, std::string> parseNumber(const std::string_view option,
                                               const std::string_view token) {
  Number value{};
  // NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic) -- std::from_chars takes a raw
  // [begin, end) range.
  const auto [ptr, error] = std::from_chars(token.data(), token.data() + token.size(), value);
  if (token.empty() || error != std::errc{} || ptr != token.data() + token.size()) {
    // NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    return std::unexpected(std::format("invalid {} value '{}'", option, token));
  }
  return value;
}

// Every option but the flags takes one value. One branch per option keeps
// the grammar readable in one place.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
std::expected<Options, std::string> parseOptions(const std::span<char* const> args) {
  Options options;
  for (std::size_t index = 1; index < args.size(); ++index) {
    const std::string_view arg = args[index];
    if (arg == "--no-restarts") {
      options.restarts = false;
      continue;
    }
    if (index + 1 >= args.size()) {
      return std::unexpected(std::format("unknown argument or missing value '{}'", arg));
    }
    const std::string_view value = args[++index];
    std::expected<void, std::string> parsed;
    const auto positive = [&parsed, arg, value](int& target) {
      const auto number = parseNumber<int>(arg, value);
      if (!number || *number < 1) {
        parsed = std::unexpected(
            std::format("invalid {} value '{}', expected a positive number", arg, value));
        return;
      }
      target = *number;
    };
    if (arg == "--home") {
      options.home = value;
    } else if (arg == "--away") {
      options.away = value;
    } else if (arg == "--out") {
      options.out = value;
    } else if (arg == "--style") {
      options.styles.emplace_back(value);
    } else if (arg == "--verify-replays") {
      positive(options.verifyReplays);
    } else if (arg == "--matches") {
      positive(options.matches);
    } else if (arg == "--minutes") {
      positive(options.minutes);
    } else if (arg == "--jobs") {
      positive(options.jobs);
    } else if (arg == "--seed") {
      const auto seed = parseNumber<std::uint64_t>(arg, value);
      if (!seed) {
        return std::unexpected(seed.error());
      }
      options.seed = *seed;
    } else if (arg == "--max-seconds-per-match" || arg == "--max-seconds-total") {
      const auto seconds = parseNumber<double>(arg, value);
      if (!seconds || !(*seconds > 0.0)) {
        return std::unexpected(std::format("invalid {} value '{}'", arg, value));
      }
      (arg == "--max-seconds-total" ? options.maxSecondsTotal : options.maxSecondsPerMatch) =
          *seconds;
    } else {
      return std::unexpected(std::format("unknown argument '{}'", arg));
    }
    if (!parsed) {
      return std::unexpected(parsed.error());
    }
  }
  const bool series = !options.home.empty() || !options.away.empty();
  if (series && !options.styles.empty()) {
    return std::unexpected("--home and --away cannot be combined with --style");
  }
  if (!series && options.styles.size() < 2) {
    return std::unexpected("--home and --away, or at least two --style, are required");
  }
  if (series && (options.home.empty() || options.away.empty())) {
    return std::unexpected("--home and --away are required together");
  }
  return options;
}

std::string interval(const ElyverseFootball::SimAnalytics::SeriesSummary& summary) {
  if (!summary.mean) {
    return "-";
  }
  if (!summary.ciLow || !summary.ciHigh) {
    return std::format("{:.3f}", *summary.mean);
  }
  return std::format("{:.3f} [{:.3f}, {:.3f}]", *summary.mean, *summary.ciLow, *summary.ciHigh);
}

// A few headline metrics per side, mean with its 95 % interval.
void printSummary(const std::vector<MatchResult>& results) {
  for (const std::string_view name :
       {"possessionShare", "passCompletion", "progressivePasses", "regains",
        "regainsAttackingThird", "ppda", "pitchControlShare"}) {
    std::vector<std::string> sides;
    for (const bool home : {true, false}) {
      std::vector<double> values;
      for (const MatchResult& result : results) {
        for (const auto& metric : ElyverseFootball::SimAnalytics::metricsOf(
                 home ? result.stats.home : result.stats.away)) {
          if (metric.name == name && metric.value) {
            values.push_back(*metric.value);
          }
        }
      }
      sides.push_back(interval(ElyverseFootball::SimAnalytics::summarize(values)));
    }
    std::cout << std::format("{:<24}home {:<28} away {}\n", name, sides.at(0), sides.at(1));
  }
}

std::string failureText(const ElyverseFootball::Benchmark::BenchmarkFailure& failure) {
  return std::format("match {} (seed {}) failed at tick {}: {}", failure.index, failure.seed,
                     failure.tick, failure.message);
}

// The series settings shared by both modes.
template <typename Spec>
void applyOptions(const Options& options, Spec& spec) {
  spec.baseSeed = options.seed;
  spec.ticks = static_cast<std::int64_t>(options.minutes) * 60 * spec.config.ticksPerSecond;
  spec.config.restarts.enabled = options.restarts;
  spec.jobs = options.jobs;
}

// Plays the first --verify-replays matches of the series again as replays.
std::expected<void, std::string> verifyReplays(const Options& options, const BenchmarkSpec& spec) {
  for (int index = 0; index < std::min(options.verifyReplays, spec.matches); ++index) {
    if (const auto verified = ElyverseFootball::Benchmark::verifyReplay(spec, index); !verified) {
      return std::unexpected(failureText(verified.error()));
    }
  }
  return {};
}

// Writes the text to the file, if there is a path.
std::expected<void, std::string> writeFile(const std::filesystem::path& path,
                                           const std::string_view text) {
  if (path.empty()) {
    return {};
  }
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  file << text;
  if (!file.flush()) {
    return std::unexpected(path.string() + ": cannot write");
  }
  return {};
}

// Wall-clock times of the matches: the slowest and the sum.
struct Timing {
  double slowest = 0.0;
  double total = 0.0;
  std::size_t matches = 0;

  void add(const MatchResult& result) {
    slowest = std::max(slowest, result.wallSeconds);
    total += result.wallSeconds;
    ++matches;
  }
};

// The time budgets, checked after the results are written.
std::expected<void, std::string> checkBudgets(const Options& options, const Timing& timing) {
  std::cout << std::format(
      "{} matches of {} minutes: mean {:.3f} s, slowest {:.3f} s, total "
      "{:.1f} s wall time\n",
      timing.matches, options.minutes, timing.total / static_cast<double>(timing.matches),
      timing.slowest, timing.total);
  if (options.maxSecondsPerMatch && timing.slowest > *options.maxSecondsPerMatch) {
    return std::unexpected(
        std::format("the slowest match took {:.3f} s, over the budget of "
                    "{:.3f} s",
                    timing.slowest, *options.maxSecondsPerMatch));
  }
  if (options.maxSecondsTotal && timing.total > *options.maxSecondsTotal) {
    return std::unexpected(std::format("the matches took {:.1f} s, over the budget of {:.1f} s",
                                       timing.total, *options.maxSecondsTotal));
  }
  return {};
}

int runSeries(const Options& options) {
  const auto home = ElyverseFootball::SimTactics::loadTactic(options.home);
  if (!home) {
    return fail(home.error().message);
  }
  const auto away = ElyverseFootball::SimTactics::loadTactic(options.away);
  if (!away) {
    return fail(away.error().message);
  }
  BenchmarkSpec spec{.home = *home, .away = *away};
  spec.matches = options.matches;
  applyOptions(options, spec);

  const auto results = ElyverseFootball::Benchmark::runBenchmark(spec);
  if (!results) {
    return fail(failureText(results.error()));
  }
  Timing timing;
  for (const MatchResult& result : *results) {
    std::cout << std::format("match {:>3}  seed {:>20}  {:.2f} s\n", result.index, result.seed,
                             result.wallSeconds);
    timing.add(result);
  }
  std::cout << std::format("{} vs {}\n", home->name(), away->name());
  printSummary(*results);
  if (const auto verified = verifyReplays(options, spec); !verified) {
    return fail(verified.error());
  }
  if (const auto written =
          writeFile(options.out, ElyverseFootball::Benchmark::toBenchmarkJson(spec, *results));
      !written) {
    return fail(written.error());
  }
  if (const auto budget = checkBudgets(options, timing); !budget) {
    return fail(budget.error());
  }
  return EXIT_SUCCESS;
}

int runStyles(const Options& options) {
  ElyverseFootball::Benchmark::RoundRobinSpec spec;
  for (const std::string& path : options.styles) {
    auto style = ElyverseFootball::SimTactics::loadTactic(path);
    if (!style) {
      return fail(style.error().message);
    }
    spec.styles.push_back(*std::move(style));
  }
  spec.matchesPerPairing = options.matches;
  applyOptions(options, spec);

  const auto pairings = ElyverseFootball::Benchmark::runRoundRobin(spec);
  if (!pairings) {
    return fail(failureText(pairings.error()));
  }
  Timing timing;
  for (const auto& pairing : *pairings) {
    for (const MatchResult& result : pairing.results) {
      timing.add(result);
    }
    std::cout << std::format("\n{} vs {}\n", spec.styles.at(pairing.home).name(),
                             spec.styles.at(pairing.away).name());
    printSummary(pairing.results);
    if (const auto verified =
            verifyReplays(options, ElyverseFootball::Benchmark::seriesOf(spec, pairing));
        !verified) {
      return fail(verified.error());
    }
  }
  std::cout << "\nseparated beyond chance (95 % intervals):\n";
  for (const auto& separation : ElyverseFootball::Benchmark::separations(spec, *pairings)) {
    std::cout << std::format("  {:<28}{} > {}\n", separation.metric,
                             spec.styles.at(separation.higher).name(),
                             spec.styles.at(separation.lower).name());
  }
  if (const auto written =
          writeFile(options.out, ElyverseFootball::Benchmark::toRoundRobinJson(spec, *pairings));
      !written) {
    return fail(written.error());
  }
  if (const auto budget = checkBudgets(options, timing); !budget) {
    return fail(budget.error());
  }
  return EXIT_SUCCESS;
}

int run(const Options& options) {
  return options.styles.empty() ? runSeries(options) : runStyles(options);
}

}  // namespace

int main(int argc, char** argv) {
  const auto options = parseOptions(std::span<char* const>(argv, static_cast<std::size_t>(argc)));
  if (!options) {
    std::cerr << "sim-benchmark: " << options.error() << "\n" << kUsage << "\n";
    return EXIT_FAILURE;
  }
  try {
    return run(*options);
  } catch (const std::exception& error) {
    return fail(std::string("unexpected error: ") + error.what());
  }
}
