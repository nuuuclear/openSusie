#include "msx.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace {
struct Color {
	std::uint8_t r;
	std::uint8_t g;
	std::uint8_t b;
};

constexpr std::array<Color, 16> MSX2Palette = {{
	{  0,   0,   0}, {  0,   0,   0}, { 33, 218,  66}, {109, 237,  85},
	{ 85,  85, 221}, {117, 183, 255}, {204,  85,  85}, { 85, 221, 221},
	{221,  85,  85}, {255, 119, 119}, {204, 204,  85}, {238, 238, 136},
	{ 85, 170,  85}, {183, 102, 183}, {204, 204, 204}, {255, 255, 255}
}};

bool HasRange(std::size_t size, std::size_t offset, std::size_t count) {
	return offset <= size && count <= size - offset;
}

std::uint8_t Expand3(unsigned int value) {
	return static_cast<std::uint8_t>((value * 255 + 3) / 7);
}

std::uint8_t Expand2(unsigned int value) {
	return static_cast<std::uint8_t>((value * 255 + 1) / 3);
}

void SetPixel(MSX::Image& image, int x, int y, Color color) {
	if (x < 0 || y < 0 || x >= image.width || y >= image.height)
		return;

	const std::size_t offset = (static_cast<std::size_t>(y) * image.width + x) * 4;
	image.rgba[offset + 0] = color.r;
	image.rgba[offset + 1] = color.g;
	image.rgba[offset + 2] = color.b;
	image.rgba[offset + 3] = 255;
}

bool Allocate(MSX::Image& image, int width, int height) {
	if (width <= 0 || height <= 0)
		return false;

	const std::uint64_t pixels = static_cast<std::uint64_t>(width) * height;
	if (pixels > std::numeric_limits<std::size_t>::max() / 4)
		return false;

	try {
		image.width = width;
		image.height = height;
		image.rgba.assign(static_cast<std::size_t>(pixels) * 4, 0);
	} catch (...) {
		image = {};
		return false;
	}

	return true;
}

bool GetBsavePayload(
	const std::uint8_t* data,
	std::size_t size,
	const std::uint8_t*& payload,
	std::size_t& payloadSize
) {
	if (!data || size < 7 || data[0] != 0xFE)
		return false;

	payload = data + 7;
	payloadSize = size - 7;
	return payloadSize != 0;
}

Color DecodePalette7(const std::uint8_t* payload, std::size_t size, unsigned int index) {
	if (HasRange(size, 0xFA80 + index * 2, 2)) {
		const std::uint8_t a = payload[0xFA80 + index * 2];
		const std::uint8_t b = payload[0xFA80 + index * 2 + 1];
		return {
			Expand3((a >> 4) & 7),
			Expand3(b & 7),
			Expand3(a & 7)
		};
	}

	return MSX2Palette[index & 15];
}

bool DecodeScreen2(const std::uint8_t* vram, std::size_t size, MSX::Image& image) {
	if (!HasRange(size, 0x0000, 0x1800)
	||  !HasRange(size, 0x1800, 0x0300)
	||  !HasRange(size, 0x2000, 0x1800)) {
		return false;
	}

	if (!Allocate(image, 256, 192))
		return false;

	for (int y = 0; y < 192; ++y) {
		const int region = y / 64;
		const int row = y & 7;
		const int cellRow = y / 8;
		for (int cellX = 0; cellX < 32; ++cellX) {
			const std::size_t nameOffset = 0x1800 + cellRow * 32 + cellX;
			const unsigned int pattern = vram[nameOffset] + region * 256;
			const std::size_t patternOffset = pattern * 8 + row;
			const std::size_t colorOffset = 0x2000 + pattern * 8 + row;
			if (!HasRange(size, patternOffset, 1) || !HasRange(size, colorOffset, 1)) {
				image = {};
				return false;
			}

			const std::uint8_t patternByte = vram[patternOffset];
			const std::uint8_t colorByte = vram[colorOffset];
			const Color foreground = MSX2Palette[colorByte >> 4];
			const Color background = MSX2Palette[colorByte & 15];

			for (int bit = 0; bit < 8; ++bit) {
				const Color color = (patternByte & (0x80 >> bit)) ? foreground : background;
				SetPixel(image, cellX * 8 + bit, y, color);
			}
		}
	}

	return true;
}

