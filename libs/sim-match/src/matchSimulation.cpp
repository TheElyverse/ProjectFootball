#include "matchSimulation.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <functional>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "tacticHash.hpp"

namespace ElyverseFootball::SimMatch {
namespace {

using SimCore::RandomNumberGenerator;
using SimCore::RandomNumberGeneratorDomain;

// MatchRandomStreams is indexed by domain value, so it must list every domain
// in declaration order. A domain added after kCognition fails this assertion instead
// of silently sharing a stream.
static_assert(static_cast<std::size_t>(RandomNumberGeneratorDomain::kCognition) + 1 ==
              std::tuple_size_v<MatchRandomStreams>);

[[nodiscard]] MatchRandomStreams makeRandomStreams(const std::uint64_t seed) noexcept {
  return {
      RandomNumberGenerator(SimCore::deriveSeed(seed, RandomNumberGeneratorDomain::kExecution)),
      RandomNumberGenerator(SimCore::deriveSeed(seed, RandomNumberGeneratorDomain::kInjuries)),
      RandomNumberGenerator(SimCore::deriveSeed(seed, RandomNumberGeneratorDomain::kGeneration)),
      RandomNumberGenerator(SimCore::deriveSeed(seed, RandomNumberGeneratorDomain::kMarket)),
      RandomNumberGenerator(SimCore::deriveSeed(seed, RandomNumberGeneratorDomain::kAi)),
      RandomNumberGenerator(SimCore::deriveSeed(seed, RandomNumberGeneratorDomain::kCognition)),
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

// The squad a command is checked against: the players in the state, and those
// substitutions queued or waiting will bring on, for their side
// (docs/match-engine.md#substitutions) -- a replay schedules every command
// before the first step, a command for a substitute included.
struct Squad {
  const MatchState* state;
  std::vector<std::pair<SimCore::PlayerId, TeamSide>> comingOn;

  [[nodiscard]] bool knows(const SimCore::PlayerId playerId) const {
    return findPlayerIndex(*state, playerId) ||
           std::ranges::any_of(
               comingOn, [playerId](const auto& arrival) { return arrival.first == playerId; });
  }
  // Whether the player is on the pitch for the side, or will come on for it.
  [[nodiscard]] bool plays(const SimCore::PlayerId playerId, const TeamSide side) const {
    if (const auto index = findPlayerIndex(*state, playerId)) {
      return state->players()[*index].side == side && isOnPitch(state->players()[*index]);
    }
    return std::ranges::find(comingOn, std::pair{playerId, side}) != comingOn.end();
  }
};

[[nodiscard]] std::optional<MatchCommandError> validate(const Squad& squad,
                                                        const MovePlayerCommand& command) {
  if (!squad.knows(command.playerId)) {
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

[[nodiscard]] std::optional<MatchCommandError> validate(const Squad& squad,
                                                        const GiveBallCommand& command) {
  if (!squad.knows(command.playerId)) {
    return unknownPlayer(command.playerId);
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<MatchCommandError> validate(const Squad& squad,
                                                        const PassCommand& command) {
  if (!squad.knows(command.playerId)) {
    return unknownPlayer(command.playerId);
  }
  if (command.receiver && !squad.knows(*command.receiver)) {
    return unknownPlayer(*command.receiver);
  }
  if (!command.target.isFinite()) {
    return MatchCommandError{.code = MatchCommandErrorCode::kNonFiniteTarget,
                             .message = "pass command for player " +
                                        std::to_string(command.playerId.value()) +
                                        " has a non-finite target"};
  }
  // Written so that NaN fails too.
  const bool positiveFiniteSpeed =
      command.speed > 0.0 && command.speed <= std::numeric_limits<double>::max();
  if (!positiveFiniteSpeed) {
    return MatchCommandError{.code = MatchCommandErrorCode::kInvalidPassSpeed,
                             .message = "pass command for player " +
                                        std::to_string(command.playerId.value()) +
                                        " needs a positive, finite speed"};
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<MatchCommandError> validate(const Squad& squad,
                                                        const ChangeTacticCommand& command) {
  const MatchState& state = *squad.state;
  if (std::cmp_not_equal(command.tactic.slots().size(), state.playersPerSide())) {
    return MatchCommandError{
        .code = MatchCommandErrorCode::kTacticDoesNotFitSquad,
        .message = std::format("{}'s new tactic '{}' has {} slots for {} players",
                               teamSideName(command.side), command.tactic.name(),
                               command.tactic.slots().size(), state.playersPerSide())};
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<MatchCommandError> validate(const Squad& squad,
                                                        const SubstituteCommand& command) {
  const MatchState& state = *squad.state;
  if (!squad.plays(command.off, command.side)) {
    return MatchCommandError{
        .code = MatchCommandErrorCode::kInvalidSubstitution,
        .message = std::format("player {} is not on the pitch for {} to come off",
                               command.off.value(), teamSideName(command.side))};
  }
  if (!command.on.isValid() || findPlayerIndex(state, command.on) || state.cameOff(command.on)) {
    return MatchCommandError{
        .code = MatchCommandErrorCode::kInvalidSubstitution,
        .message = std::format("player {} cannot come on: his id is invalid or used, or he "
                               "came off before",
                               command.on.value())};
  }
  if (!areValid(command.attributes)) {
    return MatchCommandError{
        .code = MatchCommandErrorCode::kInvalidSubstitution,
        .message =
            std::format("player {} cannot come on with invalid attributes", command.on.value())};
  }
  return std::nullopt;
}

// Validated against the squad when scheduled. A substitution may change it
// before a command is applied: a command for a player not on the pitch then
// does nothing, and a substitution the stoppage rules out is refused there.
[[nodiscard]] std::optional<MatchCommandError> validate(const Squad& squad,
                                                        const MatchCommand& command) {
  return std::visit([&squad](const auto& alternative) { return validate(squad, alternative); },
                    command);
}

// A player who has come off since the command was scheduled is no longer
// there: nothing to do.
void apply(const MovePlayerCommand& command, const MatchState& state,
           const SimCore::SimTick /*tick*/, MatchStateWriter& writer,
           std::vector<MatchEvent>& /*events*/) {
  if (const auto index = findPlayerIndex(state, command.playerId)) {
    writer.setPlayerTarget(*index, state.pitch().clamp(command.target));
  }
}

void apply(const GiveBallCommand& command, const MatchState& state, const SimCore::SimTick tick,
           MatchStateWriter& writer, std::vector<MatchEvent>& events) {
  if (!findPlayerIndex(state, command.playerId)) {
    return;
  }
  if (state.ball().owner != command.playerId) {
    events.emplace_back(PossessionChanged{
        .tick = tick, .previousOwner = state.ball().owner, .newOwner = command.playerId});
  }
  writer.setBallOwner(command.playerId);
  writer.setBallLastTouch(BallTouch{.playerId = command.playerId, .tick = tick});
}

void apply(const PassCommand& command, const MatchState& state, const SimCore::SimTick /*tick*/,
           MatchStateWriter& writer, std::vector<MatchEvent>& /*events*/) {
  const auto passer = findPlayerIndex(state, command.playerId);
  if (!passer || (command.receiver && !findPlayerIndex(state, *command.receiver))) {
    return;
  }
  writer.setPendingAction(*passer, PassIntent{.passer = command.playerId,
                                              .target = command.target,
                                              .speed = command.speed,
                                              .receiver = command.receiver});
}

// The coaching system passes it on to the players (coaching.hpp).
void apply(const ChangeTacticCommand& command, const MatchState& /*state*/,
           const SimCore::SimTick tick, MatchStateWriter& writer,
           std::vector<MatchEvent>& /*events*/) {
  writer.setPendingTactic(command.side, PendingTactic{.tactic = command.tactic, .given = tick});
}

// The coaching system makes it at the next stoppage (coaching.hpp).
void apply(const SubstituteCommand& command, const MatchState& state, const SimCore::SimTick tick,
           MatchStateWriter& writer, std::vector<MatchEvent>& /*events*/) {
  std::vector<Substitution> pending(state.pendingSubstitutions().begin(),
                                    state.pendingSubstitutions().end());
  pending.push_back({.side = command.side,
                     .off = command.off,
                     .on = command.on,
                     .attributes = command.attributes,
                     .ordered = tick});
  writer.setPendingSubstitutions(std::move(pending));
}

}  // namespace

RandomNumberGenerator& MatchStepContext::random(const RandomNumberGeneratorDomain domain) const {
  return random_->at(static_cast<std::size_t>(domain));
}

void MatchStepContext::record(const MatchEvent& event) const {
  events_->push_back(event);
}

bool DiagnosticsFilter::includesTick(const SimCore::SimTick tick) const noexcept {
  return (!from || tick >= *from) && (!to || tick <= *to);
}

bool DiagnosticsFilter::includesPlayer(const SimCore::PlayerId player) const noexcept {
  return players.empty() || std::ranges::find(players, player) != players.end();
}

void MatchStepContext::diagnose(DecisionDiagnostic diagnostic) const {
  if (collectsDiagnostics(diagnostic.player)) {
    diagnostics_.decisions->push_back(std::move(diagnostic));
  }
}

void MatchStepContext::diagnose(ActionDiagnostic diagnostic) const {
  if (collectsDiagnostics(diagnostic.player)) {
    diagnostics_.actions->push_back(std::move(diagnostic));
  }
}

void MatchStepContext::diagnose(DuelDiagnostic diagnostic) const {
  if (collectsDiagnostics(diagnostic.player)) {
    diagnostics_.duels->push_back(std::move(diagnostic));
  }
}

void MatchStepContext::diagnose(DiveDiagnostic diagnostic) const {
  if (collectsDiagnostics(diagnostic.player)) {
    diagnostics_.dives->push_back(std::move(diagnostic));
  }
}

void MatchStepContext::diagnose(SweepDiagnostic diagnostic) const {
  if (collectsDiagnostics(diagnostic.player)) {
    diagnostics_.sweeps->push_back(diagnostic);
  }
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
  Squad squad{.state = &current_, .comingOn = {}};
  const auto arrive = [&squad](const TeamSide side, const SimCore::PlayerId newcomer) {
    squad.comingOn.emplace_back(newcomer, side);
  };
  for (const Substitution& waiting : current_.pendingSubstitutions()) {
    arrive(waiting.side, waiting.on);
  }
  for (std::size_t index = appliedCount_; index < commands_.size(); ++index) {
    if (const auto* queued = std::get_if<SubstituteCommand>(&commands_[index].command)) {
      arrive(queued->side, queued->on);
    }
  }
  if (auto error = validate(squad, command.command)) {
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
        [this, &writer](const auto& command) {
          apply(command, commanded_, clock_.tick(), writer, stepEvents_);
        },
        commands_[index].command);
  }
  return commanded_;
}

std::expected<SimCore::SimTick, MatchStepError> MatchSimulation::step() {
  if (failure_) {
    return std::unexpected(*failure_);
  }

  // clear() keeps the buffers' capacity: steady-state steps allocate nothing.
  stepEvents_.clear();
  stepDiagnostics_.clear();
  stepActionDiagnostics_.clear();
  stepSweepDiagnostics_.clear();
  stepDuelDiagnostics_.clear();
  stepDiveDiagnostics_.clear();
  const MatchState& current = applyDueCommands();
  // Copy-assigning a state of the same squad reuses next_'s storage.
  next_ = current;

  MatchStateWriter writer(next_);
  const MatchStepContext context(
      clock_.tick(), clock_.secondsPerTick(), random_, stepEvents_,
      collectDiagnostics_ && diagnosticsFilter_.includesTick(clock_.tick())
          ? MatchStepContext::Diagnostics{.decisions = &stepDiagnostics_,
                                          .actions = &stepActionDiagnostics_,
                                          .sweeps = &stepSweepDiagnostics_,
                                          .duels = &stepDuelDiagnostics_,
                                          .dives = &stepDiveDiagnostics_,
                                          .filter = &diagnosticsFilter_}
          : MatchStepContext::Diagnostics{});
  // After the final whistle nothing moves (docs/match-clock.md#full-time).
  const bool over = current.clock().period == MatchPeriod::kFullTime;
  for (const MatchSystem& system : systems_) {
    if (over || !isDue(system, context.tick())) {
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
  std::swap(events_, stepEvents_);
  std::swap(diagnostics_, stepDiagnostics_);
  std::swap(actionDiagnostics_, stepActionDiagnostics_);
  std::swap(sweepDiagnostics_, stepSweepDiagnostics_);
  std::swap(duelDiagnostics_, stepDuelDiagnostics_);
  std::swap(diveDiagnostics_, stepDiveDiagnostics_);
  return tick;
}

}  // namespace ElyverseFootball::SimMatch
