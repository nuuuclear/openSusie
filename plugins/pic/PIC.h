#pragma once

#include <cstdint>
#include <vector>
#include <cstddef>
#include <stdexcept>
#include <algorithm>

namespace PICDecode {
	struct Image {
		uint32_t width = 0;
		uint32_t height = 0;
		std::vector<uint8_t> pixels;
	};

	class Bits {
	public:
		Bits(const uint8_t* data, size_t size, size_t offset)
			: data(data), size(size), position(offset)
		{
		}

		uint32_t read(int count) {
			if (count < 0 || count > 24)
				throw std::runtime_error("invalid bit count");

			uint32_t value = 0;

			for (int i = 0; i < count; ++i) {
				if (remaining == 0) {
					if (position >= size) {
						throw std::runtime_error("truncated PIC bitstream");
					}

					current = data[position++];
					remaining = 8;
				}

				value = (value << 1) | ((current >> 7) & 1);
				current <<= 1;
				--remaining;
			}

			return value;
		}

	private:
		const uint8_t* data;
		size_t size;
		size_t position;
		uint8_t current = 0;
		int remaining = 0;
	};

	struct Format {
		int mode = 0;
		int type = 0;
		int colorBits = 0;

		int redBits = 0;
		int greenBits = 0;
		int blueBits = 0;
		int intensityBits = 0;

		bool indexed = false;
		bool cached = false;
		bool invertGreenRed = false;
		bool tiled = false;

		uint32_t width = 0;
		uint32_t height = 0;
	};

	inline uint8_t expandBits(uint32_t value, int bits) {
		switch (bits) {
		case 1:
			return value ? 255 : 0;

		case 2:
			return uint8_t((value << 6) | (value << 4) | (value << 2) | value);

		case 3:
			return uint8_t((value << 5) | (value << 2) | (value >> 1));

		case 4:
			return uint8_t((value << 4) | value);

		case 5:
			return uint8_t((value << 3) | (value >> 2));

		case 6:
			return uint8_t((value << 2) | (value >> 4));

		case 7:
			return uint8_t((value << 1) | (value >> 6));

		default:
			return uint8_t(value);
		}
	}

	inline bool skipHeader(const uint8_t* data, size_t size, size_t& position) {
		if (!data || size < 8)
			return false;
		
		if (data[0] != 'P'
		|| 	data[1] != 'I'
		|| 	data[2] != 'C'
		) return false;

		position = 3;

		while (position < size && data[position] != 0x1a)
			++position;

		if (position >= size)
			return false;

		++position;

		while (position < size && data[position] != 0)
			++position;

		if (position >= size)
			return false;

		++position;

		if (position >= size || data[position] != 0)
			return false;

		++position;
		return true;
	}

	inline bool readFormat(Bits& bits, Format& format) {
		format.mode = int(bits.read(4));
		format.type = int(bits.read(4));
		format.colorBits = int(bits.read(16));

		format.width = bits.read(16);
		format.height = bits.read(16);

		const uint64_t pixelCount = uint64_t(format.width) * format.height;

		if (!format.width || !format.height)
			return false;

		if (format.width > 32768 || format.height > 32768)
			return false;

		if (pixelCount > 100000000)
			return false;

		switch (format.type) {
		case 0x0:
			if (format.mode != 0)
				return false;

			if (format.colorBits == 4 || format.colorBits == 8) {
				format.redBits = 5;
				format.greenBits = 5;
				format.blueBits = 5;
				format.intensityBits = 1;
				format.indexed = true;
				return true;
			}

			if (format.colorBits == 15) {
				format.redBits = 5;
				format.greenBits = 5;
				format.blueBits = 5;
				format.cached = true;
				return true;
			}

			if (format.colorBits == 16) {
				format.redBits = 5;
				format.greenBits = 5;
				format.blueBits = 5;
				format.intensityBits = 1;
				format.cached = true;
				return true;
			}

			return false;

		case 0x1:
			if (format.height > 1000)
				return false;

			{
				const uint32_t resolution = format.width * 1000 + format.height;

				if (resolution != 640400
				&&	resolution != 640204 
				&&	resolution != 640200 
				&& 	resolution != 320408 
				&& 	resolution != 320400 
				&& 	resolution != 320200
				) {
					return false;
				}
			}

			if (format.mode & 2) {
				if (format.colorBits != 16)
					return false;

				// stores two palette indices in each 16-bit value for Tiled PC-88VA data
				format.tiled = true;
				format.cached = false;
				format.indexed = false;
				if (pixelCount > 50000000)
					return false;
			}

			if (format.colorBits == 8) {
				format.redBits = 3;
				format.greenBits = 3;
				format.blueBits = 2;
				return true;
			}

			if (format.colorBits == 12) {
				format.redBits = 4;
				format.greenBits = 4;
				format.blueBits = 4;
				format.cached = true;
				return true;
			}

			if (format.colorBits == 16) {
				format.redBits = 5;
				format.greenBits = 6;
				format.blueBits = 5;
				format.cached = true;
				return true;
			}

			return false;

		case 0x2:
			if (format.colorBits != 15)
				return false;

			if (format.mode != 5 && format.mode != 12)
				return false;

			format.redBits = 5;
			format.greenBits = 5;
			format.blueBits = 5;
			format.cached = true;
			return true;

		case 0x3:
			if (format.colorBits != 15)
				return false;

			format.redBits = 5;
			format.greenBits = 5;
			format.blueBits = 5;
			format.invertGreenRed = true;
			return true;

		case 0xf:
			if (format.mode != 0 && format.mode != 1 && format.mode != 15)
				return false;

			bits.read(16);
			bits.read(16);

			{
				const int aspectHigh = int(bits.read(8));
				const int aspectLow = int(bits.read(8));

				if (format.mode == 15 && (aspectHigh == 0 || aspectLow == 0))
					return false;
			}

			if (format.colorBits == 4 || format.colorBits == 8) {
				format.redBits = int(bits.read(8));
				format.greenBits = format.redBits;
				format.blueBits = format.redBits;
				format.indexed = true;
				return true;
			}

			if (format.colorBits == 12) {
				format.redBits = 4;
				format.greenBits = 4;
				format.blueBits = 4;
				format.cached = true;
				return true;
			}

			if (format.colorBits == 15) {
				format.redBits = 5;
				format.greenBits = 5;
				format.blueBits = 5;
				format.cached = true;
				return true;
			}

			if (format.colorBits == 16) {
				format.redBits = 5;
				format.greenBits = 5;
				format.blueBits = 5;
				format.intensityBits = 1;
				format.cached = true;
				return true;
			}

			if (format.colorBits == 24) {
				format.redBits = 8;
				format.greenBits = 8;
				format.blueBits = 8;
				format.cached = true;
				return true;
			}

			return false;

		default:
			return false;
		}
	}

