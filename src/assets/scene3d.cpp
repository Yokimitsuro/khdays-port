#include "khdays/assets/scene3d.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace khdays::assets {

namespace {

using Vec3 = std::array<float, 3>;

Vec3 sub(const Vec3& a, const Vec3& b) {
    return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
}

float dot(const Vec3& a, const Vec3& b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2],
            a[0] * b[1] - a[1] * b[0]};
}

Vec3 normalize(const Vec3& v) {
    const float len = std::sqrt(dot(v, v));
    if (len < 1e-6F) {
        return {0.0F, 0.0F, 1.0F};
    }
    return {v[0] / len, v[1] / len, v[2] / len};
}

// A view basis: the camera's right/up/forward and its eye. Transforming a world
// point is three dot products, which is cheaper and clearer than a 4x4 here.
struct ViewBasis final {
    Vec3 right{1.0F, 0.0F, 0.0F};
    Vec3 up{0.0F, 1.0F, 0.0F};
    Vec3 forward{0.0F, 0.0F, -1.0F};  // the direction the camera looks
    Vec3 eye{0.0F, 0.0F, 0.0F};
};

ViewBasis make_view(const Camera3D& camera) {
    ViewBasis basis;
    basis.eye = camera.eye;
    basis.forward = normalize(sub(camera.target, camera.eye));
    Vec3 up = normalize(camera.up);
    // Guard a degenerate up parallel to the view direction.
    if (std::fabs(dot(basis.forward, up)) > 0.999F) {
        up = {0.0F, 0.0F, 1.0F};
        if (std::fabs(dot(basis.forward, up)) > 0.999F) {
            up = {1.0F, 0.0F, 0.0F};
        }
    }
    basis.right = normalize(cross(basis.forward, up));
    basis.up = cross(basis.right, basis.forward);
    return basis;
}

// World -> view space, with the camera looking down -Z.
Vec3 to_view(const ViewBasis& basis, const Vec3& world) {
    const Vec3 d = sub(world, basis.eye);
    return {dot(d, basis.right), dot(d, basis.up), -dot(d, basis.forward)};
}

// A vertex ready to rasterize: screen position, 1/z, and attributes already
// divided by z so they interpolate linearly in screen space.
struct Projected final {
    float x = 0.0F;
    float y = 0.0F;
    float inv_z = 0.0F;
    float u_over_z = 0.0F;
    float v_over_z = 0.0F;
    std::array<float, 4> color_over_z{0.0F, 0.0F, 0.0F, 0.0F};
};

struct ClipVertex final {
    Vec3 view{0.0F, 0.0F, 0.0F};
    std::array<float, 2> texcoord{0.0F, 0.0F};
    std::array<float, 4> color{255.0F, 255.0F, 255.0F, 255.0F};
};

ClipVertex interpolate(
    const ClipVertex& a,
    const ClipVertex& b,
    const float t) {
    ClipVertex out;
    for (std::size_t i = 0; i < 3U; ++i) {
        out.view[i] = a.view[i] + (b.view[i] - a.view[i]) * t;
    }
    for (std::size_t i = 0; i < 2U; ++i) {
        out.texcoord[i] =
            a.texcoord[i] + (b.texcoord[i] - a.texcoord[i]) * t;
    }
    for (std::size_t i = 0; i < 4U; ++i) {
        out.color[i] = a.color[i] + (b.color[i] - a.color[i]) * t;
    }
    return out;
}

