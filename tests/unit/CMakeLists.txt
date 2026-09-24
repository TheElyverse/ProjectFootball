CPMAddPackage(
        NAME Catch2
        GITHUB_REPOSITORY catchorg/Catch2
        GIT_TAG v3.16.0
)

list(APPEND CMAKE_MODULE_PATH ${Catch2_SOURCE_DIR}/extras)

add_executable(sim-core-tests
        sim-core/strongIdTests.cpp
        sim-core/simTimeTests.cpp
        sim-core/randomTests.cpp
        sim-core/eventBusTests.cpp
        sim-core/vec2Tests.cpp
        sim-core/stableHashTests.cpp
)

target_link_libraries(sim-core-tests PRIVATE
        ElyverseFootball::sim-core
        Catch2::Catch2WithMain
        pf_compiler_warnings
)

include(CTest)
include(Catch)
catch_discover_tests(sim-core-tests)

add_executable(sim-match-tests
        sim-match/pitchTests.cpp
        sim-match/matchStateTests.cpp
        sim-match/kickoffScenarioTests.cpp
        sim-match/matchSimulationTests.cpp
        sim-match/matchCommandTests.cpp
        sim-match/playerMovementTests.cpp
        sim-match/ballMovementTests.cpp
        sim-match/matchStateHashTests.cpp
        sim-match/matchSetupTests.cpp
)
target_link_libraries(sim-match-tests PRIVATE
        ElyverseFootball::sim-match
        Catch2::Catch2WithMain
        pf_compiler_warnings
)
catch_discover_tests(sim-match-tests)

add_test(NAME sim-cli-smoke
        COMMAND ${CMAKE_COMMAND}
        -DSIM_CLI=$<TARGET_FILE:sim-cli>
        -DTEST_OUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/sim-cli-smoke
        -P ${CMAKE_CURRENT_SOURCE_DIR}/../cli/smoke.cmake
)
