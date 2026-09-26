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
        OR NOT output MATCHES "event hash: +([0-9a-f]+)\n")
    message(FATAL_ERROR "Scenario run failed: ${result}: ${output}${error}")
endif()
set(runEventHash "${CMAKE_MATCH_1}")
string(REGEX MATCH "state hash: +([0-9a-f]+)\n" stateHashLine "${output}")
set(runHash "${CMAKE_MATCH_1}")
file(READ "${replay}" contents)
string(JSON seed GET "${contents}" seed)
string(JSON seedType TYPE "${contents}" seed)
string(JSON gameTime GET "${contents}" gameTime)
string(JSON schemaVersion GET "${contents}" schemaVersion)
if(NOT seedType STREQUAL "STRING" OR NOT seed STREQUAL "42" OR NOT gameTime STREQUAL "300"
        OR NOT schemaVersion STREQUAL "4")
    message(FATAL_ERROR "Unexpected replay: ${contents}")
endif()

# The saved replay reproduces through the CLI.
run_cli(--play "${replay}")
if(NOT result STREQUAL "0" OR NOT output MATCHES "state hash: +${runHash}\n"
        OR NOT output MATCHES "event hash: +${runEventHash}\n"
        OR NOT output MATCHES "checkpoints: +11 verified")
    message(FATAL_ERROR "Replay playback failed: ${result}: ${output}${error}")
endif()

# Debug frames for the viewer come from the same run: collecting them leaves
# the match, and so the state hash, unchanged.
set(frames "${TEST_OUTPUT_DIR}/frames.json")
run_cli(--scenario rolling-ball --seed 42 --ticks 300 --replay-out "${replay}"
        --frames-out "${frames}")
if(NOT result STREQUAL "0" OR NOT output MATCHES "state hash: +${runHash}\n"
        OR NOT output MATCHES "frames: +[^\n]*frames.json\n")
    message(FATAL_ERROR "Run with frames failed: ${result}: ${output}${error}")
endif()
file(READ "${frames}" framesContents)
string(JSON framesFormat GET "${framesContents}" format)
string(JSON frameCount LENGTH "${framesContents}" frames)
string(JSON lastTick GET "${framesContents}" frames 300 tick)
string(JSON lastHash GET "${framesContents}" frames 300 stateHash)
if(NOT framesFormat STREQUAL "elyverse-debug-frames" OR NOT frameCount STREQUAL "301"
        OR NOT lastTick STREQUAL "300" OR NOT lastHash STREQUAL runHash)
    message(FATAL_ERROR "Unexpected debug frames: ${framesFormat} ${frameCount} ${lastTick}")
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
expect_failure("cannot be combined" --play "${replay}" --frames-out "${frames}")
expect_failure("--frames-out records at most 3600 ticks, got --ticks 3601" --ticks 3601
        --replay-out "${replay}" --frames-out "${frames}")
expect_failure("name the same file" --ticks 1 --replay-out "${replay}" --frames-out "${replay}")
expect_failure("name the same file" --ticks 1 --frames-out replay.json)
expect_failure("cannot open for writing" --ticks 1 --replay-out "${replay}"
        --frames-out "${TEST_OUTPUT_DIR}/missing/frames.json")
expect_failure("cannot read" --play "${TEST_OUTPUT_DIR}/missing.json")
file(WRITE "${TEST_OUTPUT_DIR}/broken.json" "{ not json")
expect_failure("not a valid JSON document" --play "${TEST_OUTPUT_DIR}/broken.json")

# A replay longer than a run may be is refused before playback starts.
run_cli(--ticks 10 --replay-out "${replay}")
file(READ "${replay}" contents)
string(JSON longReplay SET "${contents}" gameTime 9007199254740992)
file(WRITE "${TEST_OUTPUT_DIR}/long.json" "${longReplay}")
expect_failure("plays at most 10000000" --play "${TEST_OUTPUT_DIR}/long.json")

# Two tactic files face each other in the tactic match, and the replay plays
# back without them.
run_cli(--home-tactic "${DATA_DIR}/tactics/pressing.json" --away-tactic
        "${DATA_DIR}/tactics/counter.json" --seed 3 --ticks 90 --replay-out "${replay}")
