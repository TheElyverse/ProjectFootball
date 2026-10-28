#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>
#include <variant>
#include <vector>

#include "carrierScan.hpp"
#include "choicePolicy.hpp"
#include "kickoffScenario.hpp"
#include "matchCommand.hpp"
#include "matchEvents.hpp"
#include "matchSetup.hpp"
#include "matchSimulation.hpp"
#include "matchState.hpp"
#include "matchStateHash.hpp"
#include "passCandidates.hpp"
#include "passDecision.hpp"
#include "random.hpp"
#include "scenarios.hpp"
#include "shotCandidates.hpp"
#include "stableHash.hpp"
#include "vec2.hpp"

using ElyverseFootball::SimCore::PlayerId;
using ElyverseFootball::SimCore::RandomNumberGenerator;
using ElyverseFootball::SimCore::SimTick;
using ElyverseFootball::SimCore::StableHasher;
using ElyverseFootball::SimCore::Vec2;
using ElyverseFootball::SimMatch::activeCarry;
using ElyverseFootball::SimMatch::activeScan;
using ElyverseFootball::SimMatch::addEvent;
using ElyverseFootball::SimMatch::CarryCandidate;
using ElyverseFootball::SimMatch::CarryKind;
using ElyverseFootball::SimMatch::choiceTemperature;
using ElyverseFootball::SimMatch::chooseOnBall;
using ElyverseFootball::SimMatch::DecisionConfig;
using ElyverseFootball::SimMatch::DecisionDiagnostic;
using ElyverseFootball::SimMatch::DecisionOutcome;
using ElyverseFootball::SimMatch::findScenario;
using ElyverseFootball::SimMatch::GiveBallCommand;
using ElyverseFootball::SimMatch::hashMatchState;
using ElyverseFootball::SimMatch::kDefaultDecisionMaking;
using ElyverseFootball::SimMatch::lookDirection;
using ElyverseFootball::SimMatch::makePassDecisionSystem;
using ElyverseFootball::SimMatch::makeSevenASideKickoff;
using ElyverseFootball::SimMatch::MatchConfig;
using ElyverseFootball::SimMatch::MatchSetup;
using ElyverseFootball::SimMatch::MatchSimulation;
using ElyverseFootball::SimMatch::MatchState;
using ElyverseFootball::SimMatch::MatchStateSpec;
using ElyverseFootball::SimMatch::misjudgementWidth;
using ElyverseFootball::SimMatch::misjudgeOptions;
using ElyverseFootball::SimMatch::OnBallChoice;
using ElyverseFootball::SimMatch::PassCandidate;
using ElyverseFootball::SimMatch::PassCandidateRules;
using ElyverseFootball::SimMatch::passContributions;
using ElyverseFootball::SimMatch::PassIntent;
using ElyverseFootball::SimMatch::PassRejection;
using ElyverseFootball::SimMatch::PassScoringConfig;
using ElyverseFootball::SimMatch::Pitch;
using ElyverseFootball::SimMatch::PlayerMatchState;
using ElyverseFootball::SimMatch::ShotCandidate;
using ElyverseFootball::SimMatch::shotContributions;
using ElyverseFootball::SimMatch::ShotRejection;
using ElyverseFootball::SimMatch::ShotScoringConfig;
using ElyverseFootball::SimMatch::shotScoringForRisk;
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

[[nodiscard]] CarryCandidate carryOption(const CarryKind kind, const double utility) {
  CarryCandidate candidate;
  candidate.kind = kind;
  candidate.utility = utility;
  return candidate;
}

[[nodiscard]] ShotCandidate shotOption(const double utility,
                                       const ShotRejection rejection = ShotRejection::kValid) {
  ShotCandidate candidate;
  candidate.utility = utility;
  candidate.rejection = rejection;
  return candidate;
}

// The kickoff fixture with player 4, the center midfielder, on the ball.
// A carry this short never reaches the first point a carry is checked at: a
// carrier plans none, not even a turn.
constexpr double kNoCarrySeconds = 0.1;

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

  const std::vector shots{shotOption(2.0, ShotRejection::kTooNarrow)};

  REQUIRE_FALSE(chooseOnBall(candidates, shots, 0.15, random).has_value());
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
    const auto chosen = chooseOnBall(candidates, {}, 0.15, random);
    REQUIRE(chosen.has_value());
    REQUIRE(chosen.value_or(OnBallChoice{}).outcome == DecisionOutcome::kPassed);
    ++counts.at(
        chosen.value_or(OnBallChoice{.outcome = DecisionOutcome::kPassed, .index = 2}).index);
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
    REQUIRE(chooseOnBall(candidates, {}, 0.005, random) ==
            OnBallChoice{.outcome = DecisionOutcome::kPassed, .index = 0});
  }
}