// Clip in view space against z-distance planes before perspective division.
// Retaining all attributes here avoids both disappearing room polygons and
// texture seams where a polygon intersects the camera frustum.
std::vector<ClipVertex> clip_distance_plane(
    const std::vector<ClipVertex>& input,
    const float plane,
    const bool keep_greater) {
    std::vector<ClipVertex> output;
    if (input.empty()) {
        return output;
    }
    output.reserve(input.size() + 1U);
    const auto distance = [](const ClipVertex& vertex) {
        return -vertex.view[2];
    };
    const auto inside = [&](const ClipVertex& vertex) {
        return keep_greater ? distance(vertex) >= plane
                            : distance(vertex) <= plane;
    };

    ClipVertex previous = input.back();
    bool previous_inside = inside(previous);
    for (const auto& current : input) {
        const bool current_inside = inside(current);
        if (previous_inside != current_inside) {
            const float previous_distance = distance(previous);
            const float denominator = distance(current) - previous_distance;
            if (std::fabs(denominator) > 1.0e-8F) {
                output.push_back(interpolate(
                    previous, current,
                    (plane - previous_distance) / denominator));
            }
        }
        if (current_inside) {
            output.push_back(current);
        }
        previous = current;
        previous_inside = current_inside;
    }
    return output;
}

float edge(const Projected& a, const Projected& b, const float px, const float py) {
    return (b.x - a.x) * (py - a.y) - (b.y - a.y) * (px - a.x);
}

}  // namespace

SceneBounds scene_bounds(const std::vector<ModelInstance>& instances) {
    SceneBounds bounds;
    float lo[3] = {1e30F, 1e30F, 1e30F};
    float hi[3] = {-1e30F, -1e30F, -1e30F};
    for (const auto& instance : instances) {
        if (instance.model == nullptr) {
            continue;
        }
        for (const auto& mesh : instance.model->meshes) {
            for (const auto& vertex : mesh.vertices) {
                const auto p = transform_point(
                    instance.transform,
                    posed_position(*instance.model, vertex));
                for (int i = 0; i < 3; ++i) {
                    lo[i] = std::min(lo[i], p[static_cast<std::size_t>(i)]);
                    hi[i] = std::max(hi[i], p[static_cast<std::size_t>(i)]);
                }
                bounds.valid = true;
            }
        }
    }
    if (!bounds.valid) {
        return bounds;
    }
    float radius_sq = 0.0F;
    for (int i = 0; i < 3; ++i) {
        const auto k = static_cast<std::size_t>(i);
        bounds.min[k] = lo[i];
        bounds.max[k] = hi[i];
        bounds.center[k] = (lo[i] + hi[i]) * 0.5F;
        const float half = (hi[i] - lo[i]) * 0.5F;
        radius_sq += half * half;
    }
    bounds.radius = std::sqrt(radius_sq);
    return bounds;
}

Camera3D frame_scene(
    const std::vector<ModelInstance>& instances,
    const float yaw,
    const float pitch,
    const float distance_scale) {
    Camera3D camera;
    const SceneBounds bounds = scene_bounds(instances);
    if (!bounds.valid || bounds.radius <= 0.0F) {
        return camera;
    }
    camera.target = bounds.center;
    // Far enough that the bounding sphere fits the vertical field of view.
    const float distance =
        (bounds.radius / std::sin(std::min(camera.fov_y, 3.0F) * 0.5F))
        * std::max(distance_scale, 0.01F);
    const float cp = std::cos(pitch);
    camera.eye = {bounds.center[0] + distance * cp * std::sin(yaw),
                  bounds.center[1] + distance * std::sin(pitch),
                  bounds.center[2] + distance * cp * std::cos(yaw)};
    camera.near_z = std::max(0.01F, bounds.radius * 0.01F);
    camera.far_z = distance + bounds.radius * 4.0F;
    return camera;
}

