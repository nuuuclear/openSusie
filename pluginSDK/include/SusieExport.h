#pragma once

#include "SusieImage.h"

#include <cstdint>
#include <limits>
#include <vector>

using SusieCreatePictureHandler = int (*)(
    const char* filepath,
    unsigned int flags,
    const std::vector<std::uint8_t>& rgba,
    int width,
    int height,
    SusieProgressCallback progress,
    std::intptr_t progressData
);

inline int SusieCreatePictureFromDib(
    const char* filepath,
    unsigned int flags,
    void** bitmapInfo,
    void** bitmapData,
    SusiePictureInfo* pictureInfo,
    SusieProgressCallback progress,
    std::intptr_t progressData,
    SusieCreatePictureHandler encoder
) {
    if (!filepath || !bitmapInfo || !bitmapData || !encoder) {
        return SPI_OTHER_ERROR;
    }

    std::vector<std::uint8_t> rgba;
    int width = 0;
    int height = 0;

    if (!SusieConvertDibToRgba(*bitmapInfo, *bitmapData, rgba, width, height)) {
        return SPI_NOT_SUPPORT;
    }

    if (pictureInfo) {
        pictureInfo->width = width;
        pictureInfo->height = height;
        pictureInfo->colorDepth = 32;
    }

    return encoder(
        filepath,
        flags,
        rgba,
        width,
        height,
        progress,
        progressData
    );
}

inline bool SusieReportProgress(
    SusieProgressCallback callback,
    int current,
    int total,
    std::intptr_t data
) {
    return !callback || callback(current, total, data) == 0;
}

using SusieCreatePictureWHandler = int (*)(
    const wchar_t* filepath,
    unsigned int flags,
    const std::vector<std::uint8_t>& rgba,
    int width,
    int height,
    SusieProgressCallback progress,
    std::intptr_t progressData
);

inline int SusieCreatePictureFromDibW(
    const wchar_t* filepath,
    unsigned int flags,
    void** bitmapInfo,
    void** bitmapData,
    SusiePictureInfo* pictureInfo,
    SusieProgressCallback progress,
    std::intptr_t progressData,
    SusieCreatePictureWHandler encoder
) {
    if (!filepath || !bitmapInfo || !bitmapData || !encoder) {
        return SPI_OTHER_ERROR;
    }

    std::vector<std::uint8_t> rgba;
    int width = 0;
    int height = 0;
    if (!SusieConvertDibToRgba(*bitmapInfo, *bitmapData, rgba, width, height)) {
        return SPI_NOT_SUPPORT;
    }

    if (pictureInfo) {
        pictureInfo->width = width;
        pictureInfo->height = height;
        pictureInfo->colorDepth = 32;
    }

    return encoder(filepath, flags, rgba, width, height, progress, progressData);
}