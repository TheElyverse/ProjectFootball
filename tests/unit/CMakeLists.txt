CPMAddPackage(
  NAME Catch2
  GITHUB_REPOSITORY catchorg/Catch2
  GIT_TAG v3.16.0
)

list(APPEND CMAKE_MODULE_PATH ${Catch2_SOURCE_DIR}/extras)

add_executable(sim-core-tests
  sim-core/strong_id_tests.cpp
  sim-core/sim_time_tests.cpp
  sim-core/rng_tests.cpp
  sim-core/event_bus_tests.cpp
)

target_link_libraries(sim-core-tests PRIVATE
  ElyverseFootball::sim-core
  Catch2::Catch2WithMain
  pf_compiler_warnings
)

include(CTest)
include(Catch)
catch_discover_tests(sim-core-tests)
