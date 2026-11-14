#include "SusieExport.h"

#include "external/tga/tga.h"

#include <cstdio>
#include <cstring>
#include <cwchar>
#include <new>
#include <string>
#include <vector>

namespace {

constexpr std::uint64_t kMaxPixels = 1ull << 28;

const char* const kPluginInfo[] = {
    "00IN",
    "TGA Images",
    "*.tga;*.icb;*.vda;*.vst;*.targa",
    "Truevision TGA (*.tga)",
};

const std::vector<std::string> kExtensions = {
    ".tga",
    ".icb",
    ".vda",
    ".vst",
    ".targa"
};

class MemoryReader : public tga::FileInterface {
public:
    MemoryReader(const std::uint8_t* data, std::size_t size) : data_(data), size_(size) {}

    bool ok() const override {
        return ok_;
    }

    std::size_t tell() override {
        return pos_;
    }

    void seek(std::size_t absPos) override {
        pos_ = absPos;
    }

    std::uint8_t read8() override {
        if (pos_ >= size_) {
            ok_ = false; return 0;
        }

        return data_[pos_++];
    }

    void write8(std::uint8_t) override {
        ok_ = false;
    }

private:
    const std::uint8_t* data_;
    std::size_t size_;
    std::size_t pos_ = 0;
    bool ok_ = true;
};

class MemoryWriter : public tga::FileInterface {
public:
    explicit MemoryWriter(std::vector<std::uint8_t>& out) : out_(out) {}
    bool ok() const override {
        return ok_;
    }

    std::size_t tell() override {
        return pos_;
    }

    void seek(std::size_t absPos) override {
        if (absPos > out_.size()) out_.resize(absPos);
        pos_ = absPos;
    }
    
    std::uint8_t read8() override {
        ok_ = false;
        return 0;
    }

