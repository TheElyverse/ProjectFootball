#pragma once

#include <string_view>

namespace ElyverseFootball::SimCore {

// Bumped whenever a change could affect replay determinism (see
// docs/implementation-plan.md section 5.3, "Replay Contract").
[[nodiscard]] std::string_view coreVersion() noexcept;

}  // namespace ElyverseFootball::SimCore
