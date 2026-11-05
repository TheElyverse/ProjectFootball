# The header layers of sim-match (docs/match-engine.md#header-layers): a
# public header includes sim-match headers of its own layer or a lower one
# only, and every header belongs to a layer. Invoked by CTest with
# INCLUDE_DIR set to libs/sim-match/include.

# Lowest first.
set(layers model mechanics loop behaviour setup)
# The match state, its parts, the events and commands, and the data types of
# the candidates decisions weigh.
set(model
        actionCandidate carryCandidate choicePolicy matchCommand matchEvents matchState
        matchStateHash observation passCandidate pitch pitchControlGrid pressingActions
        crossArea crossCandidate restartKind shotCandidate shotType tacticalState
        teamPress)
# Pure functions of physics, geometry and execution, and the queries every
# decision asks.
set(mechanics
        aerialDuels ballPhysics carrierScan fitness goalFrame goalkeeper matchClock passing
        reception shooting spatialQueries teamFrame zones)
# The fixed-timestep loop and the interface of its systems.
set(loop matchSimulation)
# The decisions and the systems that run them.
set(behaviour
        ballMovement carrying defensiveActions desiredRegion distribution duel offBallActions
        crossCandidates matchClockSystem passCandidates passDecision perception pitchControl
        playerMovement
        pursuit restart
        shotCandidates shotStopping tacticalMovement tacticalPhases teamPressing)
# Configuration and fixtures that put whole matches together.
set(setup goldenScenarios kickoffScenario matchSetup scenarios)

foreach(index RANGE 4)
    list(GET layers ${index} layer)
    foreach(header IN LISTS ${layer})
        set(layerOf_${header} ${index})
    endforeach()
endforeach()

file(GLOB headers "${INCLUDE_DIR}/*.hpp")
set(problems "")
foreach(path IN LISTS headers)
    get_filename_component(header ${path} NAME_WE)
    if(NOT DEFINED layerOf_${header})
        string(APPEND problems "\n  ${header}.hpp belongs to no layer")
        continue()
    endif()
    file(STRINGS ${path} includes REGEX "^#include \"[A-Za-z]+\\.hpp\"")
    foreach(line IN LISTS includes)
        string(REGEX REPLACE "^#include \"([A-Za-z]+)\\.hpp\".*" "\\1" included "${line}")
        if(DEFINED layerOf_${included} AND layerOf_${included} GREATER layerOf_${header})
            list(GET layers ${layerOf_${header}} from)
            list(GET layers ${layerOf_${included}} to)
            string(APPEND problems "\n  ${header}.hpp (${from}) includes ${included}.hpp (${to})")
        endif()
    endforeach()
endforeach()

if(problems)
    message(FATAL_ERROR "sim-match headers break their layers:${problems}")
endif()
list(LENGTH headers count)
message(STATUS "${count} sim-match headers keep their layers")
