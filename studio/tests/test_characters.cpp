// A mod's characters: their description changed line by line, made new, given a model and clips, checked by the
// port's reader; a made-up model posed by its own clip as the port poses it.
#include "test.h"

#include "core/characters/character_files.h"
#include "core/characters/character_pose.h"
#include "core/files/paths.h"
#include "core/models/model_files.h"

#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>

using namespace studio;

namespace {

std::string read(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

std::string base64(const std::string& bytes) {
    static const char* const digits = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    for (std::size_t i = 0; i < bytes.size(); i += 3) {
        const unsigned a = static_cast<unsigned char>(bytes[i]);
        const unsigned b = i + 1 < bytes.size() ? static_cast<unsigned char>(bytes[i + 1]) : 0u;
        const unsigned c = i + 2 < bytes.size() ? static_cast<unsigned char>(bytes[i + 2]) : 0u;
        const unsigned n = (a << 16) | (b << 8) | c;
        out += digits[(n >> 18) & 63];
        out += digits[(n >> 12) & 63];
        out += i + 1 < bytes.size() ? digits[(n >> 6) & 63] : '=';
        out += i + 2 < bytes.size() ? digits[n & 63] : '=';
    }
    return out;
}

template <typename T> void put(std::string* out, std::initializer_list<T> values) {
    for (const T v : values)
        out->append(reinterpret_cast<const char*>(&v), sizeof(T));
}

// A made-up model: a triangle on two bones (`root`, and `arm` one up), and its own clip turning `arm` a quarter
// about z over a second. `bone` names the second bone (another name: a clip of nothing of the model's).
std::string arm_gltf(const char* bone) {
    std::string bin;
    put<float>(&bin, {0, 0, 0, 0, 1, 0, 0.5f, 1, 0});                   // 0: positions, 36
    put<std::uint8_t>(&bin, {0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0});      // 36: joints, 12
    put<float>(&bin, {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0});             // 48: weights, 48
    put<std::uint16_t>(&bin, {0, 1, 2, 0});                             // 96: indices, 8
    put<float>(&bin, {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0,  0, 1,  // 104: inverse binds, 128
                      1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, -1, 0, 1});
    put<float>(&bin, {0, 1});                                        // 232: times, 8
    put<float>(&bin, {0, 0, 0, 1, 0, 0, 0.70710678f, 0.70710678f});  // 240: rotations, 32
    return std::string(R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0,2]}],
"nodes":[{"name":"root","children":[1]},{"name":")") +
           bone + R"(","translation":[0,1,0]},{"name":"body","mesh":0,"skin":0}],
"skins":[{"joints":[0,1],"inverseBindMatrices":4}],
"meshes":[{"primitives":[{"attributes":{"POSITION":0,"JOINTS_0":1,"WEIGHTS_0":2},"indices":3}]}],
"animations":[{"name":"lift","channels":[{"sampler":0,"target":{"node":1,"path":"rotation"}}],
"samplers":[{"input":5,"output":6}]}],
"buffers":[{"byteLength":272,"uri":"data:application/octet-stream;base64,)" +
           base64(bin) + R"("}],
"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":12},
{"buffer":0,"byteOffset":48,"byteLength":48},{"buffer":0,"byteOffset":96,"byteLength":6},
{"buffer":0,"byteOffset":104,"byteLength":128},{"buffer":0,"byteOffset":232,"byteLength":8},
{"buffer":0,"byteOffset":240,"byteLength":32}],
"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[0.5,1,0]},
{"bufferView":1,"componentType":5121,"count":3,"type":"VEC4"},
{"bufferView":2,"componentType":5126,"count":3,"type":"VEC4"},
{"bufferView":3,"componentType":5123,"count":3,"type":"SCALAR"},
{"bufferView":4,"componentType":5126,"count":2,"type":"MAT4"},
{"bufferView":5,"componentType":5126,"count":2,"type":"SCALAR","min":[0],"max":[1]},
{"bufferView":6,"componentType":5126,"count":2,"type":"VEC4"}]})";
}

}  // namespace

TEST(character_ini_changed_line_by_line) {
    IniText ini = IniText::parse(
        "# a made-up one\n[character]\nformat = 1\nname  es = Bloque   # its Spanish name\nbase = roxas\n\n"
        "[model]\nfile = model/a.gltf\n");
    CHECK(ini.get("character", "name es") == "Bloque");
    CHECK(ini.get("model", "file") == "model/a.gltf");
    bool found = true;
    CHECK(ini.get("model", "scale", &found).empty() && !found);
    ini.set("character", "name es", "Cubo");
    ini.set("character", "base", "axel");
    ini.set("model", "scale", "100");
    ini.set("actions", "b_ro_A000", "clips/idle.gltf");  // a part made at the end
    CHECK(ini.text() == "# a made-up one\n[character]\nformat = 1\nname es = Cubo   # its Spanish name\nbase = axel\n\n"
                        "[model]\nfile = model/a.gltf\nscale = 100\n\n[actions]\nb_ro_A000 = clips/idle.gltf\n");
    ini.remove("character", "name es");
    CHECK(!ini.get("character", "name es", &found).size() && !found);
    // a part's lines replaced, its comments kept; emptied, the part goes
    ini.set_entries("actions", {{"fallback", "clips/idle.gltf"}, {"b_ro_A001", "clips/run.gltf blend 120"}});
    CHECK(ini.entries("actions").size() == 2 && ini.get("actions", "b_ro_A001") == "clips/run.gltf blend 120");
    ini.set_entries("actions", {});
    CHECK(!ini.has_part("actions"));
    CHECK(ini.text().back() == '\n' && ini.text().find("[actions]") == std::string::npos);
    // what the tool does not know, kept as written
    const std::string odd = "[character]\nformat = 1\nsomething = new   # a later port's\n";
    CHECK(IniText::parse(odd).text() == odd);
}

