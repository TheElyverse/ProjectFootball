CPMAddPackage(
        NAME Catch2
        GITHUB_REPOSITORY catchorg/Catch2
        GIT_TAG v3.16.0
        SYSTEM YES
)

list(APPEND CMAKE_MODULE_PATH ${Catch2_SOURCE_DIR}/extras)
include(CTest)
include(Catch)

# Fixtures the unit and acceptance tests share, header-only.
add_library(pf_test_support INTERFACE)
target_include_directories(pf_test_support INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/support)

# Unit tests, one executable per library.
add_subdirectory(unit)
# Acceptance scenarios: whole matches, determinism and stability.
add_subdirectory(acceptance)

# sim-match's headers keep their layers (docs/match-engine.md#header-layers).
add_test(NAME sim-match-header-layers
        COMMAND ${CMAKE_COMMAND}
        -DINCLUDE_DIR=${PROJECT_SOURCE_DIR}/libs/sim-match/include
                -P ${CMAKE_CURRENT_SOURCE_DIR}/architecture/headerLayers.cmake
)
set_tests_properties(sim-match-header-layers PROPERTIES LABELS architecture)
add_test(NAME sim-cli-smoke
        COMMAND ${CMAKE_COMMAND}
        -DSIM_CLI=$<TARGET_FILE:sim-cli>
        -DTEST_OUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/sim-cli-smoke
        -DDATA_DIR=${PROJECT_SOURCE_DIR}/data
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cli/smoke.cmake
)
add_test(NAME sim-benchmark-smoke
        COMMAND ${CMAKE_COMMAND}
        -DSIM_BENCHMARK=$<TARGET_FILE:sim-benchmark>
        -DDATA_DIR=${PROJECT_SOURCE_DIR}/data
        -DTEST_OUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/sim-benchmark-smoke
                -P ${CMAKE_CURRENT_SOURCE_DIR}/benchmark/smoke.cmake
)
# The reduced P2 style benchmark takes minutes in a Debug build: Release
# only, run by its own CI job (ctest -L benchmark).
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_test(NAME p2-style-benchmark
            COMMAND ${CMAKE_COMMAND}
            -DSIM_BENCHMARK=$<TARGET_FILE:sim-benchmark>
            -DDATA_DIR=${PROJECT_SOURCE_DIR}/data
            -DTEST_OUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/p2-style-benchmark
            -P ${CMAKE_CURRENT_SOURCE_DIR}/benchmark/p2StyleBenchmark.cmake
    )
    set_tests_properties(p2-style-benchmark PROPERTIES LABELS benchmark)
    add_test(NAME eleven-a-side-budget
            COMMAND ${CMAKE_COMMAND}
            -DSIM_CLI=$<TARGET_FILE:sim-cli>
            -DTEST_OUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/eleven-a-side-budget
            -P ${CMAKE_CURRENT_SOURCE_DIR}/benchmark/elevenASideBudget.cmake
    )
    set_tests_properties(eleven-a-side-budget PROPERTIES LABELS benchmark)
    add_test(NAME p3-acceptance
            COMMAND ${CMAKE_COMMAND}
            -DSIM_BENCHMARK=$<TARGET_FILE:sim-benchmark>
            -DDATA_DIR=${PROJECT_SOURCE_DIR}/data
            -DTEST_OUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/p3-acceptance
            -P ${CMAKE_CURRENT_SOURCE_DIR}/benchmark/p3Acceptance.cmake
    )
    set_tests_properties(p3-acceptance PROPERTIES LABELS benchmark)
endif()
