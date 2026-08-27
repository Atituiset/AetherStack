add_library(phy STATIC
    src/qpsk.cpp
    src/qam.cpp
    src/ofdm.cpp
    src/phy_io.cpp
    src/fec.cpp
    src/sync.cpp
    src/frame.cpp
)

target_include_directories(phy PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(phy PUBLIC common)
