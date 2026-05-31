include(FetchContent)

FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/release-1.12.1.zip
)

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(stack_tests
    example_test.cpp
)

target_link_libraries(stack_tests PRIVATE
    common
    gtest_main
)

add_executable(phy_tests
    ../phy/tests/test_qpsk.cpp
)

target_link_libraries(phy_tests PRIVATE
    phy
    gtest_main
)

include(GoogleTest)
gtest_discover_tests(stack_tests)
gtest_discover_tests(phy_tests)
