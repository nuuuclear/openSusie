cmake_minimum_required(VERSION 3.16)
project(openSusie)

include(GNUInstallDirs)

option(OPENSUSIE_BUILD_32BIT_PLUGIN_HOST "Build the 32-bit plugin host from 64-bit Windows builds" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE MinSizeRel CACHE STRING "Build configuration" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
endif()

set(openSusie_runtime_output_directory "${CMAKE_BINARY_DIR}")
if(CMAKE_CONFIGURATION_TYPES)
    set(openSusie_runtime_output_directory "${CMAKE_BINARY_DIR}/$<CONFIG>")
endif()
set(openSusie_plugin_output_directory "${openSusie_runtime_output_directory}/plugins")

include(CheckIPOSupported)
check_ipo_supported(RESULT openSusie_ipo_supported OUTPUT openSusie_ipo_error LANGUAGES C CXX)
if(openSusie_ipo_supported)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_MINSIZEREL ON)
else()
    message(STATUS "IPO/LTO unavailable: ${openSusie_ipo_error}")
endif()

function(openSusie_configure_size_optimizations target)
    if(MSVC)
        target_compile_options(${target} PRIVATE
            "$<$<OR:$<CONFIG:Release>,$<CONFIG:MinSizeRel>>:/O1>"
            "$<$<OR:$<CONFIG:Release>,$<CONFIG:MinSizeRel>>:/Gy>"
            "$<$<OR:$<CONFIG:Release>,$<CONFIG:MinSizeRel>>:/Gw>"
        )
        target_link_options(${target} PRIVATE
            "$<$<OR:$<CONFIG:Release>,$<CONFIG:MinSizeRel>>:/OPT:REF>"
            "$<$<OR:$<CONFIG:Release>,$<CONFIG:MinSizeRel>>:/OPT:ICF>"
            "$<$<OR:$<CONFIG:Release>,$<CONFIG:MinSizeRel>>:/INCREMENTAL:NO>"
        )
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE
            "$<$<OR:$<CONFIG:Release>,$<CONFIG:MinSizeRel>>:-ffunction-sections>"
            "$<$<OR:$<CONFIG:Release>,$<CONFIG:MinSizeRel>>:-fdata-sections>"
        )
        if(APPLE)
            target_link_options(${target} PRIVATE
                "$<$<OR:$<CONFIG:Release>,$<CONFIG:MinSizeRel>>:-Wl,-dead_strip>"
                "$<$<OR:$<CONFIG:Release>,$<CONFIG:MinSizeRel>>:-Wl,-x>"
            )
        else()
            target_link_options(${target} PRIVATE
                "$<$<OR:$<CONFIG:Release>,$<CONFIG:MinSizeRel>>:-Wl,--gc-sections>"
                "$<$<OR:$<CONFIG:Release>,$<CONFIG:MinSizeRel>>:-s>"
            )
        endif()
    endif()
endfunction()

function(openSusie_disable_broken_mingw_ipo target)
    if(MINGW AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 16)
        set_property(TARGET ${target} PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE OFF)
        set_property(TARGET ${target} PROPERTY INTERPROCEDURAL_OPTIMIZATION_MINSIZEREL OFF)
        message(STATUS "IPO disabled for ${target} with MinGW GCC 16+ due to an LTO compiler error")
    endif()
endfunction()

include(cmake/PluginHost32.cmake)

add_compile_definitions(_GLIBCXX_USE_CXX11_ABI=0)

# externals

set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_AUDIO OFF CACHE BOOL "" FORCE)
set(SDL_CAMERA OFF CACHE BOOL "" FORCE)
set(SDL_GPU OFF CACHE BOOL "" FORCE)
set(SDL_HAPTIC OFF CACHE BOOL "" FORCE)
set(SDL_HIDAPI OFF CACHE BOOL "" FORCE)
set(SDL_JOYSTICK OFF CACHE BOOL "" FORCE)
set(SDL_NOTIFICATION OFF CACHE BOOL "" FORCE)
set(SDL_POWER OFF CACHE BOOL "" FORCE)
set(SDL_SENSOR OFF CACHE BOOL "" FORCE)
set(SDL_TRAY OFF CACHE BOOL "" FORCE)

set(SDLTTF_VENDORED ON CACHE BOOL "" FORCE)

add_subdirectory(external/SDL EXCLUDE_FROM_ALL)
if(TARGET SDL3-shared)
    openSusie_configure_size_optimizations(SDL3-shared)
endif()
add_subdirectory(external/nlohmann_json EXCLUDE_FROM_ALL)

add_library(inicpp INTERFACE)
target_include_directories(inicpp INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/external/inicpp/"
)

add_library(stbImageWrite INTERFACE)
target_include_directories(stbImageWrite INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/external/stb_image_write/"
)

# Project components

add_subdirectory(pluginSDK)

function(openSusie_configure_susie_plugin target)
    if(CMAKE_SIZEOF_VOID_P EQUAL 4)
        if(MSVC)
            target_sources(${target} PRIVATE
                "${CMAKE_SOURCE_DIR}/pluginSDK/SusiePlugin32.def"
            )
        elseif(MINGW)
            set_property(TARGET ${target} APPEND_STRING PROPERTY LINK_FLAGS " -Wl,--kill-at")
        endif()
    endif()
endfunction()

add_subdirectory(plugins)
add_subdirectory(src)

if(WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 8 AND OPENSUSIE_BUILD_32BIT_PLUGIN_HOST)
    openSusie_add_plugin_host32_subbuild()
endif()

add_custom_target(openSusie_copy_resources
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/resources"
    "${openSusie_runtime_output_directory}/resources"
    COMMENT "Copying resources to build directory"
    VERBATIM
)
add_dependencies(openSusie
    openSusie_copy_resources
    openSusie_stb
    openSusie_qoi
    openSusie_pnm
    openSusie_maki
    openSusie_pic
    openSusie_xyz
    openSusie_pnm
)

# install rules

install(TARGETS openSusie
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)

if(TARGET SDL3-shared)
    install(TARGETS SDL3-shared
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
        LIBRARY DESTINATION "${CMAKE_INSTALL_BINDIR}"
    )
endif()

install(DIRECTORY resources/
    DESTINATION "${CMAKE_INSTALL_BINDIR}/resources"
    PATTERN "*.log" EXCLUDE
    PATTERN ".DS_Store" EXCLUDE
)

if(APPLE)
    set_property(TARGET openSusie PROPERTY INSTALL_RPATH "@loader_path")
elseif(UNIX)
    set_property(TARGET openSusie PROPERTY INSTALL_RPATH "$ORIGIN")
endif()