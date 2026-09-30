#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

#include "matchSimulation.hpp"
#include "matchState.hpp"
#include "perception.hpp"
#include "pitch.hpp"
#include "referenceTactic.hpp"
#include "shotCandidates.hpp"
#include "tactic.hpp"
#include "vec2.hpp"

using Catch::Matchers::WithinAbs;
using ElyverseFootball::SimCore::PlayerId;
using ElyverseFootball::SimCore::SimTick;
using ElyverseFootball::SimCore::Vec2;
using ElyverseFootball::SimMatch::generateShotCandidates;
using ElyverseFootball::SimMatch::GoalEnd;
using ElyverseFootball::SimMatch::makePerceptionSystem;
using ElyverseFootball::SimMatch::MatchSimulation;
using ElyverseFootball::SimMatch::MatchState;
using ElyverseFootball::SimMatch::Pitch;
using ElyverseFootball::SimMatch::PlayerMatchState;
using ElyverseFootball::SimMatch::ShotCandidate;
using ElyverseFootball::SimMatch::ShotCandidateRules;
using ElyverseFootball::SimMatch::shotContributions;
using ElyverseFootball::SimMatch::ShotRejection;
using ElyverseFootball::SimMatch::shotRejectionName;
using ElyverseFootball::SimMatch::ShotScoringConfig;
using ElyverseFootball::SimMatch::TeamSide;
using ElyverseFootball::SimMatch::TeamTactics;
using ElyverseFootball::SimTactics::referenceTacticSpec;
using ElyverseFootball::SimTactics::Tactic;

namespace {

constexpr double kSecondsPerTick = 1.0 / 30.0;

[[nodiscard]] PlayerMatchState playerAt(const std::uint32_t playerId, const TeamSide side,
                                        const Vec2 position, const double shotAccuracy = 0.5) {
  PlayerMatchState player{.playerId = PlayerId(playerId),
                          .side = side,
                          .position = position,
                          .velocity = {},
                          .attributes = {},
                          .target = std::nullopt,
                          .facing = {.x = side == TeamSide::kHome ? 1.0 : -1.0, .y = 0.0}};
  player.attributes.shotAccuracy = shotAccuracy;
  return player;
}

// What the shooter faces: where the away keeper stands, and optionally an away
// defender.
struct Chance {
  Vec2 shooter;
  Vec2 keeper{.x = 59.5, .y = 20.0};
  std::optional<Vec2> defender = std::nullopt;
  double accuracy = 0.5;
};

// Seven a side on the sandbox pitch, attacking the goal at x = 60. Home 1 has
// the ball at his feet; his teammates wait in his own half. Away plays the
// reference tactic, whose first slot, away 8, keeps goal; away 9 stands where
// the chance puts a defender, the rest far behind the shooter.
[[nodiscard]] MatchState chanceState(const Chance& chance) {
  std::vector<PlayerMatchState> players{
      playerAt(1, TeamSide::kHome, chance.shooter, chance.accuracy)};
  for (std::uint32_t id = 2; id <= 7; ++id) {
    players.push_back(playerAt(id, TeamSide::kHome, {.x = 5.0, .y = 5.0 * id}));
  }
  players.push_back(playerAt(8, TeamSide::kAway, chance.keeper));
  players.push_back(
      playerAt(9, TeamSide::kAway, chance.defender.value_or(Vec2{.x = 2.0, .y = 2.0})));
  for (std::uint32_t id = 10; id <= 14; ++id) {
    players.push_back(playerAt(id, TeamSide::kAway, {.x = 2.0, .y = 5.0 * (id - 9)}));
  }
  auto tactic = Tactic::create(referenceTacticSpec());
  REQUIRE(tactic.has_value());
  auto state = MatchState::create({.pitch = Pitch(60.0, 40.0),
                                   .players = std::move(players),
                                   .ball = {.position = chance.shooter + Vec2{.x = 0.5, .y = 0.0},
                                            .velocity = {},
                                            .owner = PlayerId(1),
                                            .lastTouch = std::nullopt},
                                   .playersPerSide = 7},
                                  TeamTactics{.home = std::nullopt, .away = *std::move(tactic)});
  REQUIRE(state.has_value());
  return *std::move(state);
}

// The state after one perception update, so the shooter remembers what he
// sees.
[[nodiscard]] MatchState perceived(MatchState state) {
  MatchSimulation simulation({.initialState = std::move(state),
                              .seed = 1,
                              .ticksPerSecond = 30,
                              .systems = {makePerceptionSystem({})},
                              .commands = {}});
  REQUIRE(simulation.step().has_value());
  return simulation.state();
}

[[nodiscard]] std::vector<ShotCandidate> shotsOf(const MatchState& state,
                                                 const ShotCandidateRules& rules = {}) {
  return generateShotCandidates(state, 0, SimTick(1), kSecondsPerTick, rules);
}

[[nodiscard]] std::vector<ShotCandidate> shotsAt(const Chance& chance,
                                                 const ShotCandidateRules& rules = {}) {
  return shotsOf(perceived(chanceState(chance)), rules);
}

[[nodiscard]] ShotCandidate zone(const std::vector<ShotCandidate>& shots, const std::size_t column,
                                 const std::size_t row) {
  const auto found = std::ranges::find_if(
      shots, [&](const ShotCandidate& shot) { return shot.column == column && shot.row == row; });
  REQUIRE(found != shots.end());
  return found == shots.end() ? ShotCandidate{} : *found;
}

// A clear chance: eight meters out, straight in front of the goal.
constexpr Vec2 kClearChance{.x = 51.5, .y = 20.0};

}  // namespace

