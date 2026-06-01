add_library(rrc STATIC
    src/rrc_types.cpp
    src/rrc_messages.cpp
    src/rrc_ue.cpp
    src/rrc_bs.cpp
)

target_include_directories(rrc PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(rrc PUBLIC common)
