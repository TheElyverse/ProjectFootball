# The performance budget of an eleven-a-side match (docs/match-engine.md#performance):
# one 90-minute match of the eleven-a-side reference tactic against itself on
# the standard pitch, "tactic-match-eleven", must finish within 45 s of wall
# time in a Release build. Invoked by CTest with SIM_CLI set.

cmake_minimum_required(VERSION 3.25)

set(budgetSeconds 45)
string(TIMESTAMP started "%s" UTC)
execute_process(
        COMMAND "${SIM_CLI}" --scenario tactic-match-eleven --seed 1 --ticks 162000
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
