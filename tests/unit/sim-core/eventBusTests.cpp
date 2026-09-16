#include <catch2/catch_test_macros.hpp>

#include "eventBus.hpp"

namespace {
struct PlayerJoinedClub {
  int playerId;
};
}  // namespace

TEST_CASE("EventBus delivers published events to all subscribers", "[eventBus]") {
  ElyverseFootball::SimCore::EventBus<PlayerJoinedClub> bus;

  int calls = 0;
  int lastPlayerId = -1;

  bus.subscribe([&](const PlayerJoinedClub& event) {
    ++calls;
    lastPlayerId = event.playerId;
  });
  bus.subscribe([&](const PlayerJoinedClub&) { ++calls; });

  bus.publish(PlayerJoinedClub{42});

  REQUIRE(calls == 2);
  REQUIRE(lastPlayerId == 42);
}