bool DecodeScreen0(const std::uint8_t* vram, std::size_t size, MSX::Image& image) {
	constexpr int columns = 40;
	constexpr int rows = 24;		
	constexpr int glyphWidth = 6;
	constexpr int glyphHeight = 8;

	constexpr std::size_t nameTableOffset = 0x0000;
	constexpr std::size_t nameTableSize = columns * rows;
	constexpr std::size_t patternTableOffset = 0x0800;
	constexpr std::size_t patternTableSize = 0x0800;

	if (!HasRange(size, nameTableOffset, nameTableSize)
	||  !HasRange(size, patternTableOffset, patternTableSize)) {
		return false;
	}

	if (!Allocate(image, columns * glyphWidth, rows * glyphHeight))
		return false;

	const Color foreground = MSX2Palette[15];
	const Color background = MSX2Palette[4];

	for (int cellY = 0; cellY < rows; ++cellY) {
		for (int cellX = 0; cellX < columns; ++cellX) {
			const unsigned int character = vram[nameTableOffset + cellY * columns + cellX];
			const std::size_t glyphOffset = patternTableOffset + character * glyphHeight;

			for (int row = 0; row < glyphHeight; ++row) {
				const std::uint8_t pattern = vram[glyphOffset + row];
				for (int column = 0; column < glyphWidth; ++column) {
					const bool set = (pattern & (0x80 >> column)) != 0;
					SetPixel(
						image,
						cellX * glyphWidth + column,
						cellY * glyphHeight + row,
						set ? foreground : background
					);
				}
			}
		}
	}

	return true;
}

bool DecodeScreen1(const std::uint8_t* vram, std::size_t size, MSX::Image& image) {
	constexpr int columns = 32;
	constexpr int rows = 24;
	constexpr int glyphWidth = 8;
	constexpr int glyphHeight = 8;

	constexpr std::size_t patternTableOffset = 0x0000;
	constexpr std::size_t nameTableOffset = 0x1800;
	constexpr std::size_t colorTableOffset = 0x2000;

	if (!HasRange(size, patternTableOffset, 0x0800)
	||  !HasRange(size, nameTableOffset, columns * rows)
	||  !HasRange(size, colorTableOffset, 0x0020)) {
		return false;
	}

	if (!Allocate(image, columns * glyphWidth, rows * glyphHeight))
		return false;

	for (int cellY = 0; cellY < rows; ++cellY) {
		for (int cellX = 0; cellX < columns; ++cellX) {
			const unsigned int character = vram[nameTableOffset + cellY * columns + cellX];
			const std::size_t glyphOffset = patternTableOffset + character * glyphHeight;
			const std::uint8_t colorByte = vram[colorTableOffset + character / 8];
			const Color foreground = MSX2Palette[colorByte >> 4];
			const Color background = MSX2Palette[colorByte & 15];

			for (int row = 0; row < glyphHeight; ++row) {
				const std::uint8_t pattern = vram[glyphOffset + row];

				for (int column = 0; column < glyphWidth; ++column) {
					const bool set = (pattern & (0x80 >> column)) != 0;
					SetPixel(
						image,
						cellX * glyphWidth + column,
						cellY * glyphHeight + row,
						set ? foreground : background	
					);
				}
			}
		}
	}

	return true;
}


bool DecodeScreen3(const std::uint8_t* vram, std::size_t size, MSX::Image& image) {
	if (!HasRange(size, 0, 0x600) || !Allocate(image, 256, 192))
		return false;

	for (int blockY = 0; blockY < 48; ++blockY) {
		for (int blockX = 0; blockX < 64; blockX += 2) {
			const std::size_t offset
			=	static_cast<std::size_t>(blockY / 8) * 0x100
			+ 	static_cast<std::size_t>(blockX / 2) * 8
			+ 	static_cast<std::size_t>(blockY & 7);

			if (!HasRange(size, offset, 1)) {
				image = {};
				return false;
			}

			const std::uint8_t colors = vram[offset];
			const Color left = MSX2Palette[colors >> 4];
			const Color right = MSX2Palette[colors & 15];

			const int pixelY = blockY * 4;
			const int leftX = blockX * 4;
			const int rightX = leftX + 4;

			for (int py = 0; py < 4; ++py) {
				for (int px = 0; px < 4; ++px) {
					SetPixel(image, leftX + px, pixelY + py, left);
					SetPixel(image, rightX + px, pixelY + py, right);
				}
			}
		}
	}

	return true;
}

bool DecodeScreen5(const std::uint8_t* vram, std::size_t size, MSX::Image& image) {
	constexpr int width = 256;
	constexpr int height = 212;
	constexpr std::size_t bytes = width * height / 2;
	if (!HasRange(size, 0, bytes) || !Allocate(image, width, height))
		return false;

	for (int y = 0; y < height; ++y) {
		for (int x = 0; x < width; x += 2) {
			const std::uint8_t value = vram[(y * width + x) / 2];
			SetPixel(image, x, y, MSX2Palette[value >> 4]);
			SetPixel(image, x + 1, y, MSX2Palette[value & 15]);
		}
	}
	return true;
}

