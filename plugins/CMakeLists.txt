set(openSusie_plugins
    stb
    qoi
    gif
    pixelmap
    maki
    pic
    xyz
    msx
)

foreach(plugin IN LISTS openSusie_plugins)
    add_subdirectory("${plugin}")
endforeach()

foreach(plugin IN ITEMS stb qoi gif pnm maki pic xyz)
    openSusie_configure_size_optimizations("openSusie_${plugin}")
endforeach()
