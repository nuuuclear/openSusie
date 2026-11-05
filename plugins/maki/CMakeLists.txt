add_library(openSusie_maki SHARED
    osp_maki.cpp
    makiDecode.h
)
openSusie_configure_susie_plugin(openSusie_maki)

target_link_libraries(openSusie_maki PRIVATE
    openSusie_susie_sdk
)

set_target_properties(openSusie_maki PROPERTIES
    PREFIX ""
    RUNTIME_OUTPUT_DIRECTORY "${openSusie_plugin_output_directory}"
    LIBRARY_OUTPUT_DIRECTORY "${openSusie_plugin_output_directory}"
    SUFFIX ".spi"
)

install(TARGETS openSusie_maki
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}/plugins"
    LIBRARY DESTINATION "${CMAKE_INSTALL_BINDIR}/plugins"
)