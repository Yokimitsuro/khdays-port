#include "khdays/assets/kh2_map.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <utility>

// The map's models are the PS2's own packets: DMA tags (ID in bits 28-30, QWC
// in bits 0-15, ADDR in the upper word) whose second half and data are VIF
// codes (imm 16 bits, num 8, cmd 8) that UNPACK lists into the VU's memory.
// Each packet's first quadwords say where its lists went: q1 (UV/index/flag
// count, address, ...), q2 (colour count, address), q3 (position count,
// address, ...). A vertex's flag ends a triangle with the two before it: 0x20,
// 0x30 the same turned, 0x00 the same seen from both sides; 0x10 none.
// Characters' packets may also blend vertices from several bones (q2, q3[2]).

namespace khdays::assets::kh2 {

namespace {

class Bytes final {
public:
    Bytes(const std::uint8_t* data, std::size_t size) : data_(data), size_(size) {}

    std::size_t size() const {
        return size_;
    }
    const std::uint8_t* at(std::size_t o, std::size_t n) const {
        if (o > size_ || n > size_ - o) {
            throw std::runtime_error("KH2 map: read past the end of the file");
        }
        return data_ + o;
    }
    std::uint8_t u8(std::size_t o) const {
        return *at(o, 1);
    }
    std::uint16_t u16(std::size_t o) const {
        const auto* p = at(o, 2);
        return static_cast<std::uint16_t>(p[0] | (p[1] << 8U));
    }
    std::int16_t i16(std::size_t o) const {
        return static_cast<std::int16_t>(u16(o));
    }
    std::uint32_t u32(std::size_t o) const {
        const auto* p = at(o, 4);
        return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8U) |
               (static_cast<std::uint32_t>(p[2]) << 16U) | (static_cast<std::uint32_t>(p[3]) << 24U);
    }
    std::int32_t i32(std::size_t o) const {
        return static_cast<std::int32_t>(u32(o));
    }
    std::uint64_t u64(std::size_t o) const {
        return static_cast<std::uint64_t>(u32(o)) | (static_cast<std::uint64_t>(u32(o + 4)) << 32U);
    }
    float f32(std::size_t o) const {
        const std::uint32_t bits = u32(o);
        float f = 0.0F;
        std::memcpy(&f, &bits, sizeof f);
        return f;
    }

private:
    const std::uint8_t* data_;
    std::size_t size_;
};

float as_float(std::uint32_t bits) {
    float f = 0.0F;
    std::memcpy(&f, &bits, sizeof f);
    return f;
}

std::string hex(std::uint32_t value) {
    static const char* digits = "0123456789abcdef";
    std::string text;
    do {
        text.insert(text.begin(), digits[value & 15U]);
        value >>= 4U;
    } while (value != 0);
    return "0x" + text;
}

// --- VIF ---

using Quad = std::array<std::uint32_t, 4>;

// The VU memory a packet's VIF codes fill: 1024 quadwords. STCYCL is taken as
// one row per quadword (cl = wl = 1, the only cycle seen), so the mask's first
// row applies to every element.
class VuMemory final {
public:
    VuMemory() : quads_(1024, Quad{}) {}

    const Quad& operator[](std::size_t address) const {
        return quads_[address & 1023U];
    }

    void run(const Bytes& b, std::size_t p, const std::size_t end) {
        while (p + 4 <= end) {
            const std::uint32_t code = b.u32(p);
            p += 4;
            const auto imm = static_cast<std::uint32_t>(code & 0xFFFFU);
            const auto num = static_cast<std::uint32_t>((code >> 16U) & 0xFFU);
            const auto cmd = static_cast<std::uint32_t>((code >> 24U) & 0x7FU);
            if (cmd == 0x20) {  // STMASK
                mask_ = b.u32(p);
                p += 4;
            } else if (cmd == 0x30 || cmd == 0x31) {  // STROW, STCOL
                Quad& target = cmd == 0x30 ? row_ : col_;
                for (std::size_t i = 0; i < 4; ++i) {
                    target[i] = b.u32(p + i * 4);
                }
                p += 16;
            } else if (cmd == 0x4A) {  // MPG
                p += (num == 0 ? 256U : num) * 8U;
            } else if (cmd == 0x50 || cmd == 0x51) {  // DIRECT, DIRECTHL
                p += (imm == 0 ? 65536U : imm) * 16U;
            } else if (cmd >= 0x60) {
                p = unpack(b, p, imm, num, cmd);
            }
            // Others (NOP, STCYCL, OFFSET, BASE, ITOP, STMOD, MARK, FLUSH, MSCAL...) carry no data.
        }
    }

private:
    std::size_t unpack(const Bytes& b, std::size_t p, const std::uint32_t imm, const std::uint32_t num,
                       const std::uint32_t cmd) {
        const unsigned components = ((cmd >> 2U) & 3U) + 1U;
        const unsigned width_code = cmd & 3U;  // 0: 32 bits, 1: 16, 2: 8, 3: 5 (V4-5)
        const bool is_unsigned = ((imm >> 14U) & 1U) != 0;
        const bool masked = ((cmd >> 4U) & 1U) != 0;
        const std::uint32_t count = num == 0 ? 256U : num;
        const std::size_t element = width_code == 3 ? 2U : components * (4U >> width_code);
        for (std::uint32_t k = 0; k < count; ++k) {
            const std::size_t e = p + k * element;
            Quad values{};
            for (unsigned c = 0; c < components && width_code != 3; ++c) {
                if (width_code == 0) {
                    values[c] = b.u32(e + c * 4U);
                } else if (width_code == 1) {
                    values[c] = is_unsigned ? b.u16(e + c * 2U)
                                            : static_cast<std::uint32_t>(static_cast<std::int32_t>(b.i16(e + c * 2U)));
                } else {
                    const std::uint8_t byte = b.u8(e + c);
                    values[c] =
                        is_unsigned
                            ? byte
                            : static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int8_t>(byte)));
                }
            }
            if (components == 1) {
                values = {values[0], values[0], values[0], values[0]};
            }
            Quad& q = quads_[((imm & 0x3FFU) + k) & 1023U];
            for (unsigned i = 0; i < 4; ++i) {
                switch (masked ? (mask_ >> (2U * i)) & 3U : 0U) {
                case 0: q[i] = values[i]; break;
                case 1: q[i] = row_[i]; break;
                case 2: q[i] = col_[i]; break;
                default: break;  // write-protected
                }
            }
        }
        return p + ((count * element + 3U) & ~static_cast<std::size_t>(3U));
    }

    std::vector<Quad> quads_;
    std::uint32_t mask_ = 0;
    Quad row_{};
    Quad col_{};
};