TEST_CASE("Every zone of the goal is an aiming option", "[shotCandidates]") {
  const auto shots = shotsAt({.shooter = kClearChance});
  const auto goal = Pitch(60.0, 40.0).goal(GoalEnd::kMaxX);

  REQUIRE(shots.size() == 35);
  std::set<std::pair<std::size_t, std::size_t>> zones;
  for (const ShotCandidate& shot : shots) {
    CAPTURE(shot.column, shot.row);
    zones.emplace(shot.column, shot.row);
    REQUIRE(shot.target.x == 60.0);
    REQUIRE(goal.framesPoint(shot.target.y, shot.height));
    REQUIRE_THAT(shot.distance, WithinAbs(8.0, 1e-12));
    REQUIRE(shot.onTarget <= 1.0 + 1e-12);
    REQUIRE(shot.goalChance + shot.saveRisk <= shot.onTarget + 1e-12);
  }
  REQUIRE(zones.size() == 35);
}

TEST_CASE("A clear chance aims away from the keeper", "[shotCandidates]") {
  const auto shots = shotsAt({.shooter = kClearChance});

  const ShotCandidate& best = shots.front();
  CAPTURE(best.column, best.row, best.goalChance, best.saveRisk);
  REQUIRE(best.isValid());
  REQUIRE(best.goalChance > 0.3);
  REQUIRE((best.column == 0 || best.column == 6));
  // Straight at the keeper, he saves.
  REQUIRE(zone(shots, 3, 0).saveRisk > 0.5);
  REQUIRE(zone(shots, 3, 0).goalChance < best.goalChance);
}

TEST_CASE("A keeper the shooter does not remember saves nothing", "[shotCandidates]") {
  // Without a perception update the shooter remembers no one.
  const auto shots = shotsOf(chanceState({.shooter = kClearChance}));

  for (const ShotCandidate& shot : shots) {
    REQUIRE(shot.saveRisk == 0.0);
    REQUIRE(shot.blockRisk == 0.0);
  }
}

TEST_CASE("A goal seen from a hopeless angle offers no shot", "[shotCandidates]") {
  const auto shots = shotsAt({.shooter = {.x = 58.5, .y = 12.0}});

  for (const ShotCandidate& shot : shots) {
    REQUIRE(shot.rejection == ShotRejection::kTooNarrow);
    REQUIRE(shot.utility == 0.0);
  }
}

TEST_CASE("A player facing away from the goal offers no shot", "[shotCandidates]") {
  MatchState state = chanceState({.shooter = kClearChance});
  auto spec = [&state] {
    std::vector<PlayerMatchState> players(state.players().begin(), state.players().end());
    players.front().facing = {.x = -1.0, .y = 0.0};
    return players;
  }();
  auto turned = MatchState::create({.pitch = state.pitch(),
                                    .players = std::move(spec),
                                    .ball = state.ball(),
                                    .playersPerSide = 7},
                                   state.tactics());
  REQUIRE(turned.has_value());

  for (const ShotCandidate& shot : shotsOf(perceived(*std::move(turned)))) {
    REQUIRE(shot.rejection == ShotRejection::kGoalUnseen);
  }
}

TEST_CASE("A goal too far away offers no shot", "[shotCandidates]") {
  const auto shots = shotsAt({.shooter = {.x = 20.0, .y = 20.0}});

  for (const ShotCandidate& shot : shots) {
    REQUIRE(shot.rejection == ShotRejection::kTooFar);
  }
}

TEST_CASE("A defender in the lane blocks the shots behind him", "[shotCandidates]") {
  const Vec2 shooter{.x = 48.0, .y = 20.0};
  const auto open = shotsAt({.shooter = shooter});
  const auto blocked = shotsAt({.shooter = shooter, .defender = Vec2{.x = 49.5, .y = 20.0}});

  CAPTURE(zone(blocked, 3, 1).blockRisk);
  REQUIRE(zone(blocked, 3, 1).blockRisk > 0.7);
  REQUIRE(zone(blocked, 3, 1).rejection == ShotRejection::kBlocked);
  REQUIRE(zone(open, 3, 1).blockRisk == 0.0);
  REQUIRE(blocked.front().goalChance < open.front().goalChance);
}

