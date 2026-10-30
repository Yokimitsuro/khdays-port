// The 3D previews (models, rooms): triangles gathered on the processor and drawn by the graphics card
// (studio/shaders/preview.hlsl) into a picture twice the shown size, textured as the DS samples, times
// the vertex colour, translucent ones last (docs/STUDIO.md, "Previews").
#pragma once

#include "core/models/game_model.h"

#include <SDL3/SDL.h>

#include <array>
#include <cstdint>
#include <map>
#include <tuple>
#include <utility>
#include <vector>

namespace studio {

struct PreviewImage {
    const std::uint8_t* rgba = nullptr;  // null: drawn in the vertex colour
    int width = 0, height = 0;
    std::array<bool, 2> repeat{true, true};    // past 0..1 on S, T: repeated (else clamped)
    std::array<bool, 2> mirror{false, false};  // and mirrored
};

struct PreviewVertex {
    std::array<float, 3> position{};
    std::array<float, 3> normal{};  // zero: not lit (drawn in its colour)
    std::array<float, 2> uv{};      // 0..1 over the image
    std::array<float, 4> colour{1, 1, 1, 1};
};

class Preview {
public:
    explicit Preview(SDL_GPUDevice* device) : device_(device) {}
    ~Preview();
    Preview(const Preview&) = delete;
    Preview& operator=(const Preview&) = delete;

    // A new picture of `width` x `height` seen through `view_projection`
    // (column-major); `light` the direction light comes from (unit).
    void begin(int width, int height, const Mat& view_projection, std::array<float, 3> light);
    // A triangle; `cull`: its back left out (front counter-clockwise, as glTF);
    // `tint` multiplies its colour; `translucent`: blended all over, its solid
    // texels too (a glTF BLEND material, as the port draws a mod's).
    void triangle(const PreviewVertex& a, const PreviewVertex& b, const PreviewVertex& c, const PreviewImage& image,
                  bool cull, std::array<float, 3> tint, bool translucent = false);
    // The picture drawn on the graphics card, twice the size given to begin()
    // (shown at that size it is smoothed); null when it could not be drawn.
    SDL_GPUTexture* finish();

    // Where a point is on the picture (begin()'s size), in pixels; false behind the viewer.
    bool project(const std::array<float, 3>& p, float* x, float* y) const;

    int width() const {
        return width_;
    }
    int height() const {
        return height_;
    }

    // Releases what the last frame no longer uses (call at the frame's start).
    void collect();

private:
    struct Image {
        SDL_GPUTexture* texture = nullptr;
        bool partial_alpha = false;  // alpha between 8 and 247 somewhere (the DS's 5 bits: neither 0 nor 31): blended
        std::uint64_t mark = 0;      // a few of its bytes, to tell a reused address
        std::uint64_t seen = 0;      // the last frame that drew it
    };
    struct Batch {
        const std::uint8_t* image = nullptr;
        int sampler = 0;
        bool cull = false, blend = false, translucent = false;
        bool operator<(const Batch& o) const {
            return std::tie(blend, image, sampler, cull, translucent) <
                   std::tie(o.blend, o.image, o.sampler, o.cull, o.translucent);
        }
    };

    bool ready();
    Image* image_of(const PreviewImage& image);

    SDL_GPUDevice* device_;
    int width_ = 0, height_ = 0;
    Mat vp_{};
    std::array<float, 3> light_{};
    std::map<Batch, std::vector<float>> batches_;
    std::map<const std::uint8_t*, Image> images_;
    std::vector<std::pair<const std::uint8_t*, PreviewImage>> new_images_;
    SDL_GPUTexture* colour_ = nullptr;
    SDL_GPUTexture* depth_ = nullptr;
    int target_w_ = 0, target_h_ = 0;
    SDL_GPUBuffer* vertices_ = nullptr;
    std::uint32_t vertex_capacity_ = 0;
    std::vector<SDL_GPUTexture*> released_;
    std::uint64_t frame_ = 0;
    bool failed_ = false;
};

// A camera turning around `target`: yaw and pitch in degrees, `distance`.
Mat orbit_view_projection(std::array<float, 3> target, float yaw, float pitch, float distance, float aspect);
std::array<float, 3> orbit_eye_direction(float yaw, float pitch);

// The point an orbit camera turns around, moved over a view `view_height`
// pixels tall while the mouse is over it (`hovered`): dragged with the right
// or middle button across the view, as far as the picture moves under the
// mouse; W/S forward and back, A/D left and right, Q/E down and up (Shift:
// four times as fast), half as far a second as the camera is from it.
void orbit_move(std::array<float, 3>& target, float yaw, float pitch, float distance, float view_height, bool hovered);

}  // namespace studio
