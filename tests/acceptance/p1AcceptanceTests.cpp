// Milestone P1 acceptance: the passing pipeline -- perception, candidates,
// the seeded decision, execution, flight and reception -- in three scenarios
// with no scripted action but the kickoff possession. Checks what each
// scenario is for, that possession stays consistent on every tick, that runs
// and replay playback agree on state and event hashes, and pins the hashes.

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "ballMovement.hpp"
#include "ids.hpp"
#include "matchEvents.hpp"
#include "matchSetup.hpp"
#include "matchSimulation.hpp"
#include "matchState.hpp"
#include "matchStateHash.hpp"
#include "replay.hpp"
#include "replayJson.hpp"
#include "scenarios.hpp"

using ElyverseFootball::SimCore::PlayerId;
using ElyverseFootball::SimCore::SimTick;
using ElyverseFootball::SimMatch::DecisionDiagnostic;
using ElyverseFootball::SimMatch::DecisionOutcome;
using ElyverseFootball::SimMatch::findNonFiniteValues;
using ElyverseFootball::SimMatch::findPlayerIndex;
using ElyverseFootball::SimMatch::findScenario;
using ElyverseFootball::SimMatch::hashMatchState;
using ElyverseFootball::SimMatch::LooseBallRecovered;
using ElyverseFootball::SimMatch::MatchEvent;
using ElyverseFootball::SimMatch::MatchSetup;
using ElyverseFootball::SimMatch::MatchSimulation;
using ElyverseFootball::SimMatch::MatchState;
using ElyverseFootball::SimMatch::PassAttempted;
using ElyverseFootball::SimMatch::PassIntercepted;
using ElyverseFootball::SimMatch::PassReceived;
using ElyverseFootball::SimMatch::PossessionChanged;
using ElyverseFootball::SimMatch::startMatch;
using ElyverseFootball::SimMatch::TeamSide;
using ElyverseFootball::SimReplay::kDefaultCheckpointIntervalTicks;
using ElyverseFootball::SimReplay::parseReplayJson;
using ElyverseFootball::SimReplay::playReplay;
using ElyverseFootball::SimReplay::recordMatch;
using ElyverseFootball::SimReplay::Replay;
using ElyverseFootball::SimReplay::toReplayJson;

