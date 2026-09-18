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

  bus.subscribe([&calls, &lastPlayerId](const PlayerJoinedClub& event) {
    ++calls;
    lastPlayerId = event.playerId;
  });
  bus.subscribe([&](const PlayerJoinedClub&) { ++calls; });

  bus.publish(PlayerJoinedClub{42});

  REQUIRE(calls == 2);
  REQUIRE(lastPlayerId == 42);
}

TEST_CASE("EventBus rejects subscribe() called reentrantly from a handler", "[eventBus]") {
  ElyverseFootball::SimCore::EventBus<PlayerJoinedClub> bus;

  bus.subscribe([&bus](const PlayerJoinedClub&) {
    REQUIRE_THROWS_AS(bus.subscribe([](const PlayerJoinedClub&) {}), std::logic_error);
  });

  bus.publish(PlayerJoinedClub{7});

  // The bus must remain usable afterwards -- publishing is not "stuck" mid-flight.
  int calls = 0;
  bus.subscribe([&calls](const PlayerJoinedClub&) { ++calls; });
  bus.publish(PlayerJoinedClub{7});
  REQUIRE(calls == 1);
}

TEST_CASE("EventBus::isPublishing lets a handler check proactively before subscribing",
          "[eventBus]") {
  ElyverseFootball::SimCore::EventBus<PlayerJoinedClub> bus;

  REQUIRE_FALSE(bus.isPublishing());

  bool observedPublishing = false;
  bool subscribedFromHandler = false;
  bus.subscribe([&](const PlayerJoinedClub&) {
    observedPublishing = bus.isPublishing();
    if (!bus.isPublishing()) {
      bus.subscribe([](const PlayerJoinedClub&) {});
      subscribedFromHandler = true;
    }
  });

  bus.publish(PlayerJoinedClub{7});

  REQUIRE(observedPublishing);
  REQUIRE_FALSE(subscribedFromHandler);
  REQUIRE_FALSE(bus.isPublishing());
}
