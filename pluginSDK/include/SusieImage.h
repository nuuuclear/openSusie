#pragma once

#include "SusiePlugin.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

struct SusieDecodedImage {
    std::uint8_t* pixels = nullptr;
    int width = 0;
    int height = 0;
    int channels = 0;
};

inline int SusieWritePluginInfo(int index, char* buffer, int bufferLength, const char* const* values, int valueCount) {
    if (!buffer || bufferLength <= 0 || !values || index < 0 || index >= valueCount) {
        return 0;
    }

    const std::size_t length = std::min(
        std::strlen(values[index]),
        static_cast<std::size_t>(bufferLength - 1)
    );
    std::memcpy(buffer, values[index], length);
    buffer[length] = '\0';
    return 1;
}

inline bool SusieWriteRgbaDib(
    const std::uint8_t* rgba,
    int width,
    int height,
    HANDLE* bitmapInfo,
    HANDLE* bitmapData
) {
    if (!bitmapInfo || !bitmapData) return false;
    *bitmapInfo = nullptr;
    *bitmapData = nullptr;

    if (!rgba || width <= 0 || height <= 0) return false;

    if (static_cast<std::size_t>(width) > std::numeric_limits<std::size_t>::max() / 4) {
        return false;
    }
    const std::size_t rowBytes = static_cast<std::size_t>(width) * 4;
    if (rowBytes > std::numeric_limits<std::size_t>::max() / static_cast<std::size_t>(height)) {
        return false;
    }
    const std::size_t imageBytes = rowBytes * static_cast<std::size_t>(height);
    if (imageBytes > std::numeric_limits<std::uint32_t>::max()) return false;

    constexpr std::size_t dibHeaderSize = sizeof(SusieBitmapInfoHeader) + 4 * sizeof(std::uint32_t);
    HLOCAL infoHandle = LocalAlloc(LMEM_MOVEABLE, dibHeaderSize);
    if (!infoHandle) return false;

    HLOCAL dataHandle = LocalAlloc(LMEM_MOVEABLE, imageBytes);
    if (!dataHandle) {
        LocalFree(infoHandle);
        return false;
    }

    auto* header = static_cast<SusieBitmapInfoHeader*>(LocalLock(infoHandle));
    auto* pixels = static_cast<std::uint8_t*>(LocalLock(dataHandle));
    if (!header || !pixels) {
        if (header) LocalUnlock(infoHandle);
        if (pixels) LocalUnlock(dataHandle);
        LocalFree(infoHandle);
        LocalFree(dataHandle);
        return false;
    }

    *header = {};
    header->size = static_cast<std::uint32_t>(dibHeaderSize);
    header->width = width;
    header->height = height;
    header->planes = 1;
    header->bitCount = 32;
    header->compression = SUSIE_BI_BITFIELDS;
    header->sizeImage = static_cast<std::uint32_t>(imageBytes);
    const std::uint32_t channelMasks[] = {
        0x00ff0000,
        0x0000ff00,
        0x000000ff,
        0xff000000
    };
    std::memcpy(
        reinterpret_cast<std::uint8_t*>(header) + sizeof(SusieBitmapInfoHeader),
        channelMasks,
        sizeof(channelMasks)
    );

    for (int y = 0; y < height; ++y) {
        const std::uint8_t* source = rgba + static_cast<std::size_t>(y) * rowBytes;
        std::uint8_t* destination = pixels + static_cast<std::size_t>(height - 1 - y) * rowBytes;
        for (int x = 0; x < width; ++x) {
            destination[4 * x + 0] = source[4 * x + 2];
            destination[4 * x + 1] = source[4 * x + 1];
            destination[4 * x + 2] = source[4 * x + 0];
            destination[4 * x + 3] = source[4 * x + 3];
        }
    }

    LocalUnlock(dataHandle);
    LocalUnlock(infoHandle);
    *bitmapInfo = infoHandle;
    *bitmapData = dataHandle;
    return true;
}

inline bool SusieHasFileExtension(const char* filename, const std::vector<std::string>& extensions) {
    if (!filename) return false;

    const char* extensionStart = nullptr;
    for (const char* cursor = filename; *cursor; ++cursor) {
        if (*cursor == '/' || *cursor == '\\') {
            extensionStart = nullptr;
        } else if (*cursor == '.') {
            extensionStart = cursor;
        }
    }
    if (!extensionStart) return false;

    const std::size_t length = std::strlen(extensionStart);
    for (const std::string& extension : extensions) {
        if (length != extension.size()) continue;
        bool matches = true;
        for (std::size_t i = 0; i < length; ++i) {
            if (std::tolower(static_cast<unsigned char>(extensionStart[i]))
                != std::tolower(static_cast<unsigned char>(extension[i]))) {
                matches = false;
                break;
            }
        }
        if (matches) return true;
    }

    return false;
}

