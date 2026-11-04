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

# Plugins

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

add_subdirectory(plugins/stb)
add_subdirectory(plugins/qoi)
add_subdirectory(plugins/gif)
foreach(plugin IN ITEMS stb qoi gif)
    openSusie_configure_size_optimizations(openSusie_${plugin})
endforeach()

if(WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 4)
    add_executable(openSusie_plugin_host32 EXCLUDE_FROM_ALL
        "src/PluginHost32.cpp"
        "src/PluginHostProtocol.h"
        "src/PluginManager.h"
        "src/PlatformSymbols.h"
    )
    target_link_libraries(openSusie_plugin_host32 PRIVATE openSusie_susie_sdk)
    openSusie_configure_size_optimizations(openSusie_plugin_host32)
    openSusie_disable_broken_mingw_ipo(openSusie_plugin_host32)
    set_target_properties(openSusie_plugin_host32 PROPERTIES
        OUTPUT_NAME "openSusie_plugin_host32"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    )
    install(TARGETS openSusie_plugin_host32
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
        COMPONENT plugin_host32
    )
endif()

if(WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 8 AND NOT OPENSUSIE_BUILD_32BIT_PLUGIN_HOST)
    message(STATUS
        "Automatic openSusie_plugin_host32 build disabled by OPENSUSIE_BUILD_32BIT_PLUGIN_HOST"
    )
endif()

# openSusie executable

add_executable(openSusie
    "src/main.cpp"
    "src/App.cpp"
    "src/App.h"
    "src/Viewer.cpp"
    "src/Viewer.h"
    "src/ButtonsWindow.cpp"
    "src/ButtonsWindow.h"
    "src/UI.cpp"
    "src/UI.h"
    "src/NativeMenu.cpp"
    "src/NativeMenu.h"
    "src/NativeMenuPlatform.h"
    "src/PluginManager.h"
    "src/PluginHostClient.h"
    "src/PluginHostProtocol.h"
    "src/PlatformSymbols.h"
    "src/Paths.h"
    "src/FileSystem.cpp"
    "src/FileSystem.h"
    "src/Subroutine.h"
    "src/Button.cpp"
    "src/Button.h"
    "src/ButtonPanel.cpp"
    "src/ButtonPanel.h"
)

target_link_libraries(openSusie PRIVATE
    SDL3::SDL3 
    nlohmann_json::nlohmann_json
    inicpp 
    openSusie_susie_sdk
)
openSusie_configure_size_optimizations(openSusie)
openSusie_disable_broken_mingw_ipo(openSusie)

if(WIN32)
    target_sources(openSusie PRIVATE "src/NativeMenuWin32.cpp")
    target_link_libraries(openSusie PRIVATE user32)
elseif(APPLE)
    enable_language(OBJCXX)
    set(CMAKE_OBJCXX_STANDARD ${CMAKE_CXX_STANDARD})
    set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
    target_sources(openSusie PRIVATE "src/NativeMenuMac.mm")
    find_library(COCOA_FRAMEWORK Cocoa)
    if(NOT COCOA_FRAMEWORK)
        message(FATAL_ERROR "The Cocoa framework is required for native macOS menus")
    endif()
    target_link_libraries(openSusie PRIVATE "${COCOA_FRAMEWORK}")
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GTK3 REQUIRED IMPORTED_TARGET "gtk+-3.0>=3.22")
    target_sources(openSusie PRIVATE "src/NativeMenuGtk.cpp")
    target_link_libraries(openSusie PRIVATE PkgConfig::GTK3)
endif()

set_target_properties(openSusie PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${openSusie_runtime_output_directory}"
)

if(WIN32)
    set_target_properties(openSusie PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

if(WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 8 AND OPENSUSIE_BUILD_32BIT_PLUGIN_HOST)
    openSusie_add_plugin_host32_subbuild()
endif()

if(MSVC)
    target_link_options(openSusie PRIVATE "/ENTRY:mainCRTStartup")
endif()

if(UNIX AND NOT APPLE)
    # Linux/BSD: Link to pthread and lm
    target_link_libraries(openSusie PRIVATE pthread m ${CMAKE_DL_LIBS})
    # Link to -latomic if needed (for specific compilers/architectures)
    # target_link_libraries(MyApp PRIVATE atomic)
endif()


add_custom_target(openSusie_copy_resources
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/resources"
    "${openSusie_runtime_output_directory}/resources"
    COMMENT "Copying resources to build directory"
    VERBATIM
)
add_dependencies(openSusie openSusie_copy_resources openSusie_stb openSusie_qoi openSusie_gif)

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