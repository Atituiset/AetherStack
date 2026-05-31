add_library(phy STATIC
    src/qpsk.cpp
)

target_include_directories(phy PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(phy PUBLIC common)