// The triangles of one packet's lists. `bone_of` gives a position's bone
// (object models), empty for map models; `blended`, when not empty, the
// vertices the strip indexes instead of the positions.
void packet_triangles(const VuMemory& mem, const std::vector<int>& bone_of, const std::uint32_t index_mask,
                      const std::vector<std::vector<Influence>>& blended, std::vector<Triangle>& out) {
    const std::uint32_t count = mem[1][0];
    const std::uint32_t list = mem[1][1];
    const std::uint32_t colors = mem[2][0];
    const std::uint32_t color_list = mem[2][1];
    const std::uint32_t positions = mem[3][1];
    std::vector<Vertex> strip;
    strip.reserve(count);
    for (std::uint32_t k = 0; k < count; ++k) {
        const Quad& q = mem[list + k];
        Vertex v;
        v.uv = {static_cast<float>(static_cast<std::int16_t>(q[0] & 0xFFFFU)) / 4096.0F,
                static_cast<float>(static_cast<std::int16_t>(q[1] & 0xFFFFU)) / 4096.0F};
        const std::uint32_t index = q[2] & index_mask;
        const Quad& pos = mem[positions + index];
        v.position = {as_float(pos[0]), as_float(pos[1]), as_float(pos[2])};
        if (k < colors) {
            const Quad& c = mem[color_list + k];
            v.color = {static_cast<std::uint8_t>(c[0]), static_cast<std::uint8_t>(c[1]),
                       static_cast<std::uint8_t>(c[2]), static_cast<std::uint8_t>(c[3])};
        }
        if (!blended.empty()) {
            if (index >= blended.size() || blended[index].empty()) {
                throw std::runtime_error("KH2 model: strip vertex " + std::to_string(index) + " of " +
                                         std::to_string(blended.size()) + " blended ones");
            }
            v.influences = blended[index];
            const Influence& first = v.influences.front();
            v.bone = first.bone;
            v.position = {first.position[0] / first.position[3], first.position[1] / first.position[3],
                          first.position[2] / first.position[3]};
        } else if (index < bone_of.size()) {
            v.bone = bone_of[index];
        }
        strip.push_back(v);
        const std::uint32_t flag = q[3] & 0xF0U;
        if (k < 2 || flag == 0x10) {
            continue;
        }
        Triangle t;
        if (flag == 0x30) {
            t.corners = {strip[k - 1], strip[k - 2], strip[k]};
        } else {
            t.corners = {strip[k - 2], strip[k - 1], strip[k]};
        }
        t.two_sided = flag == 0x00;
        out.push_back(t);
    }
}

// A character packet's blended vertices (kn::SKLPOLYGONDATA::setVertexWithWeight,
// PS3): q2[2] weight groups (up to seven), their counts at q2[3]; group g holds that
// many vertices of g + 1 positions each, their indices listed from q3[2], each list
// on a quadword. Each position (x, y, z, weight) goes with its matrix's bone. Empty
// for a packet without weights.
std::vector<std::vector<Influence>> blended_vertices(const VuMemory& mem, const std::vector<int>& bone_of) {
    const std::uint32_t groups = mem[2][2];
    std::vector<std::vector<Influence>> out;
    if (groups == 0) {
        return out;
    }
    if (groups > 7) {
        throw std::runtime_error("KH2 model: " + std::to_string(groups) + " weight groups (at most 7)");
    }
    const std::uint32_t counts = mem[2][3];
    const std::uint32_t positions = mem[3][0];
    const std::uint32_t position_list = mem[3][1];
    std::uint32_t list = mem[3][2];
    for (std::uint32_t g = 0; g < groups; ++g) {
        const std::uint32_t vertices = mem[counts + g / 4U][g % 4U] & 0xFFU;
        for (std::uint32_t k = 0; k < vertices; ++k) {
            std::vector<Influence> influences;
            for (std::uint32_t i = 0; i <= g; ++i) {
                const std::uint32_t at = k * (g + 1U) + i;
                const std::uint32_t index = mem[list + at / 4U][at % 4U];
                if (index >= positions || index >= bone_of.size()) {
                    throw std::runtime_error("KH2 model: blended position " + std::to_string(index) + " of " +
                                             std::to_string(positions));
                }
                const Quad& pos = mem[position_list + index];
                influences.push_back(
                    {bone_of[index], {as_float(pos[0]), as_float(pos[1]), as_float(pos[2]), as_float(pos[3])}});
            }
            out.push_back(std::move(influences));
        }
        list += (vertices * (g + 1U) + 3U) / 4U;
    }
    return out;
}

void read_map_model(const Bytes& b, const std::size_t h, Model& model) {
    const std::uint32_t chains = b.u32(h + 0x10);
    for (std::uint32_t c = 0; c < chains; ++c) {
        const std::size_t entry = h + 0x20 + c * 16U;
        Group group;
        group.texture = static_cast<int>(b.u32(entry + 4));
        group.transparent = b.u16(entry + 0x0a) != 0;
        group.uv_option = b.u8(entry + 0x0c);
        std::size_t p = h + b.u32(entry);
        for (;;) {
            const std::uint64_t tag = b.u64(p);
            const auto qwc = static_cast<std::size_t>(tag & 0xFFFFU);
            const auto id = static_cast<unsigned>((tag >> 28U) & 7U);
            if (id == 6 || id == 7 || qwc == 0) {  // ret, end
                break;
            }
            if (id != 1) {
                throw std::runtime_error("KH2 map model: DMA tag ID " + std::to_string(id) + " in chain " +
                                         std::to_string(c) + " not read");
            }
            VuMemory mem;
            mem.run(b, p + 8, p + 16 + qwc * 16U);
            packet_triangles(mem, {}, 0xFFU, {}, group.triangles);
            p += 16 + qwc * 16U;
        }
        model.groups.push_back(std::move(group));
    }
}

