#include "room_animation.h"

#include <algorithm>
#include <cmath>

namespace khdays::room {

namespace {

constexpr Mat kIdentity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

Mat mul(const Mat &a, const Mat &b)
{
    Mat r{};
    for (int c = 0; c < 4; ++c) {
        for (int row = 0; row < 4; ++row) {
            float s = 0.0F;
            for (int k = 0; k < 4; ++k)
                s += a[static_cast<std::size_t>(k * 4 + row)] * b[static_cast<std::size_t>(c * 4 + k)];
            r[static_cast<std::size_t>(c * 4 + row)] = s;
        }
    }
    return r;
}

// T * R * S, glTF's order.
Mat compose(const std::array<float, 3> &t, const std::array<float, 4> &q, const std::array<float, 3> &s)
{
    const float x = q[0], y = q[1], z = q[2], w = q[3];
    return {(1 - 2 * (y * y + z * z)) * s[0],
            (2 * (x * y + z * w)) * s[0],
            (2 * (x * z - y * w)) * s[0],
            0,
            (2 * (x * y - z * w)) * s[1],
            (1 - 2 * (x * x + z * z)) * s[1],
            (2 * (y * z + x * w)) * s[1],
            0,
            (2 * (x * z + y * w)) * s[2],
            (2 * (y * z - x * w)) * s[2],
            (1 - 2 * (x * x + y * y)) * s[2],
            0,
            t[0],
            t[1],
            t[2],
            1};
}

// A track's value at `t` seconds: the keys either side, linear (rotations the short way, renormalised),
// or the one before when stepped; before the first key the first, after the last the last.
void sample(const assets::ImportedTrack &track, float t, float *out)
{
    const std::size_t n = track.path == assets::ImportedTrack::Path::rotation ? 4 : 3;
    const auto &times = track.times;
    const auto at = std::upper_bound(times.begin(), times.end(), t);
    if (at == times.begin() || at == times.end()) {
        const std::size_t k = at == times.begin() ? 0 : times.size() - 1;
        std::copy(track.values.begin() + static_cast<std::ptrdiff_t>(k * n),
                  track.values.begin() + static_cast<std::ptrdiff_t>(k * n + n), out);
        return;
    }
    const std::size_t k1 = static_cast<std::size_t>(at - times.begin());
    const std::size_t k0 = k1 - 1;
    const float *a = &track.values[k0 * n];
    const float *b = &track.values[k1 * n];
    const float span = times[k1] - times[k0];
    const float f = track.step || span <= 0.0F ? 0.0F : (t - times[k0]) / span;
    float sign = 1.0F;
    if (n == 4 && a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3] < 0.0F)
        sign = -1.0F;
    for (std::size_t c = 0; c < n; ++c)
        out[c] = a[c] + (sign * b[c] - a[c]) * f;
    if (n == 4) {
        const float len = std::sqrt(out[0] * out[0] + out[1] * out[1] + out[2] * out[2] + out[3] * out[3]);
        for (std::size_t c = 0; len > 1e-8F && c < 4; ++c)
            out[c] /= len;
    }
}

// An animation's own time at `seconds` from its start.
float local_time(const assets::ImportedAnimation &a, double seconds)
{
    if (seconds <= a.duration || a.duration <= 0.0F)
        return static_cast<float>(std::max(0.0, std::min(seconds, static_cast<double>(a.duration))));
    if (!a.loop)
        return a.duration;
    const double from = std::clamp(static_cast<double>(a.loop_from), 0.0, static_cast<double>(a.duration));
    const double span = a.duration - from;
    if (span <= 0.0)
        return a.duration;
    return static_cast<float>(from + std::fmod(seconds - a.duration, span));
}

} // namespace

