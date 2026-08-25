add_library(cn STATIC
    src/cn_messages.cpp
    src/amf.cpp
    src/upf.cpp
)

target_include_directories(cn PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# The core-network layer reuses NAS message codecs and crypto primitives.
target_link_libraries(cn PUBLIC common nas)

add_executable(amfd src/amfd_main.cpp)
target_link_libraries(amfd PRIVATE cn)

add_executable(upfd src/upfd_main.cpp)
target_link_libraries(upfd PRIVATE cn)

set_target_properties(cn PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)
