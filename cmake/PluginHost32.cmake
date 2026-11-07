function(openSusie_find_vs_x86_toolchain out_generator out_generator_args out_available)
    find_program(
        vswhere_executable
        NAMES vswhere
        HINTS "$ENV{ProgramFiles\(x86\)}/Microsoft Visual Studio/Installer"
    )
    if(NOT vswhere_executable)
        return()
    endif()

    execute_process(
        COMMAND "${vswhere_executable}" -latest -products "*"
            -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64
            -property installationVersion
        OUTPUT_VARIABLE vs_version
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    execute_process(
        COMMAND "${vswhere_executable}" -latest -products "*"
            -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64
            -property installationPath
        OUTPUT_VARIABLE vs_installation
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    string(REGEX MATCH "^[0-9]+" vs_major "${vs_version}")

    if(vs_major GREATER_EQUAL 18 AND CMAKE_VERSION VERSION_GREATER_EQUAL 4.2)
        set(vs_generator "Visual Studio 18 2026")
    elseif(vs_major GREATER_EQUAL 17 AND CMAKE_VERSION VERSION_GREATER_EQUAL 3.21)
        set(vs_generator "Visual Studio 17 2022")
    elseif(vs_major GREATER_EQUAL 16 AND CMAKE_VERSION VERSION_GREATER_EQUAL 3.14)
        set(vs_generator "Visual Studio 16 2019")
    elseif(vs_major GREATER_EQUAL 15 AND CMAKE_VERSION VERSION_GREATER_EQUAL 3.7)
        set(vs_generator "Visual Studio 15 2017")
    endif()
    if(NOT vs_generator OR NOT vs_installation)
        return()
    endif()

    file(GLOB vs_x86_compilers
        "${vs_installation}/VC/Tools/MSVC/*/bin/Hostx64/x86/cl.exe"
    )
    if(NOT vs_x86_compilers)
        return()
    endif()

    set(${out_generator} "${vs_generator}" PARENT_SCOPE)
    set(${out_generator_args}
        CMAKE_GENERATOR_PLATFORM Win32
        CMAKE_GENERATOR_INSTANCE "${vs_installation}"
        PARENT_SCOPE
    )
    set(${out_available} TRUE PARENT_SCOPE)
endfunction()

function(openSusie_add_plugin_host32_subbuild)
    include(ExternalProject)

    set(host_cmake_args "-DOPENSUSIE_BUILD_32BIT_PLUGIN_HOST:BOOL=OFF")
    set(host_generator "${CMAKE_GENERATOR}")
    set(host_toolchain_available FALSE)

    if(MSVC AND CMAKE_GENERATOR MATCHES "^Visual Studio")
        set(host_toolchain_available TRUE)
        list(APPEND host_generator_args CMAKE_GENERATOR_PLATFORM Win32)
        if(CMAKE_GENERATOR_TOOLSET)
            list(APPEND host_generator_args CMAKE_GENERATOR_TOOLSET "${CMAKE_GENERATOR_TOOLSET}")
        endif()
    elseif(MINGW)
        find_program(host32_c_compiler NAMES i686-w64-mingw32-gcc)
        find_program(host32_cxx_compiler NAMES i686-w64-mingw32-g++)
        if(host32_c_compiler AND host32_cxx_compiler)
            set(host_toolchain_available TRUE)
            list(APPEND host_cmake_args
                "-DCMAKE_C_COMPILER:FILEPATH=${host32_c_compiler}"
                "-DCMAKE_CXX_COMPILER:FILEPATH=${host32_cxx_compiler}"
            )
        endif()
    endif()

    if(NOT host_toolchain_available)
        openSusie_find_vs_x86_toolchain(
            host_generator host_generator_args host_toolchain_available
        )
    endif()
    if(NOT host_toolchain_available)
        message(WARNING
            "Cannot automatically build openSusie_plugin_host32: install an x86 toolchain "
            "(Visual Studio C++ x86 tools or i686-w64-mingw32-gcc/g++)"
        )
        return()
    endif()

    if(NOT CMAKE_CONFIGURATION_TYPES)
        list(APPEND host_cmake_args "-DCMAKE_BUILD_TYPE:STRING=${CMAKE_BUILD_TYPE}")
    endif()

    set(host_build_dir "${CMAKE_BINARY_DIR}/plugin-host32")
    ExternalProject_Add(openSusie_plugin_host32_subbuild
        PREFIX "${host_build_dir}"
        SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}"
        BINARY_DIR "${host_build_dir}/build"
        BUILD_ALWAYS TRUE
        CMAKE_GENERATOR "${host_generator}"
        ${host_generator_args}
        CMAKE_ARGS ${host_cmake_args}
        BUILD_COMMAND "${CMAKE_COMMAND}" --build <BINARY_DIR>
            --config $<CONFIG> --target openSusie_plugin_host32
        INSTALL_COMMAND "${CMAKE_COMMAND}" --install <BINARY_DIR>
            --config $<CONFIG> --component plugin_host32
            --prefix "${host_build_dir}/stage"
    )
    add_dependencies(openSusie openSusie_plugin_host32_subbuild)
    install(FILES "${host_build_dir}/stage/${CMAKE_INSTALL_BINDIR}/openSusie_plugin_host32.exe"
        DESTINATION "${CMAKE_INSTALL_BINDIR}"
    )
endfunction()