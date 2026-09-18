#pragma once

#include <functional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace ElyverseFootball::SimCore {

// Minimal synchronous pub/sub for domain events. Handlers run inline on
// publish() -- there is no queuing or threading here, keep it that way in the
// simulation core (see "Dependency Rule" in docs/implementation-plan.md
// section 2.2).
//
// Reentrant subscribe() (i.e. a handler calling subscribe() on this bus while
// publish() is invoking it) is rejected rather than silently accepted: adding
// to handlers_ mid-iteration would invalidate the iteration and could destroy
// the currently executing Handler on reallocation. Handlers that may
// subscribe should check isPublishing() first; the exception subscribe()
// throws in that case signals a missed check, not expected control flow.
template <typename Event>
class EventBus {
 public:
  using Handler = std::function<void(const Event&)>;

  // Lets callers check proactively before calling subscribe() from within a
  // handler, instead of relying on the exception below as control flow.
  [[nodiscard]] bool isPublishing() const noexcept { return isPublishing_; }

  void subscribe(Handler handler) {
    if (isPublishing_) {
      throw std::logic_error("EventBus::subscribe() called reentrantly during publish()");
    }
    handlers_.push_back(std::move(handler));
  }

  void publish(const Event& event) const {
    isPublishing_ = true;
    try {
      for (const auto& handler : handlers_) {
        handler(event);
      }
    } catch (...) {
      isPublishing_ = false;
      throw;
    }
    isPublishing_ = false;
  }

 private:
  std::vector<Handler> handlers_;
  mutable bool isPublishing_ = false;
};

}  // namespace ElyverseFootball::SimCore