DecodedTexture render_scene(
    const std::vector<ModelInstance>& instances,
    const Camera3D& camera,
    const int width,
    const int height) {
    DecodedTexture frame;
    frame.name = "scene";
    frame.width = std::max(width, 0);
    frame.height = std::max(height, 0);
    if (frame.width == 0 || frame.height == 0) {
        return frame;
    }
    frame.rgba.assign(
        static_cast<std::size_t>(frame.width) * frame.height * 4U, 0U);

    // Depth buffer holds 1/z; larger is nearer, 0 is "nothing drawn yet".
    std::vector<float> depth(
        static_cast<std::size_t>(frame.width) * frame.height, 0.0F);

    const ViewBasis basis = make_view(camera);
    const float aspect =
        static_cast<float>(frame.width) / static_cast<float>(frame.height);
    const float focal = 1.0F / std::tan(std::max(camera.fov_y, 0.01F) * 0.5F);
    const float half_w = static_cast<float>(frame.width) * 0.5F;
    const float half_h = static_cast<float>(frame.height) * 0.5F;

    const auto project = [&](const ClipVertex& vertex) {
        Projected out;
        const float z = -vertex.view[2];  // distance in front of the camera
        out.inv_z = 1.0F / z;
        out.x = half_w
            + (vertex.view[0] * focal / aspect) * out.inv_z * half_w;
        out.y = half_h - (vertex.view[1] * focal) * out.inv_z * half_h;
        out.u_over_z = vertex.texcoord[0] * out.inv_z;
        out.v_over_z = vertex.texcoord[1] * out.inv_z;
        for (std::size_t i = 0; i < 4U; ++i) {
            out.color_over_z[i] = vertex.color[i] * out.inv_z;
        }
        return out;
    };

    for (const auto& instance : instances) {
        if (instance.model == nullptr) {
            continue;
        }
        const NeutralModel& model = *instance.model;
        for (const auto& mesh : model.meshes) {
            const DecodedTexture* texture = nullptr;
            if (instance.textures != nullptr) {
                const auto it = instance.textures->find(mesh.texture_name);
                if (it != instance.textures->end()) {
                    texture = &it->second;
                }
            }
            for (std::size_t i = 0; i + 3U <= mesh.indices.size(); i += 3U) {
                const NeutralVertex* corner[3] = {
                    &mesh.vertices[mesh.indices[i]],
                    &mesh.vertices[mesh.indices[i + 1U]],
                    &mesh.vertices[mesh.indices[i + 2U]]};
                std::vector<ClipVertex> polygon(3U);
                for (int k = 0; k < 3; ++k) {
                    polygon[static_cast<std::size_t>(k)].view = to_view(
                        basis,
                        transform_point(
                            instance.transform,
                            posed_position(model, *corner[k])));
                    for (std::size_t j = 0; j < 2U; ++j) {
                        polygon[static_cast<std::size_t>(k)].texcoord[j] =
                            corner[k]->texcoord[j];
                    }
                    for (std::size_t j = 0; j < 4U; ++j) {
                        polygon[static_cast<std::size_t>(k)].color[j] =
                            static_cast<float>(corner[k]->color[j]);
                    }
                }
                polygon = clip_distance_plane(
                    polygon, std::max(camera.near_z, 1.0e-5F), true);
                polygon = clip_distance_plane(
                    polygon, std::max(camera.far_z, camera.near_z), false);
                for (std::size_t triangle = 1U;
                     triangle + 1U < polygon.size(); ++triangle) {
                const Projected a = project(polygon[0]);
                const Projected b = project(polygon[triangle]);
                const Projected c = project(polygon[triangle + 1U]);

                const float area = edge(a, b, c.x, c.y);
                if (std::fabs(area) < 1e-6F) {
                    continue;
                }
                const float inv_area = 1.0F / area;

                const int lo_x = std::max(
                    0, static_cast<int>(std::floor(std::min({a.x, b.x, c.x}))));
                const int hi_x = std::min(
                    frame.width - 1,
                    static_cast<int>(std::ceil(std::max({a.x, b.x, c.x}))));
                const int lo_y = std::max(
                    0, static_cast<int>(std::floor(std::min({a.y, b.y, c.y}))));
                const int hi_y = std::min(
                    frame.height - 1,
                    static_cast<int>(std::ceil(std::max({a.y, b.y, c.y}))));

                for (int py = lo_y; py <= hi_y; ++py) {
                    for (int px = lo_x; px <= hi_x; ++px) {
                        const float cx = static_cast<float>(px) + 0.5F;
                        const float cy = static_cast<float>(py) + 0.5F;
                        float w0 = edge(b, c, cx, cy) * inv_area;
                        float w1 = edge(c, a, cx, cy) * inv_area;
                        float w2 = edge(a, b, cx, cy) * inv_area;
                        // Accept either winding: the DS emits both, and this
                        // renderer does not cull back faces.
                        if (w0 < 0.0F || w1 < 0.0F || w2 < 0.0F) {
                            if (w0 > 0.0F || w1 > 0.0F || w2 > 0.0F) {
                                continue;
                            }
                            w0 = -w0;
                            w1 = -w1;
                            w2 = -w2;
                        }

                        const float inv_z =
                            w0 * a.inv_z + w1 * b.inv_z + w2 * c.inv_z;
                        if (inv_z <= 0.0F) {
                            continue;
                        }
                        const std::size_t pixel =
                            static_cast<std::size_t>(py) * frame.width + px;
                        if (inv_z <= depth[pixel]) {
                            continue;  // something nearer is already here
                        }

                        const float z = 1.0F / inv_z;
                        const float u =
                            (w0 * a.u_over_z + w1 * b.u_over_z + w2 * c.u_over_z)
                            * z;
                        const float v =
                            (w0 * a.v_over_z + w1 * b.v_over_z + w2 * c.v_over_z)
                            * z;

                        float texel[4] = {255.0F, 255.0F, 255.0F, 255.0F};
                        if (texture != nullptr && texture->width > 0
                            && texture->height > 0) {
                            const auto repeat = [](const float coordinate,
                                                   const int extent) {
                                int value = static_cast<int>(
                                    std::floor(coordinate + 0.5F));
                                value %= extent;
                                return value < 0 ? value + extent : value;
                            };
                            // The native GPU path and the DS material default
                            // both repeat. Clamping here stretched edge texels
                            // across large room polygons as the camera moved.
                            const int tx = repeat(u, texture->width);
                            const int ty = repeat(v, texture->height);
                            const std::size_t o =
                                (static_cast<std::size_t>(ty) * texture->width
                                 + tx)
                                * 4U;
                            for (std::size_t k = 0; k < 4U; ++k) {
                                texel[k] =
                                    static_cast<float>(texture->rgba[o + k]);
                            }
                        }

                        float shade[4];
                        for (std::size_t k = 0; k < 4U; ++k) {
                            shade[k] = (w0 * a.color_over_z[k]
                                        + w1 * b.color_over_z[k]
                                        + w2 * c.color_over_z[k])
                                * z;
                        }

                        const float alpha =
                            (texel[3] / 255.0F) * (shade[3] / 255.0F);
                        if (alpha <= 0.0F) {
                            continue;
                        }
                        std::uint8_t* dst = &frame.rgba[pixel * 4U];
                        for (std::size_t k = 0; k < 3U; ++k) {
                            const float src = texel[k] * (shade[k] / 255.0F);
                            const float out = src * alpha
                                + static_cast<float>(dst[k]) * (1.0F - alpha);
                            dst[k] = static_cast<std::uint8_t>(
                                std::clamp(out, 0.0F, 255.0F));
                        }
                        const float dst_a = static_cast<float>(dst[3]) / 255.0F;
                        dst[3] = static_cast<std::uint8_t>(std::clamp(
                            (alpha + dst_a * (1.0F - alpha)) * 255.0F, 0.0F,
                            255.0F));
                        // Only an opaque pixel occludes what comes after it.
                        if (alpha >= 0.999F) {
                            depth[pixel] = inv_z;
                        }
                    }
                }
                }
            }
        }
    }
    return frame;
}

}  // namespace khdays::assets
