# The benchmark's logic as a library, so tests can run it without the CLI.
add_library(sim-benchmark-core STATIC src/benchmark.cpp)
target_include_directories(sim-benchmark-core PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
)
target_compile_features(sim-benchmark-core PUBLIC cxx_std_23)
target_link_libraries(sim-benchmark-core
        PUBLIC ElyverseFootball::sim-analytics ElyverseFootball::sim-replay ElyverseFootball::sim-tactics
        PRIVATE nlohmann_json::nlohmann_json pf_compiler_warnings
)

add_executable(sim-benchmark src/main.cpp)
target_link_libraries(sim-benchmark PRIVATE sim-benchmark-core pf_compiler_warnings)
