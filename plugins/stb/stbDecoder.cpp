#include "SusieImage.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <vector>

namespace {
const char* const PluginInfo[] = {
    "00IN",
    "stb_image",
    "*.png;*.jpg;*.jpeg;*.bmp",
    "PNG, JPEG, and BMP images"
};

const std::vector<std::string> Extensions = {".png", ".jpg", ".jpeg", ".bmp"};

SusieDecodedImage decodeImage(const char* filePath) {
    SusieDecodedImage image{};
    int sourceChannels = 0;
    image.pixels = stbi_load(filePath, &image.width, &image.height, &sourceChannels, 4);
    if (image.pixels) image.channels = 4;
    return image;
}

void freeImage(SusieDecodedImage& image) {
    stbi_image_free(image.pixels);
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

    int width = 0;
    int height = 0;
    int channels = 0;
    if (!stbi_info(buffer, &width, &height, &channels)) return SPI_NOT_SUPPORT;

    SusieSetPictureInfo(pictureInfo, width, height);
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
