#pragma once

#include <cstdint>

inline constexpr std::uint32_t PluginHostProtocolMagic = 0x50474831;

enum class PluginHostOperation : std::uint32_t {
    Info = 1,
    IsSupported = 2,
    DecodeImage = 3,
    ListArchive = 4,
    ExtractArchiveEntry = 5,
    DecodeImageMemory = 6
};

#pragma pack(push, 1)
struct PluginHostRequest {
    std::uint32_t magic = PluginHostProtocolMagic;
    std::uint32_t operation = 0;
    std::uint32_t pluginPathBytes = 0;
    std::uint32_t imagePathBytes = 0;
    std::uint32_t extraBytes = 0;
    std::uint32_t dataBytes = 0;
};

struct PluginHostResponse {
    std::uint32_t magic = PluginHostProtocolMagic;
    std::int32_t status = 1;
    std::uint32_t pluginType = 0;
    std::int32_t width = 0;
    std::int32_t height = 0;
    std::uint32_t nameBytes = 0;
    std::uint32_t extensionsBytes = 0;
    std::uint32_t descriptionBytes = 0;
    std::uint32_t metadataBytes = 0;
    std::uint32_t pixelsBytes = 0;
    std::uint32_t reasonBytes = 0;
};

struct PluginHostArchiveEntry {
    std::uint64_t position = 0;
    std::uint32_t nameBytes = 0;
};
#pragma pack(pop)
