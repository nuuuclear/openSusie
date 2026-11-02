#include "SusieImage.h"

#include <gif_lib.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>

struct GifAnimationFrame {
    std::vector<std::uint8_t> pixels;
    std::uint32_t durationMs = 100;
};

struct GifAnimationData {
    int width = 0;
    int height = 0;
    std::uint32_t loopCount = 1;
    std::vector<GifAnimationFrame> frames;
};

class GifDecoder {
public:
    SusieDecodedImage decodeImage(const std::string& filePath) {
        SusieDecodedImage result{};

        int error = 0;

        GifFileType* gif = DGifOpenFileName(
            filePath.c_str(),
            &error
        );

        if (!gif) return {};
        GifFileGuard guard{gif};

        if (DGifSlurp(gif) != GIF_OK) {
            return {};
        }

        if (gif->ImageCount <= 0) {
            return {};
        }

        const int width = gif->SWidth;
        const int height = gif->SHeight;

        if (width <= 0 || height <= 0) {
            return {};
        }

        const size_t imageWidth = static_cast<size_t>(width);
        const size_t imageHeight = static_cast<size_t>(height);
        if (imageWidth > std::numeric_limits<size_t>::max() / imageHeight / 4) {
            return {};
        }
        const size_t bufferSize = imageWidth * imageHeight * 4;

        std::vector<uint8_t> canvas(bufferSize, 0);

        // render the first frame
        renderFrame(
            gif,
            gif->SavedImages[0],
            canvas,
            transparentColorIndex(gif->SavedImages[0])
        );

        auto* pixels = new uint8_t[bufferSize];
        std::memcpy(pixels, canvas.data(), bufferSize);

        result.pixels = pixels;
        result.width = width;
        result.height = height;
        result.channels = 4;

        return result;
    }

    std::unique_ptr<GifAnimationData> decodeAnimation(const std::string& filePath) {
        int error = 0;
        GifFileType* gif = DGifOpenFileName(filePath.c_str(), &error);
        if (!gif) return nullptr;
        GifFileGuard guard{gif};

        if (DGifSlurp(gif) != GIF_OK || gif->ImageCount < 2
            || gif->ImageCount > 10000 || gif->SWidth <= 0 || gif->SHeight <= 0) {
            return nullptr;
        }

        const std::size_t width = static_cast<std::size_t>(gif->SWidth);
        const std::size_t height = static_cast<std::size_t>(gif->SHeight);
        if (width > std::numeric_limits<std::size_t>::max() / height / 4) return nullptr;
        const std::size_t frameSize = width * height * 4;
        if (frameSize > (512u * 1024u * 1024u) / static_cast<std::size_t>(gif->ImageCount)) {
            return nullptr;
        }

        auto animation = std::make_unique<GifAnimationData>();
        animation->width = gif->SWidth;
        animation->height = gif->SHeight;
        animation->loopCount = loopCount(gif);
        animation->frames.reserve(static_cast<std::size_t>(gif->ImageCount));

        std::vector<std::uint8_t> canvas(frameSize, 0);
        std::vector<std::uint8_t> restoreCanvas;
        FrameProperties previousProperties{};
        const SavedImage* previousFrame = nullptr;

        for (int i = 0; i < gif->ImageCount; ++i) {
            const SavedImage& frame = gif->SavedImages[i];
            if (previousFrame) {
                if (previousProperties.disposal == 2) {
                    clearFrameArea(
                        canvas,
                        gif->SWidth,
                        gif->SHeight,
                        *previousFrame,
                        backgroundColor(gif, previousProperties.transparentColorIndex)
                    );
                } else if (previousProperties.disposal == 3 && !restoreCanvas.empty()) {
                    canvas = restoreCanvas;
                }
            }

            const FrameProperties properties = frameProperties(frame);
            if (i == 0) {
                const auto color = backgroundColor(gif, properties.transparentColorIndex);
                for (std::size_t offset = 0; offset < canvas.size(); offset += 4) {
                    std::copy(color.begin(), color.end(), canvas.begin() + offset);
                }
            }
            if (properties.disposal == 3) restoreCanvas = canvas;
            renderFrame(gif, frame, canvas, properties.transparentColorIndex);

            GifAnimationFrame output;
            output.pixels = canvas;
            output.durationMs = properties.delayCentiseconds == 0
                ? 100u
                : static_cast<std::uint32_t>(properties.delayCentiseconds) * 10u;
            animation->frames.push_back(std::move(output));
            previousProperties = properties;
            previousFrame = &frame;
        }
        return animation;
    }