if(NOT result STREQUAL "0" OR NOT output MATCHES "scenario: +tactic-match\n"
        OR NOT output MATCHES "home tactic: +pressing\n" OR NOT output MATCHES "away tactic: +counter\n")
    message(FATAL_ERROR "Tactic match failed: ${result}: ${output}${error}")
endif()
run_cli(--play "${replay}")
if(NOT result STREQUAL "0")
    message(FATAL_ERROR "Tactic match playback failed: ${result}: ${output}${error}")
endif()
expect_failure("only apply to --scenario tactic-match" --scenario kickoff --home-tactic
        "${DATA_DIR}/tactics/pressing.json")
expect_failure("cannot read" --away-tactic "${TEST_OUTPUT_DIR}/missing-tactic.json")
expect_failure("cannot be combined" --play "${replay}" --home-tactic
        "${DATA_DIR}/tactics/pressing.json")
expect_failure("name the same file" --seed 3 --ticks 1 --home-tactic "${replay}" --replay-out "${replay}")
expect_failure("name the same file" --seed 3 --ticks 1 --away-tactic "${DATA_DIR}/tactics/counter.json"
        --frames-out "${DATA_DIR}/tactics/counter.json")

# Match statistics come from the same run and are the same for the same run.
set(stats "${TEST_OUTPUT_DIR}/stats.json")
run_cli(--scenario tactic-match --seed 5 --ticks 300 --replay-out "${replay}" --stats-out "${stats}")
if(NOT result STREQUAL "0" OR NOT output MATCHES "stats: +[^\n]*stats.json\n")
    message(FATAL_ERROR "Run with stats failed: ${result}: ${output}${error}")
endif()
file(READ "${stats}" firstStats)
string(JSON statsFormat GET "${firstStats}" format)
if(NOT statsFormat STREQUAL "elyverse-match-stats")
    message(FATAL_ERROR "Unexpected stats: ${firstStats}")
endif()
run_cli(--scenario tactic-match --seed 5 --ticks 300 --replay-out "${replay}" --stats-out "${stats}")
file(READ "${stats}" secondStats)
if(NOT firstStats STREQUAL secondStats)
    message(FATAL_ERROR "The same run wrote different stats")
endif()
expect_failure("same file" --ticks 1 --replay-out "${replay}" --stats-out "${replay}")

# The decision trace of a playback: the P1 interception, blamed on the
# decision, and the playback still verifies.
set(trace "${TEST_OUTPUT_DIR}/trace.txt")
run_cli(--scenario intercepted-pass --seed 7 --ticks 150 --replay-out "${replay}")
run_cli(--play "${replay}" --trace "${trace}" --trace-players 1 --trace-to 120)
if(NOT result STREQUAL "0" OR NOT output MATCHES "checkpoints: +6 verified"
        OR NOT output MATCHES "trace: +[^\n]*trace.txt \\(1 decisions\\)")
    message(FATAL_ERROR "Trace failed: ${result}: ${output}${error}")
endif()
file(READ "${trace}" traceText)
if(NOT traceText MATCHES "^t=18 #1 passes to #2 .* -> intercepted by #8 at t=70: decision\n$")
    message(FATAL_ERROR "Unexpected trace: ${traceText}")
endif()
expect_failure("only apply to --play" --scenario kickoff --trace "${trace}")
expect_failure("need --trace" --play "${replay}" --trace-players 1)
expect_failure("invalid --trace-players" --play "${replay}" --trace "${trace}" --trace-players 1,x)
expect_failure("must not come after" --play "${replay}" --trace "${trace}" --trace-from 9 --trace-to 3)
expect_failure("name the same file" --play "${replay}" --trace "${replay}")

# A checkpoint per tick.
run_cli(--scenario kickoff --seed 1 --ticks 20 --checkpoint-interval 1 --replay-out "${replay}")
file(READ "${replay}" contents)
string(JSON checkpointCount LENGTH "${contents}" checkpoints)
string(JSON interval GET "${contents}" checkpointIntervalTicks)
if(NOT result STREQUAL "0" OR NOT checkpointCount EQUAL 21 OR NOT interval EQUAL 1)
    message(FATAL_ERROR "Checkpoint interval not recorded: ${result}: ${output}${error}")
endif()
expect_failure("invalid --checkpoint-interval" --checkpoint-interval 0)
