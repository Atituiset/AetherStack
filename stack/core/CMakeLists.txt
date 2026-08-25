add_library(core STATIC
    src/radio_frames.cpp
    src/timer_list.cpp
    src/harq.cpp
    src/ue_node.cpp
    src/bs_node.cpp
)

target_include_directories(core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# The node orchestration layer owns entities from every upper layer.
target_link_libraries(core PUBLIC common mac rlc pdcp rrc nas cn phy)

set_target_properties(core PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)
