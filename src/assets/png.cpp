#include "khdays/assets/png.h"

#include <cstddef>
#include <stdexcept>
#include <string>

// stb_image is third-party and not warning-clean; isolate it from the project's
// strict warning settings.
#if defined(_MSC_VER)
#pragma warning(push, 0)
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#pragma GCC diagnostic ignored "-Wpedantic"
#endif

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include "stb_image.h"

#if defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace khdays::assets {

DecodedTexture load_png(const std::filesystem::path& input_path) {
    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_uc* pixels =
        stbi_load(input_path.string().c_str(), &width, &height, &channels, 4);
    if (pixels == nullptr) {
        throw std::runtime_error("cannot decode PNG: " + input_path.string());
    }

    DecodedTexture texture;
    texture.name = input_path.stem().string();
    texture.format_name = "PNG";
    texture.width = width;
    texture.height = height;
    texture.rgba.assign(
        pixels,
        pixels + static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U);
    stbi_image_free(pixels);
    return texture;
}

}  // namespace khdays::assets
