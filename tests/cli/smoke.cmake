file(MAKE_DIRECTORY "${TEST_OUTPUT_DIR}")
set(replay "${TEST_OUTPUT_DIR}/replay.json")

execute_process(
        COMMAND "${SIM_CLI}" --seed 42 --replay-out "${replay}"
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error
        TIMEOUT 10
)
if(NOT result STREQUAL "0" OR NOT output MATCHES "Started empty simulation")
    message(FATAL_ERROR "Noninteractive run failed: ${result}: ${output}${error}")
endif()
file(READ "${replay}" metadata)
string(JSON seed GET "${metadata}" seed)
string(JSON seedType TYPE "${metadata}" seed)
string(JSON gameTime GET "${metadata}" gameTime)
if(NOT seedType STREQUAL "STRING" OR NOT seed STREQUAL "42" OR NOT gameTime STREQUAL "0")
    message(FATAL_ERROR "Unexpected replay metadata: ${metadata}")
endif()

# Keep UINT64_MAX as text throughout: a JSON number may be rounded by consumers.
# Feed the decoded seed back into the CLI to exercise the full round trip.
set(maxSeed "18446744073709551615")
set(roundTripSeed "${maxSeed}")
foreach(pass IN ITEMS initial round-trip)
    execute_process(
            COMMAND "${SIM_CLI}" --seed "${roundTripSeed}" --replay-out "${replay}"
            RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error
            TIMEOUT 10
    )
    if(NOT result STREQUAL "0")
        message(FATAL_ERROR "UINT64_MAX ${pass} run failed: ${result}: ${output}${error}")
    endif()
    file(READ "${replay}" metadata)
    string(JSON seedType TYPE "${metadata}" seed)
    string(JSON roundTripSeed GET "${metadata}" seed)
    string(JSON schemaVersion GET "${metadata}" schemaVersion)
    if(NOT seedType STREQUAL "STRING" OR NOT roundTripSeed STREQUAL maxSeed
            OR NOT schemaVersion STREQUAL "1")
        message(FATAL_ERROR "UINT64_MAX ${pass} metadata lost seed precision: ${metadata}")
    endif()
endforeach()

# CTest captures stdout: an interactive request must fail promptly, before
# creating or overwriting replay metadata.
file(REMOVE "${replay}")
execute_process(
        COMMAND "${SIM_CLI}" --tui --seed 42 --replay-out "${replay}"
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error
        TIMEOUT 10
)
if(NOT result STREQUAL "1" OR NOT error MATCHES "requires an interactive terminal")
    message(FATAL_ERROR "Expected terminal validation failure: ${result}: ${output}${error}")
endif()
if(EXISTS "${replay}")
    message(FATAL_ERROR "Rejected TUI run created replay metadata")
endif()

execute_process(
        COMMAND "${SIM_CLI}" --tui --seed invalid --replay-out "${replay}"
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error
        TIMEOUT 10
)
if(NOT result STREQUAL "1" OR NOT error MATCHES "invalid --seed")
    message(FATAL_ERROR "Expected argument validation failure: ${result}: ${output}${error}")
endif()
