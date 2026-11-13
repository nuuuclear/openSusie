cmake_minimum_required(VERSION 3.31)
project(openSusie VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(GNUInstallDirs)

if(WIN32)
    set(CMAKE_INSTALL_BINDIR "." CACHE PATH "Executable installation directory" FORCE)
endif()

option(OPENSUSIE_BUILD_32BIT_PLUGIN_HOST "Build the 32-bit plugin host from 64-bit Windows builds" ON)

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

# Externals

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

add_subdirectory(external/SDL EXCLUDE_FROM_ALL)

if(TARGET SDL3-shared)
    openSusie_configure_size_optimizations(SDL3-shared)
    openSusie_disable_broken_mingw_ipo(SDL3-shared)
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

add_library(stb_truetype INTERFACE)
target_include_directories(stb_truetype INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/external/stb_truetype/"
)

add_library(textr INTERFACE)
target_include_directories(textr INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/external/textr/"
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

if(NOT TARGET openSusie)
    message(FATAL_ERROR "The src directory did not create the openSusie target")
endif()

# Application resources

add_custom_target(openSusie_copy_resources
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/resources"
        "${openSusie_runtime_output_directory}/resources"
    COMMENT "Copying application resources"
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
    openSusie_msx
)

if(WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 8 AND OPENSUSIE_BUILD_32BIT_PLUGIN_HOST)
    openSusie_add_plugin_host32_subbuild()
endif()

# Application icons

set(OPENSUSIE_ICON "${CMAKE_SOURCE_DIR}/resources/icon.png")
set(OPENSUSIE_ICON_OUTPUT "${CMAKE_BINARY_DIR}/generated")

if(NOT EXISTS "${OPENSUSIE_ICON}")
    message(FATAL_ERROR "Missing application icon: resources/icon.png")
endif()

file(MAKE_DIRECTORY "${OPENSUSIE_ICON_OUTPUT}")

if(WIN32)
    enable_language(RC)

    add_executable(openSusie_icon_generator
        tools/IconGenerator.cpp
    )

    set_target_properties(openSusie_icon_generator PROPERTIES
        WIN32_EXECUTABLE FALSE
    )

    set(OPENSUSIE_ICO "${OPENSUSIE_ICON_OUTPUT}/icon.ico")

    add_custom_command(
        OUTPUT "${OPENSUSIE_ICO}"
        COMMAND $<TARGET_FILE:openSusie_icon_generator>
            "${OPENSUSIE_ICON}"
            "${OPENSUSIE_ICO}"
        DEPENDS
            openSusie_icon_generator
            "${OPENSUSIE_ICON}"
        COMMENT "Generating Windows application icon"
        VERBATIM
    )

    add_custom_target(openSusie_icon
        DEPENDS "${OPENSUSIE_ICO}"
    )

    set(OPENSUSIE_RC "${OPENSUSIE_ICON_OUTPUT}/app.rc")

    file(GENERATE
        OUTPUT "${OPENSUSIE_RC}"
        CONTENT "IDI_ICON1 ICON \"${OPENSUSIE_ICO}\"\n"
    )

    set_source_files_properties("${OPENSUSIE_RC}"
        PROPERTIES OBJECT_DEPENDS "${OPENSUSIE_ICO}"
    )

    target_sources(openSusie PRIVATE
        "${OPENSUSIE_RC}"
    )

    add_dependencies(openSusie openSusie_icon)

elseif(APPLE)
    find_program(SIPS_EXECUTABLE NAMES sips REQUIRED)
    find_program(ICONUTIL_EXECUTABLE NAMES iconutil REQUIRED)

    set(OPENSUSIE_ICONSET "${OPENSUSIE_ICON_OUTPUT}/icon.iconset")
    set(OPENSUSIE_ICNS "${OPENSUSIE_ICON_OUTPUT}/icon.icns")

    set(OPENSUSIE_ICONSET_COMMANDS)

    foreach(size IN ITEMS 16 32 128 256 512)
        math(EXPR double_size "${size} * 2")

        list(APPEND OPENSUSIE_ICONSET_COMMANDS
            COMMAND "${SIPS_EXECUTABLE}"
                -z "${size}" "${size}"
                "${OPENSUSIE_ICON}"
                --out "${OPENSUSIE_ICONSET}/icon_${size}x${size}.png"

            COMMAND "${SIPS_EXECUTABLE}"
                -z "${double_size}" "${double_size}"
                "${OPENSUSIE_ICON}"
                --out "${OPENSUSIE_ICONSET}/icon_${size}x${size}@2x.png"
        )
    endforeach()

    add_custom_command(
        OUTPUT "${OPENSUSIE_ICNS}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${OPENSUSIE_ICONSET}"
        ${OPENSUSIE_ICONSET_COMMANDS}
        COMMAND "${ICONUTIL_EXECUTABLE}"
            -c icns
            "${OPENSUSIE_ICONSET}"
            -o "${OPENSUSIE_ICNS}"
        DEPENDS "${OPENSUSIE_ICON}"
        COMMENT "Generating macOS application icon"
        VERBATIM
    )

    add_custom_target(openSusie_icon
        DEPENDS "${OPENSUSIE_ICNS}"
    )

    set_target_properties(openSusie PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "openSusie"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.opensusie.viewer"
        MACOSX_BUNDLE_ICON_FILE "icon.icns"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    )

    set_source_files_properties("${OPENSUSIE_ICNS}"
        PROPERTIES MACOSX_PACKAGE_LOCATION "Resources"
    )

    target_sources(openSusie PRIVATE
        "${OPENSUSIE_ICNS}"
    )

    add_dependencies(openSusie openSusie_icon)
endif()

# Installation layout

if(WIN32)
    set(openSusie_install_runtime_dir ".")
    set(openSusie_install_library_dir ".")

elseif(APPLE)
    set(openSusie_install_runtime_dir "openSusie.app/Contents/MacOS")
    set(openSusie_install_library_dir "openSusie.app/Contents/Frameworks")

else()
    set(openSusie_install_runtime_dir "${CMAKE_INSTALL_BINDIR}")
    set(openSusie_install_library_dir "${CMAKE_INSTALL_BINDIR}")
endif()

# Main executable

if(APPLE)
    install(TARGETS openSusie
        BUNDLE DESTINATION "."
    )
else()
    install(TARGETS openSusie
        RUNTIME DESTINATION "${openSusie_install_runtime_dir}"
    )
endif()

# SDL3 runtime

if(TARGET SDL3-shared)
    if(APPLE)
        install(TARGETS SDL3-shared
            LIBRARY DESTINATION "${openSusie_install_library_dir}"
        )
    else()
        install(TARGETS SDL3-shared
            RUNTIME DESTINATION "${openSusie_install_library_dir}"
            LIBRARY DESTINATION "${openSusie_install_library_dir}"
        )
    endif()
endif()

# Susie plugins

set(openSusie_plugin_targets
    openSusie_stb
    openSusie_qoi
    openSusie_pnm
    openSusie_maki
    openSusie_pic
    openSusie_xyz
    openSusie_msx
)

foreach(plugin IN LISTS openSusie_plugin_targets)
    if(TARGET ${plugin})
        install(TARGETS ${plugin}
            RUNTIME DESTINATION "${openSusie_install_runtime_dir}/plugins"
            LIBRARY DESTINATION "${openSusie_install_runtime_dir}/plugins"
        )
    endif()
endforeach()

# Application resources

install(DIRECTORY resources/
    DESTINATION "${openSusie_install_runtime_dir}/resources"
    PATTERN "*.log" EXCLUDE
    PATTERN ".DS_Store" EXCLUDE
)

# Linux application icon

if(UNIX AND NOT APPLE)
    install(FILES "${OPENSUSIE_ICON}"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/32x32/apps"
        RENAME openSusie.png
    )
endif()

# Runtime library paths

if(APPLE)
    set_property(TARGET openSusie PROPERTY INSTALL_RPATH "@executable_path/../Frameworks")
elseif(UNIX)
    set_property(TARGET openSusie PROPERTY INSTALL_RPATH "$ORIGIN")
endif()

# Packaging

set(CPACK_PACKAGE_NAME "openSusie")
set(CPACK_PACKAGE_VENDOR "openSusie")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Graphics Viewer openSusie")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/nuuuclear/openSusie")

set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_VERSION_MAJOR "${PROJECT_VERSION_MAJOR}")
set(CPACK_PACKAGE_VERSION_MINOR "${PROJECT_VERSION_MINOR}")
set(CPACK_PACKAGE_VERSION_PATCH "${PROJECT_VERSION_PATCH}")

