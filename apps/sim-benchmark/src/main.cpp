// sim-benchmark: plays a series of seven-a-side matches between two tactic
// files and writes their statistics (docs/sim-benchmark.md).

#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <expected>
#include <format>
#include <fstream>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "benchmark.hpp"
#include "seriesSummary.hpp"
#include "tacticJson.hpp"

namespace {

using ElyverseFootball::Benchmark::BenchmarkSpec;
using ElyverseFootball::Benchmark::MatchResult;

constexpr std::string_view kUsage =
    "usage: sim-benchmark --home <tactic.json> --away <tactic.json> [--matches <n>]\n"
    "                     [--seed <u64>] [--minutes <n>] [--jobs <n>] [--out <path>]\n"
    "                     [--no-restarts] [--max-seconds-per-match <s>]";

struct Options {
  std::string home;
  std::string away;
  int matches = 10;
  std::uint64_t seed = 1;
  int minutes = 10;
  int jobs = 1;
  std::string out;
  bool restarts = true;
  std::optional<double> maxSecondsPerMatch;
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
    } else if (arg == "--max-seconds-per-match") {
      const auto seconds = parseNumber<double>(arg, value);
      if (!seconds || !(*seconds > 0.0)) {
        return std::unexpected(std::format("invalid {} value '{}'", arg, value));
      }
      options.maxSecondsPerMatch = *seconds;
    } else {
      return std::unexpected(std::format("unknown argument '{}'", arg));
    }
    if (!parsed) {
      return std::unexpected(parsed.error());
    }
  }
  if (options.home.empty() || options.away.empty()) {
    return std::unexpected("--home and --away are required");
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

int run(const Options& options) {
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
  spec.baseSeed = options.seed;
  spec.ticks = static_cast<std::int64_t>(options.minutes) * 60 * spec.config.ticksPerSecond;
  spec.config.restarts.enabled = options.restarts;
  spec.jobs = options.jobs;

  const auto results = ElyverseFootball::Benchmark::runBenchmark(spec);
  if (!results) {
    const auto& failure = results.error();
    return fail(std::format("match {} (seed {}) failed at tick {}: {}", failure.index, failure.seed,
                            failure.tick, failure.message));
  }
  double slowest = 0.0;
  double total = 0.0;
  for (const MatchResult& result : *results) {
    std::cout << std::format("match {:>3}  seed {:>20}  {:.2f} s\n", result.index, result.seed,
                             result.wallSeconds);
    slowest = std::max(slowest, result.wallSeconds);
    total += result.wallSeconds;
  }
  printSummary(*results);
  std::cout << std::format(
      "{} matches of {} minutes, {} vs {}: {:.2f} s simulated time per match, "
      "mean {:.3f} s, slowest {:.3f} s wall time\n",
      results->size(), options.minutes, home->name(), away->name(),
      static_cast<double>(spec.ticks) / spec.config.ticksPerSecond,
      total / static_cast<double>(results->size()), slowest);
  if (!options.out.empty()) {
    std::ofstream file(options.out, std::ios::binary | std::ios::trunc);
    file << ElyverseFootball::Benchmark::toBenchmarkJson(spec, *results);
    if (!file.flush()) {
      return fail(options.out + ": cannot write");
    }
  }
  if (options.maxSecondsPerMatch && slowest > *options.maxSecondsPerMatch) {
    return fail(std::format("the slowest match took {:.3f} s, over the budget of {:.3f} s", slowest,
                            *options.maxSecondsPerMatch));
  }
  return EXIT_SUCCESS;
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
