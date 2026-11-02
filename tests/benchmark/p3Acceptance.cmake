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
        TIMEOUT 900
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

# Every match plays to full time, and nobody stands on the ball or carries it
# about for good over the 90 minutes: no player owns it longer than 20 s at a
# time (8 to 13 s measured), as in the P2 style benchmark.
set(maxOwnershipSeconds 20)
math(EXPR lastPairing "${pairings} - 1")
foreach(index RANGE ${lastPairing})
    string(JSON matches LENGTH "${json}" pairings ${index} results)
    math(EXPR lastMatch "${matches} - 1")
    foreach(match RANGE ${lastMatch})
        string(JSON finished GET "${json}" pairings ${index} results ${match} stats finished)
        if(NOT finished)
            message(FATAL_ERROR "Pairing ${index}, match ${match} did not reach full time")
        endif()
        foreach(side IN ITEMS home away)
            string(JSON longest GET "${json}" pairings ${index} results ${match} stats ${side}
                   longestOwnershipSeconds)
            if(longest GREATER maxOwnershipSeconds)
                message(FATAL_ERROR
                        "Pairing ${index}, match ${match}: a ${side} player owned the ball for ${longest} s, more than ${maxOwnershipSeconds} s")
            endif()
        endforeach()
    endforeach()
endforeach()

# The guardrails: each style's mean per match, over its six sides of the round
# robin, inside its band. The baseline is calibrated to real football (#232),
# and the bands guard it against a change that breaks it;
# docs/scenarios.md#p3-acceptance gives the measurements, the reasoning for
# each band and real football's values.
#
#   metric          low   high   measured over seeds 1 to 5, all three styles
set(bands
        "goals          0.2   6"     # 0.5 to 3.8
        "shots          2     21"    # 3.3 to 13.3
        "passCompletion 0.68  0.88"  # 0.75 to 0.80
        "possessionShare 0.30 0.70"  # 0.39 to 0.57
        "fouls          1     24"    # 2.3 to 19.2
        "offsides       0     5"     # 0.3 to 4.0
        "corners        0.6   7"     # 1.2 to 4.0
        "throwIns       6     30"    # 12.8 to 23.8
        "goalKicks      1.8   13"    # 4.7 to 9.5
        "freeKicks      2     22"    # 4.0 to 16.0
        "penalties      0     1")    # 0.0 to 0.2
foreach(style IN ITEMS possession counter pressing)
    foreach(band IN LISTS bands)
        string(REGEX REPLACE " +" ";" fields "${band}")
        list(GET fields 0 metric)
        list(GET fields 1 low)
        list(GET fields 2 high)
        string(JSON count GET "${json}" styleSummary ${style} ${metric} count)
        string(JSON mean GET "${json}" styleSummary ${style} ${metric} mean)
        # Every side has the value: an empty mean -- no passes at all -- fails.
        if(NOT count EQUAL 6)
            message(FATAL_ERROR "${style}: ${metric} has ${count} values, expected one per side")
        endif()
        if(mean LESS low OR mean GREATER high)
            message(FATAL_ERROR
                    "${style}: ${metric} ${mean} per match, outside its band ${low} to ${high}")
        endif()
    endforeach()
endforeach()

# The rules fire: pressing, which takes carriers on most and keeps the highest
# line, commits fouls and catches opponents offside in every whole match (13.5
# to 19.2 fouls and 1.5 to 4.0 offsides per match measured over seeds 1 to 5),
# so a rule that stops being called fails here although the offsides band
# starts at 0. Penalties are too rare in nine matches to need one.
foreach(floor IN ITEMS "fouls 8" "offsides 1")
    string(REGEX REPLACE " +" ";" fields "${floor}")
    list(GET fields 0 metric)
    list(GET fields 1 low)
    string(JSON mean GET "${json}" styleSummary pressing ${metric} mean)
    if(mean LESS low)
        message(FATAL_ERROR "pressing: ${metric} ${mean} per match, below ${low}: is the rule called?")
    endif()
endforeach()
