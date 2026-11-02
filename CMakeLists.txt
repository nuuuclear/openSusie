cmake_minimum_required(VERSION 3.16)
project(openSusie)

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

add_definitions(-D_GLIBCXX_USE_CXX11_ABI=0)

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
openSusie_configure_size_optimizations(openSusie_stb)
openSusie_configure_size_optimizations(openSusie_qoi)
openSusie_configure_size_optimizations(openSusie_gif)

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
        RUNTIME DESTINATION .
        COMPONENT plugin_host32
    )
elseif(WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 8 AND NOT OPENSUSIE_BUILD_32BIT_PLUGIN_HOST)
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
    include(ExternalProject)

    set(openSusie_plugin_host32_cmake_args
        "-DOPENSUSIE_BUILD_32BIT_PLUGIN_HOST:BOOL=OFF"
    )
    set(openSusie_plugin_host32_generator_args)
    set(openSusie_plugin_host32_generator "${CMAKE_GENERATOR}")
    set(openSusie_plugin_host32_toolchain_available FALSE)

    if(MSVC AND CMAKE_GENERATOR MATCHES "^Visual Studio")
        set(openSusie_plugin_host32_toolchain_available TRUE)
        list(APPEND openSusie_plugin_host32_generator_args
            CMAKE_GENERATOR_PLATFORM Win32
        )
        if(CMAKE_GENERATOR_TOOLSET)
            list(APPEND openSusie_plugin_host32_generator_args
                CMAKE_GENERATOR_TOOLSET "${CMAKE_GENERATOR_TOOLSET}"
            )
        endif()
    elseif(MINGW)
        find_program(OPENSUSIE_MINGW32_C_COMPILER NAMES i686-w64-mingw32-gcc)
        find_program(OPENSUSIE_MINGW32_CXX_COMPILER NAMES i686-w64-mingw32-g++)
        if(OPENSUSIE_MINGW32_C_COMPILER AND OPENSUSIE_MINGW32_CXX_COMPILER)
            set(openSusie_plugin_host32_toolchain_available TRUE)
            list(APPEND openSusie_plugin_host32_cmake_args
                "-DCMAKE_C_COMPILER:FILEPATH=${OPENSUSIE_MINGW32_C_COMPILER}"
                "-DCMAKE_CXX_COMPILER:FILEPATH=${OPENSUSIE_MINGW32_CXX_COMPILER}"
            )
        endif()
    endif()

    if(NOT openSusie_plugin_host32_toolchain_available)
        find_program(
            OPENSUSIE_VSWHERE_EXECUTABLE
            NAMES vswhere
            HINTS "$ENV{ProgramFiles\(x86\)}/Microsoft Visual Studio/Installer"
        )
        if(OPENSUSIE_VSWHERE_EXECUTABLE)
            execute_process(
                COMMAND "${OPENSUSIE_VSWHERE_EXECUTABLE}"
                    -latest
                    -products "*"
                    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64
                    -property installationVersion
                OUTPUT_VARIABLE openSusie_vs_version
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET
            )
            execute_process(
                COMMAND "${OPENSUSIE_VSWHERE_EXECUTABLE}"
                    -latest
                    -products "*"
                    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64
                    -property installationPath
                OUTPUT_VARIABLE openSusie_vs_installation
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET
            )
            string(REGEX MATCH "^[0-9]+" openSusie_vs_major "${openSusie_vs_version}")
            set(openSusie_vs_generator "")
            if(openSusie_vs_major GREATER_EQUAL 18 AND CMAKE_VERSION VERSION_GREATER_EQUAL 4.2)
                set(openSusie_vs_generator "Visual Studio 18 2026")
            elseif(openSusie_vs_major GREATER_EQUAL 17 AND CMAKE_VERSION VERSION_GREATER_EQUAL 3.21)
                set(openSusie_vs_generator "Visual Studio 17 2022")
            elseif(openSusie_vs_major GREATER_EQUAL 16 AND CMAKE_VERSION VERSION_GREATER_EQUAL 3.14)
                set(openSusie_vs_generator "Visual Studio 16 2019")
            elseif(openSusie_vs_major GREATER_EQUAL 15 AND CMAKE_VERSION VERSION_GREATER_EQUAL 3.7)
                set(openSusie_vs_generator "Visual Studio 15 2017")
            endif()

            if(openSusie_vs_generator AND openSusie_vs_installation)
                file(GLOB openSusie_vs_x86_compilers
                    "${openSusie_vs_installation}/VC/Tools/MSVC/*/bin/Hostx64/x86/cl.exe"
                )
                if(openSusie_vs_x86_compilers)
                    set(openSusie_plugin_host32_toolchain_available TRUE)
                    set(openSusie_plugin_host32_generator "${openSusie_vs_generator}")
                    list(APPEND openSusie_plugin_host32_generator_args
                        CMAKE_GENERATOR_PLATFORM Win32
                        CMAKE_GENERATOR_INSTANCE "${openSusie_vs_installation}"
                    )
                endif()
            endif()
        endif()
    endif()

    if(openSusie_plugin_host32_toolchain_available)
        if(NOT CMAKE_CONFIGURATION_TYPES)
            list(APPEND openSusie_plugin_host32_cmake_args
                "-DCMAKE_BUILD_TYPE:STRING=${CMAKE_BUILD_TYPE}"
            )
        endif()

        ExternalProject_Add(openSusie_plugin_host32_subbuild
            PREFIX "${CMAKE_BINARY_DIR}/plugin-host32"
            SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}"
            BINARY_DIR "${CMAKE_BINARY_DIR}/plugin-host32/build"
            CMAKE_GENERATOR "${openSusie_plugin_host32_generator}"
            ${openSusie_plugin_host32_generator_args}
            CMAKE_ARGS ${openSusie_plugin_host32_cmake_args}
            BUILD_COMMAND
                "${CMAKE_COMMAND}" --build <BINARY_DIR>
                --config $<CONFIG>
                --target openSusie_plugin_host32
            INSTALL_COMMAND
                "${CMAKE_COMMAND}" --install <BINARY_DIR>
                --config $<CONFIG>
                --component plugin_host32
                --prefix "${openSusie_runtime_output_directory}"
        )
        add_dependencies(openSusie openSusie_plugin_host32_subbuild)
    else()
        message(WARNING
            "Cannot automatically build openSusie_plugin_host32: install an x86 toolchain "
            "(Visual Studio C++ x86 tools or i686-w64-mingw32-gcc/g++)"
        )
    endif()
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


set(RESOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/resources")

add_custom_target(openSusie_copy_resources
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${RESOURCE_DIR}"
    "${openSusie_runtime_output_directory}/resources"
    COMMENT "Copying resources to build directory"
    VERBATIM
)
add_dependencies(openSusie openSusie_copy_resources openSusie_stb openSusie_qoi openSusie_gif)


# install rules

install(TARGETS openSusie RUNTIME DESTINATION .)

# copy libraries

if(WIN32)

    install(FILES $<TARGET_FILE:SDL3::SDL3> 
        DESTINATION .
    )

endif()

install(DIRECTORY resources/
    DESTINATION resources
    PATTERN "*.log" EXCLUDE
    PATTERN ".DS_Store" EXCLUDE
)