set(CPACK_PACKAGE_INSTALL_DIRECTORY "openSusie")

set(CPACK_PACKAGE_DIRECTORY "${CMAKE_BINARY_DIR}/packages")
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY ON)

set(CPACK_PACKAGE_FILE_NAME
    "openSusie-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}"
)

set(CPACK_GENERATOR ZIP)

if(WIN32)
    list(APPEND CPACK_GENERATOR NSIS)

    set(CPACK_NSIS_DISPLAY_NAME "openSusie")
    set(CPACK_NSIS_PACKAGE_NAME "openSusie")
    set(CPACK_NSIS_INSTALL_ROOT "$PROGRAMFILES64")
    set(CPACK_NSIS_MODIFY_PATH OFF)

    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
    set(CPACK_NSIS_IGNORE_LICENSE_PAGE ON)

    set(CPACK_NSIS_DISPLAY_ICON "${OPENSUSIE_ICO}")
    set(CPACK_NSIS_INSTALLED_ICON_NAME "openSusie.exe")

    set(CPACK_NSIS_MENU_LINKS
        "openSusie.exe" "openSusie"
        "https://github.com/nuuuclear/openSusie" "View Source on GitHub"
    )
    set(CPACK_NSIS_CREATE_ICONS_EXTRA
        "CreateShortCut '$DESKTOP/openSusie.lnk' '$INSTDIR/openSusie.exe'"
        "CreateShortCut '$SMPROGRAMS/openSusie/openSusie.lnk' '$INSTDIR/openSusie.exe'"
    )

elseif(APPLE)
    list(APPEND CPACK_GENERATOR DragNDrop)

elseif(UNIX)
    list(APPEND CPACK_GENERATOR DEB TGZ)

    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "openSusie")
    set(CPACK_DEBIAN_PACKAGE_SECTION "graphics")
    set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
endif()

include(CPack)