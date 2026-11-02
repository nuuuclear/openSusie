#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <map>
#include <new>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "PlatformSymbols.h"
#include "PluginHostProtocol.h"
#include "SusieImage.h"
#include "SusiePlugin.h"

#if defined(_WIN64)
    #include "PluginHostClient.h"
#endif

namespace fs = std::filesystem;

struct LoadedPlugin {
    PluginHandle handle = nullptr;
    std::string path;
    std::string name;
    std::string description;
    std::string extensions;
    bool hostedBy32BitProcess = false;

    SusieGetPluginInfoFunc getPluginInfo = nullptr;
    SusieGetPluginInfoWFunc getPluginInfoW = nullptr;
    SusieIsSupportedFunc isSupported = nullptr;
    SusieIsSupportedWFunc isSupportedW = nullptr;
    SusieGetPictureInfoFunc getPictureInfo = nullptr;
    SusieGetPictureInfoWFunc getPictureInfoW = nullptr;
    SusieGetPictureFunc getPicture = nullptr;
    SusieGetPictureWFunc getPictureW = nullptr;
    SusieGetAnimationApiVersionFunc getAnimationApiVersion = nullptr;
    SusieOpenAnimationFunc openAnimation = nullptr;
    SusieOpenAnimationWFunc openAnimationW = nullptr;
    SusieGetAnimationFrameFunc getAnimationFrame = nullptr;
    SusieCloseAnimationFunc closeAnimation = nullptr;
};

struct DecodedImage {
    std::vector<std::uint8_t> pixels;
    int width = 0;
    int height = 0;
    std::string metadata;
};

struct PluginAnimation {
    LoadedPlugin* plugin = nullptr;
    HANDLE handle = nullptr;
    std::uint32_t frameCount = 0;
    std::uint32_t loopCount = 1;
};

class PluginManager {
public:
    PluginManager() = default;

    ~PluginManager() {
        unloadPlugins();
    }

    PluginManager(const PluginManager&) = delete;
    PluginManager& operator=(const PluginManager&) = delete;

    void loadPluginsFromFolder(const std::string& folderPath) {
        pluginLoadErrors.clear();
        if (!fs::exists(folderPath)) {
            recordPluginLoadError(
                folderPath,
                "Plugin folder does not exist"
            );
            return;
        }

        if (!fs::is_directory(folderPath)) {
            recordPluginLoadError(
                folderPath,
                "Plugin path is not a directory"
            );
            return;
        }

        std::vector<fs::path> paths;
        for (const auto& entry : fs::directory_iterator(folderPath)) {
            if (entry.is_regular_file() && isSusiePluginFile(entry.path())) {
                paths.push_back(entry.path());
            }
        }
        std::sort(paths.begin(), paths.end());

        for (const fs::path& path : paths) {
            std::string reason;
            std::uint32_t loadErrorCode = 0;
            if (!loadPlugin(path, reason, &loadErrorCode)) {
#if defined(_WIN64)
                if (loadErrorCode == ERROR_BAD_EXE_FORMAT
                    && loadPluginIn32BitHost(path, reason)) {
                    continue;
                }
#endif
                recordPluginLoadError(path.filename().string(), reason);
            }
        }

        std::cout << "<Plugins> " << plugins.size() << " Susie plugin(s) loaded\n";
    }

    bool loadPluginFile(const fs::path& path, std::string& reason) {
        return loadPlugin(path, reason);
    }

    bool decodeImage(const std::string& filePath, DecodedImage& image) {
        image = {};
        bool supportedByPlugin = false;
        for (auto& plugin : plugins) {
            if (!supportsFile(plugin, filePath)) continue;
            supportedByPlugin = true;
            if (decodeImage(&plugin, filePath, image)) return true;
        }

        if (!supportedByPlugin) {
            std::cerr << "<Plugins> ERROR: No Susie plugin supports file: "
                      << filePath << "\n";
        }
        return false;
    }

    bool decodeImage(
        const std::string& filePath,
        DecodedImage& image,
        LoadedPlugin* selectedPlugin
    ) {
        image = {};
        if (!selectedPlugin || !supportsFile(*selectedPlugin, filePath)) return false;
        return decodeImage(selectedPlugin, filePath, image);
    }

    std::vector<LoadedPlugin*> getSupportingPlugins(const std::string& filePath) {
        std::vector<LoadedPlugin*> result;
        for (auto& plugin : plugins) {
            if (supportsFile(plugin, filePath)) result.push_back(&plugin);
        }
        return result;
    }

