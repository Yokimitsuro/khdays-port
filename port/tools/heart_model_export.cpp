// Headless conversion support: reuse the port's model and skeletal-animation decoders.
#include "khdays/assets/mesh.h"
#include "khdays/assets/animation.h"
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <stdexcept>

extern "C" int khdays_export_heart_model(const char *input, const char *output)
{
    try {
        std::ifstream file(input, std::ios::binary | std::ios::ate);
        const auto size = file.tellg();
        if (!file || size < 40 || size > 16 * 1024 * 1024)
            throw std::runtime_error("invalid KAPH file size");
        std::vector<unsigned char> bytes(static_cast<std::size_t>(size));
        file.seekg(0);
        if (!file.read(reinterpret_cast<char *>(bytes.data()), size))
            throw std::runtime_error("could not read KAPH");
        const auto word = [&](std::size_t at) {
            if (at > bytes.size() - 4)
                throw std::runtime_error("truncated KAPH table");
            unsigned value;
            std::memcpy(&value, bytes.data() + at, 4);
            return value;
        };
        const auto section = [&](int index) -> std::pair<const unsigned char *, std::size_t> {
            const auto table = word(8 + 4 * std::size_t(index));
            if (table == 0 || table == ~0u)
                return {};
            const auto count = word(table);
            if (count == 0)
                return {};
            if (count != 1)
                throw std::runtime_error("expected one counter resource per section");
            const auto first = word(table + 4), length = word(table + 8);
            if (first > bytes.size() || length > bytes.size() - first)
                throw std::runtime_error("KAPH resource exceeds file");
            return {bytes.data() + first, length};
        };
        if (std::memcmp(bytes.data(), "KAPH", 4) != 0)
            throw std::runtime_error("missing KAPH header");
        const auto [model_bytes, model_size] = section(7);
        if (!model_bytes)
            throw std::runtime_error("counter has no model");
        const auto model = khdays::assets::decode_model_geometry(model_bytes, model_size);
        const auto [animation_bytes, animation_size] = section(0);
        std::optional<khdays::assets::SkeletalAnimation> animation;
        if (animation_bytes)
            animation = khdays::assets::load_nsbca(animation_bytes, animation_size);
        const unsigned frames = animation ? animation->frame_count : 1;
        if (frames == 0 || frames > 256 || model.meshes.size() > 32)
            throw std::runtime_error("counter animation exceeds conversion limits");
        std::ofstream out(output, std::ios::trunc);
        out << "{\"frames\":" << frames << ",\"meshes\":[";
        bool first_mesh = true;
        for (const auto &mesh : model.meshes) {
            if (mesh.vertices.size() != 4 || mesh.texture_name.find_first_of("\"\\") != std::string::npos ||
                mesh.material_name.find_first_of("\"\\") != std::string::npos)
                throw std::runtime_error("expected a counter quad with simple material names");
            out << (first_mesh ? "" : ",") << "{\"texture\":\"" << mesh.texture_name << "\",\"material\":\""
                << mesh.material_name << "\",\"alpha\":" << mesh.material_alpha << ",\"poses\":[";
            first_mesh = false;
            for (unsigned frame = 0; frame < frames; ++frame) {
                const auto palette =
                    animation ? khdays::assets::compute_palette(
                                    *model.skinning,
                                    khdays::assets::sample_animation(*animation, float(frame), model.object_matrices))
                              : model.palette;
                out << (frame ? ",[" : "[");
                bool first_vertex = true;
                for (const auto &v : mesh.vertices) {
                    float x = 0, y = 0;
                    for (std::size_t j = 0; j < v.weights.size(); ++j) {
                        if (v.weights[j] == 0)
                            continue;
                        const auto &m = palette.at(v.joints[j]);
                        x +=
                            v.weights[j] * (m[0] * v.position[0] + m[4] * v.position[1] + m[8] * v.position[2] + m[12]);
                        y +=
                            v.weights[j] * (m[1] * v.position[0] + m[5] * v.position[1] + m[9] * v.position[2] + m[13]);
                    }
                    out << (first_vertex ? "[" : ",[") << x << ',' << y << ',' << v.texcoord[0] << ',' << v.texcoord[1]
                        << ']';
                    first_vertex = false;
                }
                out << ']';
            }
            out << "]}";
        }
        out << "]}\n";
        if (!out)
            throw std::runtime_error("could not write counter model JSON");
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "heart model export: " << e.what() << '\n';
        return 1;
    }
}