    void freeImageData(SusieDecodedImage& data) {
        delete[] data.pixels;

        data = {};
    }

private:
    struct FrameProperties {
        int disposal = 0;
        int delayCentiseconds = 0;
        int transparentColorIndex = -1;
    };

    struct GifFileGuard {
        GifFileType* file;

        ~GifFileGuard() {
            if (file) {
                int error = 0;
                DGifCloseFile(file, &error);
            }
        }
    };

    int transparentColorIndex(const SavedImage& frame) const {
        for (int i = 0; i < frame.ExtensionBlockCount; ++i) {
            const ExtensionBlock& block = frame.ExtensionBlocks[i];
            if (block.Function != GRAPHICS_EXT_FUNC_CODE
                || block.ByteCount < 4
                || !block.Bytes) {
                continue;
            }

            if ((static_cast<std::uint8_t>(block.Bytes[0]) & 0x01) != 0) {
                return static_cast<std::uint8_t>(block.Bytes[3]);
            }
            return -1;
        }
        return -1;
    }

    FrameProperties frameProperties(const SavedImage& frame) const {
        FrameProperties properties;
        properties.transparentColorIndex = transparentColorIndex(frame);
        for (int i = 0; i < frame.ExtensionBlockCount; ++i) {
            const ExtensionBlock& block = frame.ExtensionBlocks[i];
            if (block.Function != GRAPHICS_EXT_FUNC_CODE || block.ByteCount < 4
                || !block.Bytes) {
                continue;
            }

            const std::uint8_t packed = static_cast<std::uint8_t>(block.Bytes[0]);
            properties.disposal = (packed >> 2) & 0x07;
            properties.delayCentiseconds = static_cast<std::uint8_t>(block.Bytes[1])
                | (static_cast<int>(static_cast<std::uint8_t>(block.Bytes[2])) << 8);
            break;
        }
        return properties;
    }

    std::uint32_t loopCount(const GifFileType* gif) const {
        for (int frameIndex = 0; frameIndex < gif->ImageCount; ++frameIndex) {
            const SavedImage& frame = gif->SavedImages[frameIndex];
            for (int blockIndex = 0; blockIndex + 1 < frame.ExtensionBlockCount; ++blockIndex) {
                const ExtensionBlock& application = frame.ExtensionBlocks[blockIndex];
                const ExtensionBlock& data = frame.ExtensionBlocks[blockIndex + 1];
                if (application.Function != APPLICATION_EXT_FUNC_CODE
                    || application.ByteCount < 11 || !application.Bytes
                    || data.Function != CONTINUE_EXT_FUNC_CODE
                    || data.ByteCount < 3 || !data.Bytes) {
                    continue;
                }

                const bool netscape = std::memcmp(application.Bytes, "NETSCAPE2.0", 11) == 0
                    || std::memcmp(application.Bytes, "ANIMEXTS1.0", 11) == 0;
                if (!netscape || static_cast<std::uint8_t>(data.Bytes[0]) != 1) continue;

                const std::uint32_t repeats =
                    static_cast<std::uint8_t>(data.Bytes[1])
                    | (static_cast<std::uint32_t>(
                        static_cast<std::uint8_t>(data.Bytes[2])
                    ) << 8);
                return repeats == 0 ? 0u : repeats + 1u;
            }
        }
        return 1;
    }

