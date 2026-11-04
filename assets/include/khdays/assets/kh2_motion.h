#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// KH2's character motions (an .mset: a BAR of anb, each a BAR whose type 0x09
// entry is a motion), played as KH2's PS3 build plays them (kn::MotionPrototype0,
// motion_prototype0.cpp); docs/KH2_MODELS.md has the layout and the functions each
// part follows. Matrices are column-major for column vectors, as KH2's FMATRIX44
// lies in memory; KH2 units (centimetres), the model's Y up.
namespace khdays::assets::kh2 {

using Mat4 = std::array<float, 16>;
using Vec4 = std::array<float, 4>;

// The model a motion moves: the mdlx's bones (kn::ModelHRC: scale with the
// bone's length in w, rotation, translation, parent) and its feet for IKAdjust
// (the collision list's type 10/11 elements: YS::MOTION::make_ik_adjust).
struct MotionSkeleton final {
    struct Joint final {
        int parent = -1;
        Vec4 scale{1.0F, 1.0F, 1.0F, 0.0F};
        Vec4 rotation{};
        Vec4 translation{0.0F, 0.0F, 0.0F, 1.0F};
    };
    struct Foot final {
        int bone = 0;
        bool below = false;  // type 11: the ray also reaches below the leg's length
        float bias = 0.0F;   // its Height: the foot's height over the ground
    };
    std::vector<Joint> joints;
    std::vector<Foot> feet;
};

// The skeleton of an .mdlx's model (BAR entry 0x04, its collision 0x17).
MotionSkeleton read_motion_skeleton(const std::uint8_t* data, std::size_t size);

// One motion (kn::MotionPrototype0RAW), its bytes kept as the file has them.
class Motion final {
public:
    // The motion at `at` (after the 0x90 bytes the game keeps for itself). Throws std::runtime_error when its
    // tables do not fit the file, so that a bad file fails at load.
    Motion(const std::uint8_t* data, std::size_t size, std::size_t at);

    int model_joints() const {
        return model_num_;
    }
    int joints() const {
        return calc_num_;
    }  // the model's, then the motion's helpers
    float start() const {
        return start_;
    }  // in the motion's frames
    float end() const {
        return end_;
    }
    float fps() const {
        return fps_;
    }
    float loop_from() const {
        return ret_;
    }  // retFrame: where it continues after its end
    // Its length and loop point in game ticks (60 a second), as KH2 counts time.
    float length_ticks() const {
        return (end_ - start_) * 60.0F / fps_;
    }
    float loop_ticks() const {
        return (ret_ - start_) * 60.0F / fps_;
    }

    // MotionPrototype0::getPosition: the root motion at `ticks` (0 = the motion's
    // start); identity when the motion has none (PositionInfo not_unit 0).
    Mat4 root_motion(float ticks) const;

private:
    friend class MotionPlayer;
    std::vector<std::uint8_t> bytes_;  // from the header on
    int model_num_ = 0;
    int calc_num_ = 0;
    float start_ = 0.0F, end_ = 0.0F, fps_ = 30.0F, ret_ = 0.0F;
};

// A motion's attributes (inner BAR entry 0x10, KH2's YS::ANMATR): ranges of frames and triggers on one, each an id
// and its 16-bit parameters. u8 range count, u8 trigger count, u16 the triggers' offset; a range u16 start, u16 end,
// u8 id, u8 parameter count, the parameters; a trigger u16 frame, u8 id, u8 parameter count, the parameters
// (YS::ANMATR::CheckRangeCallback, ExecTriggerCallback: range 23 holds a texture animation's slot, trigger 4 sets
// its default, 5 puts the file's back).
struct MotionAttributes final {
    struct Range {
        int start = 0, end = 0;  // the motion's ticks (Motion::length_ticks)
        int id = 0;
        std::vector<std::uint16_t> params;
    };
    struct Trigger {
        int frame = 0;
        int id = 0;
        std::vector<std::uint16_t> params;
    };
    std::vector<Range> ranges;
    std::vector<Trigger> triggers;
};

// An .mset's motions by slot (its BAR entry index). An empty slot (DUMM, a size
// of 0) or one that is not a MotionPrototype0 has none.
class MotionSet final {
public:
    MotionSet(const std::uint8_t* data, std::size_t size);
    std::size_t slots() const {
        return motions_.size();
    }
    const Motion* motion(std::size_t slot) const;
    std::string name(std::size_t slot) const;                    // its BAR entry's (R000, ...)
    const MotionAttributes* attributes(std::size_t slot) const;  // none: null
    // Another set's slots from slot `at` on (at or past this one's last; the slots between empty): one model's
    // motions from several msets (GAMEOVER.mset's, each set of it a BAR of its own)
    void add(std::size_t at, const MotionSet& other);

private:
    std::vector<std::shared_ptr<Motion>> motions_;
    std::vector<std::string> names_;
    std::vector<std::shared_ptr<MotionAttributes>> attributes_;
};

// IKAdjust's ground: a segment from `from` to `to` in the space `world` takes
// the model to (its Y up), the first point where it meets the ground in `hit`.
using GroundRay =
    std::function<bool(const std::array<float, 3>& from, const std::array<float, 3>& to, std::array<float, 3>& hit)>;

// A model's pose under KH2's motions, kept from one evaluation to the next as
// kn::ModelObj keeps it (a joint the motion leaves alone keeps its last value).
class MotionPlayer final {
public:
    explicit MotionPlayer(MotionSkeleton skeleton);