	class ColorCache {
	public:
		ColorCache() {
			for (int i = 0; i < 128; ++i) {
				nodes[i].newer = (i + 1) & 127;
				nodes[i].older = (i + 127) & 127;
			}
		}

		uint32_t get(int index) {
			if (index == newest)
				return nodes[index].color;

			const int newer = nodes[index].newer;
			const int older = nodes[index].older;

			nodes[newer].older = older;
			nodes[older].newer = newer;

			const int first = nodes[newest].newer;

			nodes[first].older = index;
			nodes[index].newer = first;
			nodes[newest].newer = index;
			nodes[index].older = newest;

			newest = index;
			return nodes[index].color;
		}

		void add(uint32_t color) {
			newest = nodes[newest].newer;
			nodes[newest].color = color;
		}

	private:
		struct Node {
			uint32_t color = 0;
			int newer = 0;
			int older = 0;
		};

		Node nodes[128];
		int newest = 0;
	};

	inline uint32_t readRGB(Bits& bits, const Format& format) {
		uint32_t red;
		uint32_t green;
		uint32_t blue;

		if (format.invertGreenRed) {
			red = bits.read(format.redBits);
			green = bits.read(format.greenBits);
		} else {
			green = bits.read(format.greenBits);
			red = bits.read(format.redBits);
		}

		blue = bits.read(format.blueBits);

		if (format.intensityBits) {
			const uint32_t intensity = bits.read(format.intensityBits);

			red = (red << format.intensityBits) | intensity;
			green = (green << format.intensityBits) | intensity;
			blue = (blue << format.intensityBits) | intensity;
		}

		const uint8_t r = expandBits(red, format.redBits + format.intensityBits);
		const uint8_t g = expandBits(green, format.greenBits + format.intensityBits);
		const uint8_t b = expandBits(blue, format.blueBits + format.intensityBits);

		return (uint32_t(r) << 16) | (uint32_t(g) << 8) | b;
	}

	inline bool readPalette(Bits& bits, const Format& format, std::vector<uint32_t>& palette) {
		if (!format.indexed && !format.tiled)
			return true;

		const uint32_t count = format.tiled ? 256u : (1u << format.colorBits);
		if (!format.tiled && (format.colorBits < 1 || format.colorBits > 8))
			return false;

		palette.resize(count);

		for (uint32_t i = 0; i < count; ++i)
			palette[i] = readRGB(bits, format);

		return true;
	}

	inline uint32_t readColor(
		Bits& bits,
		const Format& format,
		ColorCache& cache
	) {
		if (format.tiled)
			return bits.read(16);

		if (format.cached && bits.read(1))
			return cache.get(int(bits.read(7)));

		uint32_t color;

		if (format.indexed)
			color = bits.read(format.colorBits);
		else
			color = readRGB(bits, format);

		if (format.cached)
			cache.add(color);

		return color;
	}

