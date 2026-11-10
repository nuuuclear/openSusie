#include "SusieImage.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <limits>
#include <vector>

namespace {
const char* const PluginInfo[] = {
    "00IN",
    "stb_image",
    "*.png;*.jpg;*.jpeg;*.bmp;*.pbm",
    "PNG, JPEG, and Bitmap images"
};

const std::vector<std::string> Extensions = {".png", ".jpg", ".jpeg", ".bmp", ".pbm"};

SusieDecodedImage decodeImage(const char* buffer, std::size_t bufferSize, bool memoryInput) {
    SusieDecodedImage image{};
    int sourceChannels = 0;
    if (memoryInput) {
        image.pixels = stbi_load_from_memory(
            reinterpret_cast<const stbi_uc*>(buffer),
            static_cast<int>(bufferSize),
            &image.width,
            &image.height,
            &sourceChannels,
            4
        );
    } else {
        image.pixels = stbi_load(buffer, &image.width, &image.height, &sourceChannels, 4);
    }
    if (image.pixels) image.channels = 4;
    return image;
}

void freeImage(SusieDecodedImage& image) {
    stbi_image_free(image.pixels);
    image = {};
}

int checkInput(const char* buffer, std::size_t bufferSize, unsigned int flags) {
    if (!buffer) return SPI_NOT_SUPPORT;
    if ((flags & SPI_INPUT_MEMORY)
    &&  (bufferSize == 0
    ||   bufferSize > static_cast<std::size_t>(std::numeric_limits<int>::max()))
    ) {
        return SPI_NOT_SUPPORT;
    }
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
    std::size_t bufferSize,
    unsigned int flags,
    SusiePictureInfo* pictureInfo
) {
    const int inputResult = checkInput(buffer, bufferSize, flags);
    if (inputResult != SPI_ALL_RIGHT) return inputResult;
    if (!pictureInfo) return SPI_OTHER_ERROR;

    int width = 0;
    int height = 0;
    int channels = 0;
    const bool memoryInput = (flags & SPI_INPUT_MEMORY) != 0;
    const bool infoAvailable = memoryInput
        ? stbi_info_from_memory(
            reinterpret_cast<const stbi_uc*>(buffer),
            static_cast<int>(bufferSize),
            &width,
            &height,
            &channels
        ) != 0
        : stbi_info(buffer, &width, &height, &channels) != 0;
    if (!infoAvailable) return SPI_NOT_SUPPORT;

    SusieSetPictureInfo(pictureInfo, width, height);
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
    if (progress && progress(0, 1, userData) != 0) return SPI_ABORT;

    SusieDecodedImage image = decodeImage(
        buffer, bufferSize, (flags & SPI_INPUT_MEMORY) != 0
    );
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
