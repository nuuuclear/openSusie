add_library(openSusie_msx SHARED
	osp_msx.cpp
	msx.cpp
	msx.h
)

openSusie_configure_susie_plugin(openSusie_msx)

target_link_libraries(openSusie_msx PRIVATE
	openSusie_susie_sdk
)

set_target_properties(openSusie_msx PROPERTIES
	PREFIX ""
	RUNTIME_OUTPUT_DIRECTORY "${openSusie_plugin_output_directory}"
	LIBRARY_OUTPUT_DIRECTORY "${openSusie_plugin_output_directory}"
	SUFFIX ".spi"
)

install(TARGETS openSusie_msx
	RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}/plugins"
	LIBRARY DESTINATION "${CMAKE_INSTALL_BINDIR}/plugins"
)