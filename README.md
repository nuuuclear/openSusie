![openSusie logo](./resources/BMTITLE.bmp)

openSusie is a plugin supported grapics viewer, based on [Susie](https://www.digitalpad.co.jp/~takechin/).

This is made to support newer hardware and OS as the original is quite outdated, it adds zooming, panning, and better window sizing. The original only supports windows, and here the goal is to support as many platforms as needed.

The plugin format has been extended to support animated images such as GIFs, and will have further features added to it.

It has compatability with exsisting 32-bit susie plugins via a seperate 32-bit plugin host executable which interfaces seamlessly with the 64-bit viewer application.

Note: As of now, this is untested on mac and is only partially tested on linux, so it may not work as intended. lots of things are WIP.

## Plugins

Existing plugins in this repository:
- stb_image (png, jpeg, and bmp)
- qoi
- gif
- pnm (.pgm, .ppm, .pbm, .pam, .pnm)
- makichan (.mag, .maki, .max)

## Building

```bash
git submodule update --init --recursive
cmake --preset ninja
cmake --build --preset ninja
```

or
```bash
cmake --preset windows-vs2026
cmake --build --preset windows-vs2026
```

Installing:

```bash
cmake --install build/ninja --prefix ./dist
cmake --install build/windows-vs2026 --config MinSizeRel --prefix ./dist
```

## Dependencies

- SDL3
- nlohmann/json
- inicpp
- GTK 3 (Linux UI)

plugins:
- stb: stb_image
- gif: giflib
- qoi: qoi