namespace {

// The seed the documentation and the pinned hashes use.
constexpr std::uint64_t kSeed = 7;
// Twenty seconds at 30 Hz: a chain of passes, or long enough to be sure none
// comes.
constexpr SimTick::ValueType kTicks = 600;
// The seeds of the statistical checks.
constexpr std::uint64_t kSeedCount = 50;

constexpr PlayerId kCarrier{1};

[[nodiscard]] MatchSetup scenarioSetup(const std::string_view name, const std::uint64_t seed) {
  const auto* scenario = findScenario(name);
  REQUIRE(scenario != nullptr);
  auto setup = scenario->make(seed);
  REQUIRE(setup.has_value());
  return *std::move(setup);
}

[[nodiscard]] TeamSide sideOf(const MatchState& state, const PlayerId playerId) {
  const auto index = findPlayerIndex(state, playerId);
  REQUIRE(index.has_value());
  return state.players()[index.value_or(0)].side;
}

// Possession invariants between two consecutive states and the events of the
// step between them:
// - a controlled ball is at its owner's feet, kept on the pitch, and moves
//   with him,
// - a pending pass belongs to the player on the ball,
// - possession changes only with a PossessionChanged event, and the events
//   chain from the old owner to the new one,
// - a pass is played by the player on the ball, received by a teammate and
//   intercepted by an opponent.
void checkPossession(const MatchSetup& setup, const MatchState& before, const MatchState& after,
                     const std::vector<MatchEvent>& events) {
  const auto& ball = after.ball();
  REQUIRE(findNonFiniteValues(after).empty());
  REQUIRE(after.pitch().contains(ball.position));
  if (ball.owner) {
    const auto index = findPlayerIndex(after, *ball.owner);
    REQUIRE(index.has_value());
    const auto& owner = after.players()[index.value_or(0)];
    REQUIRE(ball.position == ElyverseFootball::SimMatch::carriedBallPosition(
                                 owner, setup.config.ball, after.pitch()));
    REQUIRE(ball.velocity == owner.velocity);
  }
  if (const auto& pass = after.pendingPass()) {
    REQUIRE(ball.owner == pass->passer);
  }

  std::optional<PlayerId> owner = before.ball().owner;
  for (const MatchEvent& event : events) {
    if (const auto* changed = std::get_if<PossessionChanged>(&event)) {
      REQUIRE(changed->previousOwner == owner);
      REQUIRE(changed->newOwner != owner);
      owner = changed->newOwner;
    } else if (const auto* attempted = std::get_if<PassAttempted>(&event)) {
      REQUIRE(before.ball().owner == attempted->passer);
    } else if (const auto* received = std::get_if<PassReceived>(&event)) {
      REQUIRE(sideOf(after, received->receiver) == sideOf(after, received->passer));
    } else if (const auto* intercepted = std::get_if<PassIntercepted>(&event)) {
      REQUIRE(sideOf(after, intercepted->interceptor) != sideOf(after, intercepted->passer));
    }
  }
  REQUIRE(owner == ball.owner);
}

// A scenario run to kTicks with decision diagnostics, every step checked
// against the possession invariants.
struct CheckedRun {
  std::vector<MatchEvent> events;
  std::vector<DecisionDiagnostic> decisions;
  std::vector<std::optional<PlayerId>> owners;
  std::uint64_t finalStateHash = 0;
};

[[nodiscard]] CheckedRun runChecked(const std::string_view name, const std::uint64_t seed) {
  const MatchSetup setup = scenarioSetup(name, seed);
  MatchSimulation simulation = startMatch(setup);
  simulation.setCollectDiagnostics(true);
  CheckedRun run;
  while (simulation.tick() < SimTick(kTicks)) {
    const MatchState before = simulation.state();
    REQUIRE(simulation.step().has_value());
    const std::vector<MatchEvent> events(simulation.events().begin(), simulation.events().end());
    CAPTURE(name, seed, simulation.tick().value());
    checkPossession(setup, before, simulation.state(), events);
    run.events.insert(run.events.end(), events.begin(), events.end());
    run.decisions.insert(run.decisions.end(), simulation.diagnostics().begin(),
                         simulation.diagnostics().end());
    run.owners.push_back(simulation.state().ball().owner);
  }
  run.finalStateHash = hashMatchState(simulation.state());
  return run;
}

[[nodiscard]] Replay record(const std::string_view name, const std::uint64_t seed = kSeed) {
  auto replay =
      recordMatch(scenarioSetup(name, seed), SimTick(kTicks), kDefaultCheckpointIntervalTicks, "");
  REQUIRE(replay.has_value());
  return *std::move(replay);
}

// How a pass ended: the claim event after it, or none while the ball rolls.
[[nodiscard]] bool isClaim(const MatchEvent& event) {
  return std::holds_alternative<PassReceived>(event) ||
         std::holds_alternative<PassIntercepted>(event) ||
         std::holds_alternative<LooseBallRecovered>(event);
}

// The claim events in order: receptions, interceptions, recoveries.
[[nodiscard]] std::vector<MatchEvent> claims(const std::vector<MatchEvent>& events) {
  std::vector<MatchEvent> result;
  for (const MatchEvent& event : events) {
    if (isClaim(event)) {
      result.push_back(event);
    }
  }
  return result;
}

// Completed passes before the first pass that was not received.
[[nodiscard]] std::size_t leadingReceptions(const std::vector<MatchEvent>& events) {
  std::size_t count = 0;
  for (const MatchEvent& event : claims(events)) {
    if (!std::holds_alternative<PassReceived>(event)) {
      break;
    }
    ++count;
  }
  return count;
}

constexpr std::array<std::string_view, 3> kScenarioNames{"pass-chain", "intercepted-pass",
                                                         "no-passing-option"};

}  // namespace

TEST_CASE("P1: the pass chain keeps the ball in the home side", "[acceptance][p1]") {
  const CheckedRun run = runChecked("pass-chain", kSeed);

  // At least three passes in a row, every one decided by the player on the
  // ball, played, and received by a home teammate.
  REQUIRE(leadingReceptions(run.events) >= 3);
  std::size_t passes = 0;
  for (const MatchEvent& event : run.events) {
    if (const auto* attempted = std::get_if<PassAttempted>(&event)) {
      ++passes;
      REQUIRE(attempted->intendedReceiver.has_value());
    }
  }
  REQUIRE(passes >= 3);
  std::size_t passedDecisions = 0;
  for (const DecisionDiagnostic& decision : run.decisions) {
    if (decision.outcome == DecisionOutcome::kPassed) {
      ++passedDecisions;
      REQUIRE(decision.chosen.has_value());
      REQUIRE(decision.candidates.at(decision.chosen.value_or(0)).isValid());
    }
  }
  REQUIRE(passedDecisions == passes);
}

TEST_CASE("P1: pass chains hold over many seeds", "[acceptance][p1]") {
  for (std::uint64_t seed = 1; seed <= kSeedCount; ++seed) {
    CAPTURE(seed);
    REQUIRE(leadingReceptions(runChecked("pass-chain", seed).events) >= 3);
  }
}