    std::map<std::string, std::vector<LoadedPlugin*>> getDeclaredPluginsByExtension() {
        std::map<std::string, std::vector<LoadedPlugin*>> result;
        for (auto& plugin : plugins) {
            std::string token;
            auto addExtension = [&](std::string value) {
                value.erase(
                    std::remove_if(value.begin(), value.end(), [](unsigned char ch) {
                        return std::isspace(ch) || ch == '*' || ch == '.';
                    }),
                    value.end()
                );
                std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
                    return static_cast<char>(std::tolower(ch));
                });
                if (value.empty()) return;

                auto& extensions = result[value];
                if (std::find(extensions.begin(), extensions.end(), &plugin)
                    == extensions.end()) {
                    extensions.push_back(&plugin);
                }
            };

            for (char ch : plugin.extensions) {
                if (ch == ';' || ch == ',' || std::isspace(static_cast<unsigned char>(ch))) {
                    addExtension(std::move(token));
                    token.clear();
                } else {
                    token.push_back(ch);
                }
            }
            addExtension(std::move(token));
        }
        return result;
    }

    const std::vector<std::string>& getPluginLoadErrors() const {
        return pluginLoadErrors;
    }

    bool supportsFile(const std::string& filePath) const {
        return std::any_of(plugins.begin(), plugins.end(), [&](const LoadedPlugin& plugin) {
            return supportsFile(plugin, filePath);
        });
    }

    bool openAnimation(
        const std::string& filePath,
        PluginAnimation& animation,
        LoadedPlugin* selectedPlugin = nullptr
    ) {
        closeAnimation(animation);

        std::wstring widePath;
        std::string ansiPath;
        if (!preparePaths(filePath, widePath, ansiPath)) return false;

        for (auto& plugin : plugins) {
            if (selectedPlugin && selectedPlugin != &plugin) continue;
            if (!plugin.openAnimation || !plugin.getAnimationFrame || !plugin.closeAnimation
                || !supportsFile(plugin, filePath)) {
                continue;
            }

            HANDLE handle = nullptr;
            std::uint32_t frameCount = 0;
            std::uint32_t loopCount = 1;
            int result = SPI_OTHER_ERROR;
            if (plugin.openAnimationW) {
                result = plugin.openAnimationW(
                    widePath.c_str(), 0, &handle, &frameCount, &loopCount
                );
            } else if (plugin.openAnimation && !ansiPath.empty()) {
                result = plugin.openAnimation(
                    ansiPath.c_str(), 0, &handle, &frameCount, &loopCount
                );
            }

            if (result == SPI_NOT_SUPPORT) continue;
            if (result != SPI_ALL_RIGHT
            ||  !handle
            ||  frameCount < 2
            ||  frameCount > 100000
            ) {
                if (handle) plugin.closeAnimation(handle);
                std::cerr << "<Plugins> ERROR: Could not open animation with "
                          << plugin.name << " (SPI error " << result << ")\n";
                continue;
            }

            animation.plugin = &plugin;
            animation.handle = handle;
            animation.frameCount = frameCount;
            animation.loopCount = loopCount;
            return true;
        }

        return false;
    }

    bool decodeAnimationFrame(
        const PluginAnimation& animation,
        std::uint32_t frameIndex,
        DecodedImage& image,
        std::uint32_t& durationMs
    ) {
        image = {};
        durationMs = 0;
        if (!animation.plugin
        ||  !animation.handle
        ||  frameIndex >= animation.frameCount
        ) {
            return false;
        }

        HANDLE bitmapInfo = nullptr;
        HANDLE bitmapData = nullptr;
        int result = animation.plugin->getAnimationFrame(
            animation.handle, frameIndex, &bitmapInfo, &bitmapData, &durationMs
        );
        if (result != SPI_ALL_RIGHT) {
            SusieReleasePicture(&bitmapInfo, &bitmapData);
            std::cerr << "<Plugins> ERROR: Could not decode animation frame with "
                      << animation.plugin->name << " (SPI error " << result << ")\n";
            return false;
        }

        bool converted = false;
        try {
            converted = SusieConvertDibToRgba(
                bitmapInfo, bitmapData, image.pixels, image.width, image.height
            );
        } catch (const std::bad_alloc&) {
            SusieReleasePicture(&bitmapInfo, &bitmapData);
            std::cerr << "<Plugins> ERROR: Out of memory converting animation frame from "
                      << animation.plugin->name << "\n";
            return false;
        } catch (const std::length_error&) {
            SusieReleasePicture(&bitmapInfo, &bitmapData);
            std::cerr << "<Plugins> ERROR: Animation frame dimensions exceed available memory for "
                      << animation.plugin->name << "\n";
            return false;
        }
        SusieReleasePicture(&bitmapInfo, &bitmapData);
        if (!converted) {
            image = {};
            std::cerr << "<Plugins> ERROR: Animation plugin returned an invalid DIB: "
                      << animation.plugin->name << "\n";
        }

        return converted;
    }

    void closeAnimation(PluginAnimation& animation) {
        if (animation.plugin && animation.handle && animation.plugin->closeAnimation) {
            animation.plugin->closeAnimation(animation.handle);
        }
        animation = {};
    }

    bool decodeImage(const LoadedPlugin* plugin, const std::string& filePath, DecodedImage& image) {
        image = {};
        if (!plugin) return false;

#if defined(_WIN64)
        if (plugin->hostedBy32BitProcess) {
            PluginHostReply reply;
            std::string error;
            if (!runPluginHost(
                    plugin->path,
                    PluginHostOperation::DecodeImage,
                    filePath,
                    reply,
                    error
                )) {
                std::cerr << "<Plugins> ERROR: " << error << "\n";
                return false;
            }
            if (reply.header.status != 0 || reply.header.pixelsBytes == 0) {
                const std::size_t reasonOffset
                =   reply.header.nameBytes
                +   reply.header.extensionsBytes
                +   reply.header.descriptionBytes
                +   reply.header.metadataBytes
                +   reply.header.pixelsBytes;

                const std::string reason(
                    reply.payload.data() + reasonOffset,
                    reply.header.reasonBytes
                );
                std::cerr << "<Plugins> ERROR: 32-bit plugin could not decode image: "
                          << plugin->name;
                if (!reason.empty()) std::cerr << " (" << reason << ")";
                std::cerr << "\n";
                return false;
            }

            const std::size_t metadataOffset
            =   reply.header.nameBytes
            +   reply.header.extensionsBytes
            +   reply.header.descriptionBytes;

            if (reply.header.width <= 0
            || reply.header.height <= 0
            || static_cast<std::uint64_t>(reply.header.width)
                * static_cast<std::uint64_t>(reply.header.height)
                * 4
                != reply.header.pixelsBytes
            ) {
                std::cerr << "<Plugins> ERROR: 32-bit plugin host returned invalid image dimensions\n";
                return false;
            }

            image.width = reply.header.width;
            image.height = reply.header.height;
            image.metadata.assign(
                reply.payload.data() + metadataOffset,
                reply.header.metadataBytes
            );
            const std::size_t pixelsOffset = metadataOffset + reply.header.metadataBytes;
            image.pixels.assign(
                reinterpret_cast<const std::uint8_t*>(reply.payload.data() + pixelsOffset),
                reinterpret_cast<const std::uint8_t*>(
                    reply.payload.data() + pixelsOffset + reply.header.pixelsBytes
                )
            );
            return image.width > 0 && image.height > 0;
        }
#endif

        std::wstring widePath;
        std::string ansiPath;
        if (!preparePaths(filePath, widePath, ansiPath)) {
            std::cerr << "<Plugins> ERROR: File path cannot be represented for Susie plugin: "
                      << filePath << "\n";
            return false;
        }

        SusiePictureInfo pictureInfo{};
        int infoResult = SPI_NO_FUNCTION;
        if (plugin->getPictureInfoW) {
            infoResult = plugin->getPictureInfoW(
                widePath.c_str(), 0, SPI_INPUT_FILE, &pictureInfo
            );
        } else if (plugin->getPictureInfo) {
            if (ansiPath.empty()) {
                std::cerr << "<Plugins> ERROR: File path is not representable for "
                          << plugin->name << "\n";
                return false;
            }
            infoResult = plugin->getPictureInfo(
                ansiPath.c_str(), 0, SPI_INPUT_FILE, &pictureInfo
            );
        }

        if (infoResult != SPI_ALL_RIGHT && infoResult != SPI_NO_FUNCTION) {
            freePictureInfo(pictureInfo);
            std::cerr << "<Plugins> ERROR: GetPictureInfo failed for "
                      << plugin->name << " (SPI error " << infoResult << ")\n";
            return false;
        }

        if (infoResult == SPI_ALL_RIGHT) {
            try {
                image.metadata = readPictureMetadata(pictureInfo.hInfo);
            } catch (const std::bad_alloc&) {
                freePictureInfo(pictureInfo);
                std::cerr << "<Plugins> ERROR: Out of memory reading image metadata from "
                          << plugin->name << "\n";
                return false;
            } catch (const std::length_error&) {
                freePictureInfo(pictureInfo);
                std::cerr << "<Plugins> ERROR: Image metadata is too large for "
                          << plugin->name << "\n";
                return false;
            }
        }
        freePictureInfo(pictureInfo);

        HANDLE bitmapInfo = nullptr;
        HANDLE bitmapData = nullptr;
        int pictureResult = SPI_OTHER_ERROR;
        if (plugin->getPictureW) {
            pictureResult = plugin->getPictureW(
                widePath.c_str(),
                0,
                SPI_INPUT_FILE | SPI_OUTPUT_MEMORY,
                &bitmapInfo,
                &bitmapData,
                nullptr,
                0
            );
        } else if (plugin->getPicture) {
            if (ansiPath.empty()) {
                std::cerr << "<Plugins> ERROR: File path is not representable for "
                          << plugin->name << "\n";
                return false;
            }
            pictureResult = plugin->getPicture(
                ansiPath.c_str(),
                0,
                SPI_INPUT_FILE | SPI_OUTPUT_MEMORY,
                &bitmapInfo,
                &bitmapData,
                nullptr,
                0
            );
        }

        if (pictureResult != SPI_ALL_RIGHT) {
            SusieReleasePicture(&bitmapInfo, &bitmapData);
            std::cerr << "<Plugins> ERROR: GetPicture failed for "
                      << plugin->name << " (SPI error " << pictureResult << ")\n";
            return false;
        }

        bool converted = false;
        try {
            converted = SusieConvertDibToRgba(
                bitmapInfo,
                bitmapData,
                image.pixels,
                image.width,
                image.height
            );
        } catch (const std::bad_alloc&) {
            SusieReleasePicture(&bitmapInfo, &bitmapData);
            image = {};
            std::cerr << "<Plugins> ERROR: Out of memory converting image from "
                      << plugin->name << "\n";
            return false;
        } catch (const std::length_error&) {
            SusieReleasePicture(&bitmapInfo, &bitmapData);
            image = {};
            std::cerr << "<Plugins> ERROR: Image dimensions exceed available memory for "
                      << plugin->name << "\n";
            return false;
        }
        SusieReleasePicture(&bitmapInfo, &bitmapData);

        if (!converted) {
            image = {};
            std::cerr << "<Plugins> ERROR: Susie plugin returned an unsupported or invalid DIB: "
                      << plugin->name << "\n";
            return false;
        }
        return true;
    }

    void unloadPlugins() {
        for (auto& plugin : plugins) {
            if (plugin.handle) {
                FreePluginLibrary(plugin.handle);
                plugin.handle = nullptr;
            }
        }
        plugins.clear();
    }

    std::size_t getPluginCount() const {
        return plugins.size();
    }

    std::string getFileDialogExtensions() const {
        std::vector<std::string> extensions;
        for (const auto& plugin : plugins) {
            std::size_t start = 0;
            while (start < plugin.extensions.size()) {
                const std::size_t end = plugin.extensions.find(';', start);
                std::string extension = plugin.extensions.substr(
                    start,
                    end == std::string::npos ? std::string::npos : end - start
                );
                const std::size_t first = extension.find_first_not_of(" \t");
                const std::size_t last = extension.find_last_not_of(" \t");
                if (first != std::string::npos) {
                    extension = extension.substr(first, last - first + 1);
                    if (extension.rfind("*.", 0) == 0) {
                        extension.erase(0, 2);
                    } else if (extension[0] == '.') {
                        extension.erase(0, 1);
                    }

                    const bool valid = !extension.empty()
                    && std::all_of(extension.begin(), extension.end(), [](unsigned char ch) {
                        return std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.';
                    });
                    if (valid) {
                        std::string normalized = extension;
                        std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char ch) {
                            return static_cast<char>(std::tolower(ch));
                        });
                        if (std::find(extensions.begin(), extensions.end(), normalized) == extensions.end()) {
                            extensions.push_back(std::move(normalized));
                        }
                    }
                }

                if (end == std::string::npos) break;
                start = end + 1;
            }
        }

        std::string result;
        for (const auto& extension : extensions) {
            if (!result.empty()) result += ';';
            result += extension;
        }
        return result;
    }

    const std::vector<LoadedPlugin>& getPlugins() const {
        return plugins;
    }

