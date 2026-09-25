add_executable(sim-core-tests
        sim-core/strongIdTests.cpp
        sim-core/simTimeTests.cpp
        sim-core/randomTests.cpp
        sim-core/eventBusTests.cpp
        sim-core/vec2Tests.cpp
        sim-core/stableHashTests.cpp
        sim-core/stableMathTests.cpp
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
        sim-match/spatialQueriesTests.cpp
        sim-match/perceptionTests.cpp
        sim-match/possessionTests.cpp
        sim-match/passingTests.cpp
        sim-match/receptionTests.cpp
        sim-match/passCandidatesTests.cpp
        sim-match/passDecisionTests.cpp
        sim-match/matchEventsTests.cpp
)
target_link_libraries(sim-match-tests PRIVATE
        ElyverseFootball::sim-match
        Catch2::Catch2WithMain
        pf_compiler_warnings
)
catch_discover_tests(sim-match-tests)

add_executable(sim-replay-tests
        sim-replay/debugFramesTests.cpp
        sim-replay/replayTests.cpp
        sim-replay/replayJsonTests.cpp
)
target_link_libraries(sim-replay-tests PRIVATE
        ElyverseFootball::sim-replay
        nlohmann_json::nlohmann_json
        Catch2::Catch2WithMain
        pf_compiler_warnings
)
catch_discover_tests(sim-replay-tests)

add_executable(sim-tactics-tests
        sim-tactics/tacticalPhaseTests.cpp
        sim-tactics/rolePresetTests.cpp
        sim-tactics/tacticTests.cpp
)
target_link_libraries(sim-tactics-tests PRIVATE
        ElyverseFootball::sim-tactics
        Catch2::Catch2WithMain
        pf_compiler_warnings
)
catch_discover_tests(sim-tactics-tests)
