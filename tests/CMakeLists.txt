CPMAddPackage(
        NAME Catch2
        GITHUB_REPOSITORY catchorg/Catch2
        GIT_TAG v3.16.0
)

list(APPEND CMAKE_MODULE_PATH ${Catch2_SOURCE_DIR}/extras)
include(CTest)
include(Catch)

# Unit tests, one executable per library.
add_subdirectory(unit)
# Acceptance scenarios: whole matches, determinism and stability.
add_subdirectory(acceptance)

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
