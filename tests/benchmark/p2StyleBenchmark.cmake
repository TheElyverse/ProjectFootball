# P2 acceptance, reduced for CI (docs/sim-benchmark.md): the three tactical
# identities in a round robin of every pairing, six six-minute matches each.
# The run must finish within its time budget without an invalid value, a
# sampled match of every pairing must replay identically, and the styles must
# separate where their identities promise. Invoked by CTest with
# SIM_BENCHMARK, DATA_DIR and TEST_OUTPUT_DIR set; registered for Release
# builds only.

cmake_minimum_required(VERSION 3.25)

file(MAKE_DIRECTORY "${TEST_OUTPUT_DIR}")
set(results "${TEST_OUTPUT_DIR}/p2-styles.json")
execute_process(
        COMMAND "${SIM_BENCHMARK}"
        --style "${DATA_DIR}/tactics/possession.json"
        --style "${DATA_DIR}/tactics/counter.json"
        --style "${DATA_DIR}/tactics/pressing.json"
        --matches 6 --minutes 6 --seed 1 --jobs 2 --verify-replays 1
        --max-seconds-per-match 5 --max-seconds-total 120
        --out "${results}"
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error
        TIMEOUT 600
)
message(STATUS "${output}")
if(NOT result STREQUAL "0")
    message(FATAL_ERROR "The style benchmark failed: ${result}: ${error}")
endif()

file(READ "${results}" json)
string(JSON pairings LENGTH "${json}" pairings)
if(NOT pairings EQUAL 9)
    message(FATAL_ERROR "Expected all nine pairings, got ${pairings}")
endif()

# The separations the identities promise (docs/tactical-identities.md).
set(expected
        "regains:pressing:possession" "regains:pressing:counter"
        "regainsAttackingThird:pressing:possession" "regainsAttackingThird:pressing:counter"
        "ppda:possession:pressing" "ppda:counter:pressing")
string(JSON count LENGTH "${json}" separations)
set(found "")
if(count GREATER 0)
    math(EXPR last "${count} - 1")
    foreach(index RANGE ${last})
        string(JSON metric GET "${json}" separations ${index} metric)
        string(JSON higher GET "${json}" separations ${index} higher)
        string(JSON lower GET "${json}" separations ${index} lower)
        list(APPEND found "${metric}:${higher}:${lower}")
    endforeach()
endif()
foreach(separation IN LISTS expected)
    if(NOT separation IN_LIST found)
        message(FATAL_ERROR "Expected the separation ${separation}; found ${found}")
    endif()
endforeach()
