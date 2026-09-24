# End-to-end checks of sim-cli: runs, replay files, playback and argument
# validation. Invoked by CTest with SIM_CLI and TEST_OUTPUT_DIR set.

file(MAKE_DIRECTORY "${TEST_OUTPUT_DIR}")
set(replay "${TEST_OUTPUT_DIR}/replay.json")

# Runs sim-cli with the given arguments; sets result, output and error.
macro(run_cli)
    execute_process(
            COMMAND "${SIM_CLI}" ${ARGN}
            RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error
            TIMEOUT 30
    )
endmacro()

# Expects a nonzero exit and an error message matching `pattern`.
macro(expect_failure pattern)
    run_cli(${ARGN})
    if(result STREQUAL "0" OR NOT error MATCHES "${pattern}")
        message(FATAL_ERROR "Expected failure matching '${pattern}' for '${ARGN}': "
                "${result}: ${output}${error}")
    endif()
endmacro()

# A documented headless run of the 7v7 scenario.
run_cli(--scenario rolling-ball --seed 42 --ticks 300 --replay-out "${replay}")
if(NOT result STREQUAL "0" OR NOT output MATCHES "ticks: +300\n" OR NOT output MATCHES "time: +10 s\n"
        OR NOT output MATCHES "state hash: +([0-9a-f]+)\n")
    message(FATAL_ERROR "Scenario run failed: ${result}: ${output}${error}")
endif()
set(runHash "${CMAKE_MATCH_1}")
file(READ "${replay}" contents)
string(JSON seed GET "${contents}" seed)
string(JSON seedType TYPE "${contents}" seed)
string(JSON gameTime GET "${contents}" gameTime)
string(JSON schemaVersion GET "${contents}" schemaVersion)
if(NOT seedType STREQUAL "STRING" OR NOT seed STREQUAL "42" OR NOT gameTime STREQUAL "300"
        OR NOT schemaVersion STREQUAL "2")
    message(FATAL_ERROR "Unexpected replay: ${contents}")
endif()

# The saved replay reproduces through the CLI.
run_cli(--play "${replay}")
if(NOT result STREQUAL "0" OR NOT output MATCHES "state hash: +${runHash}\n"
        OR NOT output MATCHES "checkpoints: +11 verified")
    message(FATAL_ERROR "Replay playback failed: ${result}: ${output}${error}")
endif()

# A replay that does not reproduce is reported, with a nonzero exit.
string(JSON firstHash GET "${contents}" checkpoints 1 stateHash)
string(REPLACE "\"${firstHash}\"" "\"0000000000000000\"" tampered "${contents}")
file(WRITE "${TEST_OUTPUT_DIR}/tampered.json" "${tampered}")
expect_failure("state hash at tick 30" --play "${TEST_OUTPUT_DIR}/tampered.json")

# Keep UINT64_MAX as text throughout: a JSON number may be rounded by consumers.
# Feed the decoded seed back into the CLI to exercise the full round trip.
set(maxSeed "18446744073709551615")
set(roundTripSeed "${maxSeed}")
foreach(pass IN ITEMS initial round-trip)
    run_cli(--seed "${roundTripSeed}" --ticks 0 --replay-out "${replay}")
    if(NOT result STREQUAL "0")
        message(FATAL_ERROR "UINT64_MAX ${pass} run failed: ${result}: ${output}${error}")
    endif()
    file(READ "${replay}" contents)
    string(JSON seedType TYPE "${contents}" seed)
    string(JSON roundTripSeed GET "${contents}" seed)
    if(NOT seedType STREQUAL "STRING" OR NOT roundTripSeed STREQUAL maxSeed)
        message(FATAL_ERROR "UINT64_MAX ${pass} replay lost seed precision: ${contents}")
    endif()
endforeach()

run_cli(--list-scenarios)
if(NOT result STREQUAL "0" OR NOT output MATCHES "kickoff")
    message(FATAL_ERROR "Listing scenarios failed: ${result}: ${output}${error}")
endif()

# CTest captures stdout: an interactive request must fail promptly, before
# creating or overwriting a replay.
file(REMOVE "${replay}")
expect_failure("requires an interactive terminal" --tui --seed 42 --replay-out "${replay}")
if(EXISTS "${replay}")
    message(FATAL_ERROR "Rejected TUI run created a replay")
endif()

expect_failure("invalid --seed" --tui --seed invalid --replay-out "${replay}")
expect_failure("invalid --ticks value '-5'" --ticks -5)
expect_failure("invalid --ticks value '1x'" --ticks 1x)
expect_failure("unknown scenario 'nope'" --scenario nope --replay-out "${replay}")
expect_failure("unknown argument '--fast'" --fast)
expect_failure("--seed requires a value" --seed)
expect_failure("cannot be combined" --play "${replay}" --seed 1)
expect_failure("cannot be combined" --play "${replay}" --seed 1 --list-scenarios)
expect_failure("cannot be combined" --list-scenarios --play "${replay}")
expect_failure("cannot be combined" --seed 1 --list-scenarios)
expect_failure("given twice" --play "${replay}" --play "${replay}")
run_cli(--help --play "${replay}" --seed 1)
if(NOT result STREQUAL "0" OR NOT output MATCHES "usage:")
    message(FATAL_ERROR "--help did not win: ${result}: ${output}${error}")
endif()
expect_failure("cannot read" --play "${TEST_OUTPUT_DIR}/missing.json")
file(WRITE "${TEST_OUTPUT_DIR}/broken.json" "{ not json")
expect_failure("not a valid JSON document" --play "${TEST_OUTPUT_DIR}/broken.json")
