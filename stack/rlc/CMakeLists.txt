add_library(rlc STATIC
    src/rlc_tm.cpp
    src/rlc_um.cpp
    src/rlc_am.cpp
)

target_include_directories(rlc PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(rlc PUBLIC common)
