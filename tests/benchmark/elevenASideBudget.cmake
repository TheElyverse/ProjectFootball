# The performance budget of an eleven-a-side match (docs/match-engine.md#performance):
# one 90-minute match of the eleven-a-side reference tactic against itself on
# the standard pitch, "tactic-match-eleven", must finish within 45 s of wall
# time in a Release build, and its replay must play back with every checkpoint
# verified. Invoked by CTest with SIM_CLI and TEST_OUTPUT_DIR set.

cmake_minimum_required(VERSION 3.25)

set(budgetSeconds 45)
file(MAKE_DIRECTORY "${TEST_OUTPUT_DIR}")
set(replay "${TEST_OUTPUT_DIR}/eleven-a-side.json")
string(TIMESTAMP started "%s" UTC)
execute_process(
        COMMAND "${SIM_CLI}" --scenario tactic-match-eleven --seed 1 --duration 90
        --replay-out "${replay}"
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error
        TIMEOUT 600
)
string(TIMESTAMP finished "%s" UTC)
math(EXPR seconds "${finished} - ${started}")
message(STATUS "${output}")
message(STATUS "A 90-minute eleven-a-side match took about ${seconds} s.")
if(NOT result STREQUAL "0")
    message(FATAL_ERROR "The eleven-a-side match failed: ${result}: ${error}")
endif()
if(seconds GREATER budgetSeconds)
    message(FATAL_ERROR "The eleven-a-side match took ${seconds} s, over its ${budgetSeconds} s")
endif()

execute_process(
        COMMAND "${SIM_CLI}" --play "${replay}"
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error
        TIMEOUT 600
)
if(NOT result STREQUAL "0")
    message(FATAL_ERROR "The 90-minute eleven-a-side replay did not play back: ${result}: ${error}")
endif()