void read_object_model(const Bytes& b, const std::size_t h, Model& model) {
    const std::uint16_t bones = b.u16(h + 0x10);
    const std::uint32_t bone_table = b.u32(h + 0x14);
    const std::uint16_t subparts = b.u16(h + 0x1c);
    for (std::uint16_t i = 0; i < bones; ++i) {
        const std::size_t o = h + bone_table + i * 0x40U;
        Bone bone;
        bone.parent = b.i32(o + 4);
        bone.no_envelope = (b.u32(o + 0xC) & 1U) != 0U;
        bone.scale = {b.f32(o + 0x10), b.f32(o + 0x14), b.f32(o + 0x18)};
        bone.length = b.f32(o + 0x1C);
        bone.rotation = {b.f32(o + 0x20), b.f32(o + 0x24), b.f32(o + 0x28)};
        bone.translation = {b.f32(o + 0x30), b.f32(o + 0x34), b.f32(o + 0x38)};
        model.bones.push_back(bone);
    }
    for (std::uint16_t s = 0; s < subparts; ++s) {
        const std::size_t entry = h + 0x20 + s * 0x20U;
        Group group;
        group.attribute = b.u32(entry);
        group.texture = static_cast<int>(b.u32(entry + 4));
        // its flags: 0x400000 its light added over the model (a glow)
        group.light = (b.u32(entry) & 0x400000U) != 0;
        std::size_t p = h + b.u32(entry + 0x10);
        // A packet's VIF stream gathered from its tags: ref tags (ID 3) bring
        // data from ADDR (from the header) or, with QWC 4, a bone's matrix (ADDR
        // = the bone) to the VU address their own UNPACK names; a cnt tag (ID 1)
        // brings its data inline and closes the packet.
        std::vector<std::uint8_t> stream;
        std::vector<std::pair<std::uint32_t, int>> matrices;  // (VU address, bone)
        for (;;) {
            const std::uint64_t tag = b.u64(p);
            const auto qwc = static_cast<std::size_t>(tag & 0xFFFFU);
            const auto id = static_cast<unsigned>((tag >> 28U) & 7U);
            const auto address = static_cast<std::size_t>(tag >> 32U);
            const std::uint8_t* codes = b.at(p + 8, 8);
            if (id == 3 && qwc == 4) {
                matrices.emplace_back(b.u32(p + 12) & 0x3FFU, static_cast<int>(address));
                p += 16;
                continue;
            }
            if (id == 3) {
                stream.insert(stream.end(), codes, codes + 8);
                const std::uint8_t* data = b.at(h + address, qwc * 16U);
                stream.insert(stream.end(), data, data + qwc * 16U);
                p += 16;
                continue;
            }
            if (id != 1) {
                break;  // ret, end: the subpart's last packet is done
            }
            stream.insert(stream.end(), codes, codes + 8);
            const std::uint8_t* data = b.at(p + 16, qwc * 16U);
            stream.insert(stream.end(), data, data + qwc * 16U);
            p += 16 + qwc * 16U;

            VuMemory mem;
            const Bytes packet(stream.data(), stream.size());
            mem.run(packet, 0, stream.size());
            // The affiliation list: how many positions go with each matrix, by VU address.
            const std::uint32_t affiliation = mem[1][2];
            const std::uint32_t matrix_count = mem[3][3];
            std::sort(matrices.begin(), matrices.end());
            std::vector<int> bone_of;
            for (std::uint32_t m = 0; m < matrix_count; ++m) {
                const std::uint32_t n = mem[affiliation + m / 4U][m % 4U];
                const int bone = m < matrices.size() ? matrices[m].second : 0;
                bone_of.insert(bone_of.end(), n, bone);
            }
            packet_triangles(mem, bone_of, 0xFFFFU, blended_vertices(mem, bone_of), group.triangles);
            stream.clear();
            matrices.clear();
        }
        model.groups.push_back(std::move(group));
    }
}

// A TEXA frame table from `at` to its Jump or Stop (or the chunk's end).
std::vector<TextureFrame> read_frames(const Bytes& b, std::size_t at, const std::size_t end, const std::size_t stride) {
    std::vector<TextureFrame> frames;
    while (at + 8 <= end) {
        const std::uint16_t control = b.u16(at);
        TextureFrame f;
        switch (control & 0xFU) {
        case 0: f.kind = TextureFrame::Kind::Sprite; break;
        case 1: f.kind = TextureFrame::Kind::Base; break;
        case 2: f.kind = TextureFrame::Kind::Jump; break;
        default: f.kind = TextureFrame::Kind::Stop; break;
        }
        f.jump = static_cast<std::int16_t>(control) >> 4;
        f.min_length = b.u16(at + 2);
        f.max_length = b.u16(at + 4);
        f.sprite = b.u16(at + 6);
        frames.push_back(f);
        if (f.kind == TextureFrame::Kind::Jump || f.kind == TextureFrame::Kind::Stop) {
            break;
        }
        at += stride;
    }
    return frames;
}

TextureAnimation read_texa(const Bytes& b, const std::size_t t, const std::size_t size) {
    TextureAnimation a;
    a.target = b.u16(t + 2);
    a.bits_per_pixel = b.u16(t + 6);
    const std::uint16_t first_slot = b.u16(t + 8), last_slot = b.u16(t + 10);
    const std::uint16_t animations = b.u16(t + 12);
    a.sprite_count = b.u16(t + 14);
    a.x = b.u16(t + 16) & 0xFFFU;
    a.y = b.u16(t + 18);
    a.width = b.u16(t + 20);
    a.height = b.u16(t + 22);
    const std::uint32_t slots = b.u32(t + 24), table = b.u32(t + 28), sprites = b.u32(t + 32);
    a.default_animation = static_cast<int>(b.u32(t + 36));
    // an entry is 8 bytes and a sprite index per further layer (kn::TEXAnmWork::play)
    const std::size_t stride = 8U + 2U * b.u16(t + 4);
    for (std::uint16_t i = 0; i < animations; ++i) {
        const std::uint32_t first = b.u32(t + table + i * 4U);
        a.animations.push_back(read_frames(b, t + first, t + size, stride));
    }
    // the slot table: per id from the first, its animation's number plus one (0: none)
    a.first_slot = first_slot;
    if (slots != 0 && last_slot >= first_slot) {
        for (unsigned id = first_slot; id <= last_slot; ++id) {
            const std::uint16_t n = b.u16(t + slots + (id - first_slot) * 2U);
            a.slot_animation.push_back(n != 0 && n <= animations ? n - 1 : -1);
        }
    }
    a.sprites_at = sprites != 0 ? t + sprites : 0;
    return a;
}

