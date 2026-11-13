set(openSusie_sources
    app/main.cpp
    app/App.cpp
    app/App.h
    core/FileSystem.cpp
    core/FileSystem.h
    core/Paths.h
    core/PlatformSymbols.h
    core/Subroutine.h
    plugin/PluginHostClient.h
    plugin/PluginHostProtocol.h
    plugin/PluginManager.h
    ui/Button.cpp
    ui/Button.h
    ui/ButtonPanel.cpp
    ui/ButtonPanel.h
    ui/ButtonsWindow.cpp
    ui/ButtonsWindow.h
    ui/NativeMenu.cpp
    ui/NativeMenu.h
    ui/NativeMenuPlatform.h
    ui/UI.cpp
    ui/UI.h
    ui/WindowTitlebar.cpp
    ui/WindowTitlebar.h
    viewer/Viewer.cpp
    viewer/Viewer.h
)

add_executable(openSusie ${openSusie_sources})
target_include_directories(openSusie PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(openSusie PRIVATE
    SDL3::SDL3
    nlohmann_json::nlohmann_json
    inicpp
    stbImageWrite
    stb_truetype
    textr
    openSusie_susie_sdk
)

openSusie_configure_size_optimizations(openSusie)
openSusie_disable_broken_mingw_ipo(openSusie)

if(WIN32)
    target_sources(openSusie PRIVATE ui/NativeMenuWin32.cpp)
    target_link_libraries(openSusie PRIVATE user32)
elseif(APPLE)
    enable_language(OBJCXX)
    set(CMAKE_OBJCXX_STANDARD ${CMAKE_CXX_STANDARD})
    set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
    target_sources(openSusie PRIVATE ui/NativeMenuMac.mm)
    find_library(COCOA_FRAMEWORK Cocoa REQUIRED)
    target_link_libraries(openSusie PRIVATE "${COCOA_FRAMEWORK}")
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GTK3 REQUIRED IMPORTED_TARGET "gtk+-3.0>=3.22")
    target_sources(openSusie PRIVATE ui/NativeMenuGtk.cpp)
    target_link_libraries(openSusie PRIVATE PkgConfig::GTK3 pthread m ${CMAKE_DL_LIBS})
endif()

set_target_properties(openSusie PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${openSusie_runtime_output_directory}"
)

if(WIN32)
    set_target_properties(openSusie PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

if(MSVC)
    target_link_options(openSusie PRIVATE "/ENTRY:mainCRTStartup")
endif()

if(WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 4)
    add_executable(openSusie_plugin_host32 EXCLUDE_FROM_ALL
        plugin/PluginHost32.cpp
        plugin/PluginHostProtocol.h
        plugin/PluginManager.h
        core/PlatformSymbols.h
    )
    target_include_directories(openSusie_plugin_host32 PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
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
elseif(WIN32 AND NOT OPENSUSIE_BUILD_32BIT_PLUGIN_HOST)
    message(STATUS "Automatic openSusie_plugin_host32 build disabled by OPENSUSIE_BUILD_32BIT_PLUGIN_HOST")
endif()