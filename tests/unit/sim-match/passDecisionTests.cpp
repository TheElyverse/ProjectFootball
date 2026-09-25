#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

#include "choicePolicy.hpp"
#include "kickoffScenario.hpp"
#include "matchCommand.hpp"
#include "matchSetup.hpp"
#include "matchSimulation.hpp"
#include "matchState.hpp"
#include "matchStateHash.hpp"
#include "passCandidates.hpp"
#include "passDecision.hpp"
#include "random.hpp"
#include "vec2.hpp"

using ElyverseFootball::SimCore::PlayerId;
using ElyverseFootball::SimCore::RandomNumberGenerator;
using ElyverseFootball::SimCore::SimTick;
using ElyverseFootball::SimCore::Vec2;
using ElyverseFootball::SimMatch::choosePass;
using ElyverseFootball::SimMatch::DecisionConfig;
using ElyverseFootball::SimMatch::GiveBallCommand;
using ElyverseFootball::SimMatch::hashMatchState;
using ElyverseFootball::SimMatch::makePassDecisionSystem;
using ElyverseFootball::SimMatch::makeSevenASideKickoff;
using ElyverseFootball::SimMatch::MatchConfig;
using ElyverseFootball::SimMatch::MatchSetup;
using ElyverseFootball::SimMatch::MatchSimulation;
using ElyverseFootball::SimMatch::MatchState;
using ElyverseFootball::SimMatch::MatchStateSpec;
using ElyverseFootball::SimMatch::PassCandidate;
using ElyverseFootball::SimMatch::PassCandidateRules;
using ElyverseFootball::SimMatch::PassRejection;
using ElyverseFootball::SimMatch::PassScoringConfig;
using ElyverseFootball::SimMatch::Pitch;
using ElyverseFootball::SimMatch::PlayerMatchState;
using ElyverseFootball::SimMatch::startMatch;
using ElyverseFootball::SimMatch::TeamSide;

namespace {

template <typename Function>
[[nodiscard]] bool throwsInvalidArgument(const Function& function) {
  try {
    function();
  } catch (const std::invalid_argument&) {
    return true;
  }
  return false;
}

[[nodiscard]] PassCandidate option(const PlayerId receiver, const double utility,
                                   const PassRejection rejection = PassRejection::kValid) {
  PassCandidate candidate;
  candidate.receiver = receiver;
  candidate.utility = utility;
  candidate.rejection = rejection;
  return candidate;
}

// The kickoff fixture with player 4, the center midfielder, on the ball.
[[nodiscard]] MatchSimulation kickoffWithBall(MatchConfig config, const std::uint64_t seed = 42,
                                              const std::uint32_t carrier = 4) {
  auto state = makeSevenASideKickoff(Pitch(60.0, 40.0));
  REQUIRE(state.has_value());
  return startMatch(
      MatchSetup{.initialState = *std::move(state),
                 .config = config,
                 .seed = seed,
                 .commands = {{.tick = SimTick(0),
                               .command = GiveBallCommand{.playerId = PlayerId(carrier)}}}});
}

}  // namespace

TEST_CASE("Without a valid candidate nothing is chosen and nothing drawn", "[passDecision]") {
  RandomNumberGenerator random(3);
  const std::vector candidates{option(PlayerId(2), 1.0, PassRejection::kTooFar),
                               option(PlayerId(3), 0.5, PassRejection::kUnlikely)};

  REQUIRE_FALSE(choosePass(candidates, 0.15, random).has_value());
  REQUIRE(random.nextU64() == RandomNumberGenerator(3).nextU64());
}

TEST_CASE("The softmax prefers better options without always taking them", "[passDecision]") {
  // exp(0 / 0.15) against exp(-0.1 / 0.15): the first is chosen with
  // probability 1 / (1 + e^(-2/3)) = 0.661. The invalid third never.
  const std::vector candidates{option(PlayerId(2), 1.0), option(PlayerId(3), 0.9),
                               option(PlayerId(4), 5.0, PassRejection::kUnlikely)};
  std::vector<int> counts(3, 0);
  constexpr int kDraws = 4000;
  for (std::uint64_t seed = 0; seed < kDraws; ++seed) {
    RandomNumberGenerator random(seed);
    const auto chosen = choosePass(candidates, 0.15, random);
    REQUIRE(chosen.has_value());
    ++counts.at(chosen.value_or(2));
  }

  const double first = static_cast<double>(counts.at(0)) / kDraws;
  CAPTURE(first);
  REQUIRE(first > 0.63);
  REQUIRE(first < 0.69);
  REQUIRE(counts.at(2) == 0);
}