private:
    static void* getSusieSymbol(
        PluginHandle handle,
        const char* name,
        unsigned int stdcallBytes
    ) {
        if (void* symbol = GetOptionalPluginSymbol(handle, name)) return symbol;

#if defined(_WIN32) && !defined(_WIN64)
        const std::string decorated = std::string(name) + "@" + std::to_string(stdcallBytes);
        if (void* symbol = GetOptionalPluginSymbol(handle, decorated)) return symbol;
        const std::string underscored = "_" + decorated;
        return GetOptionalPluginSymbol(handle, underscored);
#else
        (void)stdcallBytes;
        return nullptr;
#endif
    }

    static bool utf8ToWide(const std::string& source, std::wstring& result) {
#if defined(_WIN32) || defined(_WIN64)
        if (source.empty()) {
            result.clear();
            return true;
        }
        const int length = MultiByteToWideChar(
            CP_UTF8, MB_ERR_INVALID_CHARS, source.c_str(), -1, nullptr, 0
        );
        if (length <= 0) return false;
        result.resize(static_cast<std::size_t>(length));
        if (MultiByteToWideChar(
                CP_UTF8, MB_ERR_INVALID_CHARS, source.c_str(), -1, result.data(), length
            ) != length) {
            result.clear();
            return false;
        }
        result.pop_back();
        return true;
#else
        result.clear();
        result.reserve(source.size());
        for (unsigned char ch : source) {
            if (ch > 0x7f) return false;
            result.push_back(static_cast<wchar_t>(ch));
        }
        return true;
#endif
    }

    static bool wideToAnsi(const std::wstring& source, std::string& result) {
#if defined(_WIN32) || defined(_WIN64)
        if (source.empty()) {
            result.clear();
            return true;
        }

        BOOL usedDefaultCharacter = FALSE;
        const UINT codePage = GetACP();
        const DWORD flags = codePage == CP_UTF8 ? 0 : WC_NO_BEST_FIT_CHARS;
        const int length = WideCharToMultiByte(
            codePage,
            flags,
            source.c_str(),
            -1,
            nullptr,
            0,
            nullptr,
            codePage == CP_UTF8 ? nullptr : &usedDefaultCharacter
        );
        if (length <= 0 || usedDefaultCharacter) return false;

        result.resize(static_cast<std::size_t>(length));
        usedDefaultCharacter = FALSE;
        if (WideCharToMultiByte(
                codePage,
                flags,
                source.c_str(),
                -1,
                result.data(),
                length,
                nullptr,
                codePage == CP_UTF8 ? nullptr : &usedDefaultCharacter
            ) != length
            || usedDefaultCharacter) {
            result.clear();
            return false;
        }
        result.pop_back();
        return true;
#else
        result.clear();
        result.reserve(source.size());
        for (wchar_t ch : source) {
            if (ch < 0 || ch > 0x7f) return false;
            result.push_back(static_cast<char>(ch));
        }
        return true;
#endif
    }

    static std::string wideToUtf8(const std::wstring& source) {
#if defined(_WIN32) || defined(_WIN64)
        if (source.empty()) return {};
        const int length = WideCharToMultiByte(
            CP_UTF8, 0, source.c_str(), -1, nullptr, 0, nullptr, nullptr
        );
        if (length <= 0) return {};
        std::string result(static_cast<std::size_t>(length), '\0');
        if (WideCharToMultiByte(
                CP_UTF8, 0, source.c_str(), -1, result.data(), length, nullptr, nullptr
            ) != length) {
            return {};
        }
        result.pop_back();
        return result;
#else
        std::string result;
        result.reserve(source.size());
        for (wchar_t ch : source) {
            if (ch < 0 || ch > 0x7f) return {};
            result.push_back(static_cast<char>(ch));
        }
        return result;
#endif
    }

    static std::string ansiToUtf8(const std::string& source) {
#if defined(_WIN32) || defined(_WIN64)
        if (source.empty()) return {};
        const int wideLength = MultiByteToWideChar(CP_ACP, 0, source.c_str(), -1, nullptr, 0);
        if (wideLength <= 0) return {};
        std::wstring wide(static_cast<std::size_t>(wideLength), L'\0');
        if (MultiByteToWideChar(
                CP_ACP, 0, source.c_str(), -1, wide.data(), wideLength
            ) != wideLength) {
            return {};
        }
        wide.pop_back();
        return wideToUtf8(wide);
#else
        return source;
#endif
    }

    static bool preparePaths(
        const std::string& filePath,
        std::wstring& widePath,
        std::string& ansiPath
    ) {
        if (!utf8ToWide(filePath, widePath)) return false;
        ansiPath.clear();
        wideToAnsi(widePath, ansiPath);
        return true;
    }

    static bool queryInfo(const LoadedPlugin& plugin, int index, std::string& value) {
        value.clear();
        std::array<char, 1024> buffer{};
        if (plugin.getPluginInfo
            && plugin.getPluginInfo(index, buffer.data(), static_cast<int>(buffer.size())) > 0) {
            buffer.back() = '\0';
            value = ansiToUtf8(buffer.data());
            return true;
        }

        if (!plugin.getPluginInfoW) return false;
        std::array<wchar_t, 1024> wideBuffer{};
        if (plugin.getPluginInfoW(
                index,
                wideBuffer.data(),
                static_cast<int>(wideBuffer.size())
            ) <= 0) {
            return false;
        }
        wideBuffer.back() = L'\0';
        value = wideToUtf8(wideBuffer.data());
        return true;
    }

    bool loadPlugin(
        const fs::path& path,
        std::string& reason,
        std::uint32_t* loadErrorCode = nullptr
    ) {
#if defined(_WIN32) || defined(_WIN64)
        PluginHandle handle = LoadPluginLibrary(path.wstring(), &reason, loadErrorCode);
#else
        const std::string nativePath = path.string();
        PluginHandle handle = LoadPluginLibrary(
            std::wstring(nativePath.begin(), nativePath.end()),
            &reason
        );
#endif
        if (!handle) return false;

        LoadedPlugin plugin;
        plugin.handle = handle;
        plugin.path = pathToUtf8(path);
        plugin.getPluginInfo = reinterpret_cast<SusieGetPluginInfoFunc>(
            getSusieSymbol(handle, "GetPluginInfo", 12)
        );
        plugin.getPluginInfoW = reinterpret_cast<SusieGetPluginInfoWFunc>(
            getSusieSymbol(handle, "GetPluginInfoW", 12)
        );
        plugin.isSupported = reinterpret_cast<SusieIsSupportedFunc>(
            getSusieSymbol(handle, "IsSupported", 8)
        );
        plugin.isSupportedW = reinterpret_cast<SusieIsSupportedWFunc>(
            getSusieSymbol(handle, "IsSupportedW", 8)
        );
        plugin.getPictureInfo = reinterpret_cast<SusieGetPictureInfoFunc>(
            getSusieSymbol(handle, "GetPictureInfo", 16)
        );
        plugin.getPictureInfoW = reinterpret_cast<SusieGetPictureInfoWFunc>(
            getSusieSymbol(handle, "GetPictureInfoW", 16)
        );
        plugin.getPicture = reinterpret_cast<SusieGetPictureFunc>(
            getSusieSymbol(handle, "GetPicture", 28)
        );
        plugin.getPictureW = reinterpret_cast<SusieGetPictureWFunc>(
            getSusieSymbol(handle, "GetPictureW", 28)
        );
        plugin.getAnimationApiVersion = reinterpret_cast<SusieGetAnimationApiVersionFunc>(
            getSusieSymbol(handle, "OpenSusieGetAnimationApiVersion", 0)
        );
        if (!plugin.getAnimationApiVersion) {
            plugin.getAnimationApiVersion = reinterpret_cast<SusieGetAnimationApiVersionFunc>(
                getSusieSymbol(handle, "ImagrGetAnimationApiVersion", 0)
            );
        }
        plugin.openAnimation = reinterpret_cast<SusieOpenAnimationFunc>(
            getSusieSymbol(handle, "OpenSusieOpenAnimation", 20)
        );
        if (!plugin.openAnimation) {
            plugin.openAnimation = reinterpret_cast<SusieOpenAnimationFunc>(
                getSusieSymbol(handle, "ImagrOpenAnimation", 20)
            );
        }
        plugin.openAnimationW = reinterpret_cast<SusieOpenAnimationWFunc>(
            getSusieSymbol(handle, "OpenSusieOpenAnimationW", 20)
        );
        if (!plugin.openAnimationW) {
            plugin.openAnimationW = reinterpret_cast<SusieOpenAnimationWFunc>(
                getSusieSymbol(handle, "ImagrOpenAnimationW", 20)
            );
        }
        plugin.getAnimationFrame = reinterpret_cast<SusieGetAnimationFrameFunc>(
            getSusieSymbol(handle, "OpenSusieGetAnimationFrame", 20)
        );
        if (!plugin.getAnimationFrame) {
            plugin.getAnimationFrame = reinterpret_cast<SusieGetAnimationFrameFunc>(
                getSusieSymbol(handle, "ImagrGetAnimationFrame", 20)
            );
        }
        plugin.closeAnimation = reinterpret_cast<SusieCloseAnimationFunc>(
            getSusieSymbol(handle, "OpenSusieCloseAnimation", 4)
        );
        if (!plugin.closeAnimation) {
            plugin.closeAnimation = reinterpret_cast<SusieCloseAnimationFunc>(
                getSusieSymbol(handle, "ImagrCloseAnimation", 4)
            );
        }

        if (plugin.getAnimationApiVersion
        &&  plugin.getAnimationApiVersion() != SUSIE_ANIMATION_API_VERSION
        ) {
            plugin.getAnimationApiVersion = nullptr;
            plugin.openAnimation = nullptr;
            plugin.openAnimationW = nullptr;
            plugin.getAnimationFrame = nullptr;
            plugin.closeAnimation = nullptr;

        } else if (!plugin.getAnimationApiVersion
        ||  !plugin.getAnimationFrame
        ||  !plugin.closeAnimation
        ||  (!plugin.openAnimation && !plugin.openAnimationW)
        ) {
            plugin.getAnimationApiVersion = nullptr;
            plugin.openAnimation = nullptr;
            plugin.openAnimationW = nullptr;
            plugin.getAnimationFrame = nullptr;
            plugin.closeAnimation = nullptr;
        }

        if ((!plugin.isSupported && !plugin.isSupportedW)
        ||  (!plugin.getPicture && !plugin.getPictureW)
        ||  (!plugin.getPluginInfo && !plugin.getPluginInfoW)
        ) {
            reason = "Missing required Susie exports:";
            if (!plugin.isSupported && !plugin.isSupportedW) {
                reason += " IsSupported/IsSupportedW";
            }
            if (!plugin.getPicture && !plugin.getPictureW) {
                reason += " GetPicture/GetPictureW";
            }
            if (!plugin.getPluginInfo && !plugin.getPluginInfoW) {
                reason += " GetPluginInfo/GetPluginInfoW";
            }
            std::cerr << "<Plugins> ERROR: Not a Susie image plugin: "
                      << path.filename().string() << "\n";
            FreePluginLibrary(handle);
            return false;
        }

        std::string pluginType;
        if (!queryInfo(plugin, 0, pluginType)) {
            reason = "Could not read the Susie plugin type from GetPluginInfo";
            std::cerr << "<Plugins> ERROR: " << reason << ": "
                      << path.filename().string() << "\n";
            FreePluginLibrary(handle);
            return false;
        }
        if (pluginType != "00IN") {
            reason = "Plugin type is '" + pluginType + "', not '00IN'";
            std::cerr << "<Plugins> ERROR: Susie plugin is not an image plugin: "
                      << path.filename().string() << "\n";
            FreePluginLibrary(handle);
            return false;
        }

        queryInfo(plugin, 1, plugin.name);
        queryInfo(plugin, 2, plugin.extensions);
        queryInfo(plugin, 3, plugin.description);
        if (plugin.name.empty()) plugin.name = path.stem().string();

        plugins.push_back(std::move(plugin));
        std::cout << "<Plugins> Susie image plugin loaded: "
                  << plugins.back().name;
        if (!plugins.back().extensions.empty()) {
            std::cout << " (" << plugins.back().extensions << ")";
        }
        std::cout << "\n";
        return true;
    }

