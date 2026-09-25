#include "cliOptions.hpp"

#include <charconv>
#include <cstddef>
#include <stdexcept>
#include <system_error>

namespace ElyverseFootball::Cli {
namespace {

// std::span has no bounds-checked at() (unlike std::vector/std::array), so this
// is span's missing at(): the one place a bounds check plus the actual element
// access happen together, instead of trusting every call site to have checked
// first. Mirrors what std::vector::at()/gsl::at() do internally.
[[nodiscard]] char* checkedAt(const std::span<char* const> args, const std::size_t index) {
  if (index >= args.size()) {
    throw std::out_of_range("sim-cli: argument index out of range");
  }
  return args[index];  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
}

template <typename Integer>
[[nodiscard]] std::expected<Integer, std::string> parseInteger(const std::string_view option,
                                                               const std::string_view token) {
  Integer value{};
  // NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic) -- std::from_chars only takes a
  // raw [begin, end) pointer range, there is no std::string_view overload.
  const auto [ptr, ec] = std::from_chars(token.data(), token.data() + token.size(), value);
  if (token.empty() || ec != std::errc{} || ptr != token.data() + token.size()) {
    // NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    return std::unexpected("invalid " + std::string(option) + " value '" + std::string(token) +
                           "'");
  }
  return value;
}

// Walks the arguments; value() consumes the one after an option.
class ArgumentReader {
 public:
  explicit ArgumentReader(const std::span<char* const> args) : args_(args) {}

  [[nodiscard]] bool done() const noexcept { return index_ >= args_.size(); }

  [[nodiscard]] std::string_view next() { return checkedAt(args_, index_++); }

  [[nodiscard]] std::expected<std::string_view, std::string> value(const std::string_view option) {
    if (done()) {
      return std::unexpected(std::string(option) + " requires a value");
    }
    return next();
  }

 private:
  std::span<char* const> args_;
  std::size_t index_ = 1;  // skip the program name
};

}  // namespace

// One branch per option keeps the grammar readable in one place.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
std::expected<CliOptions, std::string> parseCliOptions(const std::span<char* const> args) {
  CliOptions options;
  // Recorded apart from options.mode, so conflicts are found whatever the
  // order of the arguments.
  bool help = false;
  bool listScenarios = false;
  bool play = false;
  bool newRunOption = false;
  bool scenarioGiven = false;
  ArgumentReader reader(args);
  while (!reader.done()) {
    const std::string_view arg = reader.next();
    if (arg == "--tui") {
      options.tui = true;
    } else if (arg == "--help") {
      help = true;
    } else if (arg == "--list-scenarios") {
      listScenarios = true;
    } else if (arg == "--play") {
      const auto path = reader.value(arg);
      if (!path) {
        return std::unexpected(path.error());
      }
      if (play) {
        return std::unexpected("--play is given twice");
      }
      play = true;
      options.playPath = *path;
    } else if (arg == "--scenario" || arg == "--seed" || arg == "--ticks" ||
               arg == "--replay-out" || arg == "--frames-out" || arg == "--home-tactic" ||
               arg == "--away-tactic") {
      const auto value = reader.value(arg);
      if (!value) {
        return std::unexpected(value.error());
      }
      newRunOption = true;
      if (arg == "--scenario") {
        options.scenario = *value;
        scenarioGiven = true;
      } else if (arg == "--home-tactic") {
        options.homeTactic = *value;
      } else if (arg == "--away-tactic") {
        options.awayTactic = *value;
      } else if (arg == "--replay-out") {
        options.replayOut = *value;
      } else if (arg == "--frames-out") {
        options.framesOut = *value;
      } else if (arg == "--seed") {
        const auto seed = parseInteger<std::uint64_t>(arg, *value);
        if (!seed) {
          return std::unexpected(seed.error());
        }
        options.seed = *seed;
      } else {
        const auto ticks = parseInteger<std::int64_t>(arg, *value);
        if (!ticks || *ticks < 0 || *ticks > kMaxTicks) {
          return std::unexpected("invalid --ticks value '" + std::string(*value) +
                                 "', expected 0 to " + std::to_string(kMaxTicks));
        }
        options.ticks = *ticks;
      }
    } else {
      return std::unexpected("unknown argument '" + std::string(arg) + "'");
    }
  }
  // --help always wins: whatever else was asked, the usage answers it.
  if (help) {
    options.mode = CliMode::kHelp;
    return options;
  }
  if (play && listScenarios) {
    return std::unexpected("--play and --list-scenarios cannot be combined");
  }
  if (play && newRunOption) {
    return std::unexpected(
        "--play takes everything from the replay file and cannot be combined with --scenario, "
        "--seed, --ticks, --replay-out, --frames-out or a tactic");
  }
  if (listScenarios && newRunOption) {
    return std::unexpected(
        "--list-scenarios runs nothing and cannot be combined with --scenario, --seed, --ticks, "
        "--replay-out, --frames-out or a tactic");
  }
  if (!options.homeTactic.empty() || !options.awayTactic.empty()) {
    if (scenarioGiven && options.scenario != kTacticMatchScenario) {
      return std::unexpected("--home-tactic and --away-tactic only apply to --scenario " +
                             std::string(kTacticMatchScenario) + ", got --scenario " +
                             options.scenario);
    }
    options.scenario = kTacticMatchScenario;
  }
  if (!options.framesOut.empty() && options.ticks > kMaxFrameTicks) {
    return std::unexpected("--frames-out records at most " + std::to_string(kMaxFrameTicks) +
                           " ticks, got --ticks " + std::to_string(options.ticks));
  }
  if (play) {
    options.mode = CliMode::kPlay;
  } else if (listScenarios) {
    options.mode = CliMode::kListScenarios;
  }
  return options;
}

}  // namespace ElyverseFootball::Cli