    void clearFrameArea(
        std::vector<std::uint8_t>& canvas,
        int canvasWidth,
        int canvasHeight,
        const SavedImage& frame,
        const std::array<std::uint8_t, 4>& color
    ) const {
        const GifImageDesc& desc = frame.ImageDesc;
        const int left = std::max(0, static_cast<int>(desc.Left));
        const int top = std::max(0, static_cast<int>(desc.Top));
        const int right = std::min(canvasWidth, static_cast<int>(desc.Left + desc.Width));
        const int bottom = std::min(canvasHeight, static_cast<int>(desc.Top + desc.Height));
        for (int y = top; y < bottom; ++y) {
            const std::size_t start = (
                static_cast<std::size_t>(y) * static_cast<std::size_t>(canvasWidth)
                + static_cast<std::size_t>(left)
            ) * 4;
            for (int x = left; x < right; ++x) {
                const std::size_t offset = start + static_cast<std::size_t>(x - left) * 4;
                std::copy(color.begin(), color.end(), canvas.begin() + offset);
            }
        }
    }

    std::array<std::uint8_t, 4> backgroundColor(
        const GifFileType* gif,
        int transparentColor
    ) const {
        std::array<std::uint8_t, 4> color{};
        if (transparentColor == gif->SBackGroundColor || !gif->SColorMap
            || gif->SBackGroundColor >= gif->SColorMap->ColorCount) {
            return color;
        }

        const GifColorType& background = gif->SColorMap->Colors[gif->SBackGroundColor];
        color = {background.Red, background.Green, background.Blue, 255};
        return color;
    }

    void renderFrame(
        GifFileType* gif,
        const SavedImage& frame,
        std::vector<uint8_t>& canvas,
        int transparentColorIndex = -1
    ) {
        const int canvasWidth = gif->SWidth;
        const int canvasHeight = gif->SHeight;

        const int left = frame.ImageDesc.Left;
        const int top = frame.ImageDesc.Top;

        const int width = frame.ImageDesc.Width;
        const int height = frame.ImageDesc.Height;

        if (!frame.RasterBits 
        ||  width <= 0 
        ||  height <= 0
        ) {
            return;
        }

        const ColorMapObject* colorMap 
            = frame.ImageDesc.ColorMap
            ? frame.ImageDesc.ColorMap
            : gif->SColorMap;

        if (!colorMap 
        ||  !colorMap->Colors
        ) {
            return;
        }

        for (int y = 0; y < height; ++y) {
            int canvasY = top + y;

            if (canvasY < 0 
            ||  canvasY >= canvasHeight
            ) {
                continue;
            }

            for (int x = 0; x < width; ++x) {
                int canvasX = left + x;
                if (canvasX < 0 
                ||  canvasX >= canvasWidth
                ) {
                    continue;
                }

                size_t sourceIndex =
                    static_cast<size_t>(y) 
                *   static_cast<size_t>(width) 
                +   static_cast<size_t>(x);

                int colorIndex = static_cast<uint8_t>(
                    frame.RasterBits[sourceIndex]
                );

                if (colorIndex == transparentColorIndex) {
                    continue;
                }

                if (colorIndex < 0 
                ||  colorIndex >= colorMap->ColorCount
                ) {

                    continue;
                }

                const GifColorType& color = colorMap->Colors[colorIndex];
                size_t destinationIndex = (
                    static_cast<size_t>(canvasY) 
                *   static_cast<size_t>(canvasWidth) 
                +   static_cast<size_t>(canvasX)
                ) * 4;

                canvas[destinationIndex + 0] = color.Red;
                canvas[destinationIndex + 1] = color.Green;
                canvas[destinationIndex + 2] = color.Blue;
                canvas[destinationIndex + 3] = 255; // alpha
            }
        }
    }
};


