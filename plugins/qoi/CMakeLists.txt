add_library(qoi INTERFACE)
target_include_directories(qoi INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/external/qoi"
)

add_library(openSusie_qoi SHARED
    qoiDecoder.cpp
)
openSusie_configure_susie_plugin(openSusie_qoi)

target_link_libraries(openSusie_qoi PRIVATE
    openSusie_susie_sdk
    qoi
)

set_target_properties(openSusie_qoi PROPERTIES
    PREFIX ""
    RUNTIME_OUTPUT_DIRECTORY "${openSusie_plugin_output_directory}"
    LIBRARY_OUTPUT_DIRECTORY "${openSusie_plugin_output_directory}"
    SUFFIX ".spi"
)

install(TARGETS openSusie_qoi
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}/plugins"
    LIBRARY DESTINATION "${CMAKE_INSTALL_BINDIR}/plugins"
)