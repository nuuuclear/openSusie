#include "SusiePlugin.h"

#include "SusieImage.h"
#include "msx.h"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace {
struct Format {
	const char* extension;
	int screen;
	const char* name;
};

constexpr Format Formats[] = {
	{ ".sc0", 0, "MSX SCREEN 0" },
	{ ".sc1", 1, "MSX SCREEN 1" },
	{ ".sc2", 2, "MSX SCREEN 2" },
	{ ".sc3", 3, "MSX SCREEN 3" },
	{ ".sc4", 4, "MSX SCREEN 4" },
	{ ".sc5", 5, "MSX SCREEN 5" },
	{ ".sc6", 6, "MSX SCREEN 6" },
	{ ".sc7", 7, "MSX SCREEN 7" },
	{ ".sc8", 8, "MSX SCREEN 8" },
	{ ".s10", 10, "MSX SCREEN 10" },
	{ ".s12", 12, "MSX SCREEN 12" },
};

const char* PluginInfo[] = {
	"00IN",
	"MSX image decoder",
	"*.SC2;*.SC3;*.SC4;*.SC5;*.SC6;*.SC7;*.SC8;*.S10;*.S12;*.SC0;*.SC1",
	"MSX BSAVE image formats"
};

std::string GetExtension(const char* filename) {
	if (!filename)
		return {};

	const char* extension = nullptr;
	for (const char* p = filename; *p; ++p) {
		if (*p == '/' || *p == '\\')
			extension = nullptr;
		else if (*p == '.')
			extension = p;
	}

	if (!extension)
		return {};

	std::string result(extension);
	for (char& c : result) {
		if (c >= 'A' && c <= 'Z')
			c = static_cast<char>(c - 'A' + 'a');
	}

	return result;
}

const Format* FindFormat(const char* filename) {
	const std::string extension = GetExtension(filename);
	for (const Format& format : Formats) {
		if (extension == format.extension)
			return &format;
	}

	return nullptr;
}

bool ReadInput(
	const char* buffer,
	long bufferSize,
	unsigned int flag,
	std::vector<std::uint8_t>& data
) {
	data.clear();
	if (!buffer)
		return false;

	if (flag & SPI_INPUT_MEMORY) {
		if (bufferSize <= 0)
			return false;

		try {
			const auto* begin = reinterpret_cast<const std::uint8_t*>(buffer);
			data.assign(begin, begin + static_cast<std::size_t>(bufferSize));
		} catch (...) {
			data.clear();
			return false;
		}

		return true;
	}

	std::ifstream file(buffer, std::ios::binary | std::ios::ate);
	if (!file)
		return false;

	const std::streampos end = file.tellg();
	if (end < 0)
		return false;

	const std::size_t fileSize = static_cast<std::size_t>(end);
	if (fileSize < 8 || fileSize > 1024 * 1024)
		return false;

	try {
		data.resize(fileSize);
	} catch (...) {
		return false;
	}

	file.seekg(0, std::ios::beg);
	if (!file.read(
		reinterpret_cast<char*>(data.data()),
		static_cast<std::streamsize>(data.size())
	)) {
		data.clear();
		return false;
	}

	return true;
}

bool DecodeInput(
	const char* buffer,
	long bufferSize,
	unsigned int flag,
	const char* filename,
	MSX::Image& image
) {
	std::vector<std::uint8_t> data;
	if (!ReadInput(buffer, bufferSize, flag, data))
		return false;

	const Format* format = nullptr;
	if (flag & SPI_INPUT_MEMORY) {
		if (data.size() < 7 || data[0] != 0xFE)
			return false;

		const std::size_t payloadSize = data.size() - 7;
		for (const Format& candidate : Formats) {
			if (candidate.screen == 0 && payloadSize == 0x1000) format = &candidate;
			if (candidate.screen == 1 && payloadSize == 0x2020) format = &candidate;
			if (candidate.screen == 2 && payloadSize == 0x3800) format = &candidate;
			if (candidate.screen == 3 && payloadSize == 0x0600) format = &candidate;
			if (candidate.screen == 4 && payloadSize == 0x4000) format = &candidate;
			if (candidate.screen == 7 && payloadSize == 0xFAA0) format = &candidate;
		}
	} else {
		format = FindFormat(filename);
	}

	if (!format)
		return false;

	return MSX::Decode(data.data(), data.size(), format->screen, image);
}
} // namespace

SUSIE_EXPORT int WINAPI GetPluginInfo(int index, char* buffer, int bufferLength) {
	return SusieWritePluginInfo(
		index,
		buffer,
		bufferLength,
		PluginInfo,
		static_cast<int>(sizeof(PluginInfo) / sizeof(PluginInfo[0]))
	);
}

SUSIE_EXPORT int WINAPI IsSupported(char* filename, unsigned char* header) {
	if (!FindFormat(filename))
		return 0;

	if (header && header[0] != 0xFE)
		return 0;

	return 1;
}

SUSIE_EXPORT int WINAPI GetPictureInfo(
	char* buffer,
	long bufferSize,
	unsigned int flag,
	SusiePictureInfo* info
) {
	if (!info)
		return SPI_OTHER_ERROR;

	MSX::Image image;
	if (!DecodeInput(buffer, bufferSize, flag, buffer, image))
		return SPI_NOT_SUPPORT;

	SusieSetPictureInfo(info, image.width, image.height);
	return SPI_ALL_RIGHT;
}

SUSIE_EXPORT int WINAPI GetPicture(
	char* buffer,
	long bufferSize,
	unsigned int flag,
	HANDLE* bitmapInfo,
	HANDLE* bitmapData,
	SusieProgressCallback progressCallback,
	long reserved
) {
	(void)progressCallback;
	(void)reserved;

	if (!bitmapInfo || !bitmapData)
		return SPI_OTHER_ERROR;

	*bitmapInfo = nullptr;
	*bitmapData = nullptr;

	if (!(flag & SPI_OUTPUT_MEMORY))
		return SPI_NOT_SUPPORT;

	MSX::Image image;
	if (!DecodeInput(buffer, bufferSize, flag, buffer, image))
		return SPI_NOT_SUPPORT;

	if (!SusieWriteRgbaDib(
		image.rgba.data(),
		image.width,
		image.height,
		bitmapInfo,
		bitmapData
	)) {
		SusieReleasePicture(bitmapInfo, bitmapData);
		return SPI_MEMORY_ERROR;
	}

	return SPI_ALL_RIGHT;
}