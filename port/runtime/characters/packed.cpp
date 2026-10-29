// The game's packed files (".z", LZ: Archive_LoadFile) for the characters' module (packed.h).
#include "packed.h"

#include "khdays/assets/message.h"

#include <cstdlib>
#include <cstring>
#include <exception>
#include <vector>

namespace {

int handed(const std::vector<std::uint8_t> &bytes, unsigned char **out, size_t *out_size)
{
    *out = static_cast<unsigned char *>(std::malloc(bytes.empty() ? 1 : bytes.size()));
    if (*out == nullptr) {
        return 0;
    }
    if (!bytes.empty()) {
        std::memcpy(*out, bytes.data(), bytes.size());
    }
    *out_size = bytes.size();
    return 1;
}

} // namespace

extern "C" int khdays_characters_unpack(const unsigned char *data, size_t size, unsigned char **out, size_t *out_size)
{
    try {
        return handed(khdays::assets::lz_decompress(std::vector<std::uint8_t>(data, data + size)), out, out_size);
    } catch (const std::exception &) {
        return 0;
    }
}

extern "C" int khdays_characters_pack(const unsigned char *data, size_t size, unsigned char **out, size_t *out_size)
{
    try {
        return handed(khdays::assets::lz11_literals(std::vector<std::uint8_t>(data, data + size)), out, out_size);
    } catch (const std::exception &) {
        return 0;
    }
}
