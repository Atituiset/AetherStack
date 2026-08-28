add_library(app STATIC
    src/app_layer.cpp
    src/u2u.cpp
    src/u2u_fields.cpp
)

target_include_directories(app PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(app PUBLIC common)
