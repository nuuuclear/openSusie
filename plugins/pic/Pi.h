#pragma once

#include <algorithm>
#include <vector>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace PiDecode {
struct Image {
	uint32_t width = 0, height = 0;
	std::vector<uint8_t> pixels;
};

class Bits {
public:
	Bits(const uint8_t *p, size_t n, size_t offset) : data(p), size(n), pos(offset) {}
	uint32_t read(int count) {
		if (count < 0 || count > 24)
			throw std::runtime_error("invalid bit count");
		
		uint32_t v = 0;
		for (int i = 0; i < count; ++i) {
			if (remaining == 0) {
				if (pos >= size)
					throw std::runtime_error("truncated Pi bitstream");
				current = data[pos++];
				remaining = 8;
			}

			v = (v << 1) | ((current >> 7) & 1);
			current <<= 1;
			--remaining;
		}

		return v;
	}

private:
	const uint8_t *data;
	size_t size, pos;
	uint8_t current = 0;
	int remaining = 0;
};

inline bool decode(const uint8_t *data, size_t size, Image &out) {
	try {
		if (!data
		|| 	size < 16
		|| 	data[0] != 'P'
		|| 	data[1] != 'i'
		) return false;

		size_t p = 2;
		while (p < size && data[p] != 0x1a) {
			++p;
		}

		if (p >= size) return false;

		++p;

		while (p < size && data[p] != 0) {
			++p;
		}

		if (p >= size
		|| 	++p > size
		|| 	size - p < 14
		) {
			return false;
		}

		const uint8_t *h = data + p;
		const uint8_t mode = h[0], cbits = h[3];
		if (cbits != 4 && cbits != 8)
			return false;

		const uint32_t colors = 1u << cbits;
		const uint32_t mda = (uint32_t(h[8]) << 8) | h[9];
		p += 10;
		if (mda > size - p)
			return false;

		p += mda;
		if (size - p < 4) {
			return false;
		}

		const uint32_t width = (uint32_t(data[p]) << 8) | data[p + 1];
		const uint32_t height = (uint32_t(data[p + 2]) << 8) | data[p + 3];
		p += 4;
		if (!width || !height || width > 32768 || height > 32768 || uint64_t(width) * height > 100000000) {
			return false;
		}

		std::vector<uint8_t> palette(size_t(colors) * 3);
		if (mode & 0x80) {
			if (colors == 16) {
				for (uint32_t i = 0; i < 16; ++i) {
					const uint8_t v = i < 8 ? 0x77 : 0xff;
					palette[i * 3] = (i & 2) ? v : 0;
					palette[i * 3 + 1] = (i & 4) ? v : 0;
					palette[i * 3 + 2] = (i & 1) ? v : 0;
				}
			} else {
				uint8_t r = 0, g = 0, b = 0;
				for (uint32_t i = 0; i < 256; ++i) {
					palette[i * 3] = r;
					palette[i * 3 + 1] = g;
					palette[i * 3 + 2] = b;
					b = uint8_t(b + 0x40);
					if (!b) {
						r = uint8_t(r + 0x20);
						if (!r) {
							g = uint8_t(g + 0x20);
						}
					}
				}
			}
		} else {
			if (size - p < palette.size()) {
				return false;
			}

			std::copy_n(data + p, palette.size(), palette.data());
			p += palette.size();
		}

		Bits bits(data, size, p);
		std::vector<std::vector<uint8_t>> tables(colors, std::vector<uint8_t>(colors));
		std::vector<std::vector<uint8_t>> order(colors, std::vector<uint8_t>(colors));
		for (uint32_t left = 0; left < colors; ++left) {
			for (uint32_t j = 0; j < colors; ++j) {
				order[left][j] = uint8_t((left + j + 1) & (colors - 1));
			}
		}

		auto readColor = [&](uint8_t prev) -> uint8_t {
			uint32_t n = 0;
			if (cbits == 4) {
				if (bits.read(1))
					n = bits.read(1);
				else if (!bits.read(1))
					n = bits.read(1) + 2;
				else if (!bits.read(1))
					n = bits.read(2) + 4;
				else
					n = bits.read(3) + 8;
			} else {
				if (bits.read(1)) {
					n = bits.read(1);
				} else {
					int b = 0;
					uint32_t base = 2;

					while (b < 6) {
						if (!bits.read(1))
							break;
						++b;
						base <<= 1;
					}

					n = bits.read(b + 1) + base;
				}
			}

			auto &list = order[prev];
			if (n >= colors) {
				throw std::runtime_error("bad Pi colour index");
			}

			const uint8_t value = list[colors - 1 - n];
			if (n) {
				const size_t selected = size_t(colors - 1 - n);
				std::rotate(list.begin() + selected, list.begin() + selected + 1, list.end());
			}

			return value;
		};

		const size_t total = size_t(width) * height;
		std::vector<uint8_t> indices(total);
		size_t count = 0;
		uint8_t previous = 0;
		int previousPosition = -1;

		if (width > 2) {
			indices[0] = readColor(0);
			indices[1] = readColor(indices[0]);
			count = 2;
		}

		while (count < total) {
			if (width <= 2) {
				previous = readColor(previous);
				indices[count++] = previous;
				continue;
			}

			const int positionCode = int(bits.read(2));
			const int position = positionCode == 3 ? int(bits.read(1)) + 3 : positionCode;

			if (position != previousPosition) {
				uint32_t length = 1;
				int nbits = 0;
				while (bits.read(1)) {
					if (++nbits > 24)
						throw std::runtime_error("bad Pi run length");
					length <<= 1;
				}
				if (nbits)
					length += bits.read(nbits);
				length = std::min<uint32_t>(length * 2, uint32_t(total - count));
				int64_t src = 0;
				switch (position)
				{
				case 0:
					src = int64_t(count) - ((count < 2 ? indices[0] == indices[1] : indices[count - 2] == indices[count - 1]) ? 2 : 4);
					break;
				case 1:
					src = int64_t(count) - width;
					break;
				case 2:
					src = int64_t(count) - width * 2;
					break;
				case 3:
					src = int64_t(count) - width + 1;
					break;
				case 4:
					src = int64_t(count) - width - 1;
					break;
				}
				for (uint32_t i = 0; i < length; ++i) {
					size_t source;
					if (src < 0)
					{
						source = size_t((-src) % 2);
						++src;
					}
					else
					{
						source = size_t(src++);
					}
					if (source >= count)
						throw std::runtime_error("invalid Pi copy reference");
					indices[count++] = indices[source];
				}
				previous = indices[count - 1];
				previousPosition = position;
			} else {
				do {
					if (count >= total)
						break;

					previous = readColor(previous);
					indices[count++] = previous;
					if (count >= total)
						break;

					previous = readColor(previous);
					indices[count++] = previous;
				} while (bits.read(1) == 1);

				previousPosition = -1;
			}
		}

		out.width = width;
		out.height = height;
		out.pixels.resize(total * 4);
		for (size_t i = 0; i < total; ++i) {
			const size_t q = size_t(indices[i]) * 3, d = i * 4;
			out.pixels[d] = palette[q];
			out.pixels[d + 1] = palette[q + 1];
			out.pixels[d + 2] = palette[q + 2];
			out.pixels[d + 3] = 255;
		}

		return true;
	} catch (...) {
		out = {};
		return false;
	}
}

} // namespace PiDecode