    void write8(std::uint8_t value) override {
        if (pos_ >= out_.size()) out_.resize(pos_ + 1);
        out_[pos_++] = value;
    }

private:
    std::vector<std::uint8_t>& out_;
    std::size_t pos_ = 0;
    bool ok_ = true;
};

struct Span {
    const std::uint8_t* data = nullptr;
    std::size_t size = 0;
};

FILE* OpenFileA(const char* path, const char* mode) {
    return std::fopen(path, mode);
}

FILE* OpenFileW(const wchar_t* path, const char* mode) {
#if defined(_WIN32) || defined(_WIN64)
    const std::wstring wideMode(mode, mode + std::strlen(mode));
    return _wfopen(path, wideMode.c_str());
#else
    const std::size_t n = std::wcstombs(nullptr, path, 0);
    if (n == static_cast<std::size_t>(-1)) {
        return nullptr;
    }

    std::string narrow(n + 1, '\0');
    std::wcstombs(&narrow[0], path, n + 1);

    return std::fopen(narrow.c_str(), mode);
#endif
}

bool ReadFileFrom(FILE* file, std::size_t offset, std::vector<std::uint8_t>& out) {
    if (!file) return false;

    bool ok = std::fseek(file, 0, SEEK_END) == 0;
    const long end = ok ? std::ftell(file) : -1;
    if (!ok || end < 0 || static_cast<std::size_t>(end) <= offset) {
        std::fclose(file);
        return false;
    }

    const std::size_t size = static_cast<std::size_t>(end) - offset;
    
    ok = std::fseek(file, static_cast<long>(offset), SEEK_SET) == 0;
    if (ok) {
        out.resize(size);
        ok = std::fread(out.data(), 1, size, file) == size;
    }

    std::fclose(file);
    return ok;
}

int LoadInput(
    const void* buf,
    std::size_t len,
    unsigned int flags,
    bool wide,
    std::vector<std::uint8_t>& storage,
    Span& span
) {
    if ((flags & 7u) == SPI_INPUT_MEMORY) {
        if (!buf || !len) return SPI_FILE_READ_ERROR;
        span.data = static_cast<const std::uint8_t*>(buf);
        span.size = len;
        return SPI_ALL_RIGHT;
    }

    if (!buf) return SPI_FILE_READ_ERROR;

    // tgaPullDat(buf, pts->makeAll(), len, nullptr);

    FILE* file = wide
    ?   OpenFileW(static_cast<const wchar_t*>(buf), "rb")
    :   OpenFileA(static_cast<const char*>(buf), "rb");

    if (!ReadFileFrom(file, len, storage)) {
        return SPI_FILE_READ_ERROR;
    }

    span.data = storage.data();
    span.size = storage.size();

    return SPI_ALL_RIGHT;
}

bool LooksLikeTga(const std::uint8_t* h, std::size_t size) {
    if (!h || size < 18) return false;
    const std::uint8_t colormapType = h[1];
    const std::uint8_t imageType = h[2];
    const std::uint16_t colormapLength = static_cast<std::uint16_t>(h[5] | (h[6] << 8));
    const std::uint8_t colormapDepth = h[7];
    const std::uint16_t width = static_cast<std::uint16_t>(h[12] | (h[13] << 8));
    const std::uint16_t height = static_cast<std::uint16_t>(h[14] | (h[15] << 8));
    const std::uint8_t bpp = h[16];
    const std::uint8_t descriptor = h[17];

    if (colormapType > 1 || !width || !height || (descriptor & 0xC0)) {
        return false;
    }

    switch (imageType) {
    case 1: case 9: // indexed
        if (colormapType != 1 || !colormapLength || bpp != 8) {
            return false;
        }

        return colormapDepth == 15
            || colormapDepth == 16
            || colormapDepth == 24
            || colormapDepth == 32;

    case 2: case 10: // truecolor
        return bpp == 15
            || bpp == 16
            || bpp == 24
            || bpp == 32;

    case 3: case 11: // grayscale
        return bpp == 8;

    default:
        return false;
    }
}

inline void Unpack(std::uint32_t c, std::uint8_t& r, std::uint8_t& g, std::uint8_t& b, std::uint8_t& a) {
    r = static_cast<std::uint8_t>(c);
    g = static_cast<std::uint8_t>(c >> 8);
    b = static_cast<std::uint8_t>(c >> 16);
    a = static_cast<std::uint8_t>(c >> 24);
}

int DecodeTga(
    Span input,
    bool headerOnly,
    std::vector<std::uint8_t>& rgba,
    int& width,
    int& height,
    SusieProgressCallback progress,
    std::intptr_t progressData
) {
    try {
        MemoryReader file(input.data, input.size);
        tga::Decoder decoder(&file);
        tga::Header header;
        if (!decoder.readHeader(header) || !header.width || !header.height) {
            return SPI_NOT_SUPPORT;
        }
        width = header.width;
        height = header.height;
        if (headerOnly) return SPI_ALL_RIGHT;

        if (static_cast<std::uint64_t>(width) * height > kMaxPixels) return SPI_NO_MEMORY;

        const unsigned int bpp = header.bytesPerPixel();
        if (bpp == 0 || bpp > 4) return SPI_NOT_SUPPORT;

        if (!SusieReportProgress(progress, 0, 100, progressData)) return SPI_ABORT;

        tga::Image image;
        image.bytesPerPixel = bpp;
        image.rowstride = static_cast<std::uint32_t>(width) * bpp;
        std::vector<std::uint8_t> buffer(static_cast<std::size_t>(image.rowstride) * height);
        image.pixels = buffer.data();


        if (!decoder.readImage(header, image, nullptr)) return SPI_FILE_READ_ERROR;
        decoder.postProcessImage(header, image); // fixes all-zero alpha channels

        if (!SusieReportProgress(progress, 50, 100, progressData)) return SPI_ABORT;

        const bool indexed = header.imageType == 1 || header.imageType == 9;
        const std::size_t pixelCount = static_cast<std::size_t>(width) * height;
        rgba.assign(pixelCount * 4, 0);

        for (int y = 0; y < height; ++y) {
            const std::uint8_t* src = buffer.data() + static_cast<std::size_t>(y) * image.rowstride;
            std::uint8_t* dst = rgba.data() + static_cast<std::size_t>(y) * width * 4;

            for (int x = 0; x < width; ++x, src += bpp, dst += 4) {
                std::uint8_t r = 0, g = 0, b = 0, a = 255;
                if (bpp == 4) {
                    std::uint32_t c;
                    std::memcpy(&c, src, 4);
                    Unpack(c, r, g, b, a);

                } else if (bpp == 1 && indexed) {
                    if (src[0] < header.colormap.size()) {
                        Unpack(header.colormap[src[0]], r, g, b, a);
                    }
                } else if (bpp == 1) {
                    r = g = b = src[0];
                } else if (bpp == 2) {
                    r = g = b = src[0];
                    a = src[1];
                } else {
                    return SPI_NOT_SUPPORT;
                }

                dst[0] = r; dst[1] = g; dst[2] = b; dst[3] = a;
            }
        }

        if (!SusieReportProgress(progress, 100, 100, progressData)) return SPI_ABORT;
        return SPI_ALL_RIGHT;

    } catch (const std::bad_alloc&) {
        return SPI_NO_MEMORY;

    } catch (...) {
        return SPI_OTHER_ERROR;
    }

}

int GetPictureInfoImpl(
    const void* buf,
    std::size_t len,
    unsigned int flags,
    bool wide,
    SusiePictureInfo* info
) {
    if (!info) return SPI_OTHER_ERROR;
    std::vector<std::uint8_t> storage;
    Span span;
    int result = LoadInput(buf, len, flags, wide, storage, span);
    if (result != SPI_ALL_RIGHT) return result;

    std::vector<std::uint8_t> unused;
    int w = 0, h = 0;
    result = DecodeTga(span, true, unused, w, h, nullptr, 0);
    if (result != SPI_ALL_RIGHT) return result;

    SusieSetPictureInfo(info, w, h);
    return SPI_ALL_RIGHT;
}

int GetPictureImpl(
    const void* buf,
    std::size_t len,
    unsigned int flags,
    bool wide,
    HANDLE* bitmapInfo,
    HANDLE* bitmapData,
    SusieProgressCallback progress,
    std::intptr_t progressData
) {
    if (!bitmapInfo || !bitmapData) return SPI_OTHER_ERROR;
    *bitmapInfo = nullptr;
    *bitmapData = nullptr;

    std::vector<std::uint8_t> storage;
    Span span;
    int result = LoadInput(buf, len, flags, wide, storage, span);
    if (result != SPI_ALL_RIGHT) return result;

    std::vector<std::uint8_t> rgba;
    int w = 0, h = 0;
    result = DecodeTga(span, false, rgba, w, h, progress, progressData);
    if (result != SPI_ALL_RIGHT) return result;

    if (!SusieWriteRgbaDib(rgba.data(), w, h, bitmapInfo, bitmapData)) {
        SusieReleasePicture(bitmapInfo, bitmapData);
        return SPI_NO_MEMORY;
    }
    return SPI_ALL_RIGHT;
}

int IsSupportedImpl(const char* filename, const void* dw) {
    if (!SusieHasFileExtension(filename, kExtensions)) return 0;
 
    std::uint8_t header[18] = {};
    std::size_t available = 0;
    const std::uintptr_t value = reinterpret_cast<std::uintptr_t>(dw);
 
    if (value > 0xFFFF) {
        std::memcpy(header, dw, sizeof(header));
        available = sizeof(header);

    } else if (value != 0) {
#if defined(_WIN32) || defined(_WIN64)
        DWORD read = 0;
        if (ReadFile(reinterpret_cast<HANDLE>(value), header, sizeof(header), &read, nullptr)) {
            available = read;
        }
#endif
    }

    return LooksLikeTga(header, available) ? 1 : 0;
}

constexpr int kPluginInfoCount = static_cast<int>(sizeof(kPluginInfo) / sizeof(kPluginInfo[0]));

int EncodeTga(const std::vector<std::uint8_t>& rgba, int width, int height, std::vector<std::uint8_t>& out) {
    if (width <= 0 || height <= 0 || width > 65535 || height > 65535) return SPI_NOT_SUPPORT;
    if (rgba.size() != static_cast<std::size_t>(width) * height * 4) return SPI_OTHER_ERROR;

    try {
        std::vector<std::uint32_t> pixels(static_cast<std::size_t>(width) * height);
        for (std::size_t i = 0; i < pixels.size(); ++i) {
            const std::uint8_t* p = &rgba[i * 4];
            pixels[i]
            =   static_cast<std::uint32_t>(p[0])
            | ( static_cast<std::uint32_t>(p[1]) << 8   )
            | ( static_cast<std::uint32_t>(p[2]) << 16  )
            | ( static_cast<std::uint32_t>(p[3]) << 24  );
        }

        tga::Header header;
        header.idLength = 0;
        header.colormapType = 0;
        header.imageType = tga::UncompressedRgb;
        header.colormapOrigin = 0;
        header.colormapLength = 0;
        header.colormapDepth = 0;
        header.xOrigin = 0;
        header.yOrigin = 0;
        header.width = static_cast<std::uint16_t>(width);
        header.height = static_cast<std::uint16_t>(height);
        header.bitsPerPixel = 32;
        header.imageDescriptor = 0x28; // 8 alpha bits, top-to-bottom

        tga::Image image;
        image.pixels = reinterpret_cast<std::uint8_t*>(pixels.data());
        image.bytesPerPixel = 4;
        image.rowstride = static_cast<std::uint32_t>(width) * 4;

        MemoryWriter file(out);
        tga::Encoder encoder(&file);
        encoder.writeHeader(header);
        encoder.writeImage(header, image);
        encoder.writeFooter();
        return file.ok() ? SPI_ALL_RIGHT : SPI_FILE_WRITE_ERROR;

    } catch (const std::bad_alloc&) {
        return SPI_NO_MEMORY;

    } catch (...) {
        return SPI_OTHER_ERROR;
    }


}

int WriteBytes(FILE* file, const std::vector<std::uint8_t>& bytes) {
    if (!file) return SPI_FILE_WRITE_ERROR;
    const bool ok = std::fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size();
    const bool closed = std::fclose(file) == 0;
    return ok && closed ? SPI_ALL_RIGHT : SPI_FILE_WRITE_ERROR;
}

int TgaEncodeHandler(
    const char* path,
    unsigned int flags,
    const std::vector<std::uint8_t>& rgba,
    int width,
    int height,
    SusieProgressCallback progress,
    std::intptr_t data
) {
    if (flags & SPI_OUTPUT_MEMORY) return SPI_NOT_SUPPORT;
    if (!SusieReportProgress(progress, 0, 100, data)) return SPI_ABORT;
    std::vector<std::uint8_t> out;
    int result = EncodeTga(rgba, width, height, out);
    if (result != SPI_ALL_RIGHT) return result;
    result = WriteBytes(OpenFileA(path, "wb"), out);
    if (result == SPI_ALL_RIGHT && !SusieReportProgress(progress, 100, 100, data)) return SPI_ABORT;
    return result;
}

int TgaEncodeHandlerW(
    const wchar_t* path,
    unsigned int flags,
    const std::vector<std::uint8_t>& rgba,
    int width,
    int height,
    SusieProgressCallback
    progress,
    std::intptr_t data
) {
    if (flags & SPI_OUTPUT_MEMORY) return SPI_NOT_SUPPORT;
    if (!SusieReportProgress(progress, 0, 100, data)) return SPI_ABORT;
    std::vector<std::uint8_t> out;

    int result = EncodeTga(rgba, width, height, out);
    if (result != SPI_ALL_RIGHT) return result;

    result = WriteBytes(OpenFileW(path, "wb"), out);
    if (result == SPI_ALL_RIGHT && !SusieReportProgress(progress, 100, 100, data)) return SPI_ABORT;

    return result;
}

}  // namespace