TEST(character_made_given_a_model_and_clips_checked) {
    const fs::path mod = studio_test::scratch("characters");
    studio_test::write(mod / "mod.ini", "id = test.made\nname = Made\nkind = gameplay\n");
    std::string why;
    CHECK(!new_character(mod, "a b", "roxas", "Armo", &why));  // not a key
    CHECK(!new_character(mod, "armo", "nobody", "Armo", &why));
    CHECK(new_character(mod, "armo", "roxas", "Armo", &why));
    CHECK(!new_character(mod, "armo", "roxas", "Armo", &why));  // taken
    CHECK(character_keys(mod) == std::vector<std::string>{"armo"});
    CHECK(character_base_code("roxas") == "ro" && character_bases().size() == 19);

    // no model yet: the port would leave it out, and saving refuses it
    IniText ini = IniText::parse(read(character_dir(mod, "armo") / "character.ini"));
    CHECK(!check_character(mod, "armo", ini.text()).empty());
    CHECK(!save_character(mod, "armo", ini.text(), &why));

    // a model copied in, named; its clip copied in, given to an animation
    const fs::path source = studio_test::scratch("characters_source");
    studio_test::write(source / "arm.gltf", arm_gltf("arm"));
    std::string relative;
    CHECK(copy_into_character(mod, "armo", source / "arm.gltf", "model", &relative, &why));
    CHECK(relative == "model/arm.gltf");
    ini.set("model", "file", relative);
    ini.set("model", "skeleton", "own");
    CHECK(copy_into_character(mod, "armo", source / "arm.gltf", "clips", &relative, &why));
    CHECK(character_files(mod, "armo", "clips") == std::vector<std::string>{"clips/arm.gltf"});
    ini.set("actions", "b_ro_A000", "clips/arm.gltf");
    CHECK(check_character(mod, "armo", ini.text()).empty());
    // a clip named that is not there: told, and not saved
    ini.set("actions", "b_ro_A001", "clips/gone.gltf");
    const auto problems = check_character(mod, "armo", ini.text());
    CHECK(problems.size() == 1 && problems[0].find("clips/gone.gltf") != std::string::npos);
    CHECK(!save_character(mod, "armo", ini.text(), &why));
    ini.remove("actions", "b_ro_A001");
    CHECK(save_character(mod, "armo", ini.text(), &why));
    CHECK(read(character_dir(mod, "armo") / "character.ini") == ini.text());
    // the port's reader's own words for what it refuses
    ini.set("character", "format", "9");
    CHECK(!check_character(mod, "armo", ini.text()).empty());
}

TEST(character_posed_by_its_own_clip) {
    const fs::path dir = studio_test::scratch("characters_pose");
    studio_test::write(dir / "arm.gltf", arm_gltf("arm"));
    studio_test::write(dir / "other.gltf", arm_gltf("leg"));
    khdays::assets::ImportedModel model;
    std::string why;
    CHECK(import_model(dir / "arm.gltf", &model, &why));
    CharacterClip clip;
    CHECK(load_character_clip(dir / "arm.gltf", model, &clip, &why));
    CHECK(clip.matched == 1 && clip.tracks == 1 && std::fabs(clip.animation.duration - 1.0f) < 1e-4f);
    // a clip of other bones: none of the model's moves
    CharacterClip other;
    CHECK(!load_character_clip(dir / "other.gltf", model, &other, &why) && !why.empty());

    // at rest the triangle is as built; half a second in, its arm turned an eighth, a second in a quarter:
    // the corner one along the arm from its joint goes round it
    auto corner = [&](double seconds) {
        const auto palette = character_palette(model, character_pose(model, seconds < 0 ? nullptr : &clip, seconds));
        const auto& vx = model.model.meshes.at(0).vertices.at(2);
        std::array<float, 3> p{0, 0, 0};
        for (std::size_t k = 0; k < 4; ++k) {
            if (vx.weights[k] <= 0.0f)
                continue;
            const auto q = mat_point(palette.at(vx.joints[k]), vx.position);
            for (std::size_t c = 0; c < 3; ++c)
                p[c] += vx.weights[k] * q[c];
        }
        return p;
    };
    const auto rest = corner(-1);
    CHECK(std::fabs(rest[0] - 0.5f) < 1e-4f && std::fabs(rest[1] - 1.0f) < 1e-4f);
    const auto quarter = corner(0.9999);
    CHECK(std::fabs(quarter[0]) < 1e-3f && std::fabs(quarter[1] - 1.5f) < 1e-3f);
    const auto eighth = corner(0.5);
    CHECK(std::fabs(eighth[0] - 0.5f * 0.70710678f) < 2e-2f &&
          std::fabs(eighth[1] - (1.0f + 0.5f * 0.70710678f)) < 2e-2f);

    std::array<float, 3> lo, hi;
    character_span(model, &lo, &hi);
    CHECK(std::fabs(hi[1] - lo[1] - 1.0f) < 1e-4f);
}
