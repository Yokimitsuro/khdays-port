// A mod's character's own skeleton: its clips played for the game's animations.

#include "model_parts.h"

namespace khdays::model_parts {

// --- A mod's character's own skeleton ------------------------------------------------------

Mat compose(const Trs &x)
{
    const float qx = x.r[0], qy = x.r[1], qz = x.r[2], qw = x.r[3];
    Mat m = kIdentity;
    m[0] = (1 - 2 * (qy * qy + qz * qz)) * x.s[0];
    m[1] = (2 * (qx * qy + qz * qw)) * x.s[0];
    m[2] = (2 * (qx * qz - qy * qw)) * x.s[0];
    m[4] = (2 * (qx * qy - qz * qw)) * x.s[1];
    m[5] = (1 - 2 * (qx * qx + qz * qz)) * x.s[1];
    m[6] = (2 * (qy * qz + qx * qw)) * x.s[1];
    m[8] = (2 * (qx * qz + qy * qw)) * x.s[2];
    m[9] = (2 * (qy * qz - qx * qw)) * x.s[2];
    m[10] = (1 - 2 * (qx * qx + qy * qy)) * x.s[2];
    m[12] = x.t[0];
    m[13] = x.t[1];
    m[14] = x.t[2];
    return m;
}

// `a` to `b` by `w` (0-1): positions and scales straight, rotations normalised on the short way.
Trs mix(const Trs &a, const Trs &b, float w)
{
    Trs out;
    for (int c = 0; c < 3; ++c) {
        out.t[static_cast<std::size_t>(c)] = a.t[static_cast<std::size_t>(c)] +
                                             (b.t[static_cast<std::size_t>(c)] - a.t[static_cast<std::size_t>(c)]) * w;
        out.s[static_cast<std::size_t>(c)] = a.s[static_cast<std::size_t>(c)] +
                                             (b.s[static_cast<std::size_t>(c)] - a.s[static_cast<std::size_t>(c)]) * w;
    }
    float dot = 0.0F;
    for (int c = 0; c < 4; ++c)
        dot += a.r[static_cast<std::size_t>(c)] * b.r[static_cast<std::size_t>(c)];
    const float sign = dot < 0.0F ? -1.0F : 1.0F;
    float len = 0.0F;
    for (int c = 0; c < 4; ++c) {
        const auto i = static_cast<std::size_t>(c);
        out.r[i] = a.r[i] + (sign * b.r[i] - a.r[i]) * w;
        len += out.r[i] * out.r[i];
    }
    len = std::sqrt(len);
    for (int c = 0; c < 4 && len > 1e-8F; ++c)
        out.r[static_cast<std::size_t>(c)] /= len;
    return out;
}

// The clip's pose at `t` seconds over `pose` (the nodes its tracks do not move keep theirs).
void apply_clip(const Clip &source, float t, std::vector<Trs> &pose)
{
    using Path = khdays::assets::ImportedTrack::Path;
    for (std::size_t i = 0; i < source.animation.tracks.size(); ++i) {
        const auto &track = source.animation.tracks[i];
        const int node = source.node_of_track[i];
        const std::size_t keys = track.times.size();
        if (node < 0 || static_cast<std::size_t>(node) >= pose.size() || keys == 0)
            continue;
        const std::size_t n = track.path == Path::rotation ? 4 : 3;
        if (track.values.size() < keys * n)
            continue;
        const std::size_t after =
            static_cast<std::size_t>(std::upper_bound(track.times.begin(), track.times.end(), t) - track.times.begin());
        const std::size_t k0 = after == 0 ? 0 : after - 1, k1 = after < keys ? after : keys - 1;
        const float span = track.times[k1] - track.times[k0];
        const float w =
            track.step || span <= 0.0F ? 0.0F : std::fmin(1.0F, std::fmax(0.0F, (t - track.times[k0]) / span));
        Trs a, b;
        Trs &out = pose[static_cast<std::size_t>(node)];
        if (track.path == Path::rotation) {
            std::copy(&track.values[k0 * 4], &track.values[k0 * 4] + 4, a.r.begin());
            std::copy(&track.values[k1 * 4], &track.values[k1 * 4] + 4, b.r.begin());
            out.r = mix(a, b, w).r;
        } else {
            std::array<float, 3> v{};
            for (std::size_t c = 0; c < 3; ++c)
                v[c] = track.values[k0 * 3 + c] + (track.values[k1 * 3 + c] - track.values[k0 * 3 + c]) * w;
            (track.path == Path::translation ? out.t : out.s) = v;
        }
    }
}

// The character's clip `relative`, read the first time it is asked for: its file's first animation, or the one a
// "#<name>" after the file names, its tracks matched to the model's nodes by name.
Clip &clip_of(OwnSkeleton &o, const std::vector<khdays::assets::ImportedNode> &nodes, const std::string &relative)
{
    auto it = o.clips.find(relative);
    if (it != o.clips.end())
        return it->second;
    Clip &made = o.clips[relative];
    char path[1024];
    if (!khdays_characters_file(o.character, relative.c_str(), path, static_cast<int>(sizeof(path))))
        return made;
    try {
        const auto file = u8(path);
        const auto extension = file.extension();
        auto clip_file = extension == ".fbx" || extension == ".FBX" ? khdays::assets::import_fbx(file)
                                                                    : khdays::assets::import_gltf(file);
        if (clip_file.animations.empty()) {
            khdays_log("characters: %s: %s has no animation; name a file with one in its description",
                       o.character->identity, relative.c_str());
            return made;
        }
        const char *const at = khdays_characters_clip_mark(relative.c_str());
        const auto mark = at != nullptr ? static_cast<std::size_t>(at - relative.c_str()) : std::string::npos;
        auto chosen = clip_file.animations.begin();
        if (mark != std::string::npos) {
            const std::string wanted = relative.substr(mark + 1);
            chosen = std::find_if(clip_file.animations.begin(), clip_file.animations.end(),
                                  [&](const khdays::assets::ImportedAnimation &a) { return a.name == wanted; });
            if (chosen == clip_file.animations.end()) {
                khdays_log("characters: %s: %s has no animation named %s; check the name after # in its description",
                           o.character->identity, relative.substr(0, mark).c_str(), wanted.c_str());
                return made;
            }
        }
        made.animation = std::move(*chosen);
        std::size_t matched = 0;
        for (const auto &track : made.animation.tracks) {
            int found = -1;
            if (track.node >= 0 && static_cast<std::size_t>(track.node) < clip_file.nodes.size()) {
                const std::string &wanted = clip_file.nodes[static_cast<std::size_t>(track.node)].name;
                for (std::size_t n = 0; n < nodes.size() && found < 0; ++n) {
                    if (nodes[n].name == wanted)
                        found = static_cast<int>(n);
                }
            }
            made.node_of_track.push_back(found);
            matched += found >= 0 ? 1 : 0;
        }
        made.ok = matched > 0;
        khdays_log("characters: %s: %s read: %.2f s, %zu of %zu tracks on the model's nodes", o.character->identity,
                   relative.c_str(), static_cast<double>(made.animation.duration), matched,
                   made.animation.tracks.size());
    } catch (const std::exception &e) {
        khdays_log("characters: %s: %s cannot be read (%s); export it again", o.character->identity, relative.c_str(),
                   e.what());
    }
    return made;
}

// The name of the animation whose data the game has at `anm` (a J0AC, a PAT0's animation), "" unknown
const std::string &animation_name(u32 anm)
{
    static const std::string unknown;
    const char *file = nullptr;
    u32 offset = 0, run = 0;
    if (anm == 0 || !khdays_origin_of_ram(anm, &file, &offset, &run))
        return unknown;
    AnimationName &known = animation_names[anm];
    if (known.file != file || known.offset != offset) {
        char name[64] = "";
        if (!khdays_names_animation(file, offset, name, static_cast<int>(sizeof(name))))
            name[0] = '\0';
        known = {file, offset, name};
    }
    return known.name;
}

GameAnimation game_animation(u32 anm)
{
    GameAnimation out;
    if (anm == 0 || !khdays_game_ram(anm))
        return out;
    const auto frame = *static_cast<const s32 *>(KHDAYS_W(anm));
    const u32 resource = *static_cast<const u32 *>(KHDAYS_W(anm + 8));
    if (!khdays_game_ram(resource))
        return out;
    const u16 frames = *static_cast<const u16 *>(KHDAYS_W(resource + 4));
    out.name = animation_name(resource);
    if (out.name.empty()) {
        char unnamed[16];
        std::snprintf(unnamed, sizeof(unnamed), "@%08x", resource);
        out.name = unnamed;
    }
    out.frame = static_cast<float>(frame) / 4096.0F;
    out.frames = static_cast<float>(frames);
    out.phase = frames > 0 ? std::fmin(1.0F, std::fmax(0.0F, out.frame / frames)) : 0.0F;
    return out;
}

// The point of a clip `length` long at `t` seconds of its own time: past its end, held there or looped back to
// second `from` of it.
float clip_time(float t, float length, bool loops, float from)
{
    if (length <= 0.0F)
        return 0.0F;
    if (!loops || t < length)
        return std::fmin(t, length);
    from = std::fmin(std::fmax(from, 0.0F), length);
    return length - from <= 0.0F ? length : from + std::fmod(t - from, length - from);
}

// The own skeleton's nodes' places for this draw: the clip given to the animation the game plays, at its own speed while
// the game's animation moves or is held at its end, past its end as its action's `loop` says (else looped once the game's
// is seen to loop); or with `time game` at the same point of the game's; a native plugin's clip while it plays one (kh.
// characters' play); blended from the pose shown when the animation changes or begins again; its root's travel the
// game's unless the action keeps the clip's.
std::vector<Mat> own_pose(Model &m)
{
    OwnSkeleton &o = *m.own;
    unsigned since = 0;
    int loop = 0;
    const KhdaysCharacterAction *played = khdays_characters_played(o.character, &since, &loop);
    const GameAnimation g = played != nullptr ? GameAnimation{played->game} : game_animation(drawn_animation);
    const KhdaysCharacterAction *action =
        played != nullptr ? played : khdays_characters_action(o.character, g.name.c_str());
    const char *file = action != nullptr ? action->clip : o.character->fallback;
    const Clip *chosen = file != nullptr ? &clip_of(o, m.nodes, file) : nullptr;
    const float length = chosen != nullptr && chosen->ok ? chosen->animation.duration : 0.0F;
    const u32 now = khdays_events_vblank_count();
    bool again = false; // the game began its animation again
    if (played != nullptr) {
        o.clock = static_cast<double>(now - since) / 60.0;
        o.clock_vblank = o.moved_vblank = now;
    } else if (g.name != o.playing) {
        o.clock = static_cast<double>(g.phase * length);
        o.clock_vblank = o.moved_vblank = now;
        o.last_frame = g.frame;
    } else {
        if (g.frame != o.last_frame) {
            if (g.frame < o.last_frame && o.last_frame + kWrapFrames >= g.frames) {
                if (o.looping.insert(g.name).second && tracing() && khdays_log_on())
                    khdays_log("characters: %s: the game loops %s (%.0f frames)", o.character->identity, g.name.c_str(),
                               static_cast<double>(g.frames));
            } else if (g.frame < o.last_frame) {
                o.clock = static_cast<double>(g.phase * length);
                again = true;
            }
            o.last_frame = g.frame;
            o.moved_vblank = now;
        }
        const bool held = now - o.moved_vblank > kStillVblanks && g.frame + 1.5F < g.frames;
        if (!again && !held && !khdays_link_paused())
            o.clock += static_cast<double>(now - o.clock_vblank) / 60.0;
        o.clock_vblank = now;
    }
    std::vector<Trs> pose = o.rest;
    if (chosen != nullptr) {
        if (played != nullptr && !loop && (!chosen->ok || o.clock >= length))
            khdays_characters_play_none(o.character->number); // played once: the game's again from the next draw
        if (chosen->ok && action != nullptr && action->game_time && played == nullptr) {
            apply_clip(*chosen, g.phase * length, pose);
        } else if (chosen->ok) {
            const int mode = action != nullptr && played == nullptr ? action->loop : KHDAYS_CHARACTER_LOOP_GAME;
            const bool loops = played != nullptr                   ? loop != 0
                               : mode == KHDAYS_CHARACTER_LOOP_YES ? true
                               : mode == KHDAYS_CHARACTER_LOOP_NO  ? false
                                                                   : o.looping.count(g.name) != 0;
            apply_clip(*chosen,
                       clip_time(static_cast<float>(o.clock), length, loops,
                                 mode == KHDAYS_CHARACTER_LOOP_YES ? action->loop_from : 0.0F),
                       pose);
        }
    }
    if (o.root >= 0 && static_cast<std::size_t>(o.root) < pose.size() && (action == nullptr || !action->root_keep)) {
        const auto r = static_cast<std::size_t>(o.root);
        pose[r].t[0] = o.rest[r].t[0];
        pose[r].t[2] = o.rest[r].t[2];
    }
    if (g.name != o.playing || again) {
        if (again) {
        } else if (action == nullptr && !g.name.empty() && o.told.insert(g.name).second)
            khdays_log("characters: %s: no clip for %s: %s", o.character->identity, g.name.c_str(),
                       o.character->fallback != nullptr ? "its fallback plays" : "its rest pose");
        else if (tracing() && khdays_log_on())
            khdays_log("characters: %s plays %s (%.0f frames, from frame %.1f) with %s", o.character->identity,
                       g.name.c_str(), static_cast<double>(g.frames), static_cast<double>(g.frame),
                       file != nullptr ? file : "its rest pose");
        o.from = o.shown;
        o.playing = g.name;
        o.blend_start = now;
        o.blend_frames =
            static_cast<u32>((action != nullptr ? action->blend_ms : KHDAYS_CHARACTER_BLEND_MS) * 60 / 1000);
    }
    if (now - o.blend_start < o.blend_frames && o.from.size() == pose.size()) {
        const float w = static_cast<float>(now - o.blend_start) / static_cast<float>(o.blend_frames);
        for (std::size_t n = 0; n < pose.size(); ++n)
            pose[n] = mix(o.from[n], pose[n], w);
    }
    o.shown = pose;
    return own_places(m.nodes, pose);
}

std::vector<Mat> own_places(const std::vector<khdays::assets::ImportedNode> &nodes, const std::vector<Trs> &pose)
{
    std::vector<Mat> local(pose.size());
    for (std::size_t n = 0; n < pose.size(); ++n)
        local[n] = compose(pose[n]);
    return khdays::room::chain_worlds(nodes, local);
}

} // namespace khdays::model_parts
