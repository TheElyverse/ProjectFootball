#pragma once

#include <string>
#include <utility>
#include <vector>

namespace ElyverseFootball::Cli {

// A label and its value, one line of a summary.
using SummaryLine = std::pair<std::string, std::string>;

[[nodiscard]] bool hasInteractiveTerminal();

// Shows the summary in a bordered screen until the user closes it.
void showSummary(const std::string& title, const std::vector<SummaryLine>& lines);

}  // namespace ElyverseFootball::Cli