#if defined(_WIN64)
    bool loadPluginIn32BitHost(const fs::path& path, std::string& reason) {
        PluginHostReply reply;
        std::string hostError;
        if (!runPluginHost(
                pathToUtf8(path),
                PluginHostOperation::Info,
                {},
                reply,
                hostError
            )) {
            reason += "; " + hostError;
            return false;
        }
        if (reply.header.status != 0) {
            const std::size_t reasonOffset
            =   reply.header.nameBytes
            +   reply.header.extensionsBytes
            +   reply.header.descriptionBytes
            +   reply.header.metadataBytes
            +   reply.header.pixelsBytes;

            const std::string hostReason(
                reply.payload.data() + reasonOffset,
                reply.header.reasonBytes
            );
            reason += "; 32-bit host rejected plugin: " + hostReason;
            return false;
        }

        LoadedPlugin plugin;
        plugin.path = pathToUtf8(path);
        plugin.hostedBy32BitProcess = true;
        std::size_t offset = 0;
        auto readString = [&](std::uint32_t size, std::string& value) {
            value.assign(reply.payload.data() + offset, size);
            offset += size;
        };
        readString(reply.header.nameBytes, plugin.name);
        readString(reply.header.extensionsBytes, plugin.extensions);
        readString(reply.header.descriptionBytes, plugin.description);
        if (plugin.name.empty()) plugin.name = path.stem().string();
        plugins.push_back(std::move(plugin));
        std::cout << "<Plugins> Susie plugin loaded through 32-bit host: "
                  << plugins.back().name;
        if (!plugins.back().extensions.empty()) {
            std::cout << " (" << plugins.back().extensions << ")";
        }
        std::cout << "\n";
        return true;
    }
