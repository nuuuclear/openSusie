#include "SusieImage.h"

#include "xyz.h"

#include <algorithm>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

namespace {
const char* const PluginInfo[] = {
    "00IN",
    "XYZ image decoder",
    "*.xyz",
    "RPG Maker 2000/2003 XYZ Image"
};

const std::vector<std::string> Extensions = {
    ".xyz"
};

constexpr std::size_t MaxFileSize = 256 * 1024 * 1024;

bool readFile(
    const char* filename,
    std::vector<std::uint8_t>& data
) {
    data.clear();

    if (!filename || !*filename)
        return false;

    std::ifstream file(filename, std::ios::binary | std::ios::ate);

    if (!file)
        return false;

    const std::streampos end = file.tellg();

    if (end <= 0)
        return false;

    const auto fileSize = static_cast<std::uint64_t>(end);

    if (fileSize > MaxFileSize)
        return false;

    if (fileSize > std::numeric_limits<std::size_t>::max())
        return false;

    try {
        data.resize(static_cast<std::size_t>(fileSize));
    }
    catch (...) {
        return false;
    }

    file.seekg(0, std::ios::beg);

    if (!file)
        return false;

    file.read(
        reinterpret_cast<char*>(data.data()),
        static_cast<std::streamsize>(data.size())
    );

    if (!file) {
        data.clear();
        return false;
    }

    return true;
}

bool decodeImage(
    const char* buffer,
    std::size_t bufferSize,
    bool memoryInput,
    XYZ::Image& image
) {
    if (!buffer)
        return false;

    if (memoryInput) {
        if (bufferSize == 0 || bufferSize > MaxFileSize)
            return false;

        return XYZ::Decode(
            reinterpret_cast<const std::uint8_t*>(buffer),
            bufferSize,
            image
        );
    }

    std::vector<std::uint8_t> data;

    if (!readFile(buffer, data))
        return false;

    return XYZ::Decode(
        data.data(),
        data.size(),
        image
    );
}

int checkInput( const char* buffer, std::size_t bufferSize, unsigned int flags) {
    if (!buffer)
        return SPI_NOT_SUPPORT;

    if (flags & SPI_INPUT_MEMORY) {
        if (bufferSize == 0 || bufferSize > MaxFileSize) {
            return SPI_NOT_SUPPORT;
        }
    }

    return SPI_ALL_RIGHT;
}

} // namespace

SUSIE_EXPORT int WINAPI GetPluginInfo(
	int infoNumber,
	LPSTR buffer,
	int bufferLength
) {
	return SusieWritePluginInfo(
		infoNumber,
		buffer,
		bufferLength,
		PluginInfo,
		static_cast<int>(sizeof(PluginInfo) / sizeof(PluginInfo[0]))
	);
}

SUSIE_EXPORT int WINAPI IsSupported(
	LPCSTR filename,
	DWORD_PTR
) {
	return SusieHasFileExtension(filename, Extensions) ? 1 : 0;
}

SUSIE_EXPORT int WINAPI GetPictureInfo(
	LPCSTR buffer,
	std::size_t bufferSize,
	unsigned int flags,
	SusiePictureInfo* pictureInfo
) {
	const int inputResult = checkInput(
		buffer,
		bufferSize,
		flags
	);

	if (inputResult != SPI_ALL_RIGHT)
		return inputResult;

	if (!pictureInfo)
		return SPI_OTHER_ERROR;

	XYZ::Image image;

	if (!decodeImage(
		buffer,
		bufferSize,
		(flags & SPI_INPUT_MEMORY) != 0,
		image
	)) {
		return SPI_NOT_SUPPORT;
	}

	SusieSetPictureInfo(
		pictureInfo,
		image.width,
		image.height
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
	if (!bitmapInfo || !bitmapData)
		return SPI_OTHER_ERROR;

	*bitmapInfo = nullptr;
	*bitmapData = nullptr;

	const int inputResult = checkInput(
		buffer,
		bufferSize,
		flags
	);

	if (inputResult != SPI_ALL_RIGHT)
		return inputResult;

	if (progress && progress(0, 1, userData) != 0)
		return SPI_ABORT;

	XYZ::Image image;

	if (!decodeImage(
		buffer,
		bufferSize,
		(flags & SPI_INPUT_MEMORY) != 0,
		image
	)) {
		return SPI_NOT_SUPPORT;
	}

	const bool success = SusieWriteRgbaDib(
		image.rgba.data(),
		image.width,
		image.height,
		bitmapInfo,
		bitmapData
	);

	if (!success)
		return SPI_NO_MEMORY;

	if (progress && progress(1, 1, userData) != 0) {
		SusieReleasePicture(bitmapInfo, bitmapData);
		return SPI_ABORT;
	}

	return SPI_ALL_RIGHT;
}