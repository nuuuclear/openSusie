#pragma once

#include <cstddef>
#include <cstdint>

// Winapi
#if defined(_WIN32) || defined(_WIN64)
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
    #define SUSIE_EXPORT extern "C" __declspec(dllexport)
    #define SUSIE_CALL WINAPI
#else
    #include <cstdlib>
    #include <cstddef>

    using BYTE = std::uint8_t;
    using WORD = std::uint16_t;
    using DWORD = std::uint32_t;
    using DWORD_PTR = std::uintptr_t;
    using HANDLE = void*;
    using HLOCAL = void*;
    using LPSTR = char*;
    using LPCSTR = const char*;
    using LPWSTR = wchar_t*;
    using LPCWSTR = const wchar_t*;

    #ifndef WINAPI
        #define WINAPI
    #endif
    #ifndef CALLBACK
        #define CALLBACK
    #endif

    #define LMEM_MOVEABLE 0x0002
    #define SUSIE_EXPORT extern "C" __attribute__((visibility("default")))
    #define SUSIE_CALL

    namespace susie_detail {
    struct LocalBlock {
        std::size_t size;
        alignas(std::max_align_t) unsigned char data[1];
    };
    }

    inline HLOCAL LocalAlloc(unsigned int, std::size_t size) {
        if (size > static_cast<std::size_t>(-1) - offsetof(susie_detail::LocalBlock, data)) {
            return nullptr;
        }

        auto* block = static_cast<susie_detail::LocalBlock*>(
            std::malloc(offsetof(susie_detail::LocalBlock, data) + size)
        );
        if (!block) return nullptr;

        block->size = size;
        return block->data;
    }

    inline void* LocalLock(HLOCAL handle) {
        return handle;
    }

    inline int LocalUnlock(HLOCAL) {
        return 1;
    }

    inline std::size_t LocalSize(HLOCAL handle) {
        if (!handle) return 0;
        const auto* data = static_cast<const unsigned char*>(handle);
        const auto* block = reinterpret_cast<const susie_detail::LocalBlock*>(
            data - offsetof(susie_detail::LocalBlock, data)
        );
        return block->size;
    }

    inline HLOCAL LocalFree(HLOCAL handle) {
        if (handle) {
            auto* data = static_cast<unsigned char*>(handle);
            std::free(data - offsetof(susie_detail::LocalBlock, data));
        }
        return nullptr;
    }
#endif

#define susiedef inline constexpr

// Error
susiedef int SPI_NO_FUNCTION = -1;
susiedef int SPI_ALL_RIGHT = 0;
susiedef int SPI_ABORT = 1;
susiedef int SPI_NOT_SUPPORT = 2;
susiedef int SPI_OUT_OF_ORDER = 3;
susiedef int SPI_NO_MEMORY = 4;
susiedef int SPI_MEMORY_ERROR = 5;
susiedef int SPI_FILE_READ_ERROR = 6;
susiedef int SPI_WINDOW_ERROR = 7;
susiedef int SPI_OTHER_ERROR = 8;
susiedef int SPI_FILE_WRITE_ERROR = 9;
susiedef int SPI_END_OF_FILE = 10;

// Flags
susiedef unsigned int SPI_INPUT_FILE = 0;
susiedef unsigned int SPI_INPUT_MEMORY = 1;
susiedef unsigned int SPI_IGNORE_CASE = 1u << 7;
susiedef unsigned int SPI_OUTPUT_FILE = 0;
susiedef unsigned int SPI_OUTPUT_MEMORY = 1u << 8;

// Bit format
susiedef std::uint32_t SUSIE_BI_RGB = 0;
susiedef std::uint32_t SUSIE_BI_RLE8 = 1;
susiedef std::uint32_t SUSIE_BI_RLE4 = 2;
susiedef std::uint32_t SUSIE_BI_BITFIELDS = 3;
susiedef std::uint32_t SUSIE_BI_JPEG = 4;
susiedef std::uint32_t SUSIE_BI_PNG = 5;
susiedef std::uint32_t SUSIE_BI_ALPHABITFIELDS = 6;

// Plugin type
susiedef const char* SUSIE_INPUT_PLUGIN_TYPE = "00IN";
susiedef const char* SUSIE_ARCHIVE_PLUGIN_TYPE = "00AM";
susiedef const char* SUSIE_CREATE_PICTURE_PLUGIN_TYPE = "T0XN";

susiedef std::uint32_t SUSIE_ANIMATION_API_VERSION = 1;

#undef susiedef

using SusieProgressCallback = int (CALLBACK*)(int, int, std::intptr_t);

class SusieLocalLock {
public:
    explicit SusieLocalLock(HLOCAL handle) : handle_(handle), data_(handle ? LocalLock(handle) : nullptr) {}
    ~SusieLocalLock() {
        if (data_) LocalUnlock(handle_);
    }

    SusieLocalLock(const SusieLocalLock&) = delete;
    SusieLocalLock& operator=(const SusieLocalLock&) = delete;

