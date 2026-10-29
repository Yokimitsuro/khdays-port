// The game's loaded rooms as a secondary-motion environment (days_environment.h). Layouts from the
// decompilation: the entity manager (gEntityMgr, EntityManager_ReleaseViews, Entity_LoadAndAttach),
// its eight model groups (GetTrackEntryBase = manager + 4 + i * 8: u16 flags, u16 count, u32 blobs;
// ModelGroup_Open), a collision blob's header (Coll_ResolveModelBlobPointers), its face records
// (RoomPoly_BuildPlanes*, Coll_TestRayFace, Coll_TestSweptSphereFace) and room boxes
// (RoomBox_UpdateBounds). Only memory is read: no cast, no callback. docs/SECONDARY_MOTION.md.

#include "days_environment.h"

extern "C" {
#include "../../core/window.h"
#include "khdays_ds_symbols.h"
}

#include <algorithm>
#include <cmath>
#include <cstring>

namespace khdays::secondary {

namespace {

constexpr std::uint32_t kGroups = 8;
constexpr std::uint32_t kBlobReady = 0x8000;     // header +0x74: its pointers resolved
constexpr std::uint16_t kFaceDisabled = 0x4000;  // a wall that opened (Ov002_SetWidgetHidden)
constexpr std::uint8_t kPassThrough = 5;         // a named record's tag the camera passes through
constexpr std::uint32_t kBoxVtable = 0x02042928; // a room box (kind 3)
constexpr std::uint32_t kMaxFaces = 20000, kMaxNodes = 8192;

bool ram(std::uint32_t at, std::uint32_t size = 4)
{
    return at != 0 && khdays_game_ram(at) && khdays_game_ram(at + size - 1);
}

std::uint32_t u32(std::uint32_t at)
{
    std::uint32_t v = 0;
    std::memcpy(&v, KHDAYS_W(at), 4);
    return v;
}
std::uint16_t u16(std::uint32_t at)
{
    std::uint16_t v = 0;
    std::memcpy(&v, KHDAYS_W(at), 2);
    return v;
}
std::int16_t s16(std::uint32_t at)
{
    return static_cast<std::int16_t>(u16(at));
}
std::int32_t s32(std::uint32_t at)
{
    return static_cast<std::int32_t>(u32(at));
}
std::uint8_t u8(std::uint32_t at)
{
    return *static_cast<const std::uint8_t *>(KHDAYS_W(at));
}
float fx(std::int32_t v)
{
    return static_cast<float>(v) / 4096.0F;
}

// the blob of model group `g`, or 0
std::uint32_t blob_of(std::uint32_t manager, std::uint32_t g)
{
    const std::uint32_t group = manager + 4 + g * 8;
    if (!ram(group, 8) || u16(group + 2) == 0)
        return 0;
    const std::uint32_t list = u32(group + 4);
    if (!ram(list))
        return 0;
    const std::uint32_t blob = u32(list); // the casts test the first model only (Collision_RunRayCast)
    if (!ram(blob, 0xb0) || !(u16(blob + 0x74) & kBlobReady))
        return 0;
    return blob;
}

} // namespace

void DaysEnvironment::build(Room &room)
{
    const std::uint32_t b = room.blob;
    const Vec3 offset{fx(s32(b + 0x64)), fx(s32(b + 0x68)), fx(s32(b + 0x6c))}; // model = world + offset
    // named records carrying the pass-through tag
    std::vector<std::uint8_t> passes;
    const std::uint32_t names = u32(b + 0xac), name_count = u16(b + 0x82);
    if (ram(names, name_count * 0x14U)) {
        passes.resize(name_count, 0);
        for (std::uint32_t n = 0; n < name_count; ++n)
            for (std::uint32_t t = 0; t < 4; ++t)
                passes[n] = passes[n] || u8(names + n * 0x14 + 0xc + t) == kPassThrough;
    }
    std::vector<Triangle> tris;
    room.face.clear();
    struct Set {
        std::uint32_t table, count, size;
        Surface kind;
    };
    const Set sets[3] = {{u32(b + 0xa0), u16(b + 0x7c), 0x88, Surface::ground},
                         {u32(b + 0xa4), u16(b + 0x7e), 0x84, Surface::wall},
                         {u32(b + 0xa8), u16(b + 0x80), 0x84, Surface::ceiling}};
    for (const Set &s : sets) {
        if (s.count == 0 || s.count > kMaxFaces || !ram(s.table, s.count * s.size))
            continue;
        for (std::uint32_t f = 0; f < s.count; ++f) {
            const std::uint32_t face = s.table + f * s.size;
            const std::uint32_t corners = u16(face + 0x12);
            if (corners < 3 || corners > 4)
                continue;
            bool through = false;
            for (std::uint32_t k = 1; k < 3; ++k) { // slots 1 and 2 name records (Ov002_CollCastResolvePassThrough)
                const std::uint8_t slot = u8(face + 0x80 + k);
                through = through || (slot != 0xff && slot < passes.size() && passes[slot]);
            }
            if (through)
                continue;
            Vec3 p[4];
            for (std::uint32_t k = 0; k < corners; ++k) {
                const std::uint32_t v = face + 0x50 + k * 12;
                p[k] = Vec3{fx(s32(v)), fx(s32(v + 4)), fx(s32(v + 8))} - offset;
            }
            const Vec3 n = normalized(Vec3{static_cast<float>(s16(face + 0x14)), static_cast<float>(s16(face + 0x16)),
                                           static_cast<float>(s16(face + 0x18))});
            // wound to face where a character stands; keep the stored normal's side
            const auto add = [&](Vec3 a, Vec3 c, Vec3 d) {
                if (length(cross(c - a, d - a)) < 1e-8F)
                    return;
                tris.push_back({a, c, d, n, s.kind});
                room.face.push_back(face);
            };
            add(p[0], p[1], p[2]);
            if (corners == 4)
                add(p[0], p[2], p[3]);
        }
    }
    room.bvh.build(std::move(tris));
}

void DaysEnvironment::refresh()
{
    const std::uint32_t manager = ram(KHDAYS_DS_gEntityMgr) ? u32(KHDAYS_DS_gEntityMgr) : 0;
    std::vector<Room> next;
    bool changed = manager != manager_;
    manager_ = manager;
    if (manager != 0 && ram(manager, 0x44)) {
        for (std::uint32_t g = 0; g < kGroups; ++g) {
            const std::uint32_t blob = blob_of(manager, g);
            if (blob == 0)
                continue;
            // what identifies the room's data: a freed and reloaded room can come back at the same address
            const std::uint32_t key[8] = {u32(blob + 0x7c), u32(blob + 0x80), u32(blob + 0x84), u32(blob + 0x88),
                                          u32(blob + 0x8c), u32(blob + 0xa0), u32(blob + 0xa4), u32(blob + 0xa8)};
            auto same = std::find_if(rooms_.begin(), rooms_.end(), [&](const Room &r) {
                return r.blob == blob && std::memcmp(r.key, key, sizeof(key)) == 0;
            });
            if (same != rooms_.end()) {
                next.push_back(std::move(*same));
                rooms_.erase(same);
                continue;
            }
            Room r;
            r.blob = blob;
            std::memcpy(r.key, key, sizeof(key));
            build(r);
            next.push_back(std::move(r));
            changed = true;
        }
    }
    changed = changed || !rooms_.empty(); // rooms left over were unloaded
    rooms_ = std::move(next);
    // room boxes (opening walls, moving blocks) from each room's tree nodes: their place now
    std::vector<Box> boxes;
    for (const Room &r : rooms_) {
        const std::uint32_t tree = u32(r.blob + 0x9c), nodes = u16(r.blob + 0x7a);
        if (nodes == 0 || nodes > kMaxNodes || !ram(tree, nodes * 0x20U))
            continue;
        for (std::uint32_t n = 0; n < nodes; ++n) {
            std::uint32_t obj = u32(tree + n * 0x20 + 8);
            for (int guard = 0; ram(obj, 0x48) && guard < 256; ++guard, obj = u32(obj + 4)) {
                if (u32(obj + 0x1c) != kBoxVtable)
                    continue; // characters' cylinders are not cloth colliders
                Box x;
                x.pos = {fx(s32(obj + 0x2c)), fx(s32(obj + 0x30)), fx(s32(obj + 0x34))};
                x.half_w = fx(s32(obj + 0x38)) * 0.5F;
                x.height = fx(s32(obj + 0x3c));
                x.half_d = fx(s32(obj + 0x40)) * 0.5F;
                x.angle = static_cast<float>(s32(obj + 0x44) & 0xffff) * (2.0F * kPi / 65536.0F);
                boxes.push_back(x);
            }
        }
    }
    if (boxes.size() != boxes_.size() ||
        !std::equal(boxes.begin(), boxes.end(), boxes_.begin(), [](const Box &a, const Box &b) {
            return a.pos.x == b.pos.x && a.pos.y == b.pos.y && a.pos.z == b.pos.z && a.angle == b.angle &&
                   a.height == b.height;
        }))
        changed = true;
    boxes_ = std::move(boxes);
    if (changed)
        ++generation_;
}

void DaysEnvironment::gather(const Aabb &box, std::vector<Triangle> &out)
{
    // the box in world units
    Aabb world;
    world.add(box.lo * scale_);
    world.add(box.hi * scale_);
    const float to_sim = 1.0F / scale_;
    for (const Room &r : rooms_) {
        hits_.clear();
        r.bvh.query(world, hits_);
        for (const int i : hits_) {
            if (u16(r.face[static_cast<std::size_t>(i)] + 0x10) & kFaceDisabled)
                continue; // opened since it was read
            Triangle t = r.bvh.triangles()[static_cast<std::size_t>(i)];
            t.a = t.a * to_sim;
            t.b = t.b * to_sim;
            t.c = t.c * to_sim;
            out.push_back(t);
        }
    }
    for (const Box &b : boxes_) {
        Aabb bb;
        const float reach = std::sqrt(b.half_w * b.half_w + b.half_d * b.half_d);
        bb.add(b.pos - Vec3{reach, 0.0F, reach});
        bb.add(b.pos + Vec3{reach, b.height, reach});
        if (!bb.overlaps(world))
            continue;
        box_tris_.clear();
        box_triangles({-b.half_w, 0.0F, -b.half_d}, {b.half_w, b.height, b.half_d}, Surface::object, box_tris_);
        const Quat q = axis_angle({0.0F, 1.0F, 0.0F}, b.angle); // its sense about y is not verified
        for (Triangle t : box_tris_) {
            t.a = (b.pos + rotate(q, t.a)) * to_sim;
            t.b = (b.pos + rotate(q, t.b)) * to_sim;
            t.c = (b.pos + rotate(q, t.c)) * to_sim;
            t.n = rotate(q, t.n);
            out.push_back(t);
        }
    }
}

} // namespace khdays::secondary