void wrap_region(const int mode, const int size, const int min, const int max, int& at, int& width) {
    if (mode == 2) {  // region clamp: MIN..MAX
        at = min;
        width = max - min + 1;
    } else if (mode == 3) {  // region repeat: (u & MIN) | MAX
        at = max;
        width = min + 1;
    } else {
        at = 0;
        width = size;
    }
    if (width <= 0) {
        at = 0;
        width = size;
    }
}

void unswizzle_palette(std::vector<std::array<std::uint8_t, 4>>& colors) {
    // An 8-bit picture's palette in the PS2's CSM1 order: in each block of 32,
    // entries 8-15 and 16-23 swapped.
    for (std::size_t block = 0; block + 32 <= colors.size(); block += 32) {
        for (std::size_t i = 8; i < 16; ++i) {
            std::swap(colors[block + i], colors[block + i + 8]);
        }
    }
}

// The keys' value at a time between the first key and the last.
float evaluate_keys(const std::vector<MotionKey>& keys, const float time) {
    if (time <= keys.front().time) {
        return keys.front().value;
    }
    for (std::size_t i = 0; i + 1 < keys.size(); ++i) {
        const MotionKey& k0 = keys[i];
        const MotionKey& k1 = keys[i + 1];
        if (time > k1.time) {
            continue;
        }
        if (k0.interpolation == 0) {
            return k0.value;
        }
        const float dt = k1.time - k0.time;
        const float s = dt > 0.0F ? (time - k0.time) / dt : 0.0F;
        if (k0.interpolation == 1) {
            return k0.value + (k1.value - k0.value) * s;
        }
        const float s2 = s * s;
        const float s3 = s2 * s;
        return (2 * s3 - 3 * s2 + 1) * k0.value + (s3 - 2 * s2 + s) * dt * k0.tangent_out +
               (-2 * s3 + 3 * s2) * k1.value + (s3 - s2) * dt * k1.tangent_in;
    }
    return keys.back().value;
}

}  // namespace

std::array<float, 2> region_uv(const GsTexture& t, float u, float v) {
    if ((t.wrap_u == 2 || t.wrap_u == 3) && t.region_width > 0) {
        u = t.wrap_u == 2 ? (u * static_cast<float>(t.width) - static_cast<float>(t.region_x)) /
                                static_cast<float>(t.region_width)
                          : u * static_cast<float>(t.width) / static_cast<float>(t.region_width);
    }
    if ((t.wrap_v == 2 || t.wrap_v == 3) && t.region_height > 0) {
        v = t.wrap_v == 2 ? (v * static_cast<float>(t.height) - static_cast<float>(t.region_y)) /
                                static_cast<float>(t.region_height)
                          : v * static_cast<float>(t.height) / static_cast<float>(t.region_height);
    }
    return {u, v};
}

std::optional<int> Group::scroll_index() const {
    if ((uv_option & 2U) == 0) {
        return std::nullopt;
    }
    return (uv_option >> 2U) & 15U;
}

TextureContainer read_textures(const std::uint8_t* data, const std::size_t size, const std::size_t at) {
    const Bytes b(data, size);
    TextureContainer c;
    const std::uint32_t colors = b.u32(at + 4);
    const std::uint32_t count = b.u32(at + 12);
    const std::uint32_t gs_infos = b.u32(at + 24);
    const std::uint32_t palette = b.u32(at + 32);
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::size_t g = at + gs_infos + i * 0xA0U;
        const std::uint64_t tex0 = b.u64(g + 112);
        const std::uint64_t clamp = b.u64(g + 128);
        GsTexture t;
        t.width = 1 << static_cast<int>((tex0 >> 26U) & 15U);
        t.height = 1 << static_cast<int>((tex0 >> 30U) & 15U);
        t.wrap_u = static_cast<int>(clamp & 3U);
        t.wrap_v = static_cast<int>((clamp >> 2U) & 3U);
        t.min_u = static_cast<int>((clamp >> 4U) & 1023U);
        t.max_u = static_cast<int>((clamp >> 14U) & 1023U);
        t.min_v = static_cast<int>((clamp >> 24U) & 1023U);
        t.max_v = static_cast<int>((clamp >> 34U) & 1023U);
        wrap_region(t.wrap_u, t.width, t.min_u, t.max_u, t.region_x, t.region_width);
        wrap_region(t.wrap_v, t.height, t.min_v, t.max_v, t.region_y, t.region_height);
        c.textures.push_back(t);
    }
    // The footer chunks after the palette: _DMY (padding), UVSC, TEXA, then _KN5.
    std::size_t p = at + palette + static_cast<std::size_t>(colors) * 4U;
    while (p + 4 <= size) {
        const auto* tag = b.at(p, 4);
        const std::string name(reinterpret_cast<const char*>(tag), 4);
        if (name == "_KN5") {
            break;
        }
        if (name != "_DMY" && name != "UVSC" && name != "TEXA") {
            break;  // a container without footer: the next entry's data
        }
        const std::uint32_t length = b.u32(p + 4);
        (void)b.at(p + 8, length);
        if (name == "UVSC" && length >= 12) {
            c.scrolls.push_back({b.u32(p + 8), b.f32(p + 12), b.f32(p + 16)});
        } else if (name == "TEXA") {
            c.animations.push_back(read_texa(b, p + 8, length));
        }
        p += 8 + length;
    }
    for (std::uint32_t i = 0; i < count; ++i) {
        c.hd_pictures.push_back(c.hd_count++);
        for (auto& a : c.animations) {
            if (a.target == static_cast<int>(i)) {
                a.hd_sheet = c.hd_count++;
            }
        }
    }
    return c;
}

namespace {

// The GS's local memory (4 MB of 32-bit words) as a container's pictures fill
// it. PSMCT32 pages are 64 x 32 pixels of 32 blocks of 8 x 8; PSMT8 pages 128
// x 64 of 16 x 16 blocks numbered the same, each block the same 256 bytes as
// a PSMCT32 block. Buffer widths (BW) count 64 pixels.
class GsMemory final {
public:
    GsMemory() : bytes_(4U * 1024U * 1024U, 0) {}

