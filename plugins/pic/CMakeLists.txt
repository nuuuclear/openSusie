add_library(openSusie_pic SHARED
    osp_pic.cpp
    Pi.h
    PIC.h
)
openSusie_configure_susie_plugin(openSusie_pic)

target_link_libraries(openSusie_pic PRIVATE
    openSusie_susie_sdk
)

set_target_properties(openSusie_pic PROPERTIES
    PREFIX ""
    RUNTIME_OUTPUT_DIRECTORY "${openSusie_plugin_output_directory}"
    LIBRARY_OUTPUT_DIRECTORY "${openSusie_plugin_output_directory}"
    SUFFIX ".spi"
)

install(TARGETS openSusie_pic
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}/plugins"
    LIBRARY DESTINATION "${CMAKE_INSTALL_BINDIR}/plugins"
)