TEST_CASE("A low temperature picks the best option", "[passDecision]") {
  const std::vector candidates{option(PlayerId(2), 1.0), option(PlayerId(3), 0.9)};
  for (std::uint64_t seed = 0; seed < 500; ++seed) {
    RandomNumberGenerator random(seed);
    REQUIRE(choosePass(candidates, 0.005, random) == std::optional<std::size_t>(0));
  }
}

TEST_CASE("The decision system rejects invalid scoring", "[passDecision]") {
  const auto rejects = [](const auto& change) {
    DecisionConfig config;
    change(config.scoring);
    return throwsInvalidArgument(
        [&config] { (void)makePassDecisionSystem(config, PassCandidateRules{}); });
  };
  REQUIRE_FALSE(rejects([](PassScoringConfig&) {}));
  REQUIRE(rejects([](PassScoringConfig& scoring) { scoring.pressureRadius = 0.0; }));
  REQUIRE(rejects([](PassScoringConfig& scoring) { scoring.interceptionMarginSeconds = 0.0; }));
  REQUIRE(rejects([](PassScoringConfig& scoring) { scoring.minConfidence = 1.5; }));
  REQUIRE(rejects([](PassScoringConfig& scoring) { scoring.minCompletion = -0.1; }));
  REQUIRE(rejects([](PassScoringConfig& scoring) { scoring.maxPassDistance = 1.0; }));
  REQUIRE(rejects([](PassScoringConfig& scoring) { scoring.riskWeight = 1e300; }));
  REQUIRE(rejects([](PassScoringConfig& scoring) {
    scoring.progressionWeight = std::numeric_limits<double>::quiet_NaN();
  }));
}

TEST_CASE("Decisions run at their configured frequency", "[passDecision]") {
  MatchConfig config;
  config.decisions.intervalTicks = 9;
  config.decisions.minHoldSeconds = 0.0;
  MatchSimulation simulation = kickoffWithBall(config);

  int decisions = 0;
  for (int tick = 0; tick < 300; ++tick) {
    const bool pendingBefore = simulation.state().pendingPass().has_value();
    REQUIRE(simulation.step().has_value());
    if (!pendingBefore && simulation.state().pendingPass()) {
      // Decided in the step from tick `tick`.
      CAPTURE(tick);
      REQUIRE(tick % 9 == 0);
      ++decisions;
    }
  }
  REQUIRE(decisions >= 3);
}

TEST_CASE("Choosing a pass and playing it are separate steps", "[passDecision]") {
  MatchSimulation simulation = kickoffWithBall({});
  for (int tick = 0; tick < 300 && !simulation.state().pendingPass(); ++tick) {
    REQUIRE(simulation.step().has_value());
  }
  REQUIRE(simulation.state().pendingPass().has_value());
  const PlayerId passer =
      simulation.state().pendingPass().value_or(ElyverseFootball::SimMatch::PassIntent{}).passer;

  // Decided, not yet played: the passer still has the ball.
  REQUIRE(simulation.state().ball().owner == passer);

  REQUIRE(simulation.step().has_value());
  REQUIRE_FALSE(simulation.state().ball().owner.has_value());
  REQUIRE(simulation.state()
              .ball()
              .lastTouch.value_or(ElyverseFootball::SimMatch::BallTouch{})
              .playerId == passer);
  REQUIRE_FALSE(simulation.state().pendingPass().has_value());
}

