#include "khdays/assets/image.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <system_error>
#include <vector>

// stb_image is third-party and not warning-clean; isolate it from the project's
// strict warning settings. Its functions stay in this file (STB_IMAGE_STATIC):
// png.cpp and the host have their own.
#if defined(_MSC_VER)
#pragma warning(push, 0)
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#pragma GCC diagnostic ignored "-Wpedantic"
#pragma GCC diagnostic ignored "-Wunused-function"
#endif

#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_TGA
#define STBI_ONLY_BMP
#include "stb_image.h"

#if defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace {

// A path as UTF-8 text (u8string is std::string in C++17, std::u8string in C++20).
std::string utf8(const std::filesystem::path& path) {
    const auto text = path.u8string();
    return std::string(text.begin(), text.end());
}

}  // namespace

namespace khdays::assets {

DecodedTexture read_image(const std::uint8_t* bytes, std::size_t size, const std::string& name) {
    int width = 0;
    int height = 0;
    int channels = 0;
    if (size > static_cast<std::size_t>(0x7fffffff)) {
        throw std::runtime_error("image too large: " + name);
    }
    stbi_uc* pixels = stbi_load_from_memory(bytes, static_cast<int>(size), &width, &height, &channels, 4);
    if (pixels == nullptr) {
        throw std::runtime_error("not a PNG, JPEG, TGA or BMP image stb_image reads (" +
                                 std::string(stbi_failure_reason()) + "): " + name);
    }
    DecodedTexture texture;
    texture.name = name;
    texture.format_name = "image";
    texture.width = width;
    texture.height = height;
    texture.rgba.assign(pixels, pixels + static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U);
    stbi_image_free(pixels);
    return texture;
}

DecodedTexture read_image(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("cannot open " + utf8(path));
    }
    // read in one go: an istreambuf_iterator takes it a byte at a time (dds.cpp's read_dds)
    file.seekg(0, std::ios::end);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(std::max<std::streamoff>(0, file.tellg())));
    file.seekg(0, std::ios::beg);
    file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    bytes.resize(static_cast<std::size_t>(file.gcount()));
    return read_image(bytes.data(), bytes.size(), utf8(path.stem()));
}

std::optional<std::filesystem::path> find_texture(const std::filesystem::path& model_folder,
                                                  const std::string& stored_path) {
    if (stored_path.empty()) {
        return std::nullopt;
    }
    // either separator, whatever machine the model was made on
    std::string normalised = stored_path;
    std::replace(normalised.begin(), normalised.end(), '\\', '/');
    const std::filesystem::path stored{std::u8string(normalised.begin(), normalised.end())};
    const std::filesystem::path candidates[] = {
        model_folder / stored,
        stored,
        model_folder / stored.filename(),
    };
    for (const auto& candidate : candidates) {
        std::error_code error;
        if (!candidate.empty() && std::filesystem::is_regular_file(candidate, error)) {
            return candidate;
        }
    }
    return std::nullopt;
}

}  // namespace khdays::assets