TEST_CASE("A shot worth less than keeping the ball is not offered", "[shotCandidates]") {
  ShotCandidateRules rules;
  rules.scoring.lossWeight = 100.0;
  const auto shots = shotsAt({.shooter = kClearChance}, rules);

  for (const ShotCandidate& shot : shots) {
    REQUIRE(shot.utility <= 0.0);
    REQUIRE(shot.rejection == ShotRejection::kNotWorthIt);
  }
}

TEST_CASE("An accurate shooter aims closer to the post and scores more", "[shotCandidates]") {
  const auto average = shotsAt({.shooter = kClearChance});
  const auto pinpoint = shotsAt({.shooter = kClearChance, .accuracy = 1.0});
  const auto wild = shotsAt({.shooter = kClearChance, .accuracy = 0.0});
  const double post = Pitch(60.0, 40.0).goal(GoalEnd::kMaxX).postAtMinY().y;

  const double pinpointOff = zone(pinpoint, 0, 2).target.y - post;
  const double averageOff = zone(average, 0, 2).target.y - post;
  CAPTURE(pinpointOff, averageOff);
  REQUIRE_THAT(pinpointOff, WithinAbs(ShotScoringConfig{}.frameMargin, 1e-12));
  REQUIRE(averageOff > pinpointOff);
  REQUIRE(pinpoint.front().spread < average.front().spread);
  REQUIRE(wild.front().spread > average.front().spread);
  REQUIRE(pinpoint.front().goalChance > average.front().goalChance);
  REQUIRE(average.front().goalChance > wild.front().goalChance);
}

TEST_CASE("Pressure on the shooter widens his spread", "[shotCandidates]") {
  const auto free = shotsAt({.shooter = kClearChance});
  const auto pressed = shotsAt({.shooter = kClearChance, .defender = Vec2{.x = 52.5, .y = 21.0}});

  REQUIRE(pressed.front().spread > free.front().spread);
}

TEST_CASE("Shot utilities are their contributions, ordered best first", "[shotCandidates]") {
  const ShotScoringConfig scoring;
  const auto shots = shotsAt({.shooter = {.x = 45.0, .y = 15.0}});

  REQUIRE(shots == shotsAt({.shooter = {.x = 45.0, .y = 15.0}}));
  for (std::size_t index = 0; index < shots.size(); ++index) {
    const ShotCandidate& shot = shots[index];
    REQUIRE(shotContributions(shot, scoring).total() == shot.utility);
    if (index > 0 && shots[index - 1].isValid() == shot.isValid()) {
      REQUIRE(shots[index - 1].utility >= shot.utility);
    }
    if (index > 0) {
      REQUIRE((shots[index - 1].isValid() || !shot.isValid()));
    }
  }
}

TEST_CASE("Shot rejections have names", "[shotCandidates]") {
  REQUIRE(shotRejectionName(ShotRejection::kValid) == "valid");
  REQUIRE(shotRejectionName(ShotRejection::kTooFar) == "too far");
  REQUIRE(shotRejectionName(ShotRejection::kGoalUnseen) == "goal unseen");
  REQUIRE(shotRejectionName(ShotRejection::kTooNarrow) == "too narrow");
  REQUIRE(shotRejectionName(ShotRejection::kBlocked) == "blocked");
  REQUIRE(shotRejectionName(ShotRejection::kUnlikely) == "unlikely");
  REQUIRE(shotRejectionName(ShotRejection::kNotWorthIt) == "not worth it");
}

TEST_CASE("Shot scoring rejects an invalid configuration", "[shotCandidates]") {
  const auto invalid = [](auto change) {
    ShotScoringConfig config;
    change(config);
    return config;
  };
  REQUIRE_NOTHROW(validate(ShotScoringConfig{}));
  REQUIRE_THROWS_AS(validate(invalid([](auto& config) { config.zoneColumns = 1; })),
                    std::invalid_argument);
  REQUIRE_THROWS_AS(validate(invalid([](auto& config) { config.zoneRows = 0; })),
                    std::invalid_argument);
  REQUIRE_THROWS_AS(validate(invalid([](auto& config) { config.spreadAtZero = 0.0; })),
                    std::invalid_argument);
  REQUIRE_THROWS_AS(validate(invalid([](auto& config) { config.minOpening = 2.5; })),
                    std::invalid_argument);
  REQUIRE_THROWS_AS(validate(invalid([](auto& config) { config.goalWeight = -1.0; })),
                    std::invalid_argument);
  REQUIRE_THROWS_AS(validate(invalid([](auto& config) { config.shotSpeed = std::nan(""); })),
                    std::invalid_argument);
}
