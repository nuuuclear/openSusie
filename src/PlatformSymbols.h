#pragma once

#if defined(_WIN32) || defined(_WIN64)
    #define PLATFORM_WINDOWS

    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>

    using PluginHandle = HMODULE;

#else
    #define PLATFORM_POSIX

    #include <dlfcn.h>

    using PluginHandle = void*;
#endif

#include <iostream>
#include <cstdint>
#include <string>

inline PluginHandle LoadPluginLibrary(
    const std::wstring& path,
    std::string* errorMessage = nullptr,
    std::uint32_t* loadErrorCode = nullptr
) {
#if defined(PLATFORM_WINDOWS)
    HMODULE handle = LoadLibraryExW(
        path.c_str(),
        nullptr,
        LOAD_WITH_ALTERED_SEARCH_PATH
    );

    if (!handle) {
        DWORD error = GetLastError();
        if (loadErrorCode) *loadErrorCode = error;
        if (errorMessage) {
            *errorMessage = "Windows error " + std::to_string(error);
            wchar_t* systemMessage = nullptr;
            const DWORD messageLength = FormatMessageW(
                FORMAT_MESSAGE_ALLOCATE_BUFFER
                    | FORMAT_MESSAGE_FROM_SYSTEM
                    | FORMAT_MESSAGE_IGNORE_INSERTS,
                nullptr,
                error,
                MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                reinterpret_cast<wchar_t*>(&systemMessage),
                0,
                nullptr
            );
            if (messageLength > 0 && systemMessage) {
                const int utf8Length = WideCharToMultiByte(
                    CP_UTF8,
                    0,
                    systemMessage,
                    static_cast<int>(messageLength),
                    nullptr,
                    0,
                    nullptr,
                    nullptr
                );
                if (utf8Length > 0) {
                    std::string description(static_cast<std::size_t>(utf8Length), '\0');
                    WideCharToMultiByte(
                        CP_UTF8,
                        0,
                        systemMessage,
                        static_cast<int>(messageLength),
                        description.data(),
                        utf8Length,
                        nullptr,
                        nullptr
                    );
                    while (!description.empty()
                    && (description.back() == '\r' || description.back() == '\n')
                    ) {
                        description.pop_back();
                    }
                    *errorMessage += ": " + description;
                }
                
                LocalFree(systemMessage);
            }
        }

    }

    return handle;

#else
    std::string stringPath(
        path.begin(),
        path.end()
    );

    PluginHandle handle =
        dlopen(
            stringPath.c_str(),
            RTLD_LAZY
        );

    if (!handle) {
        const char* error = dlerror();
        if (errorMessage) {
            *errorMessage = error ? error : "Unknown dynamic loader error";
        }

    }

    return handle;

#endif
}

inline void* GetOptionalPluginSymbol(PluginHandle handle, const std::string& symbol) {
    if (!handle)
        return nullptr;

#if defined(PLATFORM_WINDOWS)
    FARPROC proc = GetProcAddress(
        handle,
        symbol.c_str()
    );
    return reinterpret_cast<void*>(proc);
#else
    dlerror();

    void* proc = dlsym(
        handle,
        symbol.c_str()
    );

    return dlerror() ? nullptr : proc;
#endif
}

inline void* GetPluginSymbol(PluginHandle handle, const std::string& symbol) {
    void* proc = GetOptionalPluginSymbol(handle, symbol);
    if (proc) return proc;

#if defined(PLATFORM_WINDOWS)
    const DWORD error = GetLastError();
    std::cerr << "<Plugins> ERROR: Could not find symbol '"
              << symbol << "' (error " << error << ")\n";
#else
    std::cerr << "<Plugins> ERROR: Could not find symbol '"
              << symbol << "'\n";
#endif
    return nullptr;
}

inline void FreePluginLibrary(PluginHandle handle) {
    if (!handle)
        return;

#if defined(PLATFORM_WINDOWS)
    FreeLibrary(handle);

#else
    dlclose(handle);

#endif
}