TEST_CASE("A pass is decided only by the player on the ball, once", "[passDecision]") {
  MatchSimulation simulation = kickoffWithBall({});
  int kicks = 0;
  for (int tick = 0; tick < 900; ++tick) {
    REQUIRE(simulation.step().has_value());
    const MatchState& state = simulation.state();
    if (state.pendingPass()) {
      REQUIRE(state.ball().owner ==
              state.pendingPass().value_or(ElyverseFootball::SimMatch::PassIntent{}).passer);
    }
    const bool kicked = !state.ball().owner && simulation.previousState().ball().owner;
    if (kicked) {
      ++kicks;
      // A kick needs a decision in the step before.
      REQUIRE(simulation.previousState().pendingPass().has_value());
    }
  }
  REQUIRE(kicks >= 3);
}

TEST_CASE("A player without a visible teammate keeps the ball", "[passDecision]") {
  // Player 1, the home goalkeeper, looks at his own goal line: every
  // teammate is behind him. Standing still, he keeps facing it.
  auto kickoff = makeSevenASideKickoff(Pitch(60.0, 40.0));
  REQUIRE(kickoff.has_value());
  MatchStateSpec spec{.pitch = kickoff->pitch(),
                      .players = {kickoff->players().begin(), kickoff->players().end()},
                      .ball = kickoff->ball(),
                      .playersPerSide = kickoff->playersPerSide()};
  spec.players.front().facing = {.x = -1.0, .y = 0.0};
  spec.ball.position = {.x = 2.5, .y = 20.0};
  spec.ball.owner = PlayerId(1);
  const auto state = MatchState::create(spec);
  REQUIRE(state.has_value());
  MatchSimulation simulation =
      startMatch({.initialState = *state, .config = {}, .seed = 1, .commands = {}});

  for (int tick = 0; tick < 150; ++tick) {
    REQUIRE(simulation.step().has_value());
    REQUIRE_FALSE(simulation.state().pendingPass().has_value());
    REQUIRE(simulation.state().ball().owner == PlayerId(1));
  }
}

TEST_CASE("The same seed makes the same choices", "[passDecision]") {
  const auto run = [](const std::uint64_t seed) {
    MatchSimulation simulation = kickoffWithBall({}, seed);
    std::vector<std::uint64_t> hashes;
    for (int tick = 0; tick < 600; ++tick) {
      REQUIRE(simulation.step().has_value());
      hashes.push_back(hashMatchState(simulation.state()));
    }
    return hashes;
  };

  REQUIRE(run(7) == run(7));
  // Different seeds diverge once a softmax draw or an execution error
  // differs.
  REQUIRE_FALSE(run(7) == run(8));
}

TEST_CASE("Players string passes together", "[passDecision]") {
  MatchSimulation simulation = kickoffWithBall({});
  int completed = 0;
  std::optional<PlayerId> lastOwner;
  for (int tick = 0; tick < 900; ++tick) {
    REQUIRE(simulation.step().has_value());
    const auto owner = simulation.state().ball().owner;
    if (owner && lastOwner && *owner != *lastOwner) {
      const auto sideOf = [&simulation](const PlayerId playerId) {
        return simulation.state().players()[playerId.value() - 1].side;
      };
      if (sideOf(*owner) == sideOf(*lastOwner)) {
        ++completed;
      }
    }
    if (owner) {
      lastOwner = owner;
    }
  }
  CAPTURE(completed);
  REQUIRE(completed >= 3);
}

TEST_CASE("The seeded softmax chooses among any options", "[passDecision]") {
  using ElyverseFootball::SimMatch::chooseByUtility;
  RandomNumberGenerator random(9);
  RandomNumberGenerator untouched(9);
  // Nothing to choose from: no option and no draw.
  REQUIRE_FALSE(chooseByUtility({}, 0.2, random).has_value());
  REQUIRE(random.nextU64() == untouched.nextU64());

  const std::vector<double> single{0.3};
  REQUIRE(chooseByUtility(single, 0.2, random) == 0);

  // A clearly better option is chosen almost always, the order of options
  // does not matter.
  const std::vector<double> utilities{0.0, 2.0, 0.1};
  std::size_t best = 0;
  for (int draw = 0; draw < 1000; ++draw) {
    best += chooseByUtility(utilities, 0.2, random) == 1 ? 1U : 0U;
  }
  REQUIRE(best > 990);
}
