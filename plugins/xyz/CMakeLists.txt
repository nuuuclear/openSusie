add_library(miniz STATIC
	${CMAKE_CURRENT_SOURCE_DIR}/external/miniz/miniz.c
)

target_include_directories(miniz PUBLIC
	${CMAKE_CURRENT_SOURCE_DIR}/external/miniz
)

add_library(openSusie_xyz SHARED
    osp_xyz.cpp
    xyz.cpp
    xyz.h
)
openSusie_configure_susie_plugin(openSusie_xyz)

target_link_libraries(openSusie_xyz PRIVATE
    openSusie_susie_sdk
    miniz
)

set_target_properties(openSusie_xyz PROPERTIES
    PREFIX ""
    RUNTIME_OUTPUT_DIRECTORY "${openSusie_plugin_output_directory}"
    LIBRARY_OUTPUT_DIRECTORY "${openSusie_plugin_output_directory}"
    SUFFIX ".spi"
)

install(TARGETS openSusie_xyz
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}/plugins"
    LIBRARY DESTINATION "${CMAKE_INSTALL_BINDIR}/plugins"
)