    std::uint8_t* ct32(const unsigned x, const unsigned y, const unsigned bp, const unsigned bw) {
        const unsigned page = (y / 32U) * bw + x / 64U;
        return word(bp, page, kBlocks[(y % 32U) / 8U][(x % 64U) / 8U], kColumns[y % 8U][x % 8U]);
    }

    std::uint8_t* t8(const unsigned x, const unsigned y, const unsigned bp, const unsigned bw) {
        const unsigned page = (y / 64U) * (bw / 2U) + x / 128U;
        // within a 16 x 16 block: which 32-bit word of the block's 8 x 8, and which of its bytes
        const unsigned bx = x % 16U, by = y % 16U;
        const unsigned swap = ((by + 2U) >> 2U & 1U) * 4U;
        const unsigned wy = ((by & ~3U) >> 1U) + (by & 1U);
        const unsigned wx = (bx + swap) & 7U;
        const unsigned byte = (by >> 1U & 1U) + (bx >> 2U & 2U);
        return word(bp, page, kBlocks[(y % 64U) / 16U][(x % 128U) / 16U], kColumns[wy & 7U][wx]) + byte;
    }

private:
    static constexpr unsigned kBlocks[4][8] = {{0, 1, 4, 5, 16, 17, 20, 21},
                                               {2, 3, 6, 7, 18, 19, 22, 23},
                                               {8, 9, 12, 13, 24, 25, 28, 29},
                                               {10, 11, 14, 15, 26, 27, 30, 31}};
    static constexpr unsigned kColumns[8][8] = {{0, 1, 4, 5, 8, 9, 12, 13},       {2, 3, 6, 7, 10, 11, 14, 15},
                                                {16, 17, 20, 21, 24, 25, 28, 29}, {18, 19, 22, 23, 26, 27, 30, 31},
                                                {32, 33, 36, 37, 40, 41, 44, 45}, {34, 35, 38, 39, 42, 43, 46, 47},
                                                {48, 49, 52, 53, 56, 57, 60, 61}, {50, 51, 54, 55, 58, 59, 62, 63}};

    std::uint8_t* word(const unsigned bp, const unsigned page, const unsigned block, const unsigned column) {
        const std::size_t w = (static_cast<std::size_t>(bp) * 64U + page * 2048U + block * 64U + column) & 0xFFFFFU;
        return bytes_.data() + w * 4U;
    }

    std::vector<std::uint8_t> bytes_;
};

std::uint64_t field(const std::uint64_t reg, const unsigned low, const unsigned bits) {
    return (reg >> low) & ((std::uint64_t{1} << bits) - 1U);
}

}  // namespace

namespace {

// The container's pictures uploaded into a model of the GS's memory, as its packets send them
GsMemory upload_pictures(const Bytes& b, const std::size_t at) {
    const std::uint32_t pictures = b.u32(at + 8);
    const std::size_t sends = at + b.u32(at + 20);
    GsMemory gs;
    // Each packet: BITBLTBUF at 0x20, TRXPOS at 0x30, TRXREG at 0x40, and the
    // image's DMA tag at 0x70 (its address, from the container, at 0x74).
    for (std::uint32_t p = 0; p <= pictures; ++p) {
        const std::size_t s = sends + p * 0x90U;
        const std::uint64_t bitblt = b.u64(s + 0x20), trxpos = b.u64(s + 0x30), trxreg = b.u64(s + 0x40);
        const auto bp = static_cast<unsigned>(field(bitblt, 32, 14)), bw = static_cast<unsigned>(field(bitblt, 48, 6));
        const auto psm = static_cast<unsigned>(field(bitblt, 56, 6));
        const auto x0 = static_cast<unsigned>(field(trxpos, 32, 11)), y0 = static_cast<unsigned>(field(trxpos, 48, 11));
        const auto w = static_cast<unsigned>(field(trxreg, 0, 12)), h = static_cast<unsigned>(field(trxreg, 32, 12));
        const std::size_t pixels = at + b.u32(s + 0x74);
        if (psm == 0x00) {  // PSMCT32
            // the palettes' upload sends a whole 64 x 32 block though the file keeps only its colours (W_EX010:
            // 976, 3,904 of 8,192 bytes, the file ending before the rest): what the file has, the rest left 0
            const std::size_t texels = std::size_t{w} * h;
            const std::size_t kept = pixels < b.size() ? std::min(texels, (b.size() - pixels) / 4U) : 0U;
            const std::uint8_t* src = b.at(pixels, kept * 4U);
            for (std::size_t i = 0; i < kept; ++i, src += 4) {
                std::memcpy(gs.ct32(x0 + static_cast<unsigned>(i % w), y0 + static_cast<unsigned>(i / w), bp, bw), src,
                            4);
            }
        } else if (psm == 0x13) {  // PSMT8
            const std::uint8_t* src = b.at(pixels, std::size_t{w} * h);
            for (unsigned y = 0; y < h; ++y) {
                for (unsigned x = 0; x < w; ++x) {
                    *gs.t8(x0 + x, y0 + y, bp, bw) = *src++;
                }
            }
        } else {
            throw std::runtime_error("KH2 textures: picture " + std::to_string(p) + " sent as GS format " + hex(psm) +
                                     ", not read");
        }
    }
    return gs;
}

// GS info `index`'s TEX0, checked to be an 8-bit texture with a 32-bit palette
std::uint64_t texture_register(const Bytes& b, const std::size_t at, const std::size_t index) {
    const std::uint32_t infos = b.u32(at + 12);
    if (index >= infos) {
        throw std::runtime_error("KH2 textures: no GS info " + std::to_string(index) + " of " + std::to_string(infos));
    }
    const std::uint64_t tex0 = b.u64(at + b.u32(at + 24) + index * 0xA0U + 112U);
    const auto psm = static_cast<unsigned>(field(tex0, 20, 6));
    if (psm != 0x13 || field(tex0, 51, 4) != 0 || field(tex0, 55, 1) != 0) {
        throw std::runtime_error("KH2 textures: GS info " + std::to_string(index) + " is GS format " + hex(psm) +
                                 " (palette " + hex(static_cast<std::uint32_t>(field(tex0, 51, 4))) +
                                 "); only 8-bit textures with 32-bit palettes are decoded");
    }
    return tex0;
}

// The palette (CSM1) a TEX0 names: 16 x 16 PSMCT32 at CBP, entries 8-15 and 16-23 of each 32 swapped
std::array<std::array<std::uint8_t, 4>, 256> palette_of(GsMemory& gs, const std::uint64_t tex0) {
    const auto cbp = static_cast<unsigned>(field(tex0, 37, 14));
    std::array<std::array<std::uint8_t, 4>, 256> palette{};
    for (unsigned k = 0; k < 256; ++k) {
        const unsigned at_k = (k & ~0x18U) | ((k & 8U) << 1U) | ((k & 16U) >> 1U);
        const std::uint8_t* c = gs.ct32(at_k % 16U, at_k / 16U, cbp, 1);
        palette[k] = {c[0], c[1], c[2], static_cast<std::uint8_t>(std::min(255, c[3] * 2))};
    }
    return palette;
}

}  // namespace