bool DecodeScreen6(const std::uint8_t* vram, std::size_t size, MSX::Image& image) {
	constexpr int width = 512;
	constexpr int height = 212;
	constexpr std::size_t bytes = width * height / 4;
	if (!HasRange(size, 0, bytes) || !Allocate(image, width, height))
		return false;

	for (int y = 0; y < height; ++y) {
		for (int x = 0; x < width; x += 4) {
			const std::uint8_t value = vram[(y * width + x) / 4];
			for (int p = 0; p < 4; ++p) {
				const unsigned int index = (value >> (6 - p * 2)) & 3;
				SetPixel(image, x + p, y, MSX2Palette[index]);
			}
		}
	}

	return true;
}

bool DecodeScreen7(const std::uint8_t* vram, std::size_t size, MSX::Image& image) {
	constexpr int width = 512;
	constexpr int height = 212;
	constexpr std::size_t bytes = width * height / 2;
	if (!HasRange(size, 0, bytes) || !Allocate(image, width, height))
		return false;

	for (int y = 0; y < height; ++y) {
		for (int x = 0; x < width; x += 2) {
			const std::uint8_t value = vram[(y * width + x) / 2];
			const unsigned int first = value >> 4;
			const unsigned int second = value & 15;
			SetPixel(image, x, y, DecodePalette7(vram, size, first));
			SetPixel(image, x + 1, y, DecodePalette7(vram, size, second));
		}
	}
	return true;
}

// TODO: fix palette issues with SC8 (everything has a green tint)

bool DecodeScreen8(const std::uint8_t* vram, std::size_t size, MSX::Image& image) {
	constexpr int width = 256;
	constexpr int height = 212;
	constexpr std::size_t bytes = width * height;
	if (!HasRange(size, 0, bytes) || !Allocate(image, width, height))
		return false;

	for (int y = 0; y < height; ++y) {
		for (int x = 0; x < width; ++x) {
			const unsigned int value = vram[y * width + x];
			const Color color = {
				Expand3((value >> 5) & 7),
				Expand3((value >> 2) & 7),
				Expand2(value & 3)
			};
			SetPixel(image, x, y, color);
		}
	}

	return true;
}

int SignExtend6(unsigned int value) {
	return (value & 0x20) ? static_cast<int>(value) - 64 : static_cast<int>(value);
}

bool DecodeYjk(const std::uint8_t* vram, std::size_t size, int screen, MSX::Image& image) {
	constexpr int width = 256;
	constexpr int height = 212;
	constexpr std::size_t bytes = width * height;
	if (!HasRange(size, 0, bytes) || !Allocate(image, width, height))
		return false;

	for (int y = 0; y < height; ++y) {
		for (int x = 0; x < width; x += 4) {
			const std::size_t offset = static_cast<std::size_t>(y) * width + x;
			const std::uint8_t b0 = vram[offset + 0];
			const std::uint8_t b1 = vram[offset + 1];
			const std::uint8_t b2 = vram[offset + 2];
			const std::uint8_t b3 = vram[offset + 3];

			const int j = SignExtend6(
				((b0 >> 5) & 7)
				| (((b1 >> 5) & 7) << 3)
			);
			const int k = SignExtend6(
				((b2 >> 5) & 7)
				| (((b3 >> 5) & 7) << 3)
			);

			const std::uint8_t values[4] = { b0, b1, b2, b3 };
			for (int p = 0; p < 4; ++p) {
				const std::uint8_t value = values[p];
				if (screen == 10 && (value & 0x80)) {
					SetPixel(image, x + p, y, MSX2Palette[value & 15]);
					continue;
				}

				const int luminance = value & 31;
				const int red = std::clamp(luminance + j, 0, 31);
				const int green = std::clamp(luminance + k, 0, 31);
				const int blue = std::clamp((5 * luminance - 2 * j - k) / 4, 0, 31);
				SetPixel(image, x + p, y, {
					static_cast<std::uint8_t>((red * 255 + 15) / 31),
					static_cast<std::uint8_t>((green * 255 + 15) / 31),
					static_cast<std::uint8_t>((blue * 255 + 15) / 31)
				});
			}
		}
	}
	return true;
}
} // namespace

namespace MSX {
bool Decode(const std::uint8_t* data, std::size_t size, int screen, Image& image) {
	image = {};

	const std::uint8_t* payload = nullptr;
	std::size_t payloadSize = 0;
	if (!GetBsavePayload(data, size, payload, payloadSize))
		return false;

	switch (screen) {
	case 0:
		return DecodeScreen0(payload, payloadSize, image);
	case 1:
		return DecodeScreen1(payload, payloadSize, image);
	case 2:
	case 4:
		return DecodeScreen2(payload, payloadSize, image);
	case 3:
		return DecodeScreen3(payload, payloadSize, image);
	case 5:
		return DecodeScreen5(payload, payloadSize, image);
	case 6:
		return DecodeScreen6(payload, payloadSize, image);
	case 7:
		return DecodeScreen7(payload, payloadSize, image);
	case 8:
		return DecodeScreen8(payload, payloadSize, image);
	case 10:
	case 12:
		return DecodeYjk(payload, payloadSize, screen, image);
	default:
		return false;
	}
}

} // namespace MSX