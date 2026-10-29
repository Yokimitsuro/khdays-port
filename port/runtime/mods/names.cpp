// The names of the game's graphics (names.h): a file's data read from the ROM as the game's loads read
// it (a P2 archive's packed entry, or a whole file, LZ when it ends in .z), the NitroSystem resources
// in it found by their headers, and their TEX0 dictionaries naming each texture and palette
// (docs/RENDERING.md, "Mods' graphics").
#include "names.h"
#include "fonts.h"

#include "../core/rom.h"
extern "C" {
#include "../sound/sound_dump.h" // khdays_dump_allowed
#include "textures.h"
}

#include "khdays/assets/gltf_export.h"
#include "khdays/assets/font.h"
#include <exception>
#include "khdays/assets/mesh.h"
#include "khdays/assets/message.h" // lz_decompress

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {

struct Named {
    u32 offset; // into the file's data
    int format; // textures: the TEXIMAGE_PARAM format
    std::string name;
    u32 param = 0; // textures: the TEXIMAGE_PARAM
    u32 extra = 0; // textures: a 4x4 one's palette indices' offset; palettes: the size
};

// An NCGR of the data (names.h KhdaysTileSheet), with the place of its
// file's first NCLR's colours (0: none) for the dump.
struct Sheet {
    KhdaysTileSheet sheet;
    std::string name;
};

struct FileNames {
    std::string label; // "<path>" or "<path>#<entry>"
    std::vector<Named> textures, palettes, models, animations;
    std::vector<Sheet> sheets;
    std::vector<u8> data; // kept when it has sheets: their tiles' bytes
};

std::map<std::string, FileNames> cache;
KhdaysRomReader *reader;

u32 le16(const std::vector<u8> &d, std::size_t at)
{
    return at + 2 <= d.size() ? static_cast<u32>(d[at] | d[at + 1] << 8) : 0;
}

u32 le32(const std::vector<u8> &d, std::size_t at)
{
    return at + 4 <= d.size() ? le16(d, at) | le16(d, at + 2) << 16 : 0;
}

std::vector<u8> read_rom(u32 offset, u32 size)
{
    std::vector<u8> bytes(size);
    if (reader == nullptr) {
        reader = khdays_rom_reader_new();
    }
    if (size != 0) {
        khdays_rom_reader_read(reader, offset, bytes.data(), size);
    }
    return bytes;
}

std::vector<u8> decompress(const std::vector<u8> &packed)
{
    try {
        return khdays::assets::lz_decompress(packed);
    } catch (...) {
        return {};
    }
}

// The data `file` (as origin.c names it) holds, and its label.
std::vector<u8> file_data(const std::string &file, std::string &label)
{
    const auto at = file.rfind('@');
    const std::string path = at == std::string::npos ? file : file.substr(0, at);
    u32 top = 0, size = 0;
    label = path;
    if (!khdays_rom_find(path.c_str(), &top, &size)) {
        return {};
    }
    if (at == std::string::npos) {
        std::vector<u8> whole = read_rom(top, size);
        return path.size() > 2 && path.compare(path.size() - 2, 2, ".z") == 0 ? decompress(whole) : whole;
    }
    // a packed entry: which one starts there
    const u32 within = static_cast<u32>(std::strtoul(file.c_str() + at + 1, nullptr, 16));
    const std::vector<u8> header = read_rom(top, size < 0x2000 ? size : 0x2000);
    if (header.size() < 0x10 || header[0] != 'P' || header[1] != '2') {
        return {};
    }
    const u32 count = le16(header, 2) & 0x1ff;
    const u32 base = le32(header, 0xc);
    const u32 sizes = 0x10 + ((count + 1) / 2) * 4;
    for (u32 entry = 0; entry < count; ++entry) {
        if (base + (le16(header, 0x10 + 2 * entry) << 9) != within) {
            continue;
        }
        const u32 raw = le32(header, sizes + 4 * entry);
        char text[16];
        std::snprintf(text, sizeof(text), "#%u", entry);
        label = path + text;
        std::vector<u8> bytes = read_rom(top + within, raw & 0x7fffffff);
        return (raw & 0x80000000u) ? decompress(bytes) : bytes;
    }
    return {};
}

// A dictionary's names and entries (NitroSystem's NNSG3dResDict).
struct Dict {
    std::vector<std::string> names;
    std::vector<std::size_t> entries; // where each entry's data starts
};

Dict read_dict(const std::vector<u8> &d, std::size_t dict)
{
    Dict out;
    if (dict + 8 > d.size())
        return out;
    const u32 count = d[dict + 1];
    const std::size_t header = dict + le16(d, dict + 6);
    const u32 unit = le16(d, header);
    const std::size_t names = header + le16(d, header + 2);
    if (unit == 0 || names + 16 * count > d.size() || header + 4 + unit * count > d.size())
        return out;
    for (u32 i = 0; i < count; ++i) {
        const char *text = reinterpret_cast<const char *>(&d[names + 16 * i]);
        out.names.emplace_back(text, strnlen(text, 16));
        out.entries.push_back(header + 4 + unit * i);
    }
    return out;
}

// The TEX0 section of the BMD0 or BTX0 at `start`, its textures and palettes.
void read_tex0(const std::vector<u8> &d, std::size_t start, FileNames &names)
{
    const u32 sections = le16(d, start + 0xe);
    if (sections == 0 || sections > 8)
        return;
    for (u32 s = 0; s < sections; ++s) {
        const std::size_t tex0 = start + le32(d, start + 0x10 + 4 * s);
        if (tex0 + 0x3c > d.size() || std::memcmp(&d[tex0], "TEX0", 4) != 0)
            continue;
        const std::size_t tex_data = tex0 + le32(d, tex0 + 0x14);
        const std::size_t tex4x4_data = tex0 + le32(d, tex0 + 0x24);
        const std::size_t tex4x4_index = tex0 + le32(d, tex0 + 0x28);
        const std::size_t pltt_data = tex0 + le32(d, tex0 + 0x38);
        const std::size_t pltt_size = le16(d, tex0 + 0x30) * 8u;
        const Dict textures = read_dict(d, tex0 + le16(d, tex0 + 0xe));
        const Dict palettes = read_dict(d, tex0 + le16(d, tex0 + 0x34));
        for (std::size_t i = 0; i < textures.names.size(); ++i) {
            const u32 param = le32(d, textures.entries[i]);
            const int format = static_cast<int>((param >> 26) & 7);
            const std::size_t at = (format == 5 ? tex4x4_data : tex_data) + (param & 0xffff) * 8u;
            Named t{static_cast<u32>(at), format, textures.names[i]};
            t.param = param;
            t.extra = static_cast<u32>(tex4x4_index + (param & 0xffff) * 4u);
            names.textures.push_back(t);
        }
        std::vector<std::size_t> starts;
        for (std::size_t i = 0; i < palettes.names.size(); ++i) {
            starts.push_back(le16(d, palettes.entries[i]) * 8u);
        }
        for (std::size_t i = 0; i < palettes.names.size(); ++i) {
            std::size_t end = pltt_size; // up to the next palette, or the end
            for (const auto other : starts) {
                if (other > starts[i] && other < end)
                    end = other;
            }
            Named p{static_cast<u32>(pltt_data + starts[i]), 0, palettes.names[i]};
            p.extra = static_cast<u32>(end > starts[i] ? end - starts[i] : 0);
            names.palettes.push_back(p);
        }
    }
}

// A second name like one before it gets "~2", a third "~3".
void number_repeats(std::vector<Named> &list)
{
    std::map<std::string, int> seen;
    for (auto &n : list) {
        const int k = ++seen[n.name];
        if (k > 1)
            n.name += "~" + std::to_string(k);
    }
}

// The models of the BMD0 at `start` (its MDL0's dictionary): where each
// one's data starts.
void read_models(const std::vector<u8> &d, std::size_t start, FileNames &names)
{
    const u32 sections = le16(d, start + 0xe);
    for (u32 s = 0; s < sections && s < 8; ++s) {
        const std::size_t mdl0 = start + le32(d, start + 0x10 + 4 * s);
        if (mdl0 + 0x10 > d.size() || std::memcmp(&d[mdl0], "MDL0", 4) != 0)
            continue;
        const Dict models = read_dict(d, mdl0 + 8);
        for (std::size_t m = 0; m < models.names.size(); ++m) {
            names.models.push_back({static_cast<u32>(mdl0 + le32(d, models.entries[m])), 0, models.names[m]});
        }
    }
}

// The joint animations of the BCA0 at `start` (its JNT0's dictionary): where each one's data starts.
void read_animations(const std::vector<u8> &d, std::size_t start, FileNames &names)
{
    const u32 sections = le16(d, start + 0xe);
    for (u32 s = 0; s < sections && s < 8; ++s) {
        const std::size_t jnt0 = start + le32(d, start + 0x10 + 4 * s);
        if (jnt0 + 0x10 > d.size() || std::memcmp(&d[jnt0], "JNT0", 4) != 0)
            continue;
        const Dict animations = read_dict(d, jnt0 + 8);
        for (std::size_t a = 0; a < animations.names.size(); ++a) {
            names.animations.push_back(
                {static_cast<u32>(jnt0 + le32(d, animations.entries[a])), 0, animations.names[a]});
        }
    }
}

// The NCGR at `at` (a Nitro file "RGCN" whose "RAHC" block gives its
// tiles: u16 height and width in tiles at +0x18 and +0x1a, the depth at
// +0x1c -- 4: 8 bits a pixel --, their size at +0x28 and their place after
// +0x18 at +0x2c), as decode_ncgr (assets/src/graphics2d.cpp) reads it.
void read_sheet(const std::vector<u8> &d, std::size_t at, FileNames &names)
{
    if (at + 0x30 > d.size() || le16(d, at + 4) != 0xfeff || std::memcmp(&d[at + 0x10], "RAHC", 4) != 0)
        return;
    const u32 bytes = le32(d, at + 0x28), start = static_cast<u32>(at + 0x18 + le32(d, at + 0x2c));
    const u32 tile_bytes = le32(d, at + 0x1c) == 4 ? 64 : 32;
    if (bytes < tile_bytes || start > d.size() || bytes > d.size() - start)
        return;
    KhdaysTileSheet s{};
    s.tiles_at = start;
    s.tile_bytes = tile_bytes;
    s.tiles = bytes / tile_bytes;
    const u32 h = le16(d, at + 0x18), w = le16(d, at + 0x1a);
    s.columns = w != 0xffff && h != 0xffff && w > 0 && w <= 128 && w * h == s.tiles ? w : s.tiles < 16 ? s.tiles : 16;
    names.sheets.push_back({s, names.label + "/chr/" + std::to_string(names.sheets.size())});
}

const FileNames &names_of(const std::string &file)
{
    auto it = cache.find(file);
    if (it != cache.end())
        return it->second;
    FileNames names;
    std::vector<u8> d = file_data(file, names.label);
    for (std::size_t at = 0; at + 0x18 <= d.size(); at += 4) {
        if ((std::memcmp(&d[at], "BMD0", 4) == 0 || std::memcmp(&d[at], "BTX0", 4) == 0) &&
            le32(d, at + 8) <= d.size() - at) {
            read_tex0(d, at, names);
            if (d[at + 1] == 'M')
                read_models(d, at, names);
        } else if (std::memcmp(&d[at], "BCA0", 4) == 0 && le32(d, at + 8) <= d.size() - at) {
            read_animations(d, at, names);
        } else if (std::memcmp(&d[at], "RGCN", 4) == 0 && le32(d, at + 8) <= d.size() - at) {
            read_sheet(d, at, names);
        }
    }
    number_repeats(names.textures);
    number_repeats(names.palettes);
    if (!names.sheets.empty())
        names.data = std::move(d);
    auto &kept = cache.emplace(file, std::move(names)).first->second;
    for (auto &s : kept.sheets)
        s.sheet.data = kept.data.data() + s.sheet.tiles_at;
    return kept;
}

// --- The dump without a game (--dump-graphics) ----------------------------------------

// Which palettes each texture is drawn with, by name: the models' materials
// (MDL0: per material set, the textures and palettes bound to the same
// materials) and the texture pattern animations' keys, over every model and
// animation the ROM holds.
std::map<std::string, std::set<std::string>> pairs;

void read_pairs(const std::vector<u8> &d, std::size_t bmd0)
{
    const u32 sections = le16(d, bmd0 + 0xe);
    for (u32 s = 0; s < sections && s < 8; ++s) {
        const std::size_t mdl0 = bmd0 + le32(d, bmd0 + 0x10 + 4 * s);
        if (mdl0 + 0x10 > d.size() || std::memcmp(&d[mdl0], "MDL0", 4) != 0)
            continue;
        const Dict models = read_dict(d, mdl0 + 8);
        for (std::size_t m = 0; m < models.entries.size(); ++m) {
            const std::size_t model = mdl0 + le32(d, models.entries[m]);
            const std::size_t mat = model + le32(d, model + 8); // NNSG3dResMat
            if (mat + 4 > d.size())
                continue;
            const Dict tex = read_dict(d, mat + le16(d, mat));
            const Dict pltt = read_dict(d, mat + le16(d, mat + 2));
            // each entry: u16 offset (from the material block) of its material indices, u8 how many
            auto materials = [&](const Dict &dict, std::size_t i) {
                std::set<u32> out;
                const std::size_t list = mat + le16(d, dict.entries[i]);
                const u32 n = dict.entries[i] + 2 < d.size() ? d[dict.entries[i] + 2] : 0;
                for (u32 k = 0; k < n && list + k < d.size(); ++k)
                    out.insert(d[list + k]);
                return out;
            };
            for (std::size_t t = 0; t < tex.names.size(); ++t) {
                const std::set<u32> tm = materials(tex, t);
                for (std::size_t p = 0; p < pltt.names.size(); ++p) {
                    for (const u32 m2 : materials(pltt, p)) {
                        if (tm.count(m2)) {
                            pairs[tex.names[t]].insert(pltt.names[p]);
                            break;
                        }
                    }
                }
            }
        }
    }
}

// The texture pattern animations' pairs (BTP0, its PAT0: per animation u16
// frames, u8 textures, u8 palettes, u16 offsets of their 16-byte names, then
// a dictionary of materials whose entries give u16 keys, u16 flag, u16 ratio
// and the u16 offset of the keys -- u16 frame, u8 texture, u8 palette each):
// every texture with the palette a key shows it with.
void read_pattern_pairs(const std::vector<u8> &d, std::size_t btp0)
{
    const u32 sections = le16(d, btp0 + 0xe);
    for (u32 s = 0; s < sections && s < 8; ++s) {
        const std::size_t pat0 = btp0 + le32(d, btp0 + 0x10 + 4 * s);
        if (pat0 + 0x10 > d.size() || std::memcmp(&d[pat0], "PAT0", 4) != 0)
            continue;
        const Dict anims = read_dict(d, pat0 + 8);
        for (std::size_t a = 0; a < anims.entries.size(); ++a) {
            const std::size_t anim = pat0 + le32(d, anims.entries[a]);
            if (anim + 12 > d.size())
                continue;
            const u32 ntex = d[anim + 6], npltt = d[anim + 7];
            const std::size_t tex_names = anim + le16(d, anim + 8), pltt_names = anim + le16(d, anim + 10);
            if (tex_names + 16 * ntex > d.size() || pltt_names + 16 * npltt > d.size())
                continue;
            auto name_at = [&](std::size_t at) {
                const char *text = reinterpret_cast<const char *>(&d[at]);
                return std::string(text, strnlen(text, 16));
            };
            const Dict materials = read_dict(d, anim + 12);
            for (std::size_t m = 0; m < materials.entries.size(); ++m) {
                const std::size_t entry = materials.entries[m];
                const u32 keys = le16(d, entry);
                const std::size_t at = anim + le16(d, entry + 6);
                for (u32 k = 0; k < keys && at + 4 * k + 4 <= d.size(); ++k) {
                    const u32 t = d[at + 4 * k + 2], p = d[at + 4 * k + 3];
                    if (t < ntex && p < npltt)
                        pairs[name_at(tex_names + 16 * t)].insert(name_at(pltt_names + 16 * p));
                }
            }
        }
    }
}

// Every piece of data a game file holds: the entries of a "P2" archive, or
// the whole file (LZ-decompressed when its path ends in ".z").
template <typename Each> void each_data(Each each)
{
    const int count = khdays_rom_file_count();
    for (int id = 0; id < count; ++id) {
        char path[160];
        u32 top = 0, size = 0;
        if (!khdays_rom_file_info(id, path, sizeof(path), &top, &size) || size == 0)
            continue;
        const std::vector<u8> head = read_rom(top, size < 0x2000 ? size : 0x2000);
        if (head.size() >= 0x10 && head[0] == 'P' && head[1] == '2') {
            const u32 n = le16(head, 2) & 0x1ff;
            const u32 base = le32(head, 0xc);
            const u32 sizes = 0x10 + ((n + 1) / 2) * 4;
            for (u32 e = 0; e < n; ++e) {
                const u32 at = base + (le16(head, 0x10 + 2 * e) << 9);
                const u32 raw = le32(head, sizes + 4 * e);
                if (at >= size || (raw & 0x7fffffff) > size - at)
                    continue;
                std::vector<u8> bytes = read_rom(top + at, raw & 0x7fffffff);
                char label[200];
                std::snprintf(label, sizeof(label), "%s#%u", path, e);
                each(std::string(label), (raw & 0x80000000u) ? decompress(bytes) : bytes);
            }
            continue;
        }
        std::vector<u8> whole = read_rom(top, size);
        const std::string p(path);
        each(p, p.size() > 2 && p.compare(p.size() - 2, 2, ".z") == 0 ? decompress(whole) : whole);
    }
}

bool has_header(const std::vector<u8> &d, std::size_t at)
{
    return (std::memcmp(&d[at], "BMD0", 4) == 0 || std::memcmp(&d[at], "BTX0", 4) == 0) &&
           le32(d, at + 8) <= d.size() - at;
}

std::size_t texel_bytes(u32 param)
{
    static const int bits[8] = {0, 8, 2, 4, 8, 2, 8, 16};
    const std::size_t w = std::size_t{8} << ((param >> 20) & 7), h = std::size_t{8} << ((param >> 23) & 7);
    return w * h * static_cast<std::size_t>(bits[(param >> 26) & 7]) / 8;
}

// The palette a texture is written with in the dump, and whether it is
// known (its name then part of the file's).
const Named *palette_for(const FileNames &names, const std::string &texture, bool &known)
{
    const std::string base = texture.substr(0, texture.find('~'));
    const auto it = pairs.find(base);
    known = true;
    for (const auto &p : names.palettes) {
        if ((it != pairs.end() && it->second.count(p.name)) || p.name == base + "_pl")
            return &p;
    }
    known = false;
    return names.palettes.empty() ? nullptr : &names.palettes.front();
}

// Every model of the BMD0 at `at` as glTF, beside its textures in the dump.
int dump_models(const std::vector<u8> &d, std::size_t at, const FileNames &names, const std::string &folder)
{
    int written = 0;
    const u32 size = le32(d, at + 8);
    const u32 sections = le16(d, at + 0xe);
    for (u32 s = 0; s < sections && s < 8; ++s) {
        const std::size_t mdl0 = at + le32(d, at + 0x10 + 4 * s);
        if (mdl0 + 0x10 > d.size() || std::memcmp(&d[mdl0], "MDL0", 4) != 0)
            continue;
        const std::size_t models = read_dict(d, mdl0 + 8).names.size();
        for (std::size_t m = 0; m < models; ++m) {
            khdays::assets::NeutralModel model;
            try {
                model = khdays::assets::decode_model_geometry(&d[at], size, m);
            } catch (const std::exception &e) {
                std::fprintf(stderr, "graphics dump: %s: model %zu not decoded (%s)\n", folder.c_str(), m, e.what());
                continue;
            }
            std::map<std::string, khdays::assets::GltfTextureRef> refs;
            for (const auto &mesh : model.meshes) {
                if (mesh.texture_name.empty() || refs.count(mesh.texture_name))
                    continue;
                for (const auto &t : names.textures) {
                    if (t.name != mesh.texture_name)
                        continue;
                    bool known = false;
                    const Named *p = t.format == 7 ? nullptr : palette_for(names, t.name, known);
                    khdays::assets::GltfTextureRef ref;
                    ref.uri = "../tex/" + t.name + (p != nullptr && known ? "@" + p->name : std::string()) + ".png";
                    ref.width = 8 << ((t.param >> 20) & 7);
                    ref.height = 8 << ((t.param >> 23) & 7);
                    ref.translucent = t.format == 1 || t.format == 6; // A3I5, A5I3: texels the DS blends
                    refs[mesh.texture_name] = ref;
                    break;
                }
            }
            const std::string utf8 = folder + "/mdl/" + model.name + ".gltf";
            const std::filesystem::path out(std::u8string(utf8.begin(), utf8.end()));
            std::error_code ignored;
            std::filesystem::create_directories(out.parent_path(), ignored);
            std::ofstream file(out, std::ios::binary);
            file << khdays::assets::to_gltf(model, refs);
            written += file.good() ? 1 : 0;
        }
    }
    return written;
}

// The data's first NCLR's colours ("RLCN": BGR555 from +0x28, their bytes
// at +0x20) as RGB, or none.
std::vector<u8> first_colours(const std::vector<u8> &d)
{
    std::vector<u8> colours;
    for (std::size_t at = 0; at + 0x28 <= d.size() && colours.empty(); at += 4) {
        if (std::memcmp(&d[at], "RLCN", 4) != 0 || std::memcmp(&d[at + 0x10], "TTLP", 4) != 0)
            continue;
        const u32 count = std::min<u32>(le32(d, at + 0x20) / 2, 256);
        for (u32 i = 0; i < count && at + 0x28 + 2 * i + 2 <= d.size(); ++i) {
            const u32 c = le16(d, at + 0x28 + 2 * i);
            for (int k = 0; k < 3; ++k) {
                const u32 v = (c >> (5 * k)) & 31;
                colours.push_back(static_cast<u8>(v << 3 | v >> 2));
            }
        }
    }
    return colours;
}

// A tile sheet as an indexed PNG at `folder`/<its name>.png: its tiles in
// rows of `columns`, each pixel the game's palette index, shown with
// `colours` (RGB), else greys.
int write_sheet(const std::vector<u8> &d, const Sheet &s, const std::vector<u8> &colours, const std::string &folder)
{
    {
        const KhdaysTileSheet &t = s.sheet;
        const u32 rows = (t.tiles + t.columns - 1) / t.columns;
        const int w = static_cast<int>(t.columns * 8), h = static_cast<int>(rows * 8);
        const int n = t.tile_bytes == 64 ? 256 : 16;
        std::vector<u8> pixels(static_cast<std::size_t>(w) * h, 0), palette(static_cast<std::size_t>(n) * 3);
        for (int i = 0; i < n; ++i) {
            for (int k = 0; k < 3; ++k) {
                palette[static_cast<std::size_t>(i) * 3 + k] = static_cast<std::size_t>(i) * 3 + k < colours.size()
                                                                   ? colours[static_cast<std::size_t>(i) * 3 + k]
                                                                   : static_cast<u8>(i * 255 / (n - 1));
            }
        }
        for (u32 tile = 0; tile < t.tiles; ++tile) {
            const u8 *src = &d[t.tiles_at + tile * t.tile_bytes];
            const std::size_t x0 = (tile % t.columns) * 8, y0 = (tile / t.columns) * 8;
            for (u32 p = 0; p < 64; ++p) {
                const u8 index = t.tile_bytes == 64 ? src[p] : static_cast<u8>((src[p / 2] >> ((p & 1) * 4)) & 15);
                pixels[(y0 + p / 8) * static_cast<std::size_t>(w) + x0 + p % 8] = index;
            }
        }
        const std::string out = folder + "\\" + s.name + ".png";
        return khdays_textures_write_indexed_png(out.c_str(), pixels.data(), w, h, palette.data(), n);
    }
}

// Every tile sheet of the data, written so.
int dump_sheets(const std::vector<u8> &d, const FileNames &names, const std::string &folder)
{
    const std::vector<u8> colours = first_colours(d);
    int written = 0;
    for (const auto &s : names.sheets)
        written += write_sheet(d, s, colours, folder);
    return written;
}

std::string json_quote(const std::string &value)
{
    std::string quoted = "\"";
    for (const unsigned char ch : value) {
        if (ch == '"' || ch == '\\') {
            quoted += '\\';
            quoted += static_cast<char>(ch);
        } else if (ch < 0x20) {
            char escape[7];
            std::snprintf(escape, sizeof(escape), "\\u%04x", static_cast<unsigned>(ch));
            quoted += escape;
        } else {
            quoted += static_cast<char>(ch);
        }
    }
    return quoted + '"';
}
// Every NFTR glyph by index, with its metrics and CMAP in adjacent JSON.
int dump_font(const std::string &label, const std::vector<u8> &data, const std::string &folder)
{
    khdays::assets::Font font;
    try {
        font = khdays::assets::decode_nftr(data);
    } catch (const std::exception &e) {
        std::fprintf(stderr, "graphics dump: cannot decode font %s: %s\n", label.c_str(), e.what());
        return 0;
    }
    constexpr int columns = 16;
    constexpr int padding = 1;
    if (font.cell_width <= 0 || font.cell_height <= 0 || font.glyphs.empty() || font.glyphs.size() > 16384) {
        std::fprintf(stderr, "graphics dump: invalid dimensions or glyph count for %s\n", label.c_str());
        return 0;
    }
    const int pitch_x = font.cell_width + padding * 2;
    const int pitch_y = font.cell_height + padding * 2;
    const std::size_t rows = (font.glyphs.size() + columns - 1) / columns;
    const int w = columns * pitch_x;
    const int h = static_cast<int>(rows * static_cast<std::size_t>(pitch_y));
    if (w > 16384 || h > 16384) {
        std::fprintf(stderr, "graphics dump: font atlas too large for %s\n", label.c_str());
        return 0;
    }
    std::vector<u8> rgba(static_cast<std::size_t>(w) * h * 4, 0);
    for (std::size_t g = 0; g < font.glyphs.size(); ++g) {
        const int x0 = static_cast<int>(g % columns) * pitch_x + padding;
        const int y0 = static_cast<int>(g / columns) * pitch_y + padding;
        const auto &alpha = font.glyphs[g].alpha;
        if (alpha.size() != static_cast<std::size_t>(font.cell_width) * font.cell_height)
            return 0;
        for (int y = 0; y < font.cell_height; ++y) {
            for (int x = 0; x < font.cell_width; ++x) {
                const std::size_t dst = (static_cast<std::size_t>(y0 + y) * w + x0 + x) * 4;
                rgba[dst] = 255;
                rgba[dst + 1] = 255;
                rgba[dst + 2] = 255;
                rgba[dst + 3] = alpha[static_cast<std::size_t>(y) * font.cell_width + x];
            }
        }
    }

    // Keep the complete original filename, e.g. text/font_eu_10.nftr.png.
    const std::filesystem::path directory(std::u8string(folder.begin(), folder.end()));
    const std::u8string filename = std::u8string(label.begin(), label.end()) + u8".png";
    const std::filesystem::path png = directory / "fonts" / filename;
    const auto png_utf8 = png.u8string();
    const std::string png_name(png_utf8.begin(), png_utf8.end());
    std::error_code ec;
    std::filesystem::create_directories(png.parent_path(), ec);
    if (ec || !khdays_textures_write_png(png_name.c_str(), rgba.data(), w, h)) {
        std::fprintf(stderr, "graphics dump: cannot write font atlas %s\n", png_name.c_str());
        return 0;
    }
    std::filesystem::path json_path = png;
    json_path.replace_extension(".json");
    const auto json_utf8 = json_path.u8string();
    const std::string json_name(json_utf8.begin(), json_utf8.end());
    std::ofstream json(json_path, std::ios::binary);
    if (!json) {
        std::fprintf(stderr, "graphics dump: cannot write font metadata %s\n", json_name.c_str());
        return 0;
    }
    json << "{\n  \"source\": " << json_quote(label) << ",\n  \"format\": \"NFTR\""
         << ",\n  \"cell_width\": " << font.cell_width << ",\n  \"cell_height\": " << font.cell_height
         << ",\n  \"baseline\": " << font.baseline << ",\n  \"max_width\": " << font.max_width
         << ",\n  \"line_feed\": " << font.line_feed << ",\n  \"bpp\": " << font.bpp
         << ",\n  \"atlas\": {\"width\": " << w << ", \"height\": " << h << ", \"columns\": " << columns
         << ", \"padding\": " << padding << "}"
         << ",\n  \"glyphs\": [\n";
    for (std::size_t i = 0; i < font.glyphs.size(); ++i) {
        const auto &glyph = font.glyphs[i];
        const int x = static_cast<int>(i % columns) * pitch_x + padding;
        const int y = static_cast<int>(i / columns) * pitch_y + padding;
        json << "    {\"index\": " << i << ", \"x\": " << x << ", \"y\": " << y << ", \"w\": " << font.cell_width
             << ", \"h\": " << font.cell_height << ", \"left\": " << glyph.left << ", \"width\": " << glyph.width
             << ", \"advance\": " << glyph.advance << "}" << (i + 1 < font.glyphs.size() ? ",\n" : "\n");
    }
    json << "  ],\n  \"charmap\": [\n";
    std::size_t i = 0;
    for (const auto &[code, glyph] : font.char_to_glyph) {
        json << "    {\"codepoint\": " << static_cast<unsigned>(code) << ", \"glyph\": " << glyph << "}"
             << (++i < font.char_to_glyph.size() ? ",\n" : "\n");
    }
    json << "  ]\n}\n";
    json.flush();
    if (!json.good()) {
        std::fprintf(stderr, "graphics dump: failed writing %s\n", json_name.c_str());
        return 0;
    }
    return 1;
}

} // namespace
extern "C" int khdays_graphics_dump(const char *directory)
{
    char full[1024];
    int written = 0, files = 0, unpaired = 0, models = 0, sheets = 0, fonts = 0;
    if (!khdays_dump_allowed(directory, full, sizeof(full))) {
        return 1;
    }
    // the pairings first, from every model
    each_data([](const std::string &, const std::vector<u8> &d) {
        for (std::size_t at = 0; at + 0x18 <= d.size(); at += 4) {
            if (std::memcmp(&d[at], "BMD0", 4) == 0 && le32(d, at + 8) <= d.size() - at)
                read_pairs(d, at);
            if (std::memcmp(&d[at], "BTP0", 4) == 0 && le32(d, at + 8) <= d.size() - at)
                read_pattern_pairs(d, at);
        }
    });
    each_data([&](const std::string &label, const std::vector<u8> &d) {
        if (d.size() >= 4 && std::memcmp(d.data(), "RTFN", 4) == 0) {
            if (dump_font(label, d, full)) {
                ++fonts;
                ++files;
                // Smoke-test any matching mod atlas without changing game rendering.
                khdays::hd_fonts::probe(label);
            }
            return;
        }
        FileNames names;
        names.label = label;
        for (std::size_t at = 0; at + 0x18 <= d.size(); at += 4) {
            if (has_header(d, at))
                read_tex0(d, at, names);
            if (std::memcmp(&d[at], "RGCN", 4) == 0 && le32(d, at + 8) <= d.size() - at)
                read_sheet(d, at, names);
        }
        if (names.textures.empty() && names.sheets.empty())
            return;
        ++files;
        sheets += dump_sheets(d, names, full);
        number_repeats(names.textures);
        number_repeats(names.palettes);
        for (std::size_t at = 0; at + 0x18 <= d.size(); at += 4) {
            if (std::memcmp(&d[at], "BMD0", 4) == 0 && le32(d, at + 8) <= d.size() - at) {
                models += dump_models(d, at, names, std::string(full) + "\\" + label);
            }
        }
        for (const auto &t : names.textures) {
            const std::size_t bytes = texel_bytes(t.param);
            const int w = 8 << ((t.param >> 20) & 7), h = 8 << ((t.param >> 23) & 7);
            if (t.offset + bytes > d.size() || bytes == 0)
                continue;
            // the palettes it is drawn with: the models', else the "<texture>_pl" one;
            // when neither names one, the first, and the file named by the texture alone
            std::vector<const Named *> with;
            bool known = true;
            if (t.format != 7) {
                const std::string base = t.name.substr(0, t.name.find('~'));
                const auto it = pairs.find(base);
                for (const auto &p : names.palettes) {
                    if ((it != pairs.end() && it->second.count(p.name)) || p.name == base + "_pl")
                        with.push_back(&p);
                }
                if (with.empty()) {
                    if (names.palettes.empty())
                        continue;
                    with.push_back(&names.palettes.front());
                    known = false;
                    ++unpaired;
                }
            }
            std::vector<u8> rgba(static_cast<std::size_t>(w) * h * 4);
            const std::size_t index_bytes = static_cast<std::size_t>(w) * h / 8; // 2 bytes a 4x4 block
            for (std::size_t k = 0; k < (with.empty() ? 1 : with.size()); ++k) {
                const Named *p = with.empty() ? nullptr : with[k];
                khdays_textures_decode(t.param, &d[t.offset], static_cast<u32>(bytes),
                                       t.format == 5 && t.extra + index_bytes <= d.size() ? &d[t.extra] : nullptr,
                                       static_cast<u32>(index_bytes),
                                       p != nullptr && p->offset + p->extra <= d.size() ? &d[p->offset] : nullptr,
                                       p != nullptr ? p->extra : 0, rgba.data());
                std::string name = label + "/tex/" + t.name;
                if (p != nullptr && known)
                    name += "@" + p->name;
                const std::string out = std::string(full) + "\\" + name + ".png";
                written += khdays_textures_write_png(out.c_str(), rgba.data(), w, h);
            }
        }
    });
    std::printf("graphics dump: %d textures, %d models, %d tile sheets and %d fonts from %d files into %s "
                "(%d textures with no known palette, under their names alone)\n",
                written, models, sheets, fonts, files, full, unpaired);
    return 0;
}

