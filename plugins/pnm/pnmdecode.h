#pragma once

#include <cctype>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace PnmDecode {

struct Image {
	std::vector<std::uint8_t> pixels;
	int width = 0;
	int height = 0;

	void clear() {
		pixels.clear();
		width = 0;
		height = 0;
	}
};

enum class Format {
	P1, P2, P3, P4, P5, P6, P7
};

enum class TupleType {
	Bitmap,
	BitmapAlpha,
	Grayscale,
	GrayscaleAlpha,
	RGB,
	RGBA
};

struct Header {
	unsigned int width = 0;
	unsigned int height = 0;
	unsigned int maxValue = 1;
	unsigned int depth = 1;
	Format format = Format::P1;
	TupleType tuple = TupleType::Grayscale;
};

class Reader {
public:
	Reader(const char* data, std::size_t size)
    :   data(data), size(size) 
    {
    }

	bool token(std::string& result, bool* hadDelimiter = nullptr) {
		result.clear();

		if (hadDelimiter) *hadDelimiter = false;
		if (!skipWhitespace()) return false;

		while (position < size) {
			const unsigned char c = static_cast<unsigned char>(data[position]);

			if (isWhitespace(c)) {
				consumeWhitespace();
				if (hadDelimiter) *hadDelimiter = true;
				return !result.empty();
			}

			if (c == '#') {
				skipComment();
				return !result.empty();
			}

			result.push_back(static_cast<char>(c));
			++position;
		}

		return !result.empty();
	}

	bool number(unsigned int& value, bool* hadDelimiter = nullptr) {
		std::string text;

		if (!token(text, hadDelimiter)) return false;
		if (text.empty()) return false;

		value = 0;

		for (unsigned char c : text) {
			if (c < '0' || c > '9') return false;

			const unsigned int digit = c - '0';

			if (value > (std::numeric_limits<unsigned int>::max() - digit) / 10) {
				return false;
			}

			value = value * 10 + digit;
		}

		return true;
	}

	bool byte(std::uint8_t& value) {
		if (position >= size) return false;

		value = static_cast<std::uint8_t>(data[position++]);
		return true;
	}

	bool sample(unsigned int maxValue, unsigned int& value) {
		std::uint8_t high = 0;
		std::uint8_t low = 0;

		if (!byte(high)) return false;

		if (maxValue <= 255) {
			value = high;
		} else {
			if (!byte(low)) return false;
			value = (static_cast<unsigned int>(high) << 8) | low;
		}

		return value <= maxValue;
	}

	bool readLine(std::string& line, bool& terminated) {
		line.clear();
		terminated = false;

		if (position >= size) return false;

		while (position < size) {
			const char c = data[position++];

			if (c == '\n') {
				terminated = true;
				break;
			}

			if (c == '\r') {
				if (position < size && data[position] == '\n') {
					++position;
				}

				terminated = true;
				break;
			}

			line.push_back(c);
		}

		return true;
	}

	std::size_t remaining() const {
		return size - position;
	}

private:
	static bool isWhitespace(unsigned char c) {
		return std::isspace(c) != 0;
	}

	bool skipWhitespace() {
		while (position < size) {
			const unsigned char c = static_cast<unsigned char>(data[position]);

			if (isWhitespace(c)) {
				++position;
				continue;
			}

			if (c == '#') {
				skipComment();
				continue;
			}

			return true;
		}

		return false;
	}

	void consumeWhitespace() {
		const char c = data[position++];

		if (c == '\r' && position < size && data[position] == '\n') {
			++position;
		}
	}

	void skipComment() {
		while (position < size) {
			const char c = data[position++];

			if (c == '\n') return;

			if (c == '\r') {
				if (position < size && data[position] == '\n') {
					++position;
				}
				return;
			}
		}
	}

	const char* data;
	std::size_t size;
	std::size_t position = 0;
};

inline std::uint8_t scale(unsigned int value, unsigned int maxValue) {
	return static_cast<std::uint8_t>(
		(static_cast<std::uint64_t>(value) * 255 + maxValue / 2) / maxValue
	);
}

