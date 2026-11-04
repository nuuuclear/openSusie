add_library(pnm INTERFACE)
target_include_directories(qoi INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/external/pnm"
)

add_library(openSusie_pnm SHARED
    osp_pixelmap.cpp
    pnmdecode.h
)
openSusie_configure_susie_plugin(openSusie_pnm)

target_link_libraries(openSusie_pnm PRIVATE
    openSusie_susie_sdk
    pnm
)

set_target_properties(openSusie_pnm PROPERTIES
    PREFIX ""
    RUNTIME_OUTPUT_DIRECTORY "${openSusie_plugin_output_directory}"
    LIBRARY_OUTPUT_DIRECTORY "${openSusie_plugin_output_directory}"
    SUFFIX ".spi"
)

install(TARGETS openSusie_pnm
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}/plugins"
    LIBRARY DESTINATION "${CMAKE_INSTALL_BINDIR}/plugins"
)