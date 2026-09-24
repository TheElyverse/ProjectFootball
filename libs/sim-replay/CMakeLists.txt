CPMAddPackage(
        NAME nlohmann_json
        GITHUB_REPOSITORY nlohmann/json
        GIT_TAG v3.12.0
        OPTIONS
        "JSON_BuildTests OFF"
        "JSON_Install OFF"
)

add_library(sim-replay STATIC
        src/debugFrames.cpp
        src/replay.cpp
        src/replayJson.cpp
)
add_library(ElyverseFootball::sim-replay ALIAS sim-replay)

target_include_directories(sim-replay PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_compile_features(sim-replay PUBLIC cxx_std_23)
target_link_libraries(sim-replay
        PUBLIC ElyverseFootball::sim-match
        PRIVATE nlohmann_json::nlohmann_json pf_compiler_warnings
)