inline bool allocate(Image& image, unsigned int width, unsigned int height) {
	if (width == 0 || height == 0) return false;

	if (width > static_cast<unsigned int>(std::numeric_limits<int>::max())
	|| height > static_cast<unsigned int>(std::numeric_limits<int>::max())) {
		return false;
	}

	const std::size_t w = width;
	const std::size_t h = height;

	if (w > std::numeric_limits<std::size_t>::max() / h) return false;

	const std::size_t count = w * h;

	if (count > std::numeric_limits<std::size_t>::max() / 4) return false;

	image.pixels.resize(count * 4);
	image.width = static_cast<int>(width);
	image.height = static_cast<int>(height);

	return true;
}

inline void setPixel(
	Image& image,
	std::size_t index,
	std::uint8_t r,
	std::uint8_t g,
	std::uint8_t b,
	std::uint8_t a = 255
) {
	const std::size_t offset = index * 4;

	image.pixels[offset + 0] = r;
	image.pixels[offset + 1] = g;
	image.pixels[offset + 2] = b;
	image.pixels[offset + 3] = a;
}

inline bool parseUnsigned(const std::string& text, unsigned int& value) {
	if (text.empty()) return false;

	value = 0;

	for (unsigned char c : text) {
		if (c < '0' || c > '9') return false;

		const unsigned int digit = c - '0';

		if (value > (std::numeric_limits<unsigned int>::max() - digit) / 10) {
			return false;
		}

		value = value * 10 + digit;
	}

	return true;
}

inline std::string trim(const std::string& text) {
	std::size_t first = 0;
	std::size_t last = text.size();

	while (first < last &&
		std::isspace(static_cast<unsigned char>(text[first]))) {
		++first;
	}

	while (last > first &&
		std::isspace(static_cast<unsigned char>(text[last - 1]))) {
		--last;
	}

	return text.substr(first, last - first);
}

inline bool parsePAM(Reader& reader, Header& header) {
	bool gotWidth = false;
	bool gotHeight = false;
	bool gotDepth = false;
	bool gotMaxValue = false;
	bool gotTupleType = false;
	bool gotEnd = false;

	std::string line;

	while (!gotEnd) {
		bool terminated = false;

		if (!reader.readLine(line, terminated)) return false;

		line = trim(line);

		if (line.empty() || line[0] == '#') {
			if (!terminated) return false;
			continue;
		}

		if (line == "ENDHDR") {
			if (!terminated) return false;
			gotEnd = true;
			break;
		}

		const std::size_t separator = line.find_first_of(" \t");
		if (separator == std::string::npos) return false;

		const std::string key = line.substr(0, separator);
		const std::string value = trim(line.substr(separator));

		if (value.empty()) return false;

		if (key == "WIDTH") {
			if (gotWidth || !parseUnsigned(value, header.width)) return false;
			gotWidth = true;
			continue;
		}

		if (key == "HEIGHT") {
			if (gotHeight || !parseUnsigned(value, header.height)) return false;
			gotHeight = true;
			continue;
		}

		if (key == "DEPTH") {
			if (gotDepth || !parseUnsigned(value, header.depth)) return false;
			gotDepth = true;
			continue;
		}

		if (key == "MAXVAL") {
			if (gotMaxValue || !parseUnsigned(value, header.maxValue)) return false;
			gotMaxValue = true;
			continue;
		}

		if (key == "TUPLTYPE") {
			if (gotTupleType) return false;

			if (value == "BLACKANDWHITE") {
				header.tuple = TupleType::Bitmap;
			} else if (value == "BLACKANDWHITE_ALPHA") {
				header.tuple = TupleType::BitmapAlpha;
			} else if (value == "GRAYSCALE") {
				header.tuple = TupleType::Grayscale;
			} else if (value == "GRAYSCALE_ALPHA") {
				header.tuple = TupleType::GrayscaleAlpha;
			} else if (value == "RGB") {
				header.tuple = TupleType::RGB;
			} else if (value == "RGB_ALPHA") {
				header.tuple = TupleType::RGBA;
			} else {
				return false;
			}

			gotTupleType = true;
		}
	}

	if (!gotWidth || !gotHeight || !gotDepth || !gotMaxValue || !gotEnd) {
		return false;
	}

	if (header.depth < 1 || header.depth > 4) return false;
	if (header.maxValue == 0 || header.maxValue > 65535) return false;

	if (!gotTupleType) {
		switch (header.depth) {
			case 1: header.tuple = TupleType::Grayscale; break;
			case 2: header.tuple = TupleType::GrayscaleAlpha; break;
			case 3: header.tuple = TupleType::RGB; break;
			case 4: header.tuple = TupleType::RGBA; break;
			default: return false;
		}
	}

	switch (header.tuple) {
		case TupleType::Bitmap:
			return header.depth == 1 && header.maxValue == 1;

		case TupleType::BitmapAlpha:
			return header.depth == 2 && header.maxValue == 1;

		case TupleType::Grayscale:
			return header.depth == 1;

		case TupleType::GrayscaleAlpha:
			return header.depth == 2;

		case TupleType::RGB:
			return header.depth == 3;

		case TupleType::RGBA:
			return header.depth == 4;
	}

	return false;
}

