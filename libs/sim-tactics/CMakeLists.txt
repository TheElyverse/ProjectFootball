add_library(sim-tactics STATIC
        src/tacticalPhase.cpp
        src/responsibility.cpp
        src/rolePreset.cpp
        src/tactic.cpp
        src/referenceTactic.cpp
        src/tacticHash.cpp
        src/tacticJson.cpp
)
add_library(ElyverseFootball::sim-tactics ALIAS sim-tactics)

target_include_directories(sim-tactics PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_compile_features(sim-tactics PUBLIC cxx_std_23)
# Only sim-core: tactics are data about football, not about a running match.
# sim-match depends on this library, never the reverse.
target_link_libraries(sim-tactics
        PUBLIC ElyverseFootball::sim-core
        PRIVATE nlohmann_json::nlohmann_json pf_compiler_warnings
)