bool khdays_names_model_bmd0(const char *file, u32 offset, std::vector<unsigned char> &bmd0, std::size_t &index)
{
    std::string label;
    const std::vector<u8> d = file_data(file, label);
    for (std::size_t at = 0; at + 0x18 <= d.size(); at += 4) {
        if (std::memcmp(&d[at], "BMD0", 4) != 0 || le32(d, at + 8) > d.size() - at)
            continue;
        FileNames names;
        read_models(d, at, names);
        for (std::size_t m = 0; m < names.models.size(); ++m) {
            if (names.models[m].offset != offset)
                continue;
            bmd0.assign(d.begin() + static_cast<std::ptrdiff_t>(at),
                        d.begin() + static_cast<std::ptrdiff_t>(at + le32(d, at + 8)));
            index = m;
            return true;
        }
    }
    return false;
}

extern "C" int khdays_names_tiles(const char *file, u32 offset, char *out, int size, KhdaysTileSheet *sheet)
{
    const FileNames &names = names_of(file);
    for (const auto &s : names.sheets) {
        if (offset < s.sheet.tiles_at || offset - s.sheet.tiles_at >= s.sheet.tiles * s.sheet.tile_bytes)
            continue;
        if (s.name.size() + 1 > static_cast<std::size_t>(size))
            return 0;
        std::memcpy(out, s.name.c_str(), s.name.size() + 1);
        *sheet = s.sheet;
        return 1;
    }
    return 0;
}

