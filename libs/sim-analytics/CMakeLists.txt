add_library(sim-analytics STATIC
        src/matchAnalyzer.cpp
        src/matchStats.cpp
        src/seriesSummary.cpp
        src/statsJson.cpp
)
add_library(ElyverseFootball::sim-analytics ALIAS sim-analytics)

target_include_directories(sim-analytics PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_compile_features(sim-analytics PUBLIC cxx_std_23)
# Read-only over sim-match's domain events: analytics never runs a system,
# never writes a state and never reads one beyond the match context.
target_link_libraries(sim-analytics
        PUBLIC ElyverseFootball::sim-match
        PRIVATE nlohmann_json::nlohmann_json pf_compiler_warnings
)
