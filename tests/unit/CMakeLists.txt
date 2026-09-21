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
)

target_link_libraries(sim-core-tests PRIVATE
        ElyverseFootball::sim-core
        Catch2::Catch2WithMain
        pf_compiler_warnings
)

include(CTest)
include(Catch)
catch_discover_tests(sim-core-tests)

add_test(NAME sim-cli-smoke
        COMMAND ${CMAKE_COMMAND}
        -DSIM_CLI=$<TARGET_FILE:sim-cli>
        -DTEST_OUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/sim-cli-smoke
        -P ${CMAKE_CURRENT_SOURCE_DIR}/../cli/smoke.cmake
)
