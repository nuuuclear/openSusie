#include "PluginHostProtocol.h"
#include "PluginManager.h"

#include <iostream>
#include <limits>
#include <string>
#include <vector>

#if defined(_WIN32)
    #include <fcntl.h>
    #include <io.h>
#endif

namespace {
bool readExact(std::istream& stream, void* data, std::size_t size) {
    if (size == 0) return true;
    stream.read(static_cast<char*>(data), static_cast<std::streamsize>(size));
    return stream.gcount() == static_cast<std::streamsize>(size);
}

bool writeExact(std::ostream& stream, const void* data, std::size_t size) {
    if (size == 0) return true;
    stream.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
    return stream.good();
}

bool appendString(
    PluginHostResponse& response,
    std::uint32_t& fieldBytes,
    std::string& payload,
    const std::string& value
) {
    if (value.size() > std::numeric_limits<std::uint32_t>::max()) return false;
    fieldBytes = static_cast<std::uint32_t>(value.size());
    payload.append(value);
    return true;
}

bool sendResponse(PluginHostResponse response, const std::string& payload) {
    std::cout.clear();
    return writeExact(std::cout, &response, sizeof(response))
        && writeExact(std::cout, payload.data(), payload.size())
        && (std::cout.flush(), std::cout.good());
}

int run() {
    std::cout.setstate(std::ios::failbit);
    PluginHostRequest request;
    if (!readExact(std::cin, &request, sizeof(request))
    ||  request.magic != PluginHostProtocolMagic
    ||  request.pluginPathBytes == 0
    ||  request.pluginPathBytes > 32768
    ||  request.imagePathBytes > 32768
    ) {
        return 2;
    }

    std::string pluginPath(request.pluginPathBytes, '\0');
    std::string imagePath(request.imagePathBytes, '\0');
    if (!readExact(std::cin, pluginPath.data(), pluginPath.size())
    ||  !readExact(std::cin, imagePath.data(), imagePath.size())
    ) {
        return 2;
    }

    PluginHostResponse response;
    std::string payload;
    try {
        PluginManager manager;
        std::string reason;
        const std::u8string pluginPathUtf8(
            reinterpret_cast<const char8_t*>(pluginPath.data()),
            pluginPath.size()
        );
        if (!manager.loadPluginFile(std::filesystem::path(pluginPathUtf8), reason)) {
            response.status = 1;
            if (!appendString(response, response.reasonBytes, payload, reason)) return 2;
            return sendResponse(response, payload) ? 0 : 2;
        }

        const LoadedPlugin& plugin = manager.getPlugins().back();
        if (request.operation == static_cast<std::uint32_t>(PluginHostOperation::Info)) {
            response.status = 0;
            if (!appendString(response, response.nameBytes, payload, plugin.name)
            ||  !appendString(response, response.extensionsBytes, payload, plugin.extensions)
            ||  !appendString(response, response.descriptionBytes, payload, plugin.description)
            ) {
                return 2;
            }
        } else if (request.operation == static_cast<std::uint32_t>(PluginHostOperation::IsSupported)) {
            response.status = manager.supportsFile(imagePath) ? 0 : 2;
        } else if (request.operation == static_cast<std::uint32_t>(PluginHostOperation::DecodeImage)) {
            DecodedImage image;
            if (!manager.decodeImage(&plugin, imagePath, image)) {
                response.status = 1;
                reason = "The plugin could not decode the selected image";
                if (!appendString(response, response.reasonBytes, payload, reason)) return 2;
            } else {
                response.status = 0;
                response.width = image.width;
                response.height = image.height;
                if (image.pixels.size() > std::numeric_limits<std::uint32_t>::max()) {
                    return 2;
                }
                if (!appendString(response, response.metadataBytes, payload, image.metadata)) {
                    return 2;
                }
                response.pixelsBytes = static_cast<std::uint32_t>(image.pixels.size());
                payload.append(
                    reinterpret_cast<const char*>(image.pixels.data()),
                    image.pixels.size()
                );
            }
        } else {
            response.status = 1;
            reason = "Unknown compatibility-host operation";
            if (!appendString(response, response.reasonBytes, payload, reason)) return 2;
        }
    } catch (const std::exception& error) {
        response = {};
        response.status = 1;
        payload.clear();
        if (!appendString(response, response.reasonBytes, payload, error.what())) return 2;
    }

    return sendResponse(response, payload) ? 0 : 2;
}
} // namespace

int main() {
#if defined(_WIN32)
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    return run();
}