#endif

    bool supportsFile(const LoadedPlugin& plugin, const std::string& filePath) const {
#if defined(_WIN64)
        if (plugin.hostedBy32BitProcess) {
            PluginHostReply reply;
            std::string error;
            if (!runPluginHost(
                    plugin.path,
                    PluginHostOperation::IsSupported,
                    filePath,
                    reply,
                    error
                )) {
                std::cerr << "<Plugins> ERROR: " << error << "\n";
                return false;
            }
            return reply.header.status == 0 || declaresExtension(plugin, filePath);
        }
#endif
        std::wstring widePath;
        std::string ansiPath;
        if (!preparePaths(filePath, widePath, ansiPath)) return false;

        if (plugin.isSupportedW) {
            return plugin.isSupportedW(widePath.c_str(), 0) > 0;
        }
        return plugin.isSupported && !ansiPath.empty()
            && plugin.isSupported(ansiPath.c_str(), 0) > 0;
    }

    static std::string pathToUtf8(const fs::path& path) {
        const auto value = path.u8string();
        return std::string(
            reinterpret_cast<const char*>(value.data()),
            value.size()
        );
    }

    static bool declaresExtension(const LoadedPlugin& plugin, const std::string& filePath) {
        const std::size_t separator = filePath.find_last_of("/\\");
        const std::size_t dot = filePath.find_last_of('.');
        if (dot == std::string::npos
        || (separator != std::string::npos && dot < separator)
        ) {
            return false;
        }
        std::string extension = filePath.substr(dot);
        std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        if (extension.empty()) return false;

        std::string token;
        auto matches = [&]() {
            token.erase(
                std::remove_if(token.begin(), token.end(), [](unsigned char ch) {
                    return std::isspace(ch) || ch == '*';
                }),
                token.end()
            );
            std::transform(token.begin(), token.end(), token.begin(), [](unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
            });
            if (!token.empty() && token.front() != '.') token.insert(token.begin(), '.');

            return token == extension;
        };

        for (char ch : plugin.extensions) {
            if (ch == ';' || ch == ',' || std::isspace(static_cast<unsigned char>(ch))) {
                if (matches()) return true;
                token.clear();
            } else {
                token.push_back(ch);
            }
        }

        return matches();
    }

    void recordPluginLoadError(const std::string& plugin, const std::string& reason) {
        const std::string message = plugin + ": " + reason;
        pluginLoadErrors.push_back(message);
        std::cerr << "<Plugins> ERROR: " << message << "\n";
    }

    static std::string readPictureMetadata(HLOCAL handle) {
        if (!handle) return {};
        const std::size_t size = LocalSize(handle);
        if (!size) return {};

        SusieLocalLock textLock(handle);
        const auto* text = static_cast<const char*>(textLock.get());
        if (!text) return {};
        const auto* end = static_cast<const char*>(std::memchr(text, '\0', size));
        std::string result;
        if (end) {
            const std::size_t length = static_cast<std::size_t>(end - text);
            result = ansiToUtf8(std::string(text, length));
        }

        return result;
    }

    static void freePictureInfo(SusiePictureInfo& info) {
        if (info.hInfo) {
            LocalFree(info.hInfo);
            info.hInfo = nullptr;
        }
    }

    static bool isSusiePluginFile(const fs::path& path) {
        std::string extension = path.extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });

        return extension == ".spi";
    }

    std::vector<LoadedPlugin> plugins;
    std::vector<std::string> pluginLoadErrors;
};