cmake_minimum_required(VERSION 3.10)

project(openSusie_stb_plugin)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(stbimage INTERFACE)
target_include_directories(stbimage INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/external/stb_image"
)

add_library(openSusie_stb SHARED
    stbDecoder.cpp
)
openSusie_configure_susie_plugin(openSusie_stb)

target_link_libraries(openSusie_stb PRIVATE
    openSusie_susie_sdk
    stbimage
)

set_target_properties(openSusie_stb PROPERTIES
    PREFIX ""
    RUNTIME_OUTPUT_DIRECTORY "${openSusie_plugin_output_directory}"
    LIBRARY_OUTPUT_DIRECTORY "${openSusie_plugin_output_directory}"
    SUFFIX ".spi"
)

install(TARGETS openSusie_stb
    RUNTIME DESTINATION plugins
    LIBRARY DESTINATION plugins
    # ARCHIVE DESTINATION plugins
)