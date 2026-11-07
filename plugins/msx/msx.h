#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace MSX {
struct Image {
	int width = 0;
	int height = 0;
	std::vector<std::uint8_t> rgba;
};

bool Decode(const std::uint8_t* data, std::size_t size, int screen, Image& image);
} // namespace MSX