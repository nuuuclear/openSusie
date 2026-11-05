#include "SusieImage.h"
#include "Pi.h"
#include "PIC.h"

#include <string>
#include <cstdint>
#include <vector>
#include <fstream>
#include <iterator>

namespace {

const char *const PluginInfo[] = {
	"00IN",
	"Yanagisawa Pi PIC",
	"*.pi;*.pic*;.jpc",
	"Yanagisawa Pi and PIC Images"
};

const std::vector<std::string> Extensions = {".pi", ".pic", ".jpc"};

bool loadData(const char *buffer, size_t bufferSize, bool memoryInput, std::vector<uint8_t> &data) {
	if (memoryInput) {
		if (!buffer || bufferSize == 0)
			return false;

		const auto *begin = reinterpret_cast<const uint8_t *>(buffer);
		data.assign(begin, begin + bufferSize);

		return true;
	}

	if (!buffer || !*buffer)
		return false;

	std::ifstream file(buffer, std::ios::binary);
	if (!file) return false;

	std::vector<char> bytes{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
	if (bytes.empty()) return false;

	data.assign(bytes.begin(), bytes.end());

	return true;
}

bool decode(const std::vector<uint8_t> &data, const char *filename, PiDecode::Image &pi, PICDecode::Image &pic, bool &isPi) {
	std::string ext;
	if (filename) {
		ext = filename;
		const size_t dot = ext.find_last_of('.');
		if (dot != std::string::npos)
			ext = ext.substr(dot);

		for (char &c : ext) {
			if (c >= 'A' && c <= 'Z') {
				c = char(c - 'A' + 'a');
			}
		}
	}

	if (ext == ".pi" || (data.size() >= 2 && data[0] == 'P' && data[1] == 'i')) {
		isPi = true;
		return PiDecode::decode(data.data(), data.size(), pi);
	}

	isPi = false;

	return PICDecode::decode(data.data(), data.size(), pic);
}

int checkInput(const char *buffer, size_t bufferSize, unsigned int flags) {
	if (!buffer)
		return SPI_NOT_SUPPORT;
		
	if (flags & SPI_INPUT_MEMORY) {
		if (!bufferSize) {
			return SPI_NOT_SUPPORT;
		}
	} else if (!*buffer) {
		return SPI_NOT_SUPPORT;
	}

	return SPI_ALL_RIGHT;
}

bool loadImage(
	const char *buffer,
	size_t bufferSize,
	unsigned int flags,
	const char *filename,
	std::vector<uint8_t> &pixels,
	uint32_t &width,
	uint32_t &height
) {
	std::vector<uint8_t> data;
	if (!loadData(buffer, bufferSize, (flags & SPI_INPUT_MEMORY) != 0, data))
		return false;

	PiDecode::Image pi;
	PICDecode::Image pic;
	bool isPi = false;

	if (!decode(data, filename, pi, pic, isPi))
		return false;

	if (isPi) {
		width = pi.width;
		height = pi.height;
		pixels = std::move(pi.pixels);
	} else {
		width = pic.width;
		height = pic.height;
		pixels = std::move(pic.pixels);
	}

	return width && height && !pixels.empty();
}
} // namespace

SUSIE_EXPORT int WINAPI GetPluginInfo(int infoNumber, LPSTR buffer, int bufferLength) {
	return SusieWritePluginInfo(
		infoNumber,
		buffer,
		bufferLength,
		PluginInfo, 
		static_cast<int>(sizeof(PluginInfo) / sizeof(PluginInfo[0]))
	);
}

SUSIE_EXPORT int WINAPI IsSupported(LPCSTR filename, DWORD_PTR) {
	if (!filename)
		return 0;

	return SusieHasFileExtension(filename, Extensions) ? 1 : 0;
}

SUSIE_EXPORT int WINAPI GetPictureInfo(
	LPCSTR buffer,
	size_t bufferSize,
	unsigned int flags,
	SusiePictureInfo *pictureInfo
) {
	const int result = checkInput(buffer, bufferSize, flags);
	if (result != SPI_ALL_RIGHT)
		return result;

	if (!pictureInfo)
		return SPI_OTHER_ERROR;

	std::vector<uint8_t> pixels;
	uint32_t width = 0, height = 0;

	if (!loadImage(buffer, bufferSize, flags, (flags & SPI_INPUT_MEMORY) ? nullptr : buffer, pixels, width, height))
		return SPI_NOT_SUPPORT;

	SusieSetPictureInfo(pictureInfo, static_cast<int>(width), static_cast<int>(height));

	return SPI_ALL_RIGHT;
}

SUSIE_EXPORT int WINAPI GetPicture(
	LPCSTR buffer,
	size_t bufferSize,
	unsigned int flags,
	HANDLE *bitmapInfo,
	HANDLE *bitmapData,
	SusieProgressCallback progress,
	intptr_t userData
) {
	if (!bitmapInfo || !bitmapData)
		return SPI_OTHER_ERROR;
	*bitmapInfo = nullptr;
	*bitmapData = nullptr;

	const int result = checkInput(buffer, bufferSize, flags);
	if (result != SPI_ALL_RIGHT)
		return result;

	if (progress && progress(0, 1, userData) != 0)
		return SPI_ABORT;

	std::vector<uint8_t> pixels;
	uint32_t width = 0, height = 0;
	if (!loadImage(buffer, bufferSize, flags, (flags & SPI_INPUT_MEMORY) ? nullptr : buffer, pixels, width, height)) {
		return SPI_NOT_SUPPORT;
	}

	if (!SusieWriteRgbaDib(pixels.data(), static_cast<int>(width), static_cast<int>(height), bitmapInfo, bitmapData)) {
		return SPI_NO_MEMORY;
	}

	if (progress && progress(1, 1, userData) != 0) {
		SusieReleasePicture(bitmapInfo, bitmapData);
		return SPI_ABORT;
	}

	return SPI_ALL_RIGHT;
}