extern "C" int khdays_names_dump_sheet(const char *file, u32 offset, const char *directory)
{
    const FileNames &names = names_of(file);
    for (const auto &s : names.sheets) {
        if (offset < s.sheet.tiles_at || offset - s.sheet.tiles_at >= s.sheet.tiles * s.sheet.tile_bytes)
            continue;
        return write_sheet(names.data, s, first_colours(names.data), directory);
    }
    return 0;
}

extern "C" int khdays_names_model(const char *file, u32 offset, char *out, int size)
{
    const FileNames &names = names_of(file);
    for (const auto &m : names.models) {
        if (m.offset != offset)
            continue;
        const std::string name = names.label + "/mdl/" + m.name;
        if (name.size() + 1 > static_cast<std::size_t>(size))
            return 0;
        std::memcpy(out, name.c_str(), name.size() + 1);
        return 1;
    }
    return 0;
}

extern "C" int khdays_names_animation(const char *file, u32 offset, char *out, int size)
{
    const FileNames &names = names_of(file);
    for (const auto &a : names.animations) {
        if (a.offset != offset)
            continue;
        if (a.name.size() + 1 > static_cast<std::size_t>(size))
            return 0;
        std::memcpy(out, a.name.c_str(), a.name.size() + 1);
        return 1;
    }
    return 0;
}

extern "C" int khdays_names_texture(const char *file, u32 offset, int format, const char *pltt_file, u32 pltt_offset,
                                    char *out, int size)
{
    const FileNames &tex = names_of(file);
    const Named *texture = nullptr;
    for (const auto &t : tex.textures) {
        if (t.offset == offset && (t.format == format || format < 0)) {
            texture = &t;
            break;
        }
    }
    if (texture == nullptr)
        return 0;
    std::string name = tex.label + "/tex/" + texture->name;
    if (pltt_file != nullptr) {
        const FileNames &pal = names_of(pltt_file);
        for (const auto &p : pal.palettes) {
            if (p.offset == pltt_offset) {
                name += "@" + p.name;
                break;
            }
        }
    }
    if (name.size() + 1 > static_cast<std::size_t>(size))
        return 0;
    std::memcpy(out, name.c_str(), name.size() + 1);
    return 1;
}