struct TexturePictures::Memory final {
    GsMemory gs;
};

TexturePictures::TexturePictures(const std::uint8_t* data, const std::size_t size, const std::size_t at)
    : data_(data), size_(size), at_(at) {}

TexturePictures::~TexturePictures() = default;

TexturePictures::Memory& TexturePictures::memory() {
    if (!memory_) {
        memory_ = std::make_unique<Memory>(Memory{upload_pictures(Bytes(data_, size_), at_)});
    }
    return *memory_;
}

DecodedTexture TexturePictures::picture(const std::size_t index) {
    // kn::Texture (KH2's PS3 build): u16 type, u16 flag, u32 counts of colours,
    // pictures and GS infos, then the offsets of the pictures' numbers, their
    // send packets (0x90 bytes each; the palette's first), the GS infos (0xA0),
    // the pixels and the palette.
    const std::uint64_t tex0 = texture_register(Bytes(data_, size_), at_, index);
    const int width_log = static_cast<int>(field(tex0, 26, 4)), height_log = static_cast<int>(field(tex0, 30, 4));
    if (width_log > 10 || height_log > 10) {  // TEX0's TW and TH: the GS's own limit
        throw std::runtime_error("KH2 textures: GS info " + std::to_string(index) + " is " +
                                 std::to_string(1 << width_log) + " x " + std::to_string(1 << height_log) +
                                 ", past the GS's 1024 x 1024; the file is damaged: extract it again from your game");
    }
    GsMemory& gs = memory().gs;
    const auto tbp = static_cast<unsigned>(field(tex0, 0, 14)), tbw = static_cast<unsigned>(field(tex0, 14, 6));
    const auto palette = palette_of(gs, tex0);
    DecodedTexture out;
    out.format_name = "kh2 psmt8";
    out.width = 1 << width_log;
    out.height = 1 << height_log;
    out.rgba.reserve(static_cast<std::size_t>(out.width) * static_cast<std::size_t>(out.height) * 4U);
    for (int y = 0; y < out.height; ++y) {
        for (int x = 0; x < out.width; ++x) {
            const auto& c = palette[*gs.t8(static_cast<unsigned>(x), static_cast<unsigned>(y), tbp, tbw)];
            out.rgba.insert(out.rgba.end(), c.begin(), c.end());
        }
    }
    return out;
}

std::vector<std::vector<std::uint8_t>> TexturePictures::sprites(const TextureAnimation& animation) {
    const Bytes b(data_, size_);
    std::vector<std::vector<std::uint8_t>> out;
    if (animation.sprites_at == 0 || animation.sprite_count <= 0 || animation.width <= 0 || animation.height <= 0) {
        return out;
    }
    if (animation.bits_per_pixel != 8) {
        throw std::runtime_error("KH2 textures: a texture animation's sprites are " +
                                 std::to_string(animation.bits_per_pixel) + "-bit; only 8-bit sprites are decoded");
    }
    const std::uint64_t tex0 = texture_register(b, at_, static_cast<std::size_t>(animation.target));
    const auto palette = palette_of(memory().gs, tex0);
    const std::size_t pixels = static_cast<std::size_t>(animation.width) * static_cast<std::size_t>(animation.height);
    for (int n = 0; n < animation.sprite_count; ++n) {
        const std::uint8_t* src = b.at(animation.sprites_at + static_cast<std::size_t>(n) * pixels, pixels);
        std::vector<std::uint8_t> rgba;
        rgba.reserve(pixels * 4U);
        for (std::size_t k = 0; k < pixels; ++k) {
            const auto& c = palette[src[k]];
            rgba.insert(rgba.end(), c.begin(), c.end());
        }
        out.push_back(std::move(rgba));
    }
    return out;
}

