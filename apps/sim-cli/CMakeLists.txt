add_executable(sim-cli src/main.cpp)
target_link_libraries(sim-cli PRIVATE ElyverseFootball::sim-core pf_compiler_warnings)
