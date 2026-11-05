#pragma once

#include <vector>
#include <array>
#include <cstring>
#include <limits>
#include <algorithm>
#include <cstdint>

namespace MakiDecode {

struct Image {
	unsigned int width = 0;
	unsigned int height = 0;
	std::vector<uint8_t> pixels;

	void clear() {
		width = 0;
		height = 0;
		pixels.clear();
	}
};

namespace Detail {

class Reader {
public:
	Reader(const void* data, std::size_t size)
    :   data_(static_cast<const uint8_t*>(data)), size_(size)
    {
    }

	bool valid(std::size_t offset, std::size_t count) const {
		return offset <= size_ && count <= size_ - offset;
	}

	bool u8(std::size_t offset, uint8_t& value) const {
		if (!valid(offset, 1)) return false;
		value = data_[offset];
		return true;
	}

	bool le16(std::size_t offset, std::uint16_t& value) const {
		if (!valid(offset, 2)) return false;

		value = static_cast<std::uint16_t>(data_[offset])
            | (static_cast<std::uint16_t>(data_[offset + 1]) << 8);

		return true;
	}

	bool be16(std::size_t offset, std::uint16_t& value) const {
		if (!valid(offset, 2)) return false;

		value = (static_cast<std::uint16_t>(data_[offset]) << 8)
			| static_cast<std::uint16_t>(data_[offset + 1]);

		return true;
	}

	bool le32(std::size_t offset, std::uint32_t& value) const {
		if (!valid(offset, 4)) return false;

		value = static_cast<std::uint32_t>(data_[offset])
			| (static_cast<std::uint32_t>(data_[offset + 1]) << 8)
			| (static_cast<std::uint32_t>(data_[offset + 2]) << 16)
			| (static_cast<std::uint32_t>(data_[offset + 3]) << 24);

		return true;
	}

	uint8_t at(std::size_t offset) const {
		return offset < size_ ? data_[offset] : 0;
	}

	const uint8_t* data() const {
		return data_;
	}

	std::size_t size() const {
		return size_;
	}

private:
	const uint8_t* data_;
	std::size_t size_;
};

struct Palette {
	std::array<std::array<uint8_t, 3>, 256> colors{};

	void setGRB(
		unsigned int index,
		uint8_t g,
		uint8_t r,
		uint8_t b,
		unsigned int bits
	) {
		if (index >= colors.size()) return;

		colors[index][0] = expand(r, bits);
		colors[index][1] = expand(g, bits);
		colors[index][2] = expand(b, bits);
	}

	static uint8_t expand(uint8_t value, unsigned int bits) {
		if (bits >= 8) return value;
		if (bits == 0) return 0;

		const unsigned int maximum = (1u << bits) - 1;
		const unsigned int component = value >> (8 - bits);

		return static_cast<uint8_t>(
			(component * 255 + maximum / 2) / maximum
		);
	}
};

static constexpr std::array<int, 16> CopyX = {
	0, -1, -2, -4, 0, -1, 0, -1,
	-2, 0, -1, -2, 0, -1, -2, 0
};

static constexpr std::array<int, 16> CopyY = {
	0, 0, 0, 0, -1, -1, -2, -2,
	-2, -4, -4, -4, -8, -8, -8, -16
};

class BitReader {
public:
	BitReader(const Reader& reader, std::size_t offset, std::size_t end)
    :   reader_(reader), offset_(offset), end_(end)
    {
    }

