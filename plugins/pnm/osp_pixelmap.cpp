#include "SusieImage.h"

#include <cstdint>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <vector>

#include "PnmDecode.h"

namespace {

const char* const PluginInfo[] = {
	"00IN",
	"pnm",
	"*.pbm;*.pgm;*.ppm;*.pnm;*.pam",
	"Portable Bitmap, Graymap, Pixmap, and Arbitrary Map images"
};

const std::vector<std::string> Extensions = {
	".pbm",
	".pgm",
	".ppm",
	".pnm",
	".pam"
};

bool loadImage(const char* buffer, std::size_t bufferSize, bool memoryInput, PnmDecode::Image& image) {
	if (memoryInput) {
		if (!buffer || bufferSize == 0) return false;
		return PnmDecode::decode(buffer, bufferSize, image);
	}

	if (!buffer || buffer[0] == '\0') return false;

	std::ifstream file(buffer, std::ios::binary);
	if (!file) return false;

	std::vector<char> data{
		std::istreambuf_iterator<char>(file),
		std::istreambuf_iterator<char>()
    };

	if (data.empty()) return false;

	return PnmDecode::decode(data.data(), data.size(), image);
}

bool validDimensions(const PnmDecode::Image& image) {
	if (image.width == 0 || image.height == 0) return false;

	if (image.width > static_cast<unsigned int>(std::numeric_limits<int>::max())) {
		return false;
	}

	if (image.height > static_cast<unsigned int>(std::numeric_limits<int>::max())) {
		return false;
	}

	return true;
}

int checkInput(const char* buffer, std::size_t bufferSize, unsigned int flags) {
	if (!buffer) return SPI_NOT_SUPPORT;

	if (flags & SPI_INPUT_MEMORY) {
		if (bufferSize == 0) return SPI_NOT_SUPPORT;
	}
	else if (buffer[0] == '\0') {
		return SPI_NOT_SUPPORT;
	}

	return SPI_ALL_RIGHT;
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
	if (!filename) return 0;
	return SusieHasFileExtension(filename, Extensions) ? 1 : 0;
}

SUSIE_EXPORT int WINAPI GetPictureInfo(
	LPCSTR buffer,
	std::size_t bufferSize,
	unsigned int flags,
	SusiePictureInfo* pictureInfo
) {
	const int inputResult = checkInput(buffer, bufferSize, flags);
	if (inputResult != SPI_ALL_RIGHT) return inputResult;

	if (!pictureInfo) return SPI_OTHER_ERROR;

	PnmDecode::Image image;

	if (!loadImage(
		buffer,
		bufferSize,
		(flags & SPI_INPUT_MEMORY) != 0,
		image
	)) {
		return SPI_NOT_SUPPORT;
	}

	if (!validDimensions(image)) {
		return SPI_NOT_SUPPORT;
	}

	SusieSetPictureInfo(
		pictureInfo,
		static_cast<int>(image.width),
		static_cast<int>(image.height)
	);

	return SPI_ALL_RIGHT;
}

SUSIE_EXPORT int WINAPI GetPicture(
	LPCSTR buffer,
	std::size_t bufferSize,
	unsigned int flags,
	HANDLE* bitmapInfo,
	HANDLE* bitmapData,
	SusieProgressCallback progress,
	std::intptr_t userData
) {
	if (!bitmapInfo || !bitmapData) return SPI_OTHER_ERROR;

	*bitmapInfo = nullptr;
	*bitmapData = nullptr;

	const int inputResult = checkInput(buffer, bufferSize, flags);
	if (inputResult != SPI_ALL_RIGHT) return inputResult;

	if (progress && progress(0, 1, userData) != 0) {
		return SPI_ABORT;
	}

	PnmDecode::Image image;

	if (!loadImage(
		buffer,
		bufferSize,
		(flags & SPI_INPUT_MEMORY) != 0,
		image
	)) {
		return SPI_NOT_SUPPORT;
	}

	if (!validDimensions(image)) {
		return SPI_NOT_SUPPORT;
	}

	const bool success = SusieWriteRgbaDib(
		image.pixels.data(),
		static_cast<int>(image.width),
		static_cast<int>(image.height),
		bitmapInfo,
		bitmapData
	);

	if (!success) return SPI_NO_MEMORY;

	if (progress && progress(1, 1, userData) != 0) {
		SusieReleasePicture(bitmapInfo, bitmapData);
		return SPI_ABORT;
	}

	return SPI_ALL_RIGHT;
}