#pragma once

#include <cstdint>
#include <functional>
#include <ostream>

using namespace std;

namespace ElyverseFootball::SimCore {

// A type-safe wrapper around an integral value, distinguished by `Tag`.
// Prevents accidentally mixing IDs from different domains (e.g. PlayerId vs
// ClubId) while staying as cheap to copy/compare as the underlying integer.
// See docs/implementation-plan.md section 4.1.
template <typename Tag, typename Value = uint32_t>
class StrongId {
 public:
  using ValueType = Value;

  constexpr StrongId() noexcept = default;
  constexpr explicit StrongId(Value value) noexcept : value_(value) {}

  [[nodiscard]] constexpr Value value() const noexcept { return value_; }
  [[nodiscard]] constexpr bool isValid() const noexcept { return value_ != kInvalid; }

  [[nodiscard]] static constexpr StrongId invalid() noexcept { return StrongId{}; }

  friend constexpr auto operator<=>(const StrongId&, const StrongId&) = default;

 private:
  static constexpr Value kInvalid = static_cast<Value>(-1);
  Value value_ = kInvalid;
};

template <typename Tag, typename Value>

ostream& operator<<(ostream& outputStream, const StrongId<Tag, Value>& strongId) {
  return outputStream << strongId.value();
}

}  // namespace ElyverseFootball::SimCore

template <typename Tag, typename Value>
struct hash<ElyverseFootball::SimCore::StrongId<Tag, Value>> {
  size_t operator()(
      const ElyverseFootball::SimCore::StrongId<Tag, Value>& strongId) const noexcept {
    return hash<Value>{}(strongId.value());
  }
};
