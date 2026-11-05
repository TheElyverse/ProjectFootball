add_executable(sim-acceptance-tests
        m0AcceptanceTests.cpp
        p1AcceptanceTests.cpp
        p2AcceptanceTests.cpp
        tacticalIdentityTests.cpp
        goldenScenarioTests.cpp
        shotScenarioTests.cpp
        goalkeeperScenarioTests.cpp
        distributionScenarioTests.cpp
        stuckCarrierScenarioTests.cpp
        carryScenarioTests.cpp
        duelScenarioTests.cpp
        crossScenarioTests.cpp
        elevenASideTests.cpp
        matchClockTests.cpp
        matchResultTests.cpp
)
target_link_libraries(sim-acceptance-tests PRIVATE
        ElyverseFootball::sim-replay
        ElyverseFootball::sim-analytics
        Catch2::Catch2WithMain
        pf_test_support
        pf_compiler_warnings
)
# The tactic files the identity tests load live in the source tree.
target_compile_definitions(sim-acceptance-tests PRIVATE PF_DATA_DIR="${PROJECT_SOURCE_DIR}/data")
catch_discover_tests(sim-acceptance-tests PROPERTIES LABELS acceptance)