TEST_CASE("P1: the risky pass is intercepted", "[acceptance][p1]") {
  const CheckedRun run = runChecked("intercepted-pass", kSeed);

  // Player 1's first decision is the pass to player 2, his only option: valid,
  // but with a high interception risk.
  REQUIRE_FALSE(run.decisions.empty());
  const DecisionDiagnostic& decision = run.decisions.front();
  REQUIRE(decision.player == kCarrier);
  REQUIRE(decision.outcome == DecisionOutcome::kPassed);
  REQUIRE(decision.candidates.size() == 1);
  REQUIRE(decision.candidates.front().receiver == PlayerId(2));
  REQUIRE(decision.candidates.front().isValid());
  REQUIRE(decision.candidates.front().interceptionRisk > 0.5);

  // Away player 8 takes it, and with it the ball.
  const std::vector<MatchEvent> claimed = claims(run.events);
  REQUIRE_FALSE(claimed.empty());
  const auto* intercepted = std::get_if<PassIntercepted>(claimed.data());
  REQUIRE(intercepted != nullptr);
  REQUIRE(intercepted->interceptor == PlayerId(8));
  REQUIRE(intercepted->passer == kCarrier);
}

TEST_CASE("P1: risky passes are mostly intercepted over many seeds", "[acceptance][p1]") {
  // A statistical guardrail, not an exact value: execution error decides
  // single runs, but the risk estimate must mean what it says.
  std::uint64_t intercepted = 0;
  for (std::uint64_t seed = 1; seed <= kSeedCount; ++seed) {
    const std::vector<MatchEvent> claimed = claims(runChecked("intercepted-pass", seed).events);
    REQUIRE_FALSE(claimed.empty());
    if (std::holds_alternative<PassIntercepted>(claimed.front())) {
      ++intercepted;
    }
  }
  CAPTURE(intercepted);
  REQUIRE(intercepted * 10 >= kSeedCount * 7);
}

TEST_CASE("P1: a player without a visible option keeps the ball", "[acceptance][p1]") {
  const CheckedRun run = runChecked("no-passing-option", kSeed);
  const MatchState initial = scenarioSetup("no-passing-option", kSeed).initialState;

  for (const MatchEvent& event : run.events) {
    REQUIRE_FALSE(std::holds_alternative<PassAttempted>(event));
  }
  for (const auto& owner : run.owners) {
    REQUIRE(owner == kCarrier);
  }
  // He decides regularly, sees players and the ball, but no teammate.
  REQUIRE(run.decisions.size() >= 10);
  for (const DecisionDiagnostic& decision : run.decisions) {
    REQUIRE(decision.player == kCarrier);
    REQUIRE(decision.outcome == DecisionOutcome::kNoValidOption);
    REQUIRE_FALSE(decision.chosen.has_value());
    REQUIRE(decision.candidates.empty());
    for (const auto& observation : decision.observations) {
      if (!observation.entity.isBall()) {
        REQUIRE(sideOf(initial, observation.entity.playerId()) == TeamSide::kAway);
      }
    }
  }
  REQUIRE_FALSE(run.decisions.back().observations.empty());
}

TEST_CASE("P1: runs and replay playback agree on state and event hashes", "[acceptance][p1]") {
  for (const std::string_view name : kScenarioNames) {
    CAPTURE(name);
    const Replay first = record(name);
    const Replay second = record(name);
    REQUIRE(first.checkpoints == second.checkpoints);

    // The file holds everything P1 needs: configuration, possession, commands.
    const auto parsed = parseReplayJson(toReplayJson(first));
    REQUIRE(parsed.has_value());
    REQUIRE(*parsed == first);
    const auto playback = playReplay(*parsed);
    REQUIRE(playback.has_value());
    REQUIRE(playback->checkpointsVerified == first.checkpoints.size());
    REQUIRE(playback->finalStateHash == first.checkpoints.back().stateHash);
    REQUIRE(playback->finalEventHash == first.checkpoints.back().eventHash);

    // Collecting diagnostics, as the checked runs and the viewer do, changes
    // nothing.
    REQUIRE(runChecked(name, kSeed).finalStateHash == first.checkpoints.back().stateHash);
  }
}

TEST_CASE("P1: the final hashes are pinned", "[acceptance][p1]") {
  // Identical on every platform and compiler CI builds with. They change only
  // with the simulation's behavior, a scenario or the state layout -- each of
  // which must bump the core version or document the scenario change, and
  // update these values deliberately.
  struct Pinned {
    std::string_view name;
    std::uint64_t stateHash;
    std::uint64_t eventHash;
  };
  const std::array<Pinned, 3> pinned{{
      {.name = "pass-chain",
       .stateHash = 0x3e7c6370e619b9f3ULL,
       .eventHash = 0x7ff2a1035691d2b1ULL},
      {.name = "intercepted-pass",
       .stateHash = 0x65bb62d8c02ed665ULL,
       .eventHash = 0x22fa8148924862c7ULL},
      {.name = "no-passing-option",
       .stateHash = 0xe3d94bf22f2c093eULL,
       .eventHash = 0xf89a1b4f0fd812ebULL},
  }};
  for (const Pinned& expected : pinned) {
    CAPTURE(expected.name);
    const Replay replay = record(expected.name);
    CHECK(replay.checkpoints.back().stateHash == expected.stateHash);
    CHECK(replay.checkpoints.back().eventHash == expected.eventHash);
  }
}
