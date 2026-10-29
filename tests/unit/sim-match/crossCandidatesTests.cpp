#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

#include "crossCandidates.hpp"
#include "ids.hpp"
#include "matchFixtures.hpp"
#include "matchSimulation.hpp"
#include "matchState.hpp"
#include "perception.hpp"
#include "pitch.hpp"
#include "referenceTactic.hpp"
#include "simTime.hpp"
#include "tactic.hpp"
#include "vec2.hpp"

using Catch::Matchers::WithinAbs;
using ElyverseFootball::SimCore::PlayerId;
using ElyverseFootball::SimCore::SimTick;
using ElyverseFootball::SimCore::Vec2;
using ElyverseFootball::SimMatch::CrossArea;
using ElyverseFootball::SimMatch::CrossCandidate;
using ElyverseFootball::SimMatch::CrossCandidateRules;
using ElyverseFootball::SimMatch::crossContributions;
using ElyverseFootball::SimMatch::CrossRejection;
using ElyverseFootball::SimMatch::CrossScoringConfig;
using ElyverseFootball::SimMatch::crossScoringForRisk;
using ElyverseFootball::SimMatch::dominantCrossContribution;
using ElyverseFootball::SimMatch::generateCrossCandidates;
using ElyverseFootball::SimMatch::makePerceptionSystem;
using ElyverseFootball::SimMatch::MatchSimulation;
using ElyverseFootball::SimMatch::MatchState;
using ElyverseFootball::SimMatch::Pitch;
using ElyverseFootball::SimMatch::PlayerMatchState;
using ElyverseFootball::SimMatch::TeamSide;
using ElyverseFootball::SimMatch::TeamTactics;
using ElyverseFootball::SimMatch::validate;
using ElyverseFootball::SimTactics::referenceTacticSpec;
using ElyverseFootball::SimTactics::Tactic;
using ElyverseFootball::TestSupport::playerAt;

namespace {

constexpr double kSecondsPerTick = 1.0 / 30.0;

// Seven a side on the sandbox pitch, home attacking the goal at x = 60: home 1
// on the ball where the test puts him, facing the box; home 2 the runner;
// away 8 keeps goal with the reference tactic; everyone else far away.
struct Wing {
  Vec2 crosser;
  Vec2 runner{.x = 53.0, .y = 19.0};
  Vec2 keeper{.x = 59.5, .y = 20.0};
  // Where the runner's side sends him, if anywhere.
  std::optional<Vec2> run = std::nullopt;
  // Away's defender 9, far away unless the test puts him somewhere.
  Vec2 defender{.x = 2.0, .y = 5.0};
};

[[nodiscard]] MatchState perceived(const Wing& wing) {
  const Vec2 facing{.x = 0.6, .y = wing.crosser.y < 20.0 ? 0.8 : -0.8};
  std::vector<PlayerMatchState> players{playerAt(1, TeamSide::kHome, wing.crosser, facing),
                                        playerAt(2, TeamSide::kHome, wing.runner)};
  players.back().target = wing.run;
  for (std::uint32_t id = 3; id <= 7; ++id) {
    players.push_back(playerAt(id, TeamSide::kHome, {.x = 5.0, .y = 5.0 * id}));
  }
  players.push_back(playerAt(8, TeamSide::kAway, wing.keeper));
  players.push_back(playerAt(9, TeamSide::kAway, wing.defender, {.x = -1.0, .y = 0.0}));
  for (std::uint32_t id = 10; id <= 14; ++id) {
    players.push_back(playerAt(id, TeamSide::kAway, {.x = 2.0, .y = 5.0 * (id - 8)}));
  }
  auto tactic = Tactic::create(referenceTacticSpec());
  REQUIRE(tactic.has_value());
  auto state = MatchState::create({.pitch = Pitch(60.0, 40.0),
                                   .players = std::move(players),
                                   .ball = {.position = wing.crosser + (facing * 0.5),
                                            .velocity = {},
                                            .owner = PlayerId(1),
                                            .lastTouch = std::nullopt},
                                   .playersPerSide = 7},
                                  TeamTactics{.home = std::nullopt, .away = *std::move(tactic)});
  REQUIRE(state.has_value());
  MatchSimulation simulation({.initialState = *std::move(state),
                              .seed = 1,
                              .ticksPerSecond = 30,
                              .systems = {makePerceptionSystem({})},
                              .commands = {}});
  REQUIRE(simulation.step().has_value());
  return simulation.state();
}

[[nodiscard]] std::vector<CrossCandidate> crossesOf(const Wing& wing,
                                                    const CrossCandidateRules& rules = {}) {
  return generateCrossCandidates(perceived(wing), 0,
                                 {.tick = SimTick(1), .secondsPerTick = kSecondsPerTick}, rules);
}

[[nodiscard]] CrossCandidate areaOf(const std::vector<CrossCandidate>& crosses,
                                    const CrossArea area) {
  for (const CrossCandidate& cross : crosses) {
    if (cross.area == area) {
      return cross;
    }
  }
  FAIL("no cross at that area");
  return {};
}

}  // namespace

