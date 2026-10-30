# P3 acceptance, reduced for CI (docs/scenarios.md#p3-acceptance): the three
# tactical identities in a round robin of every pairing, one whole 90-minute
# eleven-a-side match each with every rule on -- restarts and set pieces,
# offside, fouls and cards. The run must finish within its time budget without
# an invalid value, every match must reach full time, a sampled match of every
# pairing must replay identically, and each style's statistics per match must
# fall inside their guardrail bands. Invoked by CTest with SIM_BENCHMARK,
# DATA_DIR and TEST_OUTPUT_DIR set; registered for Release builds only.

cmake_minimum_required(VERSION 3.25)

file(MAKE_DIRECTORY "${TEST_OUTPUT_DIR}")
set(results "${TEST_OUTPUT_DIR}/p3-styles.json")
execute_process(
        COMMAND "${SIM_BENCHMARK}"
        --style "${DATA_DIR}/tactics/possession.json"
        --style "${DATA_DIR}/tactics/counter.json"
        --style "${DATA_DIR}/tactics/pressing.json"
        --matches 1 --minutes 90 --seed 1 --jobs 2 --verify-replays 1
        --max-seconds-per-match 45 --max-seconds-total 300
        --out "${results}"
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error
        TIMEOUT 600
)
message(STATUS "${output}")
if(NOT result STREQUAL "0")
    message(FATAL_ERROR "The P3 round robin failed: ${result}: ${error}")
endif()

file(READ "${results}" json)
string(JSON pairings LENGTH "${json}" pairings)
if(NOT pairings EQUAL 9)
    message(FATAL_ERROR "Expected all nine pairings, got ${pairings}")
endif()
string(JSON restarts GET "${json}" restarts)
string(JSON offside GET "${json}" offside)
string(JSON fouls GET "${json}" fouls)
if(NOT restarts OR NOT offside OR NOT fouls)
    message(FATAL_ERROR "Expected every rule on: restarts ${restarts}, offside ${offside}, fouls ${fouls}")
endif()

# Every match plays to full time.
math(EXPR lastPairing "${pairings} - 1")
foreach(index RANGE ${lastPairing})
    string(JSON matches LENGTH "${json}" pairings ${index} results)
    math(EXPR lastMatch "${matches} - 1")
    foreach(match RANGE ${lastMatch})
        string(JSON finished GET "${json}" pairings ${index} results ${match} stats finished)
        if(NOT finished)
            message(FATAL_ERROR "Pairing ${index}, match ${match} did not reach full time")
        endif()
    endforeach()
endforeach()

# The guardrails: each style's mean per match, over its six sides of the round
# robin, inside its band. The bands guard the measured baseline against a
# change that breaks it, not against the numbers of real football, which the
# baseline does not reach yet; docs/scenarios.md#p3-acceptance gives the
# measurements, the reasoning for each band and real football's values.
#
#   metric          low   high   measured over seeds 1 to 5, all three styles
set(bands
        "goals          5     55"    # 11.2 to 36.3
        "shots          14    125"   # 28.5 to 82.0
        "passCompletion 0.60  0.80"  # 0.67 to 0.73
        "possessionShare 0.35 0.65"  # 0.44 to 0.55
        "fouls          0     15"    # 0.0 to 8.2
        "offsides       0     15"    # 0.3 to 9.7
        "corners        0.5   9"     # 1.3 to 5.5
        "throwIns       0.5   8"     # 1.8 to 4.8
        "goalKicks      1.5   16"    # 3.3 to 10.3
        "freeKicks      0.5   16"    # 1.3 to 10.5
        "penalties      0     1")    # 0.0 to 0.2
foreach(style IN ITEMS possession counter pressing)
    foreach(band IN LISTS bands)
        string(REGEX REPLACE " +" ";" fields "${band}")
        list(GET fields 0 metric)
        list(GET fields 1 low)
        list(GET fields 2 high)
        string(JSON mean GET "${json}" styleSummary ${style} ${metric} mean)
        if(mean LESS low OR mean GREATER high)
            message(FATAL_ERROR
                    "${style}: ${metric} ${mean} per match, outside its band ${low} to ${high}")
        endif()
    endforeach()
endforeach()
