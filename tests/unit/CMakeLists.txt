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
        sim-match/scenariosTests.cpp
)
target_link_libraries(sim-match-tests PRIVATE
        ElyverseFootball::sim-match
        Catch2::Catch2WithMain
        pf_compiler_warnings
)
catch_discover_tests(sim-match-tests)

add_executable(sim-replay-tests
        sim-replay/replayTests.cpp
        sim-replay/replayJsonTests.cpp
)
target_link_libraries(sim-replay-tests PRIVATE
        ElyverseFootball::sim-replay
        Catch2::Catch2WithMain
        pf_compiler_warnings
)
catch_discover_tests(sim-replay-tests)
