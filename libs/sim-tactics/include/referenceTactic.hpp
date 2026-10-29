#pragma once

#include "tactic.hpp"

namespace ElyverseFootball::SimTactics {

// A neutral, valid seven-a-side tactic: a 1-2-1-3 shape (goalkeeper, two
// centre backs, a holding midfielder, two wingers and a striker) built from
// role presets, middling heights and no pressing triggers. Tests and
// hand-built fixtures start from it and change what they are about; the
// tactical identities are data files (docs/tactics.md#tactic-model), not code.
[[nodiscard]] TacticSpec referenceTacticSpec();

// The reference tactic for eleven-a-side: the 4-4-2 formation preset with the
// reference's principles and phases (docs/tactics.md#files).
[[nodiscard]] TacticSpec referenceElevenASideTacticSpec();

}  // namespace ElyverseFootball::SimTactics
