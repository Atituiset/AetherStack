include(FetchContent)

FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/release-1.12.1.zip
)

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(stack_tests
    example_test.cpp
    ../tests/test_crypto.cpp
)

target_link_libraries(stack_tests PRIVATE
    common
    gtest_main
)

add_executable(phy_tests
    ../phy/tests/test_qpsk.cpp
    ../phy/tests/test_ofdm.cpp
    ../phy/tests/test_phy_io.cpp
    ../phy/tests/test_fec.cpp
    ../phy/tests/test_sync.cpp
)

target_link_libraries(phy_tests PRIVATE
    phy
    gtest_main
)

add_executable(mac_tests
    ../mac/tests/test_mac.cpp
)

target_link_libraries(mac_tests PRIVATE
    mac
    gtest_main
)

add_executable(rlc_tests
    ../rlc/tests/test_rlc_tm.cpp
)

target_link_libraries(rlc_tests PRIVATE
    rlc
    gtest_main
)

add_executable(pdcp_tests
    ../pdcp/tests/test_pdcp_entity.cpp
)

target_link_libraries(pdcp_tests PRIVATE
    pdcp
    rlc
    gtest_main
)

add_executable(vertical_tests
    test_vertical.cpp
)

target_link_libraries(vertical_tests PRIVATE
    pdcp
    rlc
    mac
    phy
    gtest_main
)

add_executable(rrc_tests
    ../rrc/tests/test_rrc_state.cpp
    ../rrc/tests/test_rrc_connection.cpp
)

target_link_libraries(rrc_tests PRIVATE
    rrc
    gtest_main
)

add_executable(nas_tests
    ../nas/tests/test_nas_attach.cpp
)

target_link_libraries(nas_tests PRIVATE
    nas
    gtest_main
)

add_executable(full_attach_tests
    test_full_attach.cpp
)

target_link_libraries(full_attach_tests PRIVATE
    rrc
    nas
    mac
    common
    gtest_main
)

add_executable(user_plane_tests
    test_user_plane.cpp
)

target_link_libraries(user_plane_tests PRIVATE
    app
    pdcp
    rlc
    mac
    phy
    common
    gtest_main
)

add_executable(core_tests
    ../core/tests/test_radio_frames.cpp
    ../core/tests/test_timer_list.cpp
)

target_link_libraries(core_tests PRIVATE
    core
    common
    gtest_main
)

add_executable(harq_tests
    test_harq.cpp
)

target_link_libraries(harq_tests PRIVATE
    core
    mac
    phy
    common
    gtest_main
)

add_executable(e2e_node_tests
    test_e2e_nodes.cpp
)

target_link_libraries(e2e_node_tests PRIVATE
    core
    mac
    rrc
    nas
    pdcp
    rlc
    common
    gtest_main
)

include(GoogleTest)
gtest_discover_tests(stack_tests)
gtest_discover_tests(core_tests)
gtest_discover_tests(e2e_node_tests)
gtest_discover_tests(harq_tests)
gtest_discover_tests(phy_tests)
gtest_discover_tests(mac_tests)
gtest_discover_tests(rlc_tests)
gtest_discover_tests(pdcp_tests)
gtest_discover_tests(vertical_tests)
gtest_discover_tests(rrc_tests)
gtest_discover_tests(nas_tests)
gtest_discover_tests(full_attach_tests)
gtest_discover_tests(user_plane_tests)