namespace {
const char* const PluginInfo[] = {
    "00IN",
    "GIF image decoder",
    "*.gif",
    "GIF images"
};

const std::vector<std::string> Extensions = {".gif"};

int checkInput(const char* buffer, unsigned int flags) {
    if (!buffer || (flags & SPI_INPUT_MEMORY)) return SPI_NOT_SUPPORT;
    return SPI_ALL_RIGHT;
}
}

SUSIE_EXPORT std::uint32_t WINAPI OpenSusieGetAnimationApiVersion() {
    return SUSIE_ANIMATION_API_VERSION;
}

SUSIE_EXPORT int WINAPI OpenSusieOpenAnimation(
    LPCSTR buffer,
    DWORD_PTR,
    HANDLE* animationHandle,
    std::uint32_t* frameCount,
    std::uint32_t* loopCount
) {
    if (!animationHandle || !frameCount || !loopCount) return SPI_OTHER_ERROR;
    *animationHandle = nullptr;
    *frameCount = 0;
    *loopCount = 1;
    if (!buffer) return SPI_NOT_SUPPORT;

    try {
        GifDecoder decoder;
        std::unique_ptr<GifAnimationData> animation = decoder.decodeAnimation(buffer);
        if (!animation) return SPI_NOT_SUPPORT;

        *frameCount = static_cast<std::uint32_t>(animation->frames.size());
        *loopCount = animation->loopCount;
        *animationHandle = animation.release();
        return SPI_ALL_RIGHT;
    } catch (const std::bad_alloc&) {
        return SPI_NO_MEMORY;
    } catch (const std::length_error&) {
        return SPI_NO_MEMORY;
    }
}

SUSIE_EXPORT int WINAPI OpenSusieGetAnimationFrame(
    HANDLE animationHandle,
    std::uint32_t frameIndex,
    HANDLE* bitmapInfo,
    HANDLE* bitmapData,
    std::uint32_t* durationMs
) {
    if (!bitmapInfo || !bitmapData || !durationMs) return SPI_OTHER_ERROR;
    *bitmapInfo = nullptr;
    *bitmapData = nullptr;
    *durationMs = 0;

    auto* animation = static_cast<GifAnimationData*>(animationHandle);
    if (!animation || frameIndex >= animation->frames.size()) return SPI_NOT_SUPPORT;

    const GifAnimationFrame& frame = animation->frames[frameIndex];
    if (!SusieWriteRgbaDib(
            frame.pixels.data(),
            animation->width,
            animation->height,
            bitmapInfo,
            bitmapData
        )) {
        return SPI_NO_MEMORY;
    }
    *durationMs = frame.durationMs;
    return SPI_ALL_RIGHT;
}

SUSIE_EXPORT void WINAPI OpenSusieCloseAnimation(HANDLE animationHandle) {
    delete static_cast<GifAnimationData*>(animationHandle);
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

    GifDecoder decoder;
    SusieDecodedImage image{};
    try {
        image = decoder.decodeImage(buffer);
    } catch (const std::bad_alloc&) {
        return SPI_NO_MEMORY;
    } catch (const std::length_error&) {
        return SPI_NO_MEMORY;
    }
    if (!image.pixels) return SPI_NOT_SUPPORT;

    SusieSetPictureInfo(pictureInfo, image.width, image.height);
    decoder.freeImageData(image);
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

    GifDecoder decoder;
    SusieDecodedImage image{};
    try {
        image = decoder.decodeImage(buffer);
    } catch (const std::bad_alloc&) {
        return SPI_NO_MEMORY;
    } catch (const std::length_error&) {
        return SPI_NO_MEMORY;
    }
    if (!image.pixels) return SPI_NOT_SUPPORT;

    const bool success = SusieWriteRgbaDib(
        image.pixels, image.width, image.height, bitmapInfo, bitmapData
    );
    decoder.freeImageData(image);

    if (!success) return SPI_NO_MEMORY;
    if (progress && progress(1, 1, userData) != 0) {
        SusieReleasePicture(bitmapInfo, bitmapData);
        return SPI_ABORT;
    }
    return SPI_ALL_RIGHT;
}