	bool read(unsigned int& bit) {
		if (bits_ == 0) {
			if (offset_ >= end_ || !reader_.u8(offset_++, byte_)) {
				return false;
			}

			bits_ = 8;
		}

		bit = (byte_ >> 7) & 1;
		byte_ <<= 1;
		--bits_;

		return true;
	}

private:
	const Reader& reader_;
	std::size_t offset_;
	std::size_t end_;
	uint8_t byte_ = 0;
	unsigned int bits_ = 0;
};

inline bool allocate(
	Image& image,
	unsigned int width,
	unsigned int height
) {
	if (width == 0 || height == 0) return false;
	if (width > 8192 || height > 8192) return false;

	const std::uint64_t count = static_cast<std::uint64_t>(width) * height * 4;

	if (count > std::numeric_limits<std::size_t>::max()) {
		return false;
	}

	image.width = width;
	image.height = height;
	image.pixels.assign(static_cast<std::size_t>(count), 0);

	return true;
}

inline void putPixel(
	Image& image,
	unsigned int x,
	unsigned int y,
	const Palette& palette,
	unsigned int index
) {
	if (x >= image.width || y >= image.height) return;
	if (index >= palette.colors.size()) return;

	const std::size_t offset = (static_cast<std::size_t>(y) * image.width + x) * 4;

	image.pixels[offset + 0] = palette.colors[index][0];
	image.pixels[offset + 1] = palette.colors[index][1];
	image.pixels[offset + 2] = palette.colors[index][2];
	image.pixels[offset + 3] = 255;
}

inline bool decodeMKI(
	const Reader& reader,
	Image& image,
	bool variantB
) {
	if (reader.size() < 96 + 1000) return false;

	std::uint16_t flagBSize = 0;
	std::uint16_t pixelSizeA = 0;
	std::uint16_t pixelSizeB = 0;
	std::uint16_t extension = 0;

	if (!reader.be16(32, flagBSize)) return false;
	if (!reader.be16(34, pixelSizeA)) return false;
	if (!reader.be16(36, pixelSizeB)) return false;
	if (!reader.be16(38, extension)) return false;

	if (extension & 2) {
		return false;
	}

	const unsigned int outputWidth = 640;
	const unsigned int outputHeight = (extension & 1) ? 800 : 400;

	if (!allocate(image, outputWidth, outputHeight)) return false;

	Palette palette;

	for (unsigned int i = 0; i < 16; ++i) {
		const uint8_t g = reader.at(48 + i * 3);
		const uint8_t r = reader.at(49 + i * 3);
		const uint8_t b = reader.at(50 + i * 3);

		palette.setGRB(i, g, r, b, 4);
	}

	const std::size_t flagAOffset = 96;
	const std::size_t flagBOffset = flagAOffset + 1000;
	const std::size_t pixelOffset = flagBOffset + flagBSize;

	if (!reader.valid(flagAOffset, 1000)) return false;
	if (!reader.valid(flagBOffset, flagBSize)) return false;

	const std::size_t pixelSize =
		static_cast<std::size_t>(pixelSizeA) + pixelSizeB;

	if (!reader.valid(pixelOffset, pixelSize)) return false;

	std::array<uint8_t, 320 * 400> mask{};

	BitReader flagA(reader, flagAOffset, flagBOffset);
	std::size_t flagBPosition = flagBOffset;

	for (unsigned int blockY = 0; blockY < 100; ++blockY) {
		for (unsigned int blockX = 0; blockX < 80; ++blockX) {
			unsigned int bit = 0;

			if (!flagA.read(bit)) return false;

			std::uint16_t pattern = 0;

			if (bit != 0) {
				if (!reader.valid(flagBPosition, 2)) return false;

				pattern =
					(static_cast<std::uint16_t>(reader.at(flagBPosition)) << 8)
					| reader.at(flagBPosition + 1);

				flagBPosition += 2;
			}

			for (unsigned int y = 0; y < 4; ++y) {
				for (unsigned int x = 0; x < 4; ++x) {
					const unsigned int index =
						(blockY * 4 + y) * 320 + blockX * 4 + x;

					mask[index] = static_cast<uint8_t>(
						(pattern >> (15 - (y * 4 + x))) & 1
					);
				}
			}
		}
	}

	std::vector<uint8_t> output(320 * 400, 0);

	std::size_t pixelPosition = pixelOffset;

	for (std::size_t i = 0; i < output.size(); ++i) {
		if (mask[i]) {
			if (pixelPosition >= reader.size()) return false;
			output[i] = reader.at(pixelPosition++);
		}
	}

	const unsigned int verticalDistance = variantB ? 4 : 2;

	for (unsigned int y = verticalDistance; y < 400; ++y) {
		for (unsigned int x = 0; x < 320; ++x) {
			output[y * 320 + x] ^= output[(y - verticalDistance) * 320 + x];
		}
	}

	for (unsigned int y = 0; y < 400; ++y) {
		for (unsigned int x = 0; x < 640; ++x) {
			const uint8_t packed = output[y * 320 + x / 2];

			const unsigned int index = (x & 1) ? (packed >> 4) : (packed & 0x0F);
			const unsigned int outputY = y * ((extension & 1) ? 2 : 1);

			putPixel(image, x, outputY, palette, index);

			if (extension & 1) {
				putPixel(image, x, outputY + 1, palette, index);
			}
		}
	}

	return true;
}

inline bool decodeMAG(const Reader& reader, Image& image) {
	if (reader.size() < 40) return false;

	std::size_t header = 8;

	while (header < reader.size() && reader.at(header) != 0x1A) {
		++header;
	}

	if (header == reader.size()) return false;

	++header;

	while (header < reader.size() && reader.at(header) != 0) {
		++header;
	}

	if (!reader.valid(header, 32)) return false;

	const uint8_t model = reader.at(header + 1);
	const uint8_t modelFlags = reader.at(header + 2);
	const uint8_t screenMode = reader.at(header + 3);

	std::uint16_t x0 = 0;
	std::uint16_t y0 = 0;
	std::uint16_t x1 = 0;
	std::uint16_t y1 = 0;

	std::uint32_t offA = 0;
	std::uint32_t offB = 0;
	std::uint32_t sizeB = 0;
	std::uint32_t offPixel = 0;
	std::uint32_t sizePixel = 0;

	if (!reader.le16(header + 4, x0)) return false;
	if (!reader.le16(header + 6, y0)) return false;
	if (!reader.le16(header + 8, x1)) return false;
	if (!reader.le16(header + 10, y1)) return false;

	if (!reader.le32(header + 12, offA)) return false;
	if (!reader.le32(header + 16, offB)) return false;
	if (!reader.le32(header + 20, sizeB)) return false;
	if (!reader.le32(header + 24, offPixel)) return false;
	if (!reader.le32(header + 28, sizePixel)) return false;

	(void)sizeB;
	(void)sizePixel;

	const bool msx = model == 0x03;

	const bool yjk = msx && (
		modelFlags == 0x24
    ||  modelFlags == 0x34
    ||  modelFlags == 0x44
	);

	const bool eightBit = msx
        ? (modelFlags == 0x14 || (screenMode & 0x80) != 0)
        : (screenMode & 0x80) != 0;

	const unsigned int pixelsPerByte = eightBit ? 1 : 2;

	if (x1 < x0 || y1 < y0) return false;

	const unsigned int leftByte = (x0 / pixelsPerByte) & ~3u;
	const unsigned int rightByte = ((x1 / pixelsPerByte + 4) & ~3u);

	if (rightByte <= leftByte) return false;

	const unsigned int byteWidth = rightByte - leftByte;
	const unsigned int height = y1 - y0 + 1;
	if (byteWidth > 8192 || height > 8192) return false;

	const unsigned int paddedWidth = byteWidth * pixelsPerByte;
	if (paddedWidth == 0 || paddedWidth > 8192) return false;

	const std::uint64_t outputSize64 = static_cast<std::uint64_t>(byteWidth) * height;

	if (outputSize64 > std::numeric_limits<std::size_t>::max()) {
		return false;
	}

	const std::size_t outputSize = static_cast<std::size_t>(outputSize64);
	const std::size_t base = header;

	if (offA > reader.size() - base
    ||  offB > reader.size() - base
    ||  offPixel > reader.size() - base
    ) {
		return false;
	}

	const std::size_t flagAStart = base + offA;
	const std::size_t flagBStart = base + offB;
	const std::size_t pixelStart = base + offPixel;

	if (flagAStart > flagBStart || flagBStart > pixelStart) {
		return false;
	}

	if (pixelStart > reader.size()) return false;

	const std::size_t flagAEnd = flagBStart;
	const std::size_t flagBEnd = pixelStart;

	Palette palette;

	unsigned int paletteBits = 4;

	if (msx) {
		paletteBits = 3;
	}
	else if (model == 0x68) {
		paletteBits = 5;
	}
	else if (model == 0x99 || eightBit) {
		paletteBits = 8;
	}

	const unsigned int paletteCount = yjk
		? 16
		: (eightBit ? 256 : 16);

	const std::size_t paletteStart = base + 32;
	const std::size_t paletteLength = flagAStart >= paletteStart ? flagAStart - paletteStart : 0;

	const unsigned int availableColors = static_cast<unsigned int>(
		std::min<std::size_t>(paletteCount, paletteLength / 3)
	);

	for (unsigned int i = 0; i < availableColors; ++i) {
		const std::size_t p = paletteStart + i * 3;

		palette.setGRB(
			i,
			reader.at(p),
			reader.at(p + 1),
			reader.at(p + 2),
			paletteBits
		);
	}

	std::vector<uint8_t> output(outputSize, 0);
	std::vector<uint8_t> actions(byteWidth / 4, 0);

	BitReader flagA(reader, flagAStart, flagAEnd);

	std::size_t flagBPosition = flagBStart;
	std::size_t pixelPosition = pixelStart;
	std::size_t actionPosition = 0;

	auto readWord = [&]() -> std::uint16_t {
		if (!reader.valid(pixelPosition, 2)) {
			pixelPosition = reader.size();
			return 0;
		}

		const std::uint16_t value
        =   static_cast<std::uint16_t>(reader.at(pixelPosition))
        |   (static_cast<std::uint16_t>(reader.at(pixelPosition + 1)) << 8);

		pixelPosition += 2;

		return value;
	};

	for (std::size_t pos = 0; pos < outputSize; pos += 4) {
		unsigned int bit = 0;

		if (!flagA.read(bit)) return false;

		if (bit) {
			if (flagBPosition >= flagBEnd) return false;
			actions[actionPosition] ^= reader.at(flagBPosition++);
		}

		const uint8_t action = actions[actionPosition];

		const unsigned int nibbles[2] = {
            static_cast<uint8_t>(action >> 4),
			static_cast<uint8_t>(action & 0x0F)
		};

		for (unsigned int n = 0; n < 2; ++n) {
			const unsigned int code = nibbles[n];
			const std::size_t dst = pos + n * 2;

			if (dst + 1 >= output.size()) break;

			if (code == 0) {
				const std::uint16_t value = readWord();

				output[dst] = static_cast<uint8_t>(value);
				output[dst + 1] = static_cast<uint8_t>(value >> 8);

				continue;
			}

			const std::size_t distance
            =   static_cast<std::size_t>(-CopyY[code]) * byteWidth
            +   static_cast<std::size_t>(-CopyX[code]) * 2;

			if (distance > dst) {
				output[dst] = 0;
				output[dst + 1] = 0;
			} else {
				output[dst] = output[dst - distance];
				output[dst + 1] = output[dst - distance + 1];
			}
		}

		++actionPosition;

		if (actionPosition >= actions.size()) {
			actionPosition = 0;
		}
	}

	const unsigned int width = x1 - x0 + 1;
	const unsigned int leftCrop = x0 - leftByte * pixelsPerByte;

	
    if (yjk) {
        if (byteWidth < 4) return false;

        const unsigned int yjkWidth = x1 / pixelsPerByte - x0 / pixelsPerByte + 1;
        const unsigned int yjkLeftCrop = x0 / pixelsPerByte - leftByte;

        if (!allocate(image, yjkWidth, height)) return false;

        for (unsigned int y = 0; y < height; ++y) {
            const std::size_t row =
                static_cast<std::size_t>(y) * byteWidth;

            for (unsigned int groupX = 0; groupX + 3 < byteWidth; groupX += 4) {
                const uint8_t b0 = output[row + groupX];
                const uint8_t b1 = output[row + groupX + 1];
                const uint8_t b2 = output[row + groupX + 2];
                const uint8_t b3 = output[row + groupX + 3];

                const int k = (b0 & 7) | ((b1 & 7) << 3);
                const int j = (b2 & 7) | ((b3 & 7) << 3);

                const int signedK = (k & 32) ? k - 64 : k;
                const int signedJ = (j & 32) ? j - 64 : j;

                const uint8_t values[4] = {
                    b0, b1, b2, b3
                };

                for (unsigned int i = 0; i < 4; ++i) {
                    const unsigned int sourceX = groupX + i;

                    if (sourceX < yjkLeftCrop
                    ||  sourceX >= yjkLeftCrop + yjkWidth
                    ) {
                        continue;
                    }

                    const unsigned int destinationX = sourceX - yjkLeftCrop;
                    const int luminance = values[i] >> 3;

                    if (modelFlags != 0x44 && (luminance & 1)) {
                        const unsigned int index = luminance >> 1;
                        putPixel(image, destinationX, y, palette, index);
                        continue;
                    }

                    const int r = luminance + signedJ;
                    const int g = luminance + signedK;
                    const int b = (5 * luminance) / 4
                        - signedJ / 2
                        - signedK / 4;

                    const std::size_t dst = (static_cast<std::size_t>(y) * yjkWidth + destinationX) * 4;

                    image.pixels[dst + 0] = static_cast<uint8_t>(
                        std::clamp(r, 0, 31) * 255 / 31
                    );

                    image.pixels[dst + 1] = static_cast<uint8_t>(
                        std::clamp(g, 0, 31) * 255 / 31
                    );

                    image.pixels[dst + 2] = static_cast<uint8_t>(
                        std::clamp(b, 0, 31) * 255 / 31
                    );

                    image.pixels[dst + 3] = 255;
                }
            }
        }

        return true;
    }

	const bool doubleHeight = msx
		? modelFlags == 0x04
		: (!eightBit && (screenMode & 1));

	const unsigned int outputHeight =
		height * (doubleHeight ? 2 : 1);

	if (!allocate(image, width, outputHeight)) return false;

    for (unsigned int y = 0; y < height; ++y) {
        for (unsigned int x = 0; x < width; ++x) {
            const unsigned int sourceX = leftCrop + x;

            const std::size_t sourceOffset
            =   static_cast<std::size_t>(y) * byteWidth
            +   sourceX / pixelsPerByte;

            unsigned int index = 0;

            if (eightBit) {
                index = output[sourceOffset];
            } else {
                const uint8_t packed = output[sourceOffset];

                index = (sourceX & 1)
                    ? packed & 0x0F
                    : packed >> 4;
            }

            const unsigned int outputY = y * (doubleHeight ? 2 : 1);

            putPixel(image, x, outputY, palette, index);

            if (doubleHeight) {
                putPixel(image, x, outputY + 1, palette, index);
            }
        }
    }

	return true;
}

} // namespace Detail

inline bool decode(const void* data, std::size_t size, Image& image) {
	image.clear();

	if (!data || size < 8) return false;

	try {
		Detail::Reader reader(data, size);

		if (std::memcmp(reader.data(), "MAKI01A ", 8) == 0) {
			return Detail::decodeMKI(reader, image, false);
		}

		if (std::memcmp(reader.data(), "MAKI01B ", 8) == 0) {
			return Detail::decodeMKI(reader, image, true);
		}

		if (std::memcmp(reader.data(), "MAKI02  ", 8) == 0) {
			return Detail::decodeMAG(reader, image);
		}
	} catch (...) {
		image.clear();
		return false;
	}

	image.clear();
	return false;
}

} // namespace MakiDecode