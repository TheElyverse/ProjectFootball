# End-to-end check of sim-benchmark: a short series, the same results file
# whatever the number of jobs, and argument errors. Invoked by CTest with
# SIM_BENCHMARK, DATA_DIR and TEST_OUTPUT_DIR set.

file(MAKE_DIRECTORY "${TEST_OUTPUT_DIR}")

macro(run_benchmark)
    execute_process(
            COMMAND "${SIM_BENCHMARK}" ${ARGN}
            RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error
            TIMEOUT 120
    )
endmacro()

set(tactics --home "${DATA_DIR}/tactics/pressing.json" --away "${DATA_DIR}/tactics/counter.json")
run_benchmark(${tactics} --matches 2 --minutes 1 --seed 5 --out "${TEST_OUTPUT_DIR}/serial.json")
if(NOT result STREQUAL "0" OR NOT output MATCHES "match +1 +seed"
        OR NOT output MATCHES "possessionShare +home")
    message(FATAL_ERROR "Benchmark failed: ${result}: ${output}${error}")
endif()
run_benchmark(${tactics} --matches 2 --minutes 1 --seed 5 --jobs 2
        --out "${TEST_OUTPUT_DIR}/parallel.json")
file(READ "${TEST_OUTPUT_DIR}/serial.json" serial)
file(READ "${TEST_OUTPUT_DIR}/parallel.json" parallel)
if(NOT serial STREQUAL parallel)
    message(FATAL_ERROR "Results depend on the number of jobs")
endif()
string(JSON matches GET "${serial}" matches)
string(JSON homeName GET "${serial}" home name)
if(NOT matches EQUAL 2 OR NOT homeName STREQUAL "pressing")
    message(FATAL_ERROR "Unexpected results: ${serial}")
endif()

run_benchmark(--home "${DATA_DIR}/tactics/pressing.json")
if(result STREQUAL "0" OR NOT error MATCHES "--home and --away are required")
    message(FATAL_ERROR "Expected a missing --away to fail: ${result}: ${error}")
endif()
run_benchmark(${tactics} --matches 0)
if(result STREQUAL "0" OR NOT error MATCHES "invalid --matches value")
    message(FATAL_ERROR "Expected --matches 0 to fail: ${result}: ${error}")
endif()

run_benchmark(--style "${DATA_DIR}/tactics/pressing.json" --style "${DATA_DIR}/tactics/pressing.json"
        --matches 2 --minutes 1 --seed 5 --out "${TEST_OUTPUT_DIR}/duplicate.json")
if(result STREQUAL "0" OR NOT error MATCHES "has the same name")
    message(FATAL_ERROR "Expected a duplicate --style name to fail: ${result}: ${error}")
endif()
