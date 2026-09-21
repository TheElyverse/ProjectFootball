#include "terminalUi.hpp"

#include <cstdio>
#include <string>

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

void showSimulationSummary(const std::uint64_t seed, const SimCore::SimTick tick,
                           const std::string& replayPath) {
  auto screen = ftxui::ScreenInteractive::TerminalOutput();
  const auto quit = screen.ExitLoopClosure();
  auto closeButton = ftxui::Button("Close", quit);
  auto renderer = ftxui::Renderer(closeButton, [&] {
    return ftxui::vbox({
               ftxui::text("Elyverse: Football") | ftxui::bold,
               ftxui::separator(),
               ftxui::text("Empty simulation initialized"),
               ftxui::text("Core version: " + std::string(SimCore::coreVersion())),
               ftxui::text("Seed: " + std::to_string(seed)),
               ftxui::text("Game time (ticks): " + std::to_string(tick.value())),
               ftxui::paragraph("Replay metadata saved to: " + replayPath),
               ftxui::separator(),
               closeButton->Render(),
               ftxui::text("Enter: close | q / Esc: quit") | ftxui::dim,
           }) |
           ftxui::border;
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