    void* get() const {
        return data_;
    }

private:
    HLOCAL handle_;
    void* data_;
};

class SusieLocalMemory {
public:
    explicit SusieLocalMemory(HLOCAL handle) : handle_(handle) {}
    ~SusieLocalMemory() {
        if (handle_) LocalFree(handle_);
    }

    SusieLocalMemory(const SusieLocalMemory&) = delete;
    SusieLocalMemory& operator=(const SusieLocalMemory&) = delete;

    HLOCAL get() const {
        return handle_;
    }

private:
    HLOCAL handle_;
};

#pragma pack(push, 1)
struct SusiePictureInfo {
    std::int32_t left;
    std::int32_t top;
    std::int32_t width;
    std::int32_t height;
    WORD x_density;
    WORD y_density;
    std::int16_t colorDepth;
#if defined(_WIN64) || INTPTR_MAX == INT64_MAX
    std::uint8_t alignmentPadding[2];
#endif
    HLOCAL hInfo;
};
#pragma pack(pop)

struct SusieBitmapInfoHeader {
    std::uint32_t size;
    std::int32_t width;
    std::int32_t height;
    WORD planes;
    WORD bitCount;
    std::uint32_t compression;
    std::uint32_t sizeImage;
    std::int32_t xPelsPerMeter;
    std::int32_t yPelsPerMeter;
    std::uint32_t colorsUsed;
    std::uint32_t colorsImportant;
};
static_assert(sizeof(SusieBitmapInfoHeader) == 40);

#pragma pack(push, 1)
struct SusieFileInfo {
    std::uint8_t method[8];
    std::uintptr_t position;
    std::uintptr_t compressedSize;
    std::uintptr_t fileSize;
    std::uintptr_t timestamp;
    char path[200];
    char filename[200];
    std::uint32_t crc;
#if defined(_WIN64)
    char padding[4];
#endif
};
#pragma pack(pop)

struct SusieFileInfoW {
    std::uint8_t method[8];
    std::uintptr_t position;
    std::uintptr_t compressedSize;
    std::uintptr_t fileSize;
    std::uintptr_t timestamp;
    wchar_t path[200];
    wchar_t filename[200];
    std::uint32_t crc;
};

using SusieGetPluginInfoFunc = int (WINAPI*)(int, LPSTR, int);
using SusieGetPluginInfoWFunc = int (WINAPI*)(int, LPWSTR, int);
using SusieIsSupportedFunc = int (WINAPI*)(LPCSTR, const void*);
using SusieIsSupportedWFunc = int (WINAPI*)(LPCWSTR, const void*);

using SusieGetArchiveInfoFunc = int (WINAPI*)(LPCSTR, std::size_t, unsigned int, HLOCAL*);
using SusieGetArchiveInfoWFunc = int (WINAPI*)(LPCWSTR, std::size_t, unsigned int, HLOCAL*);

using SusieGetFileFunc = int (WINAPI*)(LPCSTR, std::size_t, LPSTR, unsigned int, SusieProgressCallback, std::intptr_t);
using SusieGetFileWFunc = int (WINAPI*)(LPCWSTR, std::size_t, LPWSTR, unsigned int, SusieProgressCallback, std::intptr_t);

// Picture Get
using SusieGetPictureInfoFunc = int (WINAPI*)(LPCSTR, std::size_t, unsigned int, SusiePictureInfo*);
using SusieGetPictureInfoWFunc = int (WINAPI*)(LPCWSTR, std::size_t, unsigned int, SusiePictureInfo*);
using SusieGetPictureFunc = int (WINAPI*)(LPCSTR, std::size_t, unsigned int, HANDLE*, HANDLE*, SusieProgressCallback, std::intptr_t);
using SusieGetPictureWFunc = int (WINAPI*)(LPCWSTR, std::size_t, unsigned int, HANDLE*, HANDLE*, SusieProgressCallback, std::intptr_t);

// Picture Create
using SusieCreatePictureFunc = int (WINAPI*)(LPCSTR, unsigned int, void**, void**, SusiePictureInfo*, SusieProgressCallback, std::intptr_t);
using SusieCreatePictureWFunc = int (WINAPI*)(LPCWSTR, unsigned int, void**, void**, SusiePictureInfo*, SusieProgressCallback, std::intptr_t);

// Animation
using SusieGetAnimationApiVersionFunc = std::uint32_t (WINAPI*)();
using SusieOpenAnimationFunc = int (WINAPI*)(LPCSTR, DWORD_PTR, HANDLE*, std::uint32_t*, std::uint32_t*);
using SusieOpenAnimationWFunc = int (WINAPI*)(LPCWSTR, DWORD_PTR, HANDLE*, std::uint32_t*, std::uint32_t*);
using SusieGetAnimationFrameFunc = int (WINAPI*)(HANDLE, std::uint32_t, HANDLE*, HANDLE*, std::uint32_t*);
using SusieCloseAnimationFunc = void (WINAPI*)(HANDLE);

static_assert(sizeof(SusiePictureInfo) == (sizeof(void*) == 8 ? 32 : 26));