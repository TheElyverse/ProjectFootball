#pragma once

#include <cstdint>
#include <string>

#include "simTime.hpp"

namespace ElyverseFootball::Cli {

[[nodiscard]] bool hasInteractiveTerminal();
void showSimulationSummary(std::uint64_t seed, SimCore::SimTick tick,
                           const std::string& replayPath);

}  // namespace ElyverseFootball::Cli
