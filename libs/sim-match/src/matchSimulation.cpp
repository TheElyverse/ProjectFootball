#include "matchSimulation.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace ElyverseFootball::SimMatch {
namespace {

using SimCore::RandomNumberGenerator;
using SimCore::RandomNumberGeneratorDomain;

// MatchRandomStreams is indexed by domain value, so it must list every domain
// in declaration order. A domain added after kAi fails this assertion instead
// of silently sharing a stream.
static_assert(static_cast<std::size_t>(RandomNumberGeneratorDomain::kAi) + 1 ==
              std::tuple_size_v<MatchRandomStreams>);

[[nodiscard]] MatchRandomStreams makeRandomStreams(const std::uint64_t seed) noexcept {
  return {
      RandomNumberGenerator(SimCore::deriveSeed(seed, RandomNumberGeneratorDomain::kExecution)),
      RandomNumberGenerator(SimCore::deriveSeed(seed, RandomNumberGeneratorDomain::kInjuries)),
      RandomNumberGenerator(SimCore::deriveSeed(seed, RandomNumberGeneratorDomain::kGeneration)),
      RandomNumberGenerator(SimCore::deriveSeed(seed, RandomNumberGeneratorDomain::kMarket)),
      RandomNumberGenerator(SimCore::deriveSeed(seed, RandomNumberGeneratorDomain::kAi)),
  };
}

[[nodiscard]] std::vector<MatchSystem> validatedSystems(std::vector<MatchSystem> systems) {
  std::set<std::string, std::less<>> names;
  for (const MatchSystem& system : systems) {
    if (system.name.empty()) {
      throw std::invalid_argument("MatchSimulation: every system needs a name");
    }
    if (!system.update) {
      throw std::invalid_argument("MatchSimulation: system '" + system.name +
                                  "' has no update function");
    }
    if (!names.insert(system.name).second) {
      throw std::invalid_argument("MatchSimulation: system name '" + system.name +
                                  "' is used twice");
    }
    if (system.intervalTicks < 1) {
      throw std::invalid_argument("MatchSimulation: system '" + system.name +
                                  "' needs an interval of at least one tick, got " +
                                  std::to_string(system.intervalTicks));
    }
    if (system.phaseTicks < 0 || system.phaseTicks >= system.intervalTicks) {
      throw std::invalid_argument("MatchSimulation: system '" + system.name + "' has phase " +
                                  std::to_string(system.phaseTicks) + ", expected 0 to " +
                                  std::to_string(system.intervalTicks - 1));
    }
  }
  return systems;
}

[[nodiscard]] bool isDue(const MatchSystem& system, const SimCore::SimTick tick) noexcept {
  return tick.value() % system.intervalTicks == system.phaseTicks;
}

[[nodiscard]] MatchCommandError unknownPlayer(const SimCore::PlayerId playerId) {
  return {.code = MatchCommandErrorCode::kUnknownPlayer,
          .message = "no player with id " + std::to_string(playerId.value())};
}

[[nodiscard]] std::optional<MatchCommandError> validate(const MatchState& state,
                                                        const MovePlayerCommand& command) {
  if (!findPlayerIndex(state, command.playerId)) {
    return unknownPlayer(command.playerId);
  }
  if (!command.target.isFinite()) {
    return MatchCommandError{.code = MatchCommandErrorCode::kNonFiniteTarget,
                             .message = "move command for player " +
                                        std::to_string(command.playerId.value()) +
                                        " has a non-finite target"};
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<MatchCommandError> validate(const MatchState& state,
                                                        const GiveBallCommand& command) {
  if (!findPlayerIndex(state, command.playerId)) {
    return unknownPlayer(command.playerId);
  }
  return std::nullopt;
}

// Validation needs only the squad, which no step changes, so a command valid
// when scheduled is still valid when applied.
[[nodiscard]] std::optional<MatchCommandError> validate(const MatchState& state,
                                                        const MatchCommand& command) {
  return std::visit([&state](const auto& alternative) { return validate(state, alternative); },
                    command);
}

// Commands were validated when scheduled, so the player exists.
void apply(const MovePlayerCommand& command, const MatchState& state,
           const SimCore::SimTick /*tick*/, MatchStateWriter& writer) {
  if (const auto index = findPlayerIndex(state, command.playerId)) {
    writer.setPlayerTarget(*index, state.pitch().clamp(command.target));
  }
}

void apply(const GiveBallCommand& command, const MatchState& /*state*/, const SimCore::SimTick tick,
           MatchStateWriter& writer) {
  writer.setBallOwner(command.playerId);
  writer.setBallLastTouch(BallTouch{.playerId = command.playerId, .tick = tick});
}

}  // namespace

RandomNumberGenerator& MatchStepContext::random(const RandomNumberGeneratorDomain domain) const {
  return random_->at(static_cast<std::size_t>(domain));
}

MatchSimulation::MatchSimulation(MatchSimulationSpec spec)
    : systems_(validatedSystems(std::move(spec.systems))),
      clock_(SimCore::SimClock::withTicksPerSecond(spec.ticksPerSecond)),
      random_(makeRandomStreams(spec.seed)),
      previous_(spec.initialState),
      current_(spec.initialState),
      commanded_(spec.initialState),
      next_(std::move(spec.initialState)) {
  for (std::size_t index = 0; const ScheduledCommand& command : spec.commands) {
    if (const auto scheduled = schedule(command); !scheduled) {
      throw std::invalid_argument("MatchSimulation: command at index " + std::to_string(index) +
                                  " is invalid: " + scheduled.error().message);
    }
    ++index;
  }
}

std::expected<void, MatchCommandError> MatchSimulation::schedule(const ScheduledCommand& command) {
  if (command.tick < clock_.tick()) {
    return std::unexpected(
        MatchCommandError{.code = MatchCommandErrorCode::kTickInPast,
                          .message = "command for tick " + std::to_string(command.tick.value()) +
                                     " arrives at tick " + std::to_string(clock_.tick().value())});
  }
  if (auto error = validate(current_, command.command)) {
    return std::unexpected(*std::move(error));
  }
  // After every queued command of the same tick: scheduling order is
  // execution order within a tick.
  const auto queued = commands_.begin() + static_cast<std::ptrdiff_t>(appliedCount_);
  const auto position = std::upper_bound(
      queued, commands_.end(), command.tick,
      [](const SimCore::SimTick tick, const ScheduledCommand& other) { return tick < other.tick; });
  commands_.insert(position, command);
  return {};
}

std::span<const ScheduledCommand> MatchSimulation::appliedCommands() const noexcept {
  return std::span(commands_).first(appliedCount_);
}

const MatchState& MatchSimulation::applyDueCommands() {
  const auto isDueNow = [this](const std::size_t index) {
    return index < commands_.size() && commands_[index].tick == clock_.tick();
  };
  if (!isDueNow(appliedCount_)) {
    return current_;
  }
  commanded_ = current_;
  MatchStateWriter writer(commanded_);
  for (std::size_t index = appliedCount_; isDueNow(index); ++index) {
    std::visit(
        [this, &writer](const auto& command) { apply(command, commanded_, clock_.tick(), writer); },
        commands_[index].command);
  }
  return commanded_;
}

std::expected<SimCore::SimTick, MatchStepError> MatchSimulation::step() {
  if (failure_) {
    return std::unexpected(*failure_);
  }

  const MatchState& current = applyDueCommands();
  // Copy-assigning a state of the same squad reuses next_'s storage.
  next_ = current;

  MatchStateWriter writer(next_);
  const MatchStepContext context(clock_.tick(), clock_.secondsPerTick(), random_);
  for (const MatchSystem& system : systems_) {
    if (!isDue(system, context.tick())) {
      continue;
    }
    try {
      system.update(context, current, writer);
    } catch (...) {
      failure_ = MatchStepError{.tick = context.tick(), .systemName = system.name, .errors = {}};
      throw;
    }
    // Checked after every system rather than once per step, so the error
    // names the system that wrote the value. Allocates nothing when clean.
    if (std::vector<MatchStateError> errors = findNonFiniteValues(next_); !errors.empty()) {
      failure_ = MatchStepError{
          .tick = context.tick(), .systemName = system.name, .errors = std::move(errors)};
      return std::unexpected(*failure_);
    }
  }

  // Advance before swapping: a tick overflow throws while current_ and
  // previous_ still hold the last completed step.
  const SimCore::SimTick tick = clock_.advance();
  while (appliedCount_ < commands_.size() && commands_[appliedCount_].tick < tick) {
    ++appliedCount_;
  }
  std::swap(previous_, current_);
  std::swap(current_, next_);
  return tick;
}

}  // namespace ElyverseFootball::SimMatch
