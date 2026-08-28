add_library(nas STATIC
    src/nas_messages.cpp
    src/aka.cpp
    src/nas_ue.cpp
    src/nas_bs.cpp
    src/trial_fp_engine.cpp
)

target_include_directories(nas PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(nas PUBLIC common)
