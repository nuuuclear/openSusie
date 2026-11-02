#include "SusieImage.h"

#define QOI_IMPLEMENTATION
#include "qoi.h"

namespace {
const char* const PluginInfo[] = {
    "00IN",
    "QOI image decoder",
    "*.qoi",
    "Quite OK Image"
};

const std::vector<std::string> Extensions = {".qoi"};

SusieDecodedImage decodeImage(const char* filePath) {
    SusieDecodedImage image{};
    qoi_desc description{};
    image.pixels = static_cast<std::uint8_t*>(qoi_read(filePath, &description, 4));
    if (!image.pixels) return {};

    image.width = static_cast<int>(description.width);
    image.height = static_cast<int>(description.height);
    image.channels = 4;
    return image;
}

void freeImage(SusieDecodedImage& image) {
    qoi_free_pixels(image.pixels);
    image = {};
}

int checkInput(const char* buffer, unsigned int flags) {
    if (!buffer || (flags & SPI_INPUT_MEMORY)) return SPI_NOT_SUPPORT;
    return SPI_ALL_RIGHT;
}
}

SUSIE_EXPORT int WINAPI GetPluginInfo(int infoNumber, LPSTR buffer, int bufferLength) {
    return SusieWritePluginInfo(
        infoNumber, buffer, bufferLength, PluginInfo,
        static_cast<int>(sizeof(PluginInfo) / sizeof(PluginInfo[0]))
    );
}

SUSIE_EXPORT int WINAPI IsSupported(LPCSTR filename, DWORD_PTR) {
    return SusieHasFileExtension(filename, Extensions) ? 1 : 0;
}

SUSIE_EXPORT int WINAPI GetPictureInfo(
    LPCSTR buffer,
    std::size_t,
    unsigned int flags,
    SusiePictureInfo* pictureInfo
) {
    const int inputResult = checkInput(buffer, flags);
    if (inputResult != SPI_ALL_RIGHT) return inputResult;
    if (!pictureInfo) return SPI_OTHER_ERROR;

    SusieDecodedImage image = decodeImage(buffer);
    if (!image.pixels) return SPI_NOT_SUPPORT;

    SusieSetPictureInfo(pictureInfo, image.width, image.height);
    freeImage(image);
    return SPI_ALL_RIGHT;
}

SUSIE_EXPORT int WINAPI GetPicture(
    LPCSTR buffer,
    std::size_t,
    unsigned int flags,
    HANDLE* bitmapInfo,
    HANDLE* bitmapData,
    SusieProgressCallback progress,
    std::intptr_t userData
) {
    if (!bitmapInfo || !bitmapData) return SPI_OTHER_ERROR;
    *bitmapInfo = nullptr;
    *bitmapData = nullptr;

    const int inputResult = checkInput(buffer, flags);
    if (inputResult != SPI_ALL_RIGHT) return inputResult;
    if (progress && progress(0, 1, userData) != 0) return SPI_ABORT;

    SusieDecodedImage image = decodeImage(buffer);
    if (!image.pixels) return SPI_NOT_SUPPORT;

    const bool success = SusieWriteRgbaDib(
        image.pixels, image.width, image.height, bitmapInfo, bitmapData
    );
    freeImage(image);

    if (!success) return SPI_NO_MEMORY;
    if (progress && progress(1, 1, userData) != 0) {
        SusieReleasePicture(bitmapInfo, bitmapData);
        return SPI_ABORT;
    }
    return SPI_ALL_RIGHT;
}