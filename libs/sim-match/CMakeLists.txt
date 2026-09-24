add_library(sim-match STATIC
        src/pitch.cpp
        src/matchState.cpp
        src/kickoffScenario.cpp
        src/matchSimulation.cpp
        src/playerMovement.cpp
        src/ballMovement.cpp
        src/matchStateHash.cpp
        src/matchSetup.cpp
        src/scenarios.cpp
        src/spatialQueries.cpp
        src/perception.cpp
        src/passing.cpp
        src/reception.cpp
        src/pursuit.cpp
        src/passCandidates.cpp
)
add_library(ElyverseFootball::sim-match ALIAS sim-match)

target_include_directories(sim-match PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_compile_features(sim-match PUBLIC cxx_std_23)
target_link_libraries(sim-match
        PUBLIC ElyverseFootball::sim-core
        PRIVATE pf_compiler_warnings
)