inline bool parseHeader(Reader& reader, Header& header) {
	std::string magic;

	if (!reader.token(magic)) return false;
	if (magic.size() != 2 || magic[0] != 'P') return false;

	switch (magic[1]) {
		case '1': header.format = Format::P1; break;
		case '2': header.format = Format::P2; break;
		case '3': header.format = Format::P3; break;
		case '4': header.format = Format::P4; break;
		case '5': header.format = Format::P5; break;
		case '6': header.format = Format::P6; break;
		case '7':
			header.format = Format::P7;
			return parsePAM(reader, header);
		default:
			return false;
	}

	if (!reader.number(header.width)) return false;

	bool binaryDelimiter = false;

	if (!reader.number(header.height, &binaryDelimiter)) return false;
	if (header.format == Format::P4 && !binaryDelimiter) return false;

	header.depth = (header.format == Format::P3 || header.format == Format::P6) ? 3 : 1;

	if (header.format == Format::P2
	||  header.format == Format::P3
	||  header.format == Format::P5
	||  header.format == Format::P6
    ) {
		if (!reader.number(header.maxValue, &binaryDelimiter)) return false;

		if ((header.format == Format::P5 || header.format == Format::P6)
		&& !binaryDelimiter
        ) {
			return false;
		}
	}

	if (header.maxValue == 0 || header.maxValue > 65535) return false;

	switch (header.format) {
		case Format::P1:
		case Format::P4:
			header.tuple = TupleType::Bitmap;
			header.maxValue = 1;
			break;

		case Format::P2:
		case Format::P5:
			header.tuple = TupleType::Grayscale;
			break;

		case Format::P3:
		case Format::P6:
			header.tuple = TupleType::RGB;
			break;

		default:
			return false;
	}

	return true;
}

inline bool validateRaster(const Reader& reader, const Header& header) {
	const std::size_t width = header.width;
	const std::size_t height = header.height;

	if (width == 0 || height == 0) return false;
	if (width > std::numeric_limits<std::size_t>::max() / height) return false;

	const std::size_t pixelCount = width * height;

	if (header.format == Format::P1) {
		return pixelCount <= reader.remaining();
	}

	if (header.format == Format::P4) {
		const std::size_t rowBytes = (width + 7) / 8;

		if (rowBytes > std::numeric_limits<std::size_t>::max() / height) {
			return false;
		}

		return reader.remaining() >= rowBytes * height;
	}

	if (pixelCount > std::numeric_limits<std::size_t>::max() / header.depth) {
		return false;
	}

	const std::size_t sampleCount = pixelCount * header.depth;
	const bool ascii = header.format == Format::P2 || header.format == Format::P3;

	if (ascii) {
		return sampleCount <= reader.remaining();
	}

	const std::size_t bytesPerSample = header.maxValue > 255 ? 2 : 1;

	if (sampleCount > std::numeric_limits<std::size_t>::max() / bytesPerSample) {
		return false;
	}

	return reader.remaining() >= sampleCount * bytesPerSample;
}

