#pragma once

#include "tactic.hpp"

namespace ElyverseFootball::SimTactics {

// A neutral, valid seven-a-side tactic: a 1-2-1-3 shape (goalkeeper, two
// centre backs, a holding midfielder, two wingers and a striker) built from
// role presets, middling heights and no pressing triggers. Tests and
// hand-built fixtures start from it and change what they are about; the
// tactical identities are data files (docs/tactics.md), not code.
[[nodiscard]] TacticSpec referenceTacticSpec();

}  // namespace ElyverseFootball::SimTactics