std::vector<DecodedTexture> read_tim2(const std::uint8_t* data, const std::size_t size, const std::size_t at) {
    const Bytes b(data, size);
    const auto* magic = b.at(at, 4);
    if (std::memcmp(magic, "TIM2", 4) != 0) {
        throw std::runtime_error("KH2 map: not a TIM2 picture");
    }
    const std::uint16_t count = b.u16(at + 6);
    std::size_t p = at + 16;
    std::vector<DecodedTexture> out;
    for (std::uint16_t n = 0; n < count; ++n) {
        std::uint32_t total = b.u32(p);
        std::uint32_t palette_size = b.u32(p + 4);
        const std::uint32_t image_size = b.u32(p + 8);
        const std::uint16_t header = b.u16(p + 12);
        const std::uint16_t palette_colors = b.u16(p + 14);
        const std::uint8_t palette_type = b.u8(p + 18);
        const std::uint8_t image_type = b.u8(p + 19);
        const int width = b.u16(p + 20);
        const int height = b.u16(p + 22);
        // The radars' total and palette sizes are not kept (0, 0xfffffff7..0xfffffffd
        // seen): the palette is there anyway, its size by its colour count.
        if (palette_size == 0 || palette_size >= 0x80000000U) {
            palette_size = palette_colors * 4U;
        }
        if (total == 0 || total >= 0x80000000U) {
            total = header + image_size + palette_size;
        }
        if ((image_type == 4 || image_type == 5) && (palette_type & 7U) != 3) {
            throw std::runtime_error("KH2 map: TIM2 palette of type " + std::to_string(palette_type) +
                                     " not read (32-bit colours only)");
        }
        const std::uint8_t* image = b.at(p + header, image_size);
        DecodedTexture t;
        t.name = "tim2_" + std::to_string(n);
        t.format_name = "TIM2";
        t.width = width;
        t.height = height;
        t.rgba.resize(static_cast<std::size_t>(width) * height * 4U);
        const std::size_t pixels = static_cast<std::size_t>(width) * height;
        if (image_type == 4 || image_type == 5) {
            const std::uint8_t* pal = b.at(p + header + image_size, palette_size);
            std::vector<std::array<std::uint8_t, 4>> colors(palette_size / 4U);
            for (std::size_t i = 0; i < colors.size(); ++i) {
                colors[i] = {pal[i * 4], pal[i * 4 + 1], pal[i * 4 + 2], pal[i * 4 + 3]};
            }
            if (image_type == 5 && (palette_type & 0x80U) == 0) {
                unswizzle_palette(colors);
            }
            const std::size_t need = image_type == 4 ? (pixels + 1) / 2 : pixels;
            if (image_size < need) {
                throw std::runtime_error("KH2 map: TIM2 picture cut short");
            }
            for (std::size_t i = 0; i < pixels; ++i) {
                const std::size_t index = image_type == 4 ? ((image[i / 2] >> ((i & 1U) * 4U)) & 15U) : image[i];
                const auto c = index < colors.size() ? colors[index] : std::array<std::uint8_t, 4>{};
                std::copy(c.begin(), c.end(), t.rgba.begin() + static_cast<std::ptrdiff_t>(i * 4));
            }
        } else if (image_type == 3) {
            if (image_size < pixels * 4) {
                throw std::runtime_error("KH2 map: TIM2 picture cut short");
            }
            std::copy(image, image + pixels * 4, t.rgba.begin());
        } else {
            throw std::runtime_error("KH2 map: TIM2 picture type " + std::to_string(image_type) + " not read");
        }
        for (std::size_t i = 0; i < pixels; ++i) {
            t.rgba[i * 4 + 3] = static_cast<std::uint8_t>(std::min(255, t.rgba[i * 4 + 3] * 2));
        }
        out.push_back(std::move(t));
        p += total;
    }
    return out;
}

Model read_model(const std::uint8_t* data, const std::size_t size, const std::size_t at) {
    const Bytes b(data, size);
    const std::size_t h = at + 0x90;  // after the 0x90 bytes the game keeps for itself
    Model model;
    model.kind = static_cast<int>(b.u32(h));
    if (model.kind == 2) {
        read_map_model(b, h, model);
    } else if (model.kind == 3) {
        read_object_model(b, h, model);
    } else {
        throw std::runtime_error("KH2 map: model kind " + std::to_string(model.kind) + " not read");
    }
    return model;
}

std::vector<Placement> read_placements(const std::uint8_t* data, const std::size_t size, const std::size_t at) {
    const Bytes b(data, size);
    if (b.u32(at) != 8) {
        throw std::runtime_error("KH2 map: placement table of kind " + std::to_string(b.u32(at)) + " not read");
    }
    const std::uint32_t length = b.u32(at + 4);
    std::vector<Placement> rows;
    for (std::size_t o = at + 8; o + 0x68 <= at + 8 + length; o += 0x68) {
        Placement row;
        row.position = {b.f32(o), b.f32(o + 4), b.f32(o + 8)};
        row.rotation = {b.f32(o + 12), b.f32(o + 16), b.f32(o + 20)};
        row.scale = {b.f32(o + 24), b.f32(o + 28), b.f32(o + 32)};
        row.piece = static_cast<int>(b.u32(o + 36));
        row.motion = b.i32(o + 44);
        rows.push_back(row);
    }
    return rows;
}

namespace {

Motion read_motion(const std::uint8_t* data, const std::size_t size, const std::size_t at) {
    const Bytes b(data, size);
    const std::size_t base = at + 0x90;  // the tables' offsets count from here
    const std::uint32_t kind = b.u32(base);
    if (kind != 0) {
        throw std::runtime_error("KH2 map: motion kind " + std::to_string(kind) + " not read (interpolated only)");
    }
    const std::size_t h = base + 16;
    Motion m;
    const std::int32_t pose_at = b.i32(h + 0x14);
    const std::int32_t pose_count = b.i32(h + 0x18);
    const std::int32_t curves_at = b.i32(h + 0x20);
    const std::int32_t curve_count = b.i32(h + 0x24);
    const std::int32_t keys_at = b.i32(h + 0x30);
    const std::int32_t times_at = b.i32(h + 0x34);
    const std::int32_t values_at = b.i32(h + 0x38);
    const std::int32_t tangents_at = b.i32(h + 0x3c);
    m.has_constraints = b.i32(h + 0x44) > 0 || b.i32(h + 0x54) > 0;
    m.start = b.f32(h + 0x80);
    m.end = b.f32(h + 0x84);
    m.fps = b.f32(h + 0x88);
    m.loop_from = b.f32(h + 0x8c);
    for (std::int32_t i = 0; i < pose_count; ++i) {
        const std::size_t o = base + static_cast<std::size_t>(pose_at) + static_cast<std::size_t>(i) * 8U;
        m.pose.push_back({b.i16(o), b.i16(o + 2), b.f32(o + 4)});
    }
    for (std::int32_t i = 0; i < curve_count; ++i) {
        const std::size_t o = base + static_cast<std::size_t>(curves_at) + static_cast<std::size_t>(i) * 6U;
        MotionCurve curve;
        curve.joint = b.i16(o);
        const std::uint8_t bits = b.u8(o + 2);
        curve.channel = bits & 15U;
        curve.pre = (bits >> 4U) & 3U;
        curve.post = (bits >> 6U) & 3U;
        const std::uint8_t key_count = b.u8(o + 3);
        const std::uint16_t first = b.u16(o + 4);
        for (std::uint32_t k = 0; k < key_count; ++k) {
            const std::size_t ko = base + static_cast<std::size_t>(keys_at) + (first + k) * 8U;
            const std::uint16_t time_word = b.u16(ko);
            MotionKey key;
            key.interpolation = time_word & 3U;
            key.time = b.f32(base + static_cast<std::size_t>(times_at) + (time_word >> 2U) * 4U);
            key.value =
                b.f32(base + static_cast<std::size_t>(values_at) + static_cast<std::size_t>(b.i16(ko + 2)) * 4U);
            key.tangent_in =
                b.f32(base + static_cast<std::size_t>(tangents_at) + static_cast<std::size_t>(b.i16(ko + 4)) * 4U);
            key.tangent_out =
                b.f32(base + static_cast<std::size_t>(tangents_at) + static_cast<std::size_t>(b.i16(ko + 6)) * 4U);
            curve.keys.push_back(key);
        }
        m.curves.push_back(std::move(curve));
    }
    return m;
}

}  // namespace