	inline void writePixel(
		std::vector<uint32_t>& pixels,
		uint32_t width,
		uint32_t height,
		int x,
		int y,
		uint32_t color
	) {
		if (x < 0 || y < 0 || x >= int(width) || y >= int(height))
			throw std::runtime_error("PIC chain outside image");

		pixels[size_t(y) * width + size_t(x)] = color;
	}

	inline void decodeChain(
		Bits& bits,
		std::vector<uint32_t>& pixels,
		uint32_t width,
		uint32_t height,
		int x,
		int y,
		uint32_t color
	) {
		writePixel(pixels, width, height, x, y, color);

		if (!bits.read(1))
			return;

		while (true) {
			switch (bits.read(2)) {
			case 1:
				++y;
				--x;
				break;

			case 2:
				++y;
				break;

			case 3:
				++y;
				++x;
				break;

			case 0:
				if (!bits.read(1))
					return;

				++y;

				if (!bits.read(1))
					x -= 2;
				else
					x += 2;

				break;
			}

			writePixel(pixels, width, height, x, y, color);
		}
	}

	inline bool decodePixels(
		Bits& bits,
		const Format& format,
		std::vector<uint32_t>& pixels
	) {
		const size_t total = size_t(format.width) * format.height;

		pixels.assign(total, 0xffffffffu);
		pixels[0] = 0;

		ColorCache cache;
		int64_t count = -1;

		while (count < int64_t(total)) {
			uint32_t length = 2;
			int bitCount = 1;

			while (bits.read(1)) {
				if (bitCount > 24)
					throw std::runtime_error("bad PIC run length");

				length <<= 1;
				++bitCount;
			}

			length = length - 1 + bits.read(bitCount);

			if (length > total + 1)
				throw std::runtime_error("PIC run exceeds image");

			count += length;

			if (count >= int64_t(total))
				break;

			const int x = int(count % format.width);
			const int y = int(count / format.width);

			const uint32_t color = readColor(bits, format, cache);

			decodeChain(
				bits,
				pixels,
				format.width,
				format.height,
				x,
				y,
				color);
		}

		return true;
	}

	inline void convertPixels(
		const std::vector<uint32_t>& source,
		const Format& format,
		const std::vector<uint32_t>& palette,
		Image& output
	) {
		const size_t total = size_t(format.width) * format.height;
		const size_t outputPixelCount = total * (format.tiled ? 2u : 1u);

		output.width = format.width;
		output.height = format.height * (format.tiled ? 2u : 1u);
		output.pixels.resize(outputPixelCount * 4);

		const uint32_t paletteMask = format.indexed
			? uint32_t(palette.size() - 1)
			: 0;

		uint32_t previous = 0;

		for (size_t i = 0; i < total; ++i) {
			if (source[i] != 0xffffffffu)
				previous = source[i];

			if (format.tiled) {
				if (palette.size() != 256)
					throw std::runtime_error("invalid tile palette");

				const uint32_t first = palette[previous & 0xff];
				const uint32_t second = palette[(previous >> 8) & 0xff];
				const size_t firstOffset = (i * 2) * 4;
				const size_t secondOffset = firstOffset + 4;

				output.pixels[firstOffset] = uint8_t(first >> 16);
				output.pixels[firstOffset + 1] = uint8_t(first >> 8);
				output.pixels[firstOffset + 2] = uint8_t(first);
				output.pixels[firstOffset + 3] = 255;

				output.pixels[secondOffset] = uint8_t(second >> 16);
				output.pixels[secondOffset + 1] = uint8_t(second >> 8);
				output.pixels[secondOffset + 2] = uint8_t(second);
				output.pixels[secondOffset + 3] = 255;
				continue;
			}

			const uint32_t color = format.indexed
				? palette[previous & paletteMask]
				: previous;
			const size_t offset = i * 4;

			output.pixels[offset] = uint8_t(color >> 16);
			output.pixels[offset + 1] = uint8_t(color >> 8);
			output.pixels[offset + 2] = uint8_t(color);
			output.pixels[offset + 3] = 255;
		}
	}

	inline bool decode(const uint8_t* data, size_t size, Image& output) {
		output = {};

		try {
			size_t offset = 0;

			if (!skipHeader(data, size, offset))
				return false;

			Bits bits(data, size, offset);

			Format format;

			if (!readFormat(bits, format))
				return false;

			std::vector<uint32_t> palette;

			if (!readPalette(bits, format, palette))
				return false;

			std::vector<uint32_t> decodedPixels;

			if (!decodePixels(bits, format, decodedPixels))
				return false;

			convertPixels(decodedPixels, format, palette, output);
			return true;
		} catch (...) {
			output = {};
			return false;
		}
	}
} // namespace PICDecode