TEST_CASE("The best shot competes with the passes in one softmax", "[passDecision]") {
  // Two passes and a shot of equal utility: each is chosen a third of the
  // time. The second shot, a worse zone of the goal, never competes.
  const std::vector candidates{option(PlayerId(2), 1.0), option(PlayerId(3), 1.0)};
  const std::vector shots{shotOption(1.0), shotOption(0.99)};
  int shotsTaken = 0;
  constexpr int kDraws = 3000;
  for (std::uint64_t seed = 0; seed < kDraws; ++seed) {
    RandomNumberGenerator random(seed);
    const auto chosen = chooseOnBall(candidates, shots, 0.15, random);
    REQUIRE(chosen.has_value());
    const OnBallChoice choice = chosen.value_or(OnBallChoice{});
    if (choice.outcome == DecisionOutcome::kShot) {
      REQUIRE(choice.index == 0);
      ++shotsTaken;
    }
    // One draw per choice, shot or pass.
    RandomNumberGenerator reference(seed);
    (void)reference.nextU64();
    REQUIRE(random.nextU64() == reference.nextU64());
  }

  const double share = static_cast<double>(shotsTaken) / kDraws;
  CAPTURE(share);
  REQUIRE(share > 0.30);
  REQUIRE(share < 0.37);
}

TEST_CASE("A shot is chosen when nothing else is on", "[passDecision]") {
  RandomNumberGenerator random(5);
  const std::vector candidates{option(PlayerId(2), 1.0, PassRejection::kUnlikely)};
  const std::vector shots{shotOption(0.2)};

  REQUIRE(chooseOnBall(candidates, shots, 0.15, random) ==
          OnBallChoice{.outcome = DecisionOutcome::kShot, .index = 0});
}

TEST_CASE("A bolder tactic values goals more and fears losing the ball less", "[passDecision]") {
  const ShotScoringConfig scoring;
  REQUIRE(shotScoringForRisk(scoring, 0.5) == scoring);

  const ShotScoringConfig bold = shotScoringForRisk(scoring, 0.9);
  REQUIRE(bold.goalWeight > scoring.goalWeight);
  REQUIRE(bold.lossWeight < scoring.lossWeight);
  REQUIRE(bold.minGoalChance < scoring.minGoalChance);
  const ShotScoringConfig careful = shotScoringForRisk(scoring, 0.1);
  REQUIRE(careful.goalWeight < scoring.goalWeight);
  REQUIRE(careful.lossWeight > scoring.lossWeight);
  REQUIRE(careful.minGoalChance > scoring.minGoalChance);
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
  // Without carrying, so no touch he gets back makes him decide in between.
  MatchConfig config;
  config.decisions.intervalTicks = 9;
  config.decisions.minHoldSeconds = 0.0;
  config.decisions.carrying.enabled = false;
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
      // A kick -- a pass or a shot -- needs a decision in the step before.
      const MatchState& before = simulation.previousState();
      const auto kicker = ElyverseFootball::SimMatch::findPlayerIndex(
          before, before.ball().owner.value_or(PlayerId::invalid()));
      REQUIRE(kicker.has_value());
      const auto& action = before.pendingAction(kicker.value_or(0));
      REQUIRE(action.has_value());
      REQUIRE_FALSE(std::holds_alternative<ElyverseFootball::SimMatch::CarryIntent>(
          action.value_or(ElyverseFootball::SimMatch::PendingAction{})));
    }
  }
  REQUIRE(kicks >= 3);
}

