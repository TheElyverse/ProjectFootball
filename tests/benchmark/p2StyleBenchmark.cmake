# P2 acceptance, reduced for CI (docs/tooling.md#sim-benchmark): the three
# tactical identities, eleven a side, in a round robin of every pairing,
# twenty six-minute matches each. The run must finish within its time budget
# without an invalid value, a sampled match of every pairing must replay identically, and the
# styles must separate where their identities promise. Invoked by CTest with
# SIM_BENCHMARK, DATA_DIR and TEST_OUTPUT_DIR set; registered for Release builds
# only.

cmake_minimum_required(VERSION 3.25)

file(MAKE_DIRECTORY "${TEST_OUTPUT_DIR}")
set(results "${TEST_OUTPUT_DIR}/p2-styles.json")
execute_process(
        COMMAND "${SIM_BENCHMARK}"
        --style "${DATA_DIR}/tactics/possession.json"
        --style "${DATA_DIR}/tactics/counter.json"
        --style "${DATA_DIR}/tactics/pressing.json"
        --matches 20 --minutes 6 --seed 1 --jobs 2 --verify-replays 1
        --max-seconds-per-match 5 --max-seconds-total 450
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

# The separations the identities promise (docs/tactics.md#tactical-identities).
# Two that twenty eleven-a-side matches show for most seeds are not among them:
# pressing's regains in the attacking third over possession's (seed 5 misses
# it) and possession's PPDA over pressing's (seed 4 misses it).
set(expected
        "regains:pressing:possession" "regains:pressing:counter"
        "regainsAttackingThird:pressing:counter"
        "ppda:counter:pressing" "ppda:counter:possession")
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

# How often the sides shoot (docs/on-ball.md#shot-decisions): every side of
# every pairing shoots, and none shoots at every chance. The band is set from
# the first measurement -- 2 to 27 shots a side in six minutes, 3 to 13 since
# goals restart play from the centre, 1 to 9 since both sides line up for that
# kickoff, 0.67 to 11.67 since the keeper dives, holds or parries shots and
# picks up slow balls (possession against counter's deep block), 1.62 to 11.88
# since players carry the ball (docs/on-ball.md#ball-carrying), 3.3 to 12.1
# over twenty matches since they curl and chip (docs/on-ball.md#shot-types),
# 3.7 to 14.1 over thirty since they block shots (docs/duels.md#tackles-and-blocks),
# 3.3 to 13.7 since they cross (docs/on-ball.md#crossing), and 1.3 to 7.0 --
# 0.7 at worst over seeds 1 to 5 -- since the identities play eleven a side on
# the standard pitch -- with room above; it guards against a change that stops
# the shooting or makes a shooting gallery of the pitch, not against balancing.
set(minShots 0.25)
set(maxShots 40)
math(EXPR lastPairing "${pairings} - 1")
foreach(index RANGE ${lastPairing})
    foreach(side IN ITEMS home away)
        string(JSON shots GET "${json}" pairings ${index} summary ${side} shots mean)
        string(JSON style GET "${json}" pairings ${index} ${side})
        if(shots LESS minShots OR shots GREATER maxShots)
            message(FATAL_ERROR
                    "Pairing ${index}: ${side} (${style}) took ${shots} shots, expected ${minShots} to ${maxShots}")
        endif()
    endforeach()
endforeach()

# Nobody stands on the ball or carries it about for good
# (docs/on-ball.md#pass-decisions, docs/team-tactics.md#pressing,
# docs/on-ball.md#ball-carrying): in every match no player of either side owns
# it longer than 15 s at a time. A carrier without an option once kept it for up
# to 318 s against counter's deep block; since players carry the ball, touch
# after touch, the longest is 12.7 s.
set(maxOwnershipSeconds 15)
foreach(index RANGE ${lastPairing})
    string(JSON matches LENGTH "${json}" pairings ${index} results)
    math(EXPR lastMatch "${matches} - 1")
    foreach(match RANGE ${lastMatch})
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
