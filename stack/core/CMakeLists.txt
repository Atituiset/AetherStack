add_library(core STATIC
    src/radio_frames.cpp
    src/timer_list.cpp
)

target_include_directories(core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(core PUBLIC common)

set_target_properties(core PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)
