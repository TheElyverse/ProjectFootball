#pragma once

#include <functional>
#include <utility>
#include <vector>

namespace ElyverseFootball::SimCore {

// Minimal synchronous pub/sub for domain events. Handlers run inline on
// publish() -- there is no queuing or threading here, keep it that way in the
// simulation core (see "Dependency Rule" in docs/implementation-plan.md
// section 2.2).
template <typename Event>
class EventBus {
 public:
  using Handler = std::function<void(const Event&)>;

  void subscribe(Handler handler) { handlers_.push_back(std::move(handler)); }

  void publish(const Event& event) const {
    for (const auto& handler : handlers_) {
      handler(event);
    }
  }

 private:
  std::vector<Handler> handlers_;
};

}  // namespace ElyverseFootball::SimCore