SUSIE_EXPORT int SUSIE_CALL GetPluginInfo(int infono, LPSTR buf, int buflen) {
    return SusieWritePluginInfo(infono, buf, buflen, kPluginInfo, kPluginInfoCount);
}

SUSIE_EXPORT int SUSIE_CALL GetPluginInfoW(int infono, LPWSTR buf, int buflen) {
    if (!buf || buflen <= 0 || infono < 0 || infono >= kPluginInfoCount)
        return 0;
    const char *text = kPluginInfo[infono];
    int i = 0;
    for (; text[i] && i < buflen - 1; ++i)
        buf[i] = static_cast<wchar_t>(static_cast<unsigned char>(text[i]));
    buf[i] = L'\0';

    return i;
}

SUSIE_EXPORT int SUSIE_CALL IsSupported(LPCSTR filename, const void *dw)
{
    return IsSupportedImpl(filename, dw);
}

SUSIE_EXPORT int SUSIE_CALL IsSupportedW(LPCWSTR filename, const void *dw) {
    if (!filename) return 0;

    std::string narrow;
    for (const wchar_t *c = filename; *c; ++c) {
        narrow.push_back(*c < 0x80 ? static_cast<char>(*c) : '?');
    }

    return IsSupportedImpl(narrow.c_str(), dw);
}