    // MotionPrototype0::calc: `motion` at `ticks` from its start. `world` and
    // `ground` only feed IKAdjust (none: the motion as authored).
    void evaluate(const Motion& motion, float ticks, const Mat4& world, const GroundRay* ground);

    // Each model joint's matrix (gmatrix: its global place, turn and scale), model space.
    const std::vector<Mat4>& joints() const {
        return gmatrix_;
    }

private:
    struct Ik;  // IKAdjust, one per foot
    void constants(const Motion& m);
    void curves(const Motion& m, float t);
    void activation(const Motion& m, float t);
    void matrices(const Motion& m, const Mat4& world, const GroundRay* ground);
    float expression(const Motion& m, int node, int depth);

    MotionSkeleton skeleton_;
    const Motion* current_ = nullptr;
    float frame_ = 0.0F;                   // the expressions' time (LS_57_buff in calc)
    std::uint32_t expression_visits_ = 0;  // this evaluation's, against a malformed file's endless ones
    std::vector<std::uint8_t> active_;     // each constraint's temporary_active_flag
    // per joint (model joints first, then the motion's helpers)
    std::vector<Vec4> scale_, rotation_, translation_, gscale_;
    std::vector<Mat4> lmatrix_, ugm_, all_gmatrix_;
    std::vector<Mat4> gmatrix_;  // the model joints' gmatrix
};

// One model's pose (the source: the model an mset was made for) given to another's bones (the target): a mapped
// bone turned from rest as its source bone is, its head where its parent carries it; an unmapped one at rest under
// its parent; a limb (three mapped bones) bent at the target's lengths toward where the source carries its end (the
// law of cosines, as Motion::calcIKJoint 0xf56a0), or with `lengths` at the source's own; a mapped root where its
// source is plus the rests' difference. Map a bone that carries others to a source bone carrying the same body part.
class Retarget final {
public:
    struct Limb final {
        int upper = 0, lower = 0, end = 0;  // target bones, each below the one before
        // the mapped bone whose source carries the end's rest place to where the end reaches (-1: the
        // end's own): a wrist that turns with its forearm reaching where its hand is carried
        int reach = -1;
        bool lengths = false;  // the source limb's lengths and way (its upper joint: the parent of the
                               // lower's source bone)
    };
    // `source_of[t]`: the source bone target bone t follows, -1 for none. Throws when a bone or limb
    // does not fit the skeletons.
    Retarget(const MotionSkeleton& source, const MotionSkeleton& target, std::vector<int> source_of,
             std::vector<Limb> limbs);

    // Target bone `bone` placed on target bone `by` as `local` (in by's unscaled frame) instead of by its own source
    // or its parent: a weapon bone held in a hand as another skeleton's motions hold it. Throws when either is not
    // a target bone, or `by` is `bone` or below it.
    void carry(int bone, int by, const Mat4& local);

    // The target's gmatrix (model space) for the source's (MotionPlayer::joints).
    void apply(const std::vector<Mat4>& source, std::vector<Mat4>& target) const;

private:
    void order_bones();  // order_: parents and carriers first
    Mat4 pose(std::size_t t, const std::vector<Mat4>& source, const std::vector<Mat4>& out,
              const std::vector<Mat4>& swing, const std::vector<Vec4>& heads, const std::vector<char>& moved,
              const std::vector<float>& stretch) const;
    Vec4 carried(const std::vector<Mat4>& source, int t, int by) const;
    std::vector<int> parent_, source_of_, source_parent_;
    std::vector<int> carrier_;         // per target bone: `carry`'s bone, -1 none
    std::vector<Mat4> carried_local_;  // and its frame on it
    std::vector<Vec4> stretch_axis_;   // per target bone: its length way (unit, its rest space), for `lengths`
    std::vector<Vec4> source_rest_head_;
    std::vector<std::size_t> order_;  // parents first
    std::vector<Limb> limbs_;
    std::vector<Mat4> source_rest_inverse_, target_rest_, target_rest_inverse_, target_local_;
};

// bones.txt's `twist`: a joint and those under it turned about an axis by a share of another joint's twist.
// `twist_about`: how far b has turned about `axis` (unit, model space at rest) against a since rest, radians, their
// scale left out; past 135 degrees eased back to none at half a turn, where +180 and -180 are one pose. `twist_turn`:
// a turn by `radians` about that axis as a carries it now, through `pivot`, to put before the turned joints' matrices.
float twist_about(const Mat4& rest_a, const Mat4& rest_b, const Mat4& now_a, const Mat4& now_b,
                  const std::array<float, 3>& axis);
Mat4 twist_turn(const Mat4& rest_a, const Mat4& now_a, const std::array<float, 3>& axis, float radians,
                const std::array<float, 3>& pivot);

}  // namespace khdays::assets::kh2