TEST_CASE("A player without a visible teammate looks round and passes", "[passDecision]") {
  // Player 1, the home goalkeeper -- of a side without a tactic, so with the
  // ball at his feet like anyone else -- looks at his own goal line: every
  // teammate is behind him. Planning no carry -- not even a turn -- he looks
  // over his shoulder, up the pitch, while his body keeps facing the goal
  // line, and plays to a teammate he finds.
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
  MatchConfig config;
  config.decisions.carrying.horizonSeconds = kNoCarrySeconds;
  MatchSimulation simulation =
      startMatch({.initialState = *state, .config = config, .seed = 1, .commands = {}});

  bool lookedUp = false;
  bool passed = false;
  for (int tick = 0; tick < 30 && !passed; ++tick) {
    REQUIRE(simulation.step().has_value());
    const MatchState& current = simulation.state();
    if (activeScan(current, 0) != nullptr) {
      REQUIRE(current.players()[0].facing == Vec2{.x = -1.0, .y = 0.0});
      lookedUp = lookedUp || lookDirection(current, 0).x > 0.0;
    }
    passed = current.pendingPass().has_value();
  }
  REQUIRE(lookedUp);
  REQUIRE(passed);
  // Playing the ball ends his scan.
  REQUIRE(simulation.state().tactical(0).scan == std::nullopt);
}

TEST_CASE("A carrier without any pass clears the ball long after looking round", "[passDecision]") {
  // Player 5 stands 5 m inside the touchline and 10 m from the opponent's
  // goal line, so seeing next to nothing -- a view of a meter -- he has no
  // pass and no shot, and he plans no carry. Looking round, he finds none
  // either; once stuck he clears the ball: not on the spot at the near
  // touchline, where the goal line's margin leaves him no room, but long
  // across to the far one.
  auto kickoff = makeSevenASideKickoff(Pitch(60.0, 40.0));
  REQUIRE(kickoff.has_value());
  MatchStateSpec spec{.pitch = kickoff->pitch(),
                      .players = {kickoff->players().begin(), kickoff->players().end()},
                      .ball = kickoff->ball(),
                      .playersPerSide = kickoff->playersPerSide()};
  spec.players[4].position = {.x = 50.0, .y = 5.0};
  spec.players[4].facing = {.x = 1.0, .y = 0.0};
  spec.ball.position = {.x = 50.5, .y = 5.0};
  spec.ball.owner = PlayerId(5);
  const auto state = MatchState::create(spec);
  REQUIRE(state.has_value());
  MatchConfig config;
  config.perception.viewDistance = 1.0;
  config.perception.awarenessRadius = 0.5;
  config.decisions.stuckSeconds = 1.0;
  config.decisions.carrying.horizonSeconds = kNoCarrySeconds;
  MatchSimulation simulation =
      startMatch({.initialState = *state, .config = config, .seed = 1, .commands = {}});

  std::optional<PassIntent> clearance;
  Vec2 from;
  while (!clearance && simulation.tick() < SimTick(90)) {
    REQUIRE(simulation.step().has_value());
    clearance = simulation.state().pendingPass();
    from = simulation.state().ball().position;
  }
  REQUIRE(clearance.has_value());
  const PassIntent kick = clearance.value_or(PassIntent{});
  REQUIRE(kick.lofted);
  REQUIRE_FALSE(kick.receiver.has_value());
  REQUIRE(kick.target.y > 20.0);
  REQUIRE(std::sqrt((kick.target - from).lengthSquared()) >=
          DecisionConfig{}.distribution.minLongDistance);
  // And he plays it.
  REQUIRE(simulation.step().has_value());
  REQUIRE_FALSE(simulation.state().ball().owner.has_value());
}

