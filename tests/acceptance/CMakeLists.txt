add_executable(sim-acceptance-tests
        m0AcceptanceTests.cpp
                p1AcceptanceTests.cpp
        p2AcceptanceTests.cpp
)
target_link_libraries(sim-acceptance-tests PRIVATE
        ElyverseFootball::sim-replay
        Catch2::Catch2WithMain
        pf_compiler_warnings
)
catch_discover_tests(sim-acceptance-tests PROPERTIES LABELS acceptance)
