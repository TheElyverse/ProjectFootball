#include "terminalUi.hpp"

#include <cstdio>
#include <format>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

#include <ftxui/component/component.hpp>
#include <ftxui/component/component_base.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

#include "version.hpp"

namespace ElyverseFootball::Cli {

bool hasInteractiveTerminal() {
#ifdef _WIN32
  return _isatty(_fileno(stdin)) != 0 && _isatty(_fileno(stdout)) != 0;
#else
  return isatty(STDIN_FILENO) != 0 && isatty(STDOUT_FILENO) != 0;
#endif
}

void showSummary(const std::string& title, const std::vector<SummaryLine>& lines) {
  auto screen = ftxui::ScreenInteractive::TerminalOutput();
  const auto quit = screen.ExitLoopClosure();
  auto closeButton = ftxui::Button("Close", quit);
  auto renderer = ftxui::Renderer(closeButton, [&] {
    ftxui::Elements rows{
        ftxui::text("Elyverse: Football") | ftxui::bold,
        ftxui::separator(),
        ftxui::text(title),
        ftxui::text("Core version: " + std::string(SimCore::coreVersion())),
    };
    for (const auto& [label, value] : lines) {
      rows.push_back(ftxui::paragraph(std::format("{}: {}", label, value)));
    }
    rows.push_back(ftxui::separator());
    rows.push_back(closeButton->Render());
    rows.push_back(ftxui::text("Enter: close | q / Esc: quit") | ftxui::dim);
    return ftxui::vbox(std::move(rows)) | ftxui::border;
  });
  auto component = ftxui::CatchEvent(renderer, [&](const ftxui::Event& event) {
    if (event == ftxui::Event::Character('q') || event == ftxui::Event::Escape) {
      quit();
      return true;
    }
    return false;
  });
  screen.Loop(component);
}

}  // namespace ElyverseFootball::Cli
