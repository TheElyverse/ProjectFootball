#include <catch2/catch_test_macros.hpp>

#include "sim_core/event_bus.hpp"

namespace {
struct PlayerJoinedClub {
  int player_id;
};
}  // namespace

TEST_CASE("EventBus delivers published events to all subscribers", "[event_bus]") {
  ElyverseFootball::SimCore::EventBus<PlayerJoinedClub> bus;

  int calls = 0;
  int last_player_id = -1;

  bus.subscribe([&](const PlayerJoinedClub& event) {
    ++calls;
    last_player_id = event.player_id;
  });
  bus.subscribe([&](const PlayerJoinedClub&) { ++calls; });

  bus.publish(PlayerJoinedClub{42});

  REQUIRE(calls == 2);
  REQUIRE(last_player_id == 42);
}
