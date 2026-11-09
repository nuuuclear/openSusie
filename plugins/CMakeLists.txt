set(openSusie_plugins
    stb
    qoi
    gif
    pnm
    maki
    pic
    xyz
    msx
)

foreach(plugin IN LISTS openSusie_plugins)
    add_subdirectory("${plugin}")
    openSusie_configure_size_optimizations("openSusie_${plugin}")
endforeach()