TEST_CASE("Only a wide player in the final third crosses", "[crossCandidates]") {
  REQUIRE_FALSE(crossesOf({.crosser = {.x = 46.0, .y = 4.0}}).empty());
  // In the middle, or too far from the goal line, or with crossing off.
  REQUIRE(crossesOf({.crosser = {.x = 46.0, .y = 15.0}}).empty());
  REQUIRE(crossesOf({.crosser = {.x = 30.0, .y = 4.0}}).empty());
  CrossCandidateRules off;
  off.scoring.enabled = false;
  REQUIRE(crossesOf({.crosser = {.x = 46.0, .y = 4.0}}, off).empty());
}

TEST_CASE("Crosses aim at the areas of the box, a cut-back along the ground", "[crossCandidates]") {
  const auto crosses = crossesOf({.crosser = {.x = 46.0, .y = 4.0}});
  const Vec2 from = perceived({.crosser = {.x = 46.0, .y = 4.0}}).ball().position;
  const Pitch pitch(60.0, 40.0);
  const auto marks = pitch.markings();
  // The near post on the crosser's side, the far post across, the spot.
  REQUIRE_THAT(areaOf(crosses, CrossArea::kNearPost).target.y,
               WithinAbs(20.0 - (marks.goalWidthMeters / 2.0), 1e-12));
  REQUIRE_THAT(areaOf(crosses, CrossArea::kFarPost).target.y,
               WithinAbs(20.0 + (marks.goalWidthMeters / 2.0), 1e-12));
  REQUIRE(areaOf(crosses, CrossArea::kPenaltySpot).target ==
          pitch.penaltySpot(ElyverseFootball::SimMatch::GoalEnd::kMaxX));
  for (const CrossCandidate& cross : crosses) {
    CAPTURE(static_cast<int>(cross.area));
    REQUIRE(cross.utility == crossContributions(cross, CrossScoringConfig{}).total());
    if (cross.area == CrossArea::kCutBack) {
      REQUIRE(cross.height == 0.0);
      REQUIRE(cross.curvature == 0.0);
    } else {
      REQUIRE(cross.height > 0.0);
      // It bends toward the goal: to the side of its line the goal is on.
      const Vec2 chord = cross.target - from;
      const Vec2 toGoal = Vec2{.x = 60.0, .y = 20.0} - from;
      const double side = (chord.x * toGoal.y) - (chord.y * toGoal.x);
      REQUIRE(cross.curvature * side > 0.0);
    }
    REQUIRE(cross.runner == PlayerId(2));
  }
}

TEST_CASE("A keeper near the area claims the cross the striker would win", "[crossCandidates]") {
  const auto open = crossesOf({.crosser = {.x = 46.0, .y = 4.0}, .keeper = {.x = 59.5, .y = 23.0}});
  const auto covered =
      crossesOf({.crosser = {.x = 46.0, .y = 4.0}, .keeper = {.x = 56.0, .y = 18.5}});
  const CrossCandidate before = areaOf(open, CrossArea::kNearPost);
  const CrossCandidate after = areaOf(covered, CrossArea::kNearPost);
  CAPTURE(before.winChance, after.winChance, after.keeperClaim);
  REQUIRE(after.keeperClaim > before.keeperClaim);
  REQUIRE(after.winChance < before.winChance);
}

