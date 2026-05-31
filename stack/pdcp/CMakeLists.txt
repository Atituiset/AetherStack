add_library(pdcp STATIC
    src/pdcp_entity.cpp
)

target_include_directories(pdcp PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(pdcp PUBLIC common)
