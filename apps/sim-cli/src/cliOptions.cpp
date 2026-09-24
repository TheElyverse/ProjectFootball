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
  bool newRunOption = false;
  ArgumentReader reader(args);
  while (!reader.done()) {
    const std::string_view arg = reader.next();
    if (arg == "--tui") {
      options.tui = true;
    } else if (arg == "--help") {
      options.mode = CliMode::kHelp;
    } else if (arg == "--list-scenarios") {
      options.mode = CliMode::kListScenarios;
    } else if (arg == "--play") {
      const auto path = reader.value(arg);
      if (!path) {
        return std::unexpected(path.error());
      }
      options.mode = CliMode::kPlay;
      options.playPath = *path;
    } else if (arg == "--scenario" || arg == "--seed" || arg == "--ticks" ||
               arg == "--replay-out") {
      const auto value = reader.value(arg);
      if (!value) {
        return std::unexpected(value.error());
      }
      newRunOption = true;
      if (arg == "--scenario") {
        options.scenario = *value;
      } else if (arg == "--replay-out") {
        options.replayOut = *value;
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
  if (options.mode == CliMode::kPlay && newRunOption) {
    return std::unexpected(
        "--play takes everything from the replay file and cannot be combined with --scenario, "
        "--seed, --ticks or --replay-out");
  }
  return options;
}

}  // namespace ElyverseFootball::Cli