std::vector<Motion> read_motions(const std::uint8_t* data, const std::size_t size, const std::size_t at) {
    std::vector<Motion> out;
    for (const auto& e : bar_entries(data, size, at)) {
        if (e.type == kEntryMotion && e.size != 0) {
            out.push_back(read_motion(data, size, e.offset));
        }
    }
    return out;
}

float evaluate(const MotionCurve& curve, const float time) {
    if (curve.keys.empty()) {
        return 0.0F;
    }
    const MotionKey& first = curve.keys.front();
    const MotionKey& last = curve.keys.back();
    if (time >= first.time && time <= last.time) {
        return evaluate_keys(curve.keys, time);
    }
    // Before the first key or after the last: hold (0), offset (1), repeat (2), zero (3).
    const int type = time < first.time ? curve.pre : curve.post;
    const float span = last.time - first.time;
    if (type == 3) {
        return 0.0F;
    }
    if (type == 0 || span <= 0.0F) {
        return time < first.time ? first.value : last.value;
    }
    const float cycles = std::floor((time - first.time) / span);
    const float value = evaluate_keys(curve.keys, time - cycles * span);
    return type == 1 ? value + cycles * (last.value - first.value) : value;
}

std::vector<CollisionFace> read_collision(const std::uint8_t* data, const std::size_t size, const std::size_t at) {
    const Bytes b(data, size);
    if (std::memcmp(b.at(at, 4), "COCT", 4) != 0) {
        throw std::runtime_error("KH2 map: not a COCT collision");
    }
    const std::size_t faces_at = at + b.u32(at + 40);
    const std::size_t face_count = b.u32(at + 44) / 16U;
    const std::size_t vertices_at = at + b.u32(at + 48);
    const std::size_t vertex_count = b.u32(at + 52) / 16U;
    const std::size_t flags_at = at + b.u32(at + 72);
    const std::size_t flag_count = b.u32(at + 76) / 4U;
    auto corner = [&](const int index) -> std::array<float, 3> {
        if (index < 0 || static_cast<std::size_t>(index) >= vertex_count) {
            throw std::runtime_error("KH2 map: COCT vertex " + std::to_string(index) + " out of range");
        }
        const std::size_t o = vertices_at + static_cast<std::size_t>(index) * 16U;
        return {b.f32(o), -b.f32(o + 4), -b.f32(o + 8)};
    };
    std::vector<CollisionFace> faces;
    for (std::size_t i = 0; i < face_count; ++i) {
        const std::size_t o = faces_at + i * 16U;
        const int v[4] = {b.i16(o + 2), b.i16(o + 4), b.i16(o + 6), b.i16(o + 8)};
        const std::uint16_t flags = b.u16(o + 14);
        CollisionFace face;
        face.surface = flags < flag_count ? b.u32(flags_at + flags * 4U) : 0;
        face.corners = {corner(v[0]), corner(v[1]), corner(v[2])};
        faces.push_back(face);
        if (v[3] >= 0) {
            face.corners = {corner(v[0]), corner(v[2]), corner(v[3])};
            faces.push_back(face);
        }
    }
    return faces;
}

MapFile read_map(const std::uint8_t* data, const std::size_t size) {
    MapFile map;
    map.entries = bar_entries(data, size, 0);
    const auto& entries = map.entries;
    std::vector<bool> claimed(entries.size(), false);
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const BarEntry& e = entries[i];
        if (e.type == kEntryModel && e.size != 0) {
            MapPart part;
            part.name = e.name;
            part.model_entry = i;
            std::optional<std::size_t> textures;
            // Its textures and motions follow it, before the next model.
            for (std::size_t j = i + 1; j < entries.size() && entries[j].type != kEntryModel; ++j) {
                if (!textures && entries[j].type == kEntryTextures && entries[j].name == e.name && !claimed[j]) {
                    textures = j;
                } else if (!part.motion_entry && (entries[j].type & 0xFFFFU) == kEntryMotions && entries[j].size != 0 &&
                           is_bar(data, size, entries[j].offset)) {
                    part.motion_entry = j;
                }
            }
            for (std::size_t j = 0; !textures && j < entries.size(); ++j) {
                if (entries[j].type == kEntryTextures && entries[j].name == e.name && !claimed[j]) {
                    textures = j;
                }
            }
            if (!textures) {
                throw std::runtime_error("KH2 map: no textures (entry 0x07 " + e.name + ") for model " +
                                         std::to_string(i));
            }
            claimed[*textures] = true;
            part.texture_entry = *textures;
            map.parts.push_back(std::move(part));
        } else if (e.type == kEntryPlacements && !map.placements && e.size >= 8) {
            map.placements = i;
        } else if (e.type == kEntryCollision && !map.collision && e.name.rfind("ID_", 0) == 0 && e.size != 0) {
            map.collision = i;
        } else if (e.type == kEntryRadar && e.size != 0) {
            map.radar.push_back(i);
        }
    }
    const auto background =
        std::find_if(map.parts.begin(), map.parts.end(), [](const MapPart& part) { return part.name == "MAP"; });
    if (background == map.parts.end()) {
        throw std::runtime_error("KH2 map: no background model (entry " + hex(kEntryModel) + " MAP)");
    }
    std::rotate(map.parts.begin(), background, background + 1);
    // The HD pictures run through every texture container in BAR order.
    for (std::size_t i = 0; i < entries.size(); ++i) {
        if (entries[i].type != kEntryTextures || entries[i].size == 0) {
            continue;
        }
        for (auto& part : map.parts) {
            if (part.texture_entry == i) {
                part.first_hd_picture = map.hd_pictures;
            }
        }
        map.hd_pictures += read_textures(data, size, entries[i].offset).hd_count;
    }
    return map;
}

}  // namespace khdays::assets::kh2