SUSIE_EXPORT int SUSIE_CALL GetPictureInfo(
    LPCSTR buf, std::size_t len,
    unsigned int flag,
    SusiePictureInfo *lpInfo
) {
    return GetPictureInfoImpl(buf, len, flag, false, lpInfo);
}

SUSIE_EXPORT int SUSIE_CALL GetPictureInfoW(
    LPCWSTR buf, std::size_t len,
    unsigned int flag,
    SusiePictureInfo *lpInfo
) {
    return GetPictureInfoImpl(buf, len, flag, true, lpInfo);
}

SUSIE_EXPORT int SUSIE_CALL GetPicture(
    LPCSTR buf, std::size_t len,
    unsigned int flag,
    HANDLE *pHBInfo,
    HANDLE *pHBm,
    SusieProgressCallback lpPrgressCallback,
    std::intptr_t lData
) {
    return GetPictureImpl(buf, len, flag, false, pHBInfo, pHBm, lpPrgressCallback, lData);
}

SUSIE_EXPORT int SUSIE_CALL GetPictureW(
    LPCWSTR buf, std::size_t len,
    unsigned int flag,
    HANDLE *pHBInfo,
    HANDLE *pHBm,
    SusieProgressCallback lpPrgressCallback,
    std::intptr_t lData
) {
    return GetPictureImpl(buf, len, flag, true, pHBInfo, pHBm, lpPrgressCallback, lData);
}

SUSIE_EXPORT int SUSIE_CALL CreatePicture(
    LPCSTR filepath,
    unsigned int flag,
    void **pHBInfo,
    void **pHBm, SusiePictureInfo *lpInfo,
    SusieProgressCallback lpPrgressCallback,
    std::intptr_t lData
) {
    return SusieCreatePictureFromDib(
        filepath, flag, pHBInfo, pHBm, lpInfo,
        lpPrgressCallback, lData, TgaEncodeHandler
    );
}

SUSIE_EXPORT int SUSIE_CALL CreatePictureW(
    LPCWSTR filepath,
    unsigned int flag,
    void **pHBInfo, void **pHBm,
    SusiePictureInfo *lpInfo,
    SusieProgressCallback lpPrgressCallback,
    std::intptr_t lData
) {
    return SusieCreatePictureFromDibW(
        filepath, flag, pHBInfo, pHBm, lpInfo,
        lpPrgressCallback, lData, TgaEncodeHandlerW
    );
}