#include "xyz.h"

#include "miniz.h"

#include <cstring>
#include <limits>
#include <vector>

namespace XYZ {
namespace {
constexpr std::size_t HeaderSize = 8;
constexpr std::size_t PaletteSize = 256 * 3;
constexpr std::size_t MaxPixels = 64 * 1024 * 1024;

std::uint16_t ReadU16(const std::uint8_t* data) {
    return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(data[0])
        | (static_cast<std::uint16_t>(data[1]) << 8)
    );
}
} // namespace

bool Decode(const std::uint8_t* data, std::size_t size, Image& image) {
    image = {};

    if (!data || size < HeaderSize)
        return false;

    if (std::memcmp(data, "XYZ1", 4) != 0)
        return false;

    const std::uint16_t width = ReadU16(data + 4);
    const std::uint16_t height = ReadU16(data + 6);

    if (!width || !height)
        return false;

    const std::size_t pixelCount = static_cast<std::size_t>(width) * height;

    if (pixelCount > MaxPixels)
        return false;

    if (pixelCount > std::numeric_limits<std::size_t>::max() / 4)
        return false;

    const std::size_t expectedSize = PaletteSize + pixelCount;
    const std::size_t compressedSize = size - HeaderSize;

    if (compressedSize > std::numeric_limits<mz_ulong>::max())
        return false;

    if (expectedSize > std::numeric_limits<mz_ulong>::max())
        return false;

    std::vector<std::uint8_t> decoded;

    try {
        decoded.resize(expectedSize);
    } catch (...) {
        return false;
    }

    mz_ulong decodedSize = static_cast<mz_ulong>(expectedSize);

    const int result = mz_uncompress(
        decoded.data(),
        &decodedSize,
        data + HeaderSize,
        static_cast<mz_ulong>(compressedSize)
    );

    if (result != MZ_OK || decodedSize != expectedSize)
        return false;

    const std::uint8_t* palette = decoded.data();
    const std::uint8_t* indices = palette + PaletteSize;

    try {
        image.rgba.resize(pixelCount * 4);
    } catch (...) {
        image = {};
        return false;
    }

    for (std::size_t i = 0; i < pixelCount; ++i) {
        const std::uint8_t index = indices[i];
        const std::uint8_t* color = palette + index * 3;

        image.rgba[i * 4 + 0] = color[0];
        image.rgba[i * 4 + 1] = color[1];
        image.rgba[i * 4 + 2] = color[2];

        // palette entry zero is asumed to be transparency
        image.rgba[i * 4 + 3] = index == 0 ? 0 : 255;
    }

    image.width = width;
    image.height = height;

    return true;
}
} // namespace XYZ