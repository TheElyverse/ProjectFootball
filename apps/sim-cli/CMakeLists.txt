CPMAddPackage(
        NAME ftxui
        GITHUB_REPOSITORY ArthurSonzogni/FTXUI
        GIT_TAG v6.1.9
        OPTIONS
        "FTXUI_BUILD_EXAMPLES OFF"
        "FTXUI_BUILD_DOCS OFF"
        "FTXUI_BUILD_TESTS OFF"
        "FTXUI_ENABLE_INSTALL OFF"
)

add_executable(sim-cli src/main.cpp src/cliOptions.cpp src/terminalUi.cpp)
target_link_libraries(sim-cli PRIVATE
        ElyverseFootball::sim-core
        ElyverseFootball::sim-replay
        pf_compiler_warnings
        ftxui::component
        ftxui::dom
        ftxui::screen
)