TEST_CASE("The decision system rejects a negative stuck time or margin", "[passDecision]") {
  const auto rejects = [](const auto& change) {
    DecisionConfig config;
    change(config);
    return throwsInvalidArgument(
        [&config] { (void)makePassDecisionSystem(config, PassCandidateRules{}); });
  };
  REQUIRE(rejects([](DecisionConfig& config) { config.stuckSeconds = -1.0; }));
  REQUIRE(rejects([](DecisionConfig& config) { config.clearTouchlineMargin = -1.0; }));
  REQUIRE(rejects([](DecisionConfig& config) {
    config.clearGoalLineMargin = std::numeric_limits<double>::infinity();
  }));
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

TEST_CASE("A tactic's passing risk shifts how passes are scored", "[passDecision]") {
  using ElyverseFootball::SimMatch::scoringForRisk;
  const PassScoringConfig base;
  // The reference tactic's middle risk changes nothing.
  REQUIRE(scoringForRisk(base, 0.5) == base);
  const PassScoringConfig bold = scoringForRisk(base, 1.0);
  REQUIRE(bold.progressionWeight == base.progressionWeight * 1.5);
  REQUIRE(bold.riskWeight == base.riskWeight * 0.5);
  REQUIRE(bold.minCompletion < base.minCompletion);
  const PassScoringConfig safe = scoringForRisk(base, 0.0);
  REQUIRE(safe.progressionWeight == base.progressionWeight * 0.5);
  REQUIRE(safe.riskWeight == base.riskWeight * 1.5);
  REQUIRE(safe.minCompletion > base.minCompletion);
  REQUIRE(safe.minCompletion <= 1.0);
}

TEST_CASE("The best carry of each kind competes with the passes and the shot", "[passDecision]") {
  // Two carries far better than anything else: only the first of them can be
  // chosen. Holding the ball and the turn compete too, the second carry not.
  const std::vector passes{option(PlayerId(2), 0.0)};
  const std::vector carries{carryOption(CarryKind::kCarry, 1.0),
                            carryOption(CarryKind::kCarry, 1.0), carryOption(CarryKind::kTurn, 0.5),
                            carryOption(CarryKind::kHold, 0.5)};
  int carried = 0;
  for (std::uint64_t seed = 0; seed < 500; ++seed) {
    RandomNumberGenerator random(seed);
    const auto chosen = chooseOnBall(passes, {}, carries, 0.15, random);
    REQUIRE(chosen.has_value());
    const OnBallChoice choice = chosen.value_or(OnBallChoice{});
    REQUIRE(choice.index != 1);
    if (choice.outcome == DecisionOutcome::kCarried) {
      REQUIRE(choice.index == 0);
      ++carried;
    }
    if (choice.outcome == DecisionOutcome::kTurned) {
      REQUIRE(choice.index == 2);
    }
    if (choice.outcome == DecisionOutcome::kHeld) {
      REQUIRE(choice.index == 3);
    }
  }
  // exp(0) against twice exp(-0.5 / 0.15) and exp(-1 / 0.15): about 93 %.
  REQUIRE(carried > 440);
}

TEST_CASE("A carrier decides as soon as he has his touched ball back", "[passDecision]") {
  // Decisions at the interval only every 1000 ticks: every decision after the
  // first comes in the tick after he got his touched ball back.
  const auto* scenario = ElyverseFootball::SimMatch::findScenario("carry-into-space");
  REQUIRE(scenario != nullptr);
  auto setup = scenario->make(1);
  REQUIRE(setup.has_value());
  setup->config.decisions.intervalTicks = 1000;
  setup->config.decisions.minHoldSeconds = 0.0;
  MatchSimulation simulation = startMatch(*setup);
  simulation.setCollectDiagnostics(true);
  int decisions = 0;
  while (simulation.tick() < SimTick(150)) {
    REQUIRE(simulation.step().has_value());
    const MatchState& before = simulation.previousState();
    for (const auto& decision : simulation.diagnostics()) {
      ++decisions;
      if (decision.tick == SimTick(0)) {
        continue;
      }
      const auto* run = activeCarry(before, 0);
      REQUIRE(run != nullptr);
      REQUIRE(run->retakenAt.has_value());
      REQUIRE(run->retakenAt.value_or(SimTick(0)).value() + 1 == decision.tick.value());
    }
  }
  REQUIRE(decisions >= 3);
}

TEST_CASE("Decision-making scales the softmax temperature around the default",
          "[passDecision][decisionMaking]") {
  const DecisionConfig config;
  REQUIRE(choiceTemperature(config, kDefaultDecisionMaking) == config.temperature);
  REQUIRE(choiceTemperature(config, 1.0) == config.temperature * 0.25);
  REQUIRE(choiceTemperature(config, 0.0) == config.temperature * 1.75);
  REQUIRE(misjudgementWidth(config, 1.0) == 0.0);
  REQUIRE(misjudgementWidth(config, kDefaultDecisionMaking) == 0.0);
  REQUIRE(misjudgementWidth(config, 0.25) == config.maxMisjudgement * 0.5);
  REQUIRE(misjudgementWidth(config, 0.0) == config.maxMisjudgement);
}

TEST_CASE("A better decider picks the best of two close options more reliably",
          "[passDecision][decisionMaking]") {
  // Two passes 0.1 apart: the share of the better one grows with decisionMaking.
  const std::vector candidates{option(PlayerId(2), 1.0), option(PlayerId(3), 0.9)};
  const auto bestShare = [&candidates](const double decisionMaking) {
    int best = 0;
    constexpr int kDraws = 2000;
    for (std::uint64_t seed = 0; seed < kDraws; ++seed) {
      RandomNumberGenerator random(seed);
      const auto chosen =
          chooseOnBall(candidates, {}, choiceTemperature(DecisionConfig{}, decisionMaking), random);
      best += chosen && chosen->index == 0 ? 1 : 0;
    }
    return static_cast<double>(best) / kDraws;
  };
  const double good = bestShare(1.0);
  const double average = bestShare(0.5);
  const double poor = bestShare(0.0);
  CAPTURE(good, average, poor);
  // 1 / (1 + e^(-0.1 / T)) for T = 0.0375, 0.15 and 0.2625: 0.94, 0.66, 0.59.
  REQUIRE(good > 0.9);
  REQUIRE(average > 0.63);
  REQUIRE(average < 0.69);
  REQUIRE(poor < average - 0.03);
}

TEST_CASE("A good decider misjudges nothing and draws nothing", "[passDecision][decisionMaking]") {
  PassCandidate pass = option(PlayerId(2), 1.0);
  pass.interceptionRisk = 0.4;
  pass.judgedInterceptionRisk = 0.4;
  std::vector passes{pass};
  std::vector<ShotCandidate> shots{shotOption(1.0)};
  RandomNumberGenerator random(5);
  const std::vector<ShotCandidate> before = shots;
  misjudgeOptions(passes, shots, 0.0, PassScoringConfig{}, ShotScoringConfig{}, random);
  REQUIRE(passes.front() == pass);
  REQUIRE(shots == before);
  REQUIRE(random.nextU64() == RandomNumberGenerator(5).nextU64());
}

TEST_CASE("A poor decider misjudges his estimates within his width and keeps them",
          "[passDecision][decisionMaking]") {
  const PassScoringConfig passScoring;
  const ShotScoringConfig shotScoring;
  PassCandidate pass = option(PlayerId(2), 0.0);
  pass.receiverConfidence = 1.0;
  pass.interceptionRisk = 0.4;
  PassCandidate tooFar = option(PlayerId(3), 0.0, PassRejection::kTooFar);
  tooFar.interceptionRisk = 0.2;
  tooFar.judgedInterceptionRisk = 0.2;
  ShotCandidate shot = shotOption(0.0);
  shot.blockRisk = 0.1;
  shot.saveRisk = 0.3;
  shot.goalChance = 0.5;
  ShotCandidate otherZone = shot;
  otherZone.column = 1;
  otherZone.goalChance = 0.45;
  constexpr double kWidth = 0.3;
  bool misjudged = false;
  for (std::uint64_t seed = 0; seed < 200; ++seed) {
    std::vector passes{pass, tooFar};
    std::vector shots{shot, otherZone};
    RandomNumberGenerator random(seed);
    misjudgeOptions(passes, shots, kWidth, passScoring, shotScoring, random);
    for (const PassCandidate& judged : passes) {
      if (judged.receiver == PlayerId(3)) {
        // Rejected for its distance: nothing to judge.
        REQUIRE(judged == tooFar);
        continue;
      }
      REQUIRE(judged.interceptionRisk == 0.4);
      REQUIRE(std::abs(judged.judgedInterceptionRisk - 0.4) < kWidth);
      REQUIRE(judged.completion == 1.0 - judged.judgedInterceptionRisk);
      REQUIRE(judged.utility == passContributions(judged, passScoring).total());
      REQUIRE(judged.isValid() == (judged.completion >= passScoring.minCompletion));
      misjudged = misjudged || judged.judgedInterceptionRisk != 0.4;
    }
    // One misjudgement of the situation shifts every zone alike.
    const auto shiftOf = [](const ShotCandidate& judged) {
      return judged.judgedGoalChance - judged.goalChance;
    };
    REQUIRE(std::abs(shiftOf(shots.front()) - shiftOf(shots.back())) < 1e-12);
    for (const ShotCandidate& judged : shots) {
      REQUIRE(std::abs(judged.judgedGoalChance - judged.goalChance) < kWidth);
      REQUIRE(std::abs(judged.judgedSaveRisk - 0.3) < kWidth);
      REQUIRE(judged.judgedBlockRisk >= 0.0);
      REQUIRE(judged.judgedBlockRisk < 0.1 + kWidth);
      REQUIRE(judged.secondBallChance ==
              shotScoring.reboundShare * (judged.judgedBlockRisk + judged.judgedSaveRisk));
      REQUIRE(judged.utility == shotContributions(judged, shotScoring).total());
    }
    // Valid candidates still come first.
    REQUIRE((passes.front().isValid() || !passes.back().isValid()));
  }
  REQUIRE(misjudged);
}

TEST_CASE("A position that allows no shot leaves the shots unjudged",
          "[passDecision][decisionMaking]") {
  std::vector<PassCandidate> passes;
  const std::vector shots{shotOption(0.0, ShotRejection::kGoalUnseen),
                          shotOption(0.0, ShotRejection::kGoalUnseen)};
  std::vector judged = shots;
  RandomNumberGenerator random(5);
  misjudgeOptions(passes, judged, 0.3, PassScoringConfig{}, ShotScoringConfig{}, random);
  REQUIRE(judged == shots);
  REQUIRE(random.nextU64() == RandomNumberGenerator(5).nextU64());
}

TEST_CASE("The pass decision system reads decisionMaking and misjudges from its own stream",
          "[passDecision][decisionMaking]") {
  // The clear chance, player 1 at a decisionMaking of `skill`, with `config`.
  const auto played = [](const double skill, const DecisionConfig& config) {
    auto setup = findScenario("clear-chance")->make(3);
    REQUIRE(setup.has_value());
    const MatchState& initial = setup->initialState;
    MatchStateSpec spec{.pitch = initial.pitch(),
                        .players = {initial.players().begin(), initial.players().end()},
                        .ball = initial.ball(),
                        .playersPerSide = initial.playersPerSide(),
                        .score = initial.score()};
    REQUIRE(spec.players.front().playerId == PlayerId(1));
    spec.players.front().attributes.decisionMaking = skill;
    auto state = MatchState::create(spec, initial.tactics());
    REQUIRE(state.has_value());
    setup->initialState = *std::move(state);
    setup->config.decisions = config;
    MatchSimulation simulation = startMatch(*setup);
    simulation.setCollectDiagnostics(true);
    std::optional<DecisionDiagnostic> first;
    StableHasher events;
    for (int tick = 0; tick < 60; ++tick) {
      REQUIRE(simulation.step().has_value());
      for (const auto& event : simulation.events()) {
        addEvent(events, event);
      }
      for (const DecisionDiagnostic& decision : simulation.diagnostics()) {
        if (!first && decision.player == PlayerId(1)) {
          first = decision;
        }
      }
    }
    REQUIRE(first.has_value());
    return std::pair{events.value(), first.value_or(DecisionDiagnostic{})};
  };
  // Without spread or misjudgement the skill changes nothing: the kAi stream
  // sees the same single draw, and nothing else draws from it.
  DecisionConfig flat;
  flat.skillTemperatureSpread = 0.0;
  flat.maxMisjudgement = 0.0;
  REQUIRE(played(0.0, flat).first == played(0.5, flat).first);
  // With the defaults the worst decider misjudges his shot.
  const DecisionDiagnostic poor = played(0.0, DecisionConfig{}).second;
  REQUIRE_FALSE(poor.shots.empty());
  REQUIRE(poor.shots.front().judgedGoalChance != poor.shots.front().goalChance);
  const DecisionDiagnostic average = played(0.5, DecisionConfig{}).second;
  REQUIRE(average.shots.front().judgedGoalChance == average.shots.front().goalChance);
}

TEST_CASE("The decision system rejects an invalid decision-making spread",
          "[passDecision][decisionMaking]") {
  const auto rejects = [](const auto& change) {
    DecisionConfig config;
    change(config);
    return throwsInvalidArgument(
        [&config] { (void)makePassDecisionSystem(config, PassCandidateRules{}); });
  };
  REQUIRE_FALSE(rejects([](DecisionConfig&) {}));
  REQUIRE_FALSE(rejects([](DecisionConfig& config) { config.skillTemperatureSpread = 0.0; }));
  REQUIRE(rejects([](DecisionConfig& config) { config.skillTemperatureSpread = 1.0; }));
  REQUIRE(rejects([](DecisionConfig& config) { config.skillTemperatureSpread = -0.1; }));
  REQUIRE(rejects([](DecisionConfig& config) { config.maxMisjudgement = 1.5; }));
  REQUIRE(rejects([](DecisionConfig& config) {
    config.maxMisjudgement = std::numeric_limits<double>::quiet_NaN();
  }));
}
