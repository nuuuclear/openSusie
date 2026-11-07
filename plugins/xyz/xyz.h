#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>

namespace XYZ {
struct Image {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;
};

bool Decode(
    const std::uint8_t* data,
    std::size_t size,
    Image& image
);

} // namespace XYZ