std::vector<Mat> node_worlds(const std::vector<assets::ImportedNode> &nodes,
                             const std::vector<assets::ImportedAnimation> &animations, double seconds)
{
    // each node's local matrix: its rest, or its TRS with the animated parts replaced
    std::vector<Mat> local(nodes.size());
    std::vector<std::array<float, 3>> t(nodes.size()), s(nodes.size());
    std::vector<std::array<float, 4>> r(nodes.size());
    std::vector<char> moved(nodes.size(), 0);
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        local[i] = nodes[i].local;
        t[i] = nodes[i].translation;
        r[i] = nodes[i].rotation;
        s[i] = nodes[i].scale;
    }
    for (const auto &a : animations) {
        const float time = local_time(a, seconds);
        for (const auto &track : a.tracks) {
            if (track.node < 0 || static_cast<std::size_t>(track.node) >= nodes.size() || track.times.empty())
                continue;
            const auto i = static_cast<std::size_t>(track.node);
            const std::size_t n = track.path == assets::ImportedTrack::Path::rotation ? 4 : 3;
            if (track.values.size() < track.times.size() * n)
                continue;
            float v[4] = {0, 0, 0, 1};
            sample(track, time, v);
            switch (track.path) {
            case assets::ImportedTrack::Path::translation: t[i] = {v[0], v[1], v[2]}; break;
            case assets::ImportedTrack::Path::rotation: r[i] = {v[0], v[1], v[2], v[3]}; break;
            case assets::ImportedTrack::Path::scale: s[i] = {v[0], v[1], v[2]}; break;
            }
            moved[i] = 1;
        }
    }
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        if (moved[i])
            local[i] = compose(t[i], r[i], s[i]);
    }
    return chain_worlds(nodes, local);
}

std::vector<Mat> chain_worlds(const std::vector<assets::ImportedNode> &nodes, const std::vector<Mat> &local)
{
    const std::size_t count = std::min(nodes.size(), local.size());
    std::vector<Mat> world(count, kIdentity);
    std::vector<char> done(count, 0); // 1 placed, 2 on the walk up being made
    std::vector<std::size_t> chain;
    for (std::size_t i = 0; i < count; ++i) {
        // up to a node already placed, a root or one of this walk's own (a loop), then placed down from there
        chain.clear();
        for (std::size_t up = i; done[up] == 0;) {
            chain.push_back(up);
            done[up] = 2;
            const int p = nodes[up].parent;
            if (p < 0 || static_cast<std::size_t>(p) >= count)
                break;
            up = static_cast<std::size_t>(p);
        }
        for (auto c = chain.rbegin(); c != chain.rend(); ++c) {
            const int p = nodes[*c].parent;
            const bool has_parent =
                p >= 0 && static_cast<std::size_t>(p) < count && done[static_cast<std::size_t>(p)] == 1;
            world[*c] = has_parent ? mul(world[static_cast<std::size_t>(p)], local[*c]) : local[*c];
            done[*c] = 1;
        }
    }
    return world;
}

std::array<float, 2> scroll_offset(const std::array<float, 2> &scroll, std::uint64_t frames, float width, float height)
{
    const double f = static_cast<double>(frames);
    const double u = width > 0.0F ? std::fmod(static_cast<double>(scroll[0]) * f, static_cast<double>(width))
                                  : static_cast<double>(scroll[0]) * f;
    const double v = height > 0.0F ? std::fmod(static_cast<double>(scroll[1]) * f, static_cast<double>(height))
                                   : static_cast<double>(scroll[1]) * f;
    return {static_cast<float>(u), static_cast<float>(v)};
}

std::size_t frame_at(const std::vector<std::pair<std::string, int>> &frames, bool loop, std::size_t loop_from,
                     std::uint64_t frame)
{
    if (frames.empty())
        return 0;
    std::uint64_t total = 0, tail = 0;
    loop_from = std::min(loop_from, frames.size() - 1);
    for (std::size_t i = 0; i < frames.size(); ++i) {
        const auto length = static_cast<std::uint64_t>(std::max(1, frames[i].second));
        total += length;
        if (i >= loop_from)
            tail += length;
    }
    if (frame >= total) {
        if (!loop)
            return frames.size() - 1;
        frame = (total - tail) + (frame - total) % tail;
    }
    for (std::size_t i = 0; i < frames.size(); ++i) {
        const auto length = static_cast<std::uint64_t>(std::max(1, frames[i].second));
        if (frame < length)
            return i;
        frame -= length;
    }
    return frames.size() - 1;
}

} // namespace khdays::room
