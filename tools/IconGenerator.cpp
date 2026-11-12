#include <cstdint>
#include <vector>
#include <string>
#include <fstream>
#include <iterator>

#pragma pack(push, 2)
struct IconHeader {
    uint16_t reserved   = 0;
    uint16_t type       = 1;
    uint16_t count      = 1;
};

struct IconEntry {
    uint8_t width;
    uint8_t height;
    uint8_t colorCount  = 0;
    uint8_t reserved    = 0;
    uint16_t planes     = 1;
    uint16_t bitCount   = 32;
    uint32_t imageSize;
    uint32_t imageOffset;
};
#pragma pack(pop)

bool createIco(const std::string& pngPath, const std::string& icoPath) {
    std::ifstream input(pngPath, std::ios::binary);
    if (!input)             return false;

    std::vector<uint8_t> png(std::istreambuf_iterator<char>(input), {});
    if (png.empty())        return false;
    if (png.size() < 24)    return false;

    const uint8_t signature[] = { 137, 80, 78, 71, 13, 10, 26, 10 };

    for (int i = 0; i < 8; ++i) {
        if (png[i] != signature[i]) {
            return false;
        }
    }

    uint32_t width
    =   (   uint32_t(png[16]) << 24 )
    |   (   uint32_t(png[17]) << 16 )
    |   (   uint32_t(png[18]) << 8  )
    |       uint32_t(png[19]);

    uint32_t height
    =   (   uint32_t(png[20]) << 24 )
    |   (   uint32_t(png[21]) << 16 )
    |   (   uint32_t(png[22]) << 8  )
    |       uint32_t(png[23]);

    if (width   ==  0
    ||  height  ==  0
    ||  width   >   256
    ||  height  >   256
    ) {
        return false;
    }

    IconHeader header;

    IconEntry entry{};
    entry.width         = width     == 256 ? 0 : static_cast<uint8_t>(width);
    entry.height        = height    == 256 ? 0 : static_cast<uint8_t>(height);
    entry.imageSize     = static_cast<uint32_t>(png.size());
    entry.imageOffset   = sizeof(IconHeader) + sizeof(IconEntry);

    std::ofstream output(icoPath, std::ios::binary);
    if (!output) return false;

    output.write(reinterpret_cast<const char*>(&header),    sizeof(header)  );
    output.write(reinterpret_cast<const char*>(&entry),     sizeof(entry)   );
    output.write(reinterpret_cast<const char*>(png.data()), png.size()      );

    return output.good();
}

int main(int argc, char* argv[]) {
	if (argc < 3) return 1;

	if (!createIco(argv[1], argv[2])) {
		return 1;
	}

	return 0;
}