#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace khdays::assets {

// One animated scalar track (translation or scale component).
struct AnimationCurve final {
    enum class Kind { None, Constant, Samples };
    Kind kind = Kind::None;
    float constant = 0.0F;
    std::uint16_t start_frame = 0;
    std::uint16_t end_frame = 0;
    std::vector<float> samples;
};

// The rotation track: a 3x3 matrix (column-major) per keyframe.
struct AnimationRotationCurve final {
    enum class Kind { None, Constant, Samples };
    Kind kind = Kind::None;
    std::array<float, 9> constant{1, 0, 0, 0, 1, 0, 0, 0, 1};
    std::uint16_t start_frame = 0;
    std::uint16_t end_frame = 0;
    std::vector<std::array<float, 9>> samples;
};

// TRS curves for one bone (object).
struct BoneCurves final {
    bool animated = false;
    std::array<AnimationCurve, 3> translation;
    AnimationRotationCurve rotation;
    std::array<AnimationCurve, 3> scale;
};

// A skeletal animation decoded from an NSBCA (JNT0 / J0AC) file.
struct SkeletalAnimation final {
    std::string name;
    std::uint16_t frame_count = 0;
    std::vector<BoneCurves> bones;
};

// Load a skeletal animation from an NSBCA file. Throws std::runtime_error on
// malformed data.
SkeletalAnimation load_nsbca(
    const std::filesystem::path& input_path,
    std::size_t animation_index = 0);

// Sample the animation at a (possibly fractional) frame, returning one bone
// (object) matrix per bone, column-major. Bones the animation does not drive
// keep their rest matrix; extra rest bones are passed through unchanged.
std::vector<std::array<float, 16>> sample_animation(
    const SkeletalAnimation& animation,
    float frame,
    const std::vector<std::array<float, 16>>& rest_object_matrices);

}  // namespace khdays::assets