TEST_CASE("A cross nobody can get to is not offered", "[crossCandidates]") {
  // The runner far behind the play.
  const auto crosses =
      crossesOf({.crosser = {.x = 46.0, .y = 4.0}, .runner = {.x = 10.0, .y = 20.0}});
  for (const CrossCandidate& cross : crosses) {
    REQUIRE_FALSE(cross.isValid());
  }
}

TEST_CASE("A cross's utility splits into its parts", "[crossCandidates]") {
  CrossCandidate cross;
  cross.winChance = 0.6;
  cross.threat = 0.5;
  const CrossScoringConfig scoring;
  const auto parts = crossContributions(cross, scoring);
  REQUIRE(parts.win == 0.6);
  REQUIRE(parts.threat == 0.3);
  REQUIRE_THAT(parts.loss, WithinAbs(-0.12, 1e-15));
  REQUIRE(dominantCrossContribution(parts) == "win");
  // A bolder tactic values the threat more, fears losing it less.
  const CrossScoringConfig bold = crossScoringForRisk(scoring, 1.0);
  REQUIRE(bold.threatWeight > scoring.threatWeight);
  REQUIRE(bold.lossWeight < scoring.lossWeight);
  REQUIRE(crossScoringForRisk(scoring, 0.5) == scoring);
}

TEST_CASE("Cross scoring rejects values outside its ranges", "[crossCandidates]") {
  REQUIRE_NOTHROW(validate(CrossScoringConfig{}));
  CrossScoringConfig config;
  config.crossSpeed = 0.0;
  REQUIRE_THROWS_AS(validate(config), std::invalid_argument);
  config = {};
  config.minWinChance = 1.5;
  REQUIRE_THROWS_AS(validate(config), std::invalid_argument);
}

TEST_CASE("A teammate's run into the box is a cross of its own", "[crossCandidates]") {
  const Vec2 run{.x = 55.0, .y = 21.0};
  const auto crosses = crossesOf({.crosser = {.x = 46.0, .y = 4.0}, .run = run});
  const CrossCandidate toTheRun = areaOf(crosses, CrossArea::kRun);
  REQUIRE(toTheRun.target == run);
  REQUIRE(toTheRun.runner == PlayerId(2));
  // A run that ends outside the box is no target.
  for (const CrossCandidate& cross :
       crossesOf({.crosser = {.x = 46.0, .y = 4.0}, .run = Vec2{.x = 40.0, .y = 20.0}})) {
    REQUIRE(cross.area != CrossArea::kRun);
  }
}

TEST_CASE("A cut-back is cut out on its way, and a late runner misses it", "[crossCandidates]") {
  const Wing clear{.crosser = {.x = 57.0, .y = 6.5}, .runner = {.x = 53.5, .y = 15.0}};
  Wing blocked = clear;
  // Square in the cut-back's way, nowhere near where it arrives.
  blocked.defender = {.x = 55.5, .y = 10.5};
  const double open = areaOf(crossesOf(clear), CrossArea::kCutBack).winChance;
  const double cut = areaOf(crossesOf(blocked), CrossArea::kCutBack).winChance;
  CAPTURE(open, cut);
  REQUIRE(cut < open / 2.0);
  // A runner who arrives seconds after the ball rolled by wins nothing.
  Wing late = clear;
  late.runner = {.x = 38.0, .y = 25.0};
  REQUIRE_FALSE(areaOf(crossesOf(late), CrossArea::kCutBack).isValid());
}

TEST_CASE("Crosses are floated to the head or driven low", "[crossCandidates]") {
  int floated = 0;
  int driven = 0;
  for (const CrossCandidate& cross : crossesOf({.crosser = {.x = 46.0, .y = 4.0}})) {
    floated += cross.height == CrossScoringConfig{}.headerHeight &&
                       cross.speed == CrossScoringConfig{}.crossSpeed
                   ? 1
                   : 0;
    driven += cross.height == CrossScoringConfig{}.drivenHeight &&
                      cross.speed == CrossScoringConfig{}.drivenSpeed
                  ? 1
                  : 0;
  }
  // Near post, penalty spot and far post each way.
  REQUIRE(floated == 3);
  REQUIRE(driven == 3);
}
