#include "PluginHostProtocol.h"
#include "PluginManager.h"

#include <algorithm>
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
    ||  request.extraBytes > 32768
    ||  request.dataBytes > 1024u * 1024u * 1024u
    ) {
        return 2;
    }

    std::string pluginPath(request.pluginPathBytes, '\0');
    std::string imagePath(request.imagePathBytes, '\0');
    std::string extra(request.extraBytes, '\0');
    std::vector<std::uint8_t> inputData(request.dataBytes);
    if (!readExact(std::cin, pluginPath.data(), pluginPath.size())
    ||  !readExact(std::cin, imagePath.data(), imagePath.size())
    ||  !readExact(std::cin, extra.data(), extra.size())
    ||  !readExact(std::cin, inputData.data(), inputData.size())
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
            response.pluginType = plugin.archivePlugin ? 2 : 1;
            if (!appendString(response, response.nameBytes, payload, plugin.name)
            ||  !appendString(response, response.extensionsBytes, payload, plugin.extensions)
            ||  !appendString(response, response.descriptionBytes, payload, plugin.description)
            ) {
                return 2;
            }
        } else if (request.operation == static_cast<std::uint32_t>(PluginHostOperation::IsSupported)) {
            response.status = manager.pluginSupportsFile(&plugin, imagePath) ? 0 : 2;
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
        } else if (request.operation == static_cast<std::uint32_t>(PluginHostOperation::ListArchive)) {
            std::vector<SusieArchiveEntry> entries;
            if (!manager.getArchiveEntries(&plugin, imagePath, entries)) {
                response.status = 1;
                reason = "The archive plugin could not list the selected archive";
                if (!appendString(response, response.reasonBytes, payload, reason)) return 2;
            } else {
                response.status = 0;
                for (const SusieArchiveEntry& entry : entries) {
                    std::string name = entry.path;
                    if (!name.empty() && name.back() != '/' && name.back() != '\\') {
                        name.push_back('/');
                    }
                    name += entry.filename;
                    std::replace(name.begin(), name.end(), '\\', '/');
                    if (name.size() > std::numeric_limits<std::uint32_t>::max()) return 2;
                    PluginHostArchiveEntry record;
                    record.position = entry.position;
                    record.nameBytes = static_cast<std::uint32_t>(name.size());
                    payload.append(
                        reinterpret_cast<const char*>(&record),
                        sizeof(record)
                    );
                    payload.append(name);
                }
                if (payload.size() > std::numeric_limits<std::uint32_t>::max()) return 2;
                response.metadataBytes = static_cast<std::uint32_t>(payload.size());
            }
        } else if (request.operation == static_cast<std::uint32_t>(PluginHostOperation::ExtractArchiveEntry)) {
            std::vector<std::uint8_t> extracted;
            if (!manager.extractArchiveFile(&plugin, imagePath, extra, extracted)) {
                response.status = 1;
                reason = "The archive plugin could not extract the selected file";
                if (!appendString(response, response.reasonBytes, payload, reason)) return 2;
            } else {
                if (extracted.size() > std::numeric_limits<std::uint32_t>::max()) return 2;
                response.status = 0;
                response.pixelsBytes = static_cast<std::uint32_t>(extracted.size());
                payload.append(
                    reinterpret_cast<const char*>(extracted.data()),
                    extracted.size()
                );
            }
        } else if (request.operation == static_cast<std::uint32_t>(PluginHostOperation::DecodeImageMemory)) {
            DecodedImage image;
            if (!manager.decodeImageBuffer(&plugin, imagePath, inputData, image)) {
                response.status = 1;
                reason = "The image plugin could not decode the extracted archive file";
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