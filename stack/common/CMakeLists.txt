add_library(common STATIC
    src/logger.cpp
    src/udp_transport.cpp
)

target_include_directories(common PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

set_target_properties(common PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)
