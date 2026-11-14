# Import plugin

add_library(openSusie_tga SHARED
    osp_tga.cpp

    external/tga/tga.h
    external/tga/decoder.cpp
    external/tga/encoder.cpp
    external/tga/image_iterator.cpp
    external/tga/stdio.cpp
)
openSusie_configure_susie_plugin(openSusie_tga)

target_link_libraries(openSusie_tga PRIVATE
    openSusie_susie_sdk
)

set_target_properties(openSusie_tga PROPERTIES
    PREFIX ""
    RUNTIME_OUTPUT_DIRECTORY "${openSusie_plugin_output_directory}"
    LIBRARY_OUTPUT_DIRECTORY "${openSusie_plugin_output_directory}"
    SUFFIX ".spi"
)

install(TARGETS openSusie_tga
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}/plugins"
    LIBRARY DESTINATION "${CMAKE_INSTALL_BINDIR}/plugins"
)