inline bool decodeBitmap(Reader& reader, const Header& header, Image& image) {
	const unsigned int width = header.width;
	const unsigned int height = header.height;

	if (header.format == Format::P1) {
		const std::size_t count = static_cast<std::size_t>(width) * height;

		for (std::size_t i = 0; i < count; ++i) {
			unsigned int value = 0;

			if (!reader.number(value) || value > 1) return false;

			const std::uint8_t color = value == 1 ? 0 : 255;
			setPixel(image, i, color, color, color);
		}

		return true;
	}

	const std::size_t rowBytes = (static_cast<std::size_t>(width) + 7) / 8;

	for (unsigned int y = 0; y < height; ++y) {
		for (std::size_t byteIndex = 0; byteIndex < rowBytes; ++byteIndex) {
			std::uint8_t packed = 0;

			if (!reader.byte(packed)) return false;

			for (unsigned int bit = 0; bit < 8; ++bit) {
				const std::size_t x = byteIndex * 8 + bit;

				if (x >= width) break;

				const bool black = ((packed >> (7 - bit)) & 1u) != 0;
				const std::uint8_t color = black ? 0 : 255;

				setPixel(
					image,
					static_cast<std::size_t>(y) * width + x,
					color, color, color
				);
			}
		}
	}

	return true;
}

inline bool decodeSamples(Reader& reader, const Header& header,Image& image) {
	const std::size_t pixelCount = static_cast<std::size_t>(header.width) * header.height;

	const bool ascii = header.format == Format::P2 || header.format == Format::P3;

	for (std::size_t i = 0; i < pixelCount; ++i) {
		unsigned int channels[4] = {0, 0, 0, header.maxValue};

		for (unsigned int c = 0; c < header.depth; ++c) {
			if (ascii) {
				if (!reader.number(channels[c])) return false;
				if (channels[c] > header.maxValue) return false;
				continue;
			}

			if (!reader.sample(header.maxValue, channels[c])) return false;
		}

		unsigned int r = 0;
		unsigned int g = 0;
		unsigned int b = 0;
		unsigned int a = 255;

		switch (header.tuple) {
			case TupleType::Bitmap:
				r = g = b = channels[0] == 0 ? 0 : 255;
				break;

			case TupleType::BitmapAlpha:
				r = g = b = channels[0] == 0 ? 0 : 255;
				a = channels[1] == 0 ? 0 : 255;
				break;

			case TupleType::Grayscale:
				r = g = b = scale(channels[0], header.maxValue);
				break;

			case TupleType::GrayscaleAlpha:
				r = g = b = scale(channels[0], header.maxValue);
				a = scale(channels[1], header.maxValue);
				break;

			case TupleType::RGB:
				r = scale(channels[0], header.maxValue);
				g = scale(channels[1], header.maxValue);
				b = scale(channels[2], header.maxValue);
				break;

			case TupleType::RGBA:
				r = scale(channels[0], header.maxValue);
				g = scale(channels[1], header.maxValue);
				b = scale(channels[2], header.maxValue);
				a = scale(channels[3], header.maxValue);
				break;
		}

		setPixel(
			image,
			i,
			static_cast<std::uint8_t>(r),
			static_cast<std::uint8_t>(g),
			static_cast<std::uint8_t>(b),
			static_cast<std::uint8_t>(a)
		);
	}

	return true;
}

inline bool decode(const char* data, std::size_t size, Image& image) {
	image.clear();

	if (!data || size < 3) return false;

	try {
		Reader reader(data, size);
		Header header;

		if (!parseHeader(reader, header)) return false;
		if (!validateRaster(reader, header)) return false;
		if (!allocate(image, header.width, header.height)) return false;

		const bool success
        =   header.format == Format::P1 || header.format == Format::P4
        ?   decodeBitmap(reader, header, image)
        :   decodeSamples(reader, header, image);

		if (!success) {
			image.clear();
			return false;
		}

		return true;
	} catch (...) {
		image.clear();
		return false;
	}
}

} // namespace PnmDecode