inline void SusieSetPictureInfo(SusiePictureInfo* info, int width, int height) {
    if (!info) return;
    *info = {};
    info->width = width;
    info->height = height;
    info->colorDepth = 32;
}

inline void SusieReleasePicture(HANDLE* bitmapInfo, HANDLE* bitmapData) {
    if (bitmapInfo && *bitmapInfo) {
        LocalFree(*bitmapInfo);
        *bitmapInfo = nullptr;
    }
    if (bitmapData && *bitmapData) {
        LocalFree(*bitmapData);
        *bitmapData = nullptr;
    }
}

inline bool SusieConvertDibToRgba(
    HANDLE bitmapInfo,
    HANDLE bitmapData,
    std::vector<std::uint8_t>& rgba,
    int& width,
    int& height
) {
    rgba.clear();
    width = 0;
    height = 0;

    if (!bitmapInfo || !bitmapData) return false;

    const std::size_t infoSize = LocalSize(bitmapInfo);
    const std::size_t dataSize = LocalSize(bitmapData);
    SusieLocalLock infoLock(bitmapInfo);
    SusieLocalLock pixelsLock(bitmapData);
    const auto* info = static_cast<const std::uint8_t*>(infoLock.get());
    const auto* pixels = static_cast<const std::uint8_t*>(pixelsLock.get());
    if (!info || !pixels) {
        return false;
    }

    bool success = false;
    do {
        if (infoSize < sizeof(SusieBitmapInfoHeader)) break;

        SusieBitmapInfoHeader header{};
        std::memcpy(&header, info, sizeof(header));
        if (header.size < sizeof(SusieBitmapInfoHeader) || header.size > infoSize) break;
        if (header.width <= 0 || header.height == 0
            || header.height == std::numeric_limits<std::int32_t>::min()
            || header.planes != 1) {
            break;
        }

        const int imageWidth = header.width;
        const int imageHeight = header.height < 0 ? -header.height : header.height;
        const unsigned int bitCount = header.bitCount;
        if (bitCount != 1 && bitCount != 4 && bitCount != 8
            && bitCount != 16 && bitCount != 24 && bitCount != 32) {
            break;
        }
        if (header.compression != SUSIE_BI_RGB
            && header.compression != SUSIE_BI_BITFIELDS
            && header.compression != SUSIE_BI_ALPHABITFIELDS) {
            break;
        }
        if ((header.compression == SUSIE_BI_BITFIELDS
                || header.compression == SUSIE_BI_ALPHABITFIELDS)
            && bitCount != 16 && bitCount != 32) {
            break;
        }

        const std::uint64_t rowBits = static_cast<std::uint64_t>(imageWidth) * bitCount;
        const std::uint64_t stride64 = ((rowBits + 31) / 32) * 4;
        if (stride64 > std::numeric_limits<std::uint64_t>::max()
                / static_cast<std::uint64_t>(imageHeight)) {
            break;
        }
        const std::uint64_t inputBytes64 = stride64 * static_cast<std::uint64_t>(imageHeight);
        const std::uint64_t pixelsCount64 = static_cast<std::uint64_t>(imageWidth)
            * static_cast<std::uint64_t>(imageHeight);
        if (pixelsCount64 > std::numeric_limits<std::uint64_t>::max() / 4) break;
        const std::uint64_t outputBytes64 = pixelsCount64 * 4;
        if (stride64 > std::numeric_limits<std::size_t>::max()
        ||  inputBytes64 > std::numeric_limits<std::size_t>::max()
        ||  outputBytes64 > std::numeric_limits<std::size_t>::max()
        ) {
            break;
        }

        const std::size_t stride = static_cast<std::size_t>(stride64);
        const std::size_t inputBytes = static_cast<std::size_t>(inputBytes64);
        if (dataSize && inputBytes > dataSize) break;
        if (header.sizeImage && header.sizeImage < inputBytes) break;

        std::size_t extraMaskBytes = 0;
        std::uint32_t redMask = 0;
        std::uint32_t greenMask = 0;
        std::uint32_t blueMask = 0;
        std::uint32_t alphaMask = 0;
        if (header.compression == SUSIE_BI_BITFIELDS
        ||  header.compression == SUSIE_BI_ALPHABITFIELDS
        ) {
            const std::size_t maskCount =
                header.compression == SUSIE_BI_ALPHABITFIELDS ? 4 : 3;
            if (header.size >= 52) {
                std::memcpy(&redMask, info + 40, sizeof(redMask));
                std::memcpy(&greenMask, info + 44, sizeof(greenMask));
                std::memcpy(&blueMask, info + 48, sizeof(blueMask));
                if (header.size >= 56) {
                    std::memcpy(&alphaMask, info + 52, sizeof(alphaMask));
                }
            } else {
                extraMaskBytes = maskCount * sizeof(std::uint32_t);
                if (header.size + extraMaskBytes > infoSize) break;
                std::memcpy(&redMask, info + header.size, sizeof(redMask));
                std::memcpy(&greenMask, info + header.size + 4, sizeof(greenMask));
                std::memcpy(&blueMask, info + header.size + 8, sizeof(blueMask));
                if (maskCount == 4) {
                    std::memcpy(&alphaMask, info + header.size + 12, sizeof(alphaMask));
                }
            }
            if (!redMask || !greenMask || !blueMask) break;
        } else if (bitCount == 16) {
            redMask = 0x7c00;
            greenMask = 0x03e0;
            blueMask = 0x001f;
        }

        std::size_t paletteCount = 0;
        std::size_t paletteOffset = header.size + extraMaskBytes;
        if (bitCount <= 8) {
            paletteCount = header.colorsUsed
                ? header.colorsUsed
                : static_cast<std::size_t>(1u << bitCount);
            if (paletteCount > (1u << bitCount)
                || paletteCount > (infoSize - paletteOffset) / 4) {
                break;
            }
        }

        rgba.resize(static_cast<std::size_t>(outputBytes64));
        const auto component = [](std::uint32_t value, std::uint32_t mask) -> std::uint8_t {
            if (!mask) return 0;
            unsigned int shift = 0;
            while (((mask >> shift) & 1u) == 0u) ++shift;
            const std::uint32_t maximum = mask >> shift;
            const std::uint32_t channel = (value & mask) >> shift;
            return static_cast<std::uint8_t>(
                (static_cast<std::uint64_t>(channel) * 255 + maximum / 2) / maximum
            );
        };

        for (int y = 0; y < imageHeight; ++y) {
            const int sourceY = header.height < 0 ? y : imageHeight - 1 - y;
            const std::uint8_t* row = pixels + static_cast<std::size_t>(sourceY) * stride;
            for (int x = 0; x < imageWidth; ++x) {
                std::uint8_t red = 0;
                std::uint8_t green = 0;
                std::uint8_t blue = 0;
                std::uint8_t alpha = 255;

                if (bitCount <= 8) {
                    unsigned int index = 0;
                    if (bitCount == 8) {
                        index = row[x];
                    } else if (bitCount == 4) {
                        const std::uint8_t value = row[x / 2];
                        index = (x & 1) ? value & 0x0f : value >> 4;
                    } else {
                        index = (row[x / 8] >> (7 - (x & 7))) & 1;
                    }
                    if (index >= paletteCount) {
                        rgba.clear();
                        break;
                    }
                    const std::uint8_t* color = info + paletteOffset + index * 4;
                    blue = color[0];
                    green = color[1];
                    red = color[2];
                } else if (bitCount == 24) {
                    blue = row[3 * x + 0];
                    green = row[3 * x + 1];
                    red = row[3 * x + 2];
                } else {
                    std::uint32_t value = 0;
                    if (bitCount == 16) {
                        std::uint16_t word = 0;
                        std::memcpy(&word, row + 2 * x, sizeof(word));
                        value = word;
                    } else {
                        std::memcpy(&value, row + 4 * x, sizeof(value));
                    }
                    if (header.compression == SUSIE_BI_RGB && bitCount == 32) {
                        blue = static_cast<std::uint8_t>(value);
                        green = static_cast<std::uint8_t>(value >> 8);
                        red = static_cast<std::uint8_t>(value >> 16);
                    } else {
                        red = component(value, redMask);
                        green = component(value, greenMask);
                        blue = component(value, blueMask);
                        if (alphaMask) {
                            alpha = component(value, alphaMask);
                        }
                    }
                }

                const std::size_t destination =
                    (static_cast<std::size_t>(y) * imageWidth + x) * 4;
                rgba[destination + 0] = red;
                rgba[destination + 1] = green;
                rgba[destination + 2] = blue;
                rgba[destination + 3] = alpha;
            }
            if (rgba.empty()) break;
        }

        if (rgba.empty()) break;

        width = imageWidth;
        height = imageHeight;
        success = true;
    } while (false);

    return success;
}