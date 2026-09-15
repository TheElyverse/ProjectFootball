add_library(sim-core STATIC
  src/rng.cpp
  src/version.cpp
)
add_library(ElyverseFootball::sim-core ALIAS sim-core)

target_include_directories(sim-core PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_compile_features(sim-core PUBLIC cxx_std_20)
target_link_libraries(sim-core PRIVATE pf_compiler_warnings)
