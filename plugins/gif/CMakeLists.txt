add_library(giflib STATIC
    external/giflib/dgif_lib.c
    external/giflib/egif_lib.c
    external/giflib/gif_err.c
    external/giflib/gif_font.c
    external/giflib/gifalloc.c
    external/giflib/openbsd-reallocarray.c
)
target_include_directories(giflib PUBLIC external/giflib/)

add_library(openSusie_gif SHARED
    gifDecoder.cpp
)
openSusie_configure_susie_plugin(openSusie_gif)
if(CMAKE_SIZEOF_VOID_P EQUAL 4 AND MSVC)
    target_sources(openSusie_gif PRIVATE
        "${CMAKE_SOURCE_DIR}/pluginSDK/SusieAnimation32.def"
    )
endif()

target_link_libraries(openSusie_gif PRIVATE
    openSusie_susie_sdk
    giflib
)

set_target_properties(openSusie_gif PROPERTIES
    PREFIX ""
    RUNTIME_OUTPUT_DIRECTORY "${openSusie_plugin_output_directory}"
    LIBRARY_OUTPUT_DIRECTORY "${openSusie_plugin_output_directory}"
    SUFFIX ".spi"
)

install(TARGETS openSusie_gif
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}/plugins"
    LIBRARY DESTINATION "${CMAKE_INSTALL_BINDIR}/plugins"
)