add_library(mac STATIC
    src/mac_pdu.cpp
    src/rach_common.cpp
    src/rach_ue.cpp
    src/rach_bs.cpp
)

target_include_directories(mac PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(mac PUBLIC common)
