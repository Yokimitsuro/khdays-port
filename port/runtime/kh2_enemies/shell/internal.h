// The KH2 enemies' shell's insides (shell/*.cpp): the types, the area's state and what one file asks of another.
// docs/KH2_ENEMIES.md, "Days's side: the shell".
#ifndef KHDAYS_KH2_ENEMIES_SHELL_INTERNAL_H
#define KHDAYS_KH2_ENEMIES_SHELL_INTERNAL_H

#include "../shell.h"

#include "../actor.h"
#include "../calls.h"
#include "../kinds.h"
#include "../lights.h"
#include "../patterns.h"
#include "../shell_days.h"

#include "khdays/assets/kh2_ai.h"
#include "khdays/assets/kh2_bar.h"
#include "khdays/assets/kh2_event.h"
#include "khdays/assets/kh2_mdlx.h"
#include "khdays/assets/kh2_motion.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>

#include <sstream>
#include <string>
#include <vector>

extern "C" {
#include "../../scripts/game_link.h"
#include "../../../gfx/g3d.h"
#include "../../camera/kh2_camera_hooks.h"
#include "../../battle/stop_magic.h"
#include "../../core/rom.h"
#include "../../core/session_log.h"
#include "../../core/origin.h"
#include "../../core/window.h"
#include "../../../host/app/host.h"
#include "../../missions/missions.h"
#include "../../missions/registry.h"
#include "../../scripts/enemies_link.h"
#include "../../mods/textures.h"
#include "../../sound/sound_bridge.h"
#include "../../sound/sound_shots.h"
#include "../../../sound/archive.h"
#include "../../input/input.h"
}
#include "../../../ui/reaction_command.h"
#include "../../mods/kh2_fx_draw.h"
#include "../../mods/list.h" // the mods taken at the start: each one's KH2 enemies list
#include "../../mods/models.h"

namespace khdays::kh2_enemies::shell {

namespace fs = std::filesystem;
namespace kh2 = khdays::assets::kh2;
namespace ai = khdays::assets::kh2::ai;

// KH2's units are centimetres, Days's about metres (docs/KH2_MODELS.md: a KH2 character comes out at a Days
// character's size by x 0.01), and KH2's world's y points down: a Days place (x, y, z) is KH2's (100 x, -100 y, -100 z)
constexpr float kKh2PerDays = 100.0F;
constexpr float kPi = 3.14159265358979F;
constexpr float kGravity = kKh2Pull; // a fall's pull a tick a tick: class 1's past its top (FUN_1403cb360; actor.h)
constexpr int kNoBone = 0x4000;
// how long an enemy's end may take before its Days defeat goes on without it (a script that never fades out): ticks
constexpr unsigned kLongestEnd = 60u * 10u;

// KHDAYS_TRACE_KH2_ENEMIES: 1 what each enemy does; 2 also where each of its script's threads stands, once a second;
// 3 also every call its script makes (trace_calls)
inline int trace_level()
{
    static int level = -1;
    if (level < 0) {
        const char *v = std::getenv("KHDAYS_TRACE_KH2_ENEMIES");
        level = v != nullptr && *v >= '0' && *v <= '9' ? *v - '0' : v != nullptr && *v != '\0' ? 1 : 0;
    }
    return level;
}

inline bool tracing()
{
    return trace_level() > 0;
}

// KH2's world in Days's: turned half round its x (its y down, its z the other way) and in centimetres -- as a room made
// from a KH2 map lays the map (map_point). With the z kept it would be a mirror of KH2's: whatever the scripts reckon
// to one side would come out on the other (Marluxia's doom's two figures the other way round)
inline Vec3 to_kh2(const float d[3])
{
    return {d[0] * kKh2PerDays, -d[1] * kKh2PerDays, -d[2] * kKh2PerDays};
}

inline void to_days(const Vec3 &k, float out[3])
{
    out[0] = k[0] / kKh2PerDays;
    out[1] = -k[1] / kKh2PerDays;
    out[2] = -k[2] / kKh2PerDays;
}

// A way of KH2's world in Days's, and back (to_days, to_kh2 without the move and the scale)
inline Vec3 days_way(const Vec3 &k)
{
    return {k[0], -k[1], -k[2]};
}

// A KH2 yaw as a turn about Days's up (as Days's quaternions and the models are turned: +z to (sin, 0, cos)), and
// back: a KH2 way (sin y, 0, cos y) is Days's (sin y, 0, -cos y), the turn pi - y
inline float days_turn(float yaw)
{
    return kPi - yaw;
}

// A point or way of KH2's world in the frame the shell's own placing in Days's world (its models, their effects, held
// players, the dome) is written in -- Days's axes, its y down -- and back (its own inverse); a yaw in that frame is
// days_turn's
inline Vec3 legacy(const Vec3 &k)
{
    return {k[0], k[1], -k[2]};
}

// A KH2 yaw's way in Days's world
inline Vec3 days_facing(float yaw)
{
    return {std::sin(yaw), 0.0F, -std::cos(yaw)};
}

// A motion's attack range (10 or 33: its attack's atkp and collision group, its first two values), and one covering
// `time`
inline bool attack_range(const kh2::MotionAttributes::Range &range)
{
    return (range.id == 10 || range.id == 33) && range.params.size() >= 2;
}

inline bool attack_range_on(const kh2::MotionAttributes::Range &range, float time)
{
    return attack_range(range) && time >= float(range.start) && time < float(range.end);
}

// Days's command deck (prepare.py's joins in Ov002_PanelAdvanceCursor's file, no pointers: the game's code takes them
// as its own references): the main row its cursor is on (-1 off them: a list open; -2 no deck) and how many there are
// (0 off them); its cursor on (Ov002_PanelCursorNext) or back (Ov002_PanelCursorPrev); back to its main rows
// (Ov002_HandlePanelInput 0); its rows' text drawn again (Ov002_BuildTileRow)
extern "C" int khdays_deck_main_row(void);
extern "C" int khdays_deck_main_rows(void);
extern "C" void khdays_deck_step(int back);
extern "C" void khdays_deck_to_main(void);
extern "C" void khdays_deck_relabel(void);

struct Shape;
struct Element;
struct KindFiles;
class SetMotions;
struct Mod;
struct Enemy;
struct Kh2Mission;
struct PlayerSheet;
struct Held;
struct Capture;
struct DrawnWeapon;
struct Shout;
struct SpecialMenu;
struct AttackObject;
struct ScriptOffer;
struct TopOffer;
struct Playing;
struct EventEnd;
struct Scope;
struct ScreenShake;
struct ScreenWhite;
struct LaserDome;
struct FriendAction;
struct FriendHop;
struct FriendParams;
struct PartySpec;
struct FriendRow;
struct PartyWeapon;
struct Friend;
struct Picture;
struct Command;
struct BattleRow;
struct LevelParam;
class Field;
struct PlayerFiles;
struct AttackParam;
struct Delivering;

void end_capture(unsigned object, const char *why);

// One of an .mdlx's collision shapes (its BAR entry 0x17: a 64-byte header, its count first, then 20-byte records):
// its group (+0), its kind (+4: 0x300 the body's cylinder, 0x302 a sphere on a bone, 0x306 an attack's sphere, 5 a
// reaction command's zone, its command at +2), its bone (+6, 0x4000 none), its offset on it (+8, +10, +12), its height
// (+14) and radius (+16); KH2's cm.
struct Shape final {
    int group = 0, kind = 0, bone = kNoBone, command = 0;
    Vec3 at{};
    float height = 0.0F, radius = 0.0F;
};

// An effect's element, as KH2FM.exe's FUN_1402c6b30 starts one (the PAX's records of 0x50 bytes from + 0x10, their
// count at + 8): u16 its effect (the 0x82 directory's entry of that number), u16 its number (what a motion's trigger 1
// and 1:87 name), + 6 u16 the bone it follows (0x4000 none: the actor itself; FUN_1403b50f0), + 0x10 flags, + 0x14
// when its sound starts (its time, ticks), + 0x18 its sound (KH2's id, played above 0: FUN_1402c58f0), + 0x1c its
// place, + 0x28 its turn (radians), + 0x34 its size -- KH2's axes (y down), centimetres
struct Element final {
    std::size_t effect = 0, emitter = 0; // in KindFiles::fx
    int bone = kNoBone;
    std::uint32_t flags = 0;
    float start = 0.0F;
    int sound = 0;
    std::array<float, 3> place{}, turn{}, size{1.0F, 1.0F, 1.0F};
    // its two points (flags 0x10000; KH2's ryj::PAX::MANAGER keeps them and get_obj_data sets them each tick, the
    // swing trails' base and tip): + 0x40 and + 0x42 their bones, + 0x44 and + 0x48 how far along each bone's x in its
    // lengths (YS::OBJ::get_bone_pos: the bone's length is its skeleton's scale w); + 0x4c set, the place (+ 0x1c) and
    // the turn (+ 0x28) are offsets added on those bones
    bool points = false, point_offsets = false;
    std::array<int, 2> point_bone{};
    std::array<float, 2> point_along{};
};

// An enemy kind's files, read once: its script (the .mdlx's AI, BAR entry 3), its motions and skeleton, its shapes.
struct KindFiles final {
    std::string name, mdlx;
    std::string folder; // the mod whose list gives it (Kind::folder): its own files there first
    std::shared_ptr<const ai::Script> script;
    std::shared_ptr<const kh2::MotionSet> set;
    bool player_set = false; // its set a player's or a party member's (its BAR header's + 0xc bit 0: SetMotions)
    std::shared_ptr<const kh2::MotionSkeleton> skeleton;
    std::vector<Shape> shapes;
    std::uint32_t default_groups = 1; // the groups on unless the motion or the script says (the shapes' header's + 4)
    // whether its script plays the player's motions itself (a 1:29 in its walked code, the mod's list says: the Magnum
    // Loader's Quick Blade, Cerberus's commands): its zones' commands offered; those KH2's player's own code plays (the
    // Soldier's Cyclone, the Dusk's Reversal) are not yet
    bool plays_player = false;
    // its effects (its .a.us's PAX): the effects, the elements by their number, the pages of its pictures and of KH2's
    // resident ones (effect/texcommon.dpd)
    std::vector<std::shared_ptr<const kh2::EffectBlock>> fx;
    std::map<int, Element> fx_elements;
    // the sounds it plays (its motions' triggers 8, its elements'): KH2's sound id -> the mod's kh2/se/<id>.wav
    std::map<int, int> sounds;
    // its voice's lines (1:262: its voice bank's sounds by number) -> the mod's kh2/voice/<name>/<number>.ogg (the
    // battle banks' Ogg Vorbis as it is) or .wav, each read when first asked (-1: none), and their volumes (the bank's
    // sounds', kh2/voice/<name>/volumes.txt: "<number> <volume>" a line; 1 when not given)
    std::map<int, int> voices;
    std::map<int, float> voice_volumes;
    bool voice_volumes_read = false;
    kh2::EffectBlock common;
    std::vector<std::unique_ptr<khdays::kh2_fx::Pages>> fx_pages; // by effect block: each its own pictures
    // how many HD pictures its effects have in kh2/fx/<name>/ (-0 on): its .a.us's other pictures (its command menu's,
    // "comd") are numbered on from there
    int fx_hd_pages = 0;
    // its event sequences (its .a.us's entries of type 0x16: a reaction command's cameras), by their names' up to four
    // letters as FUN_1403db2f0 packs them (the first in the low byte)
    std::map<std::uint32_t, std::shared_ptr<const kh2::Event>> events;
    // its model's own effects (the .mdlx's PAX, BAR entry 0x12: Hades's hair's flame), which KH2 starts as the model is
    // made (model_effects); none without
    std::unique_ptr<KindFiles> model_fx;
};

// A motion set as an actor's Motions: motion N is slot N (an enemy's mset maps them in order: OpenKH's MSET page); a
// player's or party member's set (its header's flag, YS::MOTIONSET::is_player) holds four of each, motion N at 4 N plus
// the first of its mode's order that it has (YS::MOTION::id_to_num, YS::ModeTable: 0 1 3 2, 1 0 2 3, 2 3 1 0, 3 2 0
// 1; the mode 2 out of battle, 1 in it, turned over -- x 1 -- for a party member with his weapon)
class SetMotions final : public Motions {
public:
    explicit SetMotions(std::shared_ptr<const kh2::MotionSet> set, bool player = false)
        : set_(std::move(set)), player_(player)
    {
    }
    int mode = 2; // a player set's (above): its member's AI sets it
    // motion `id`: another's part it is captured in (capture), the hooked slot's entry of that number when it has one
    // (YS::MOTION::get_motion: the hook's set first, MOTIONSET::get_motion), else the set's own
    const kh2::Motion *motion(int id) const
    {
        if (capture_ != nullptr && id == capture_id_)
            return capture_->motion.get();
        if (const kh2::MotionSet::Variant *v = hooked_variant(id); v != nullptr)
            return v->motion.get();
        const int n = number(id);
        return n >= 0 ? set_->motion(std::size_t(n)) : nullptr;
    }
    const kh2::MotionAttributes *attributes(int id) const
    {
        if (capture_ != nullptr && id == capture_id_)
            return capture_->attributes.get();
        if (const kh2::MotionSet::Variant *v = hooked_variant(id); v != nullptr)
            return v->attributes.get();
        const int n = number(id);
        return n >= 0 ? set_->attributes(std::size_t(n)) : nullptr;
    }
    // its entry's number in the set (id_to_num)
    int number(int id) const
    {
        if (!player_ || id < 0)
            return id;
        for (const int k : kOrder[mode & 3])
            if (set_->motion(std::size_t(4 * id + k)) != nullptr)
                return 4 * id + k;
        return -1;
    }
    bool has(int id) const override
    {
        return motion(id) != nullptr;
    }
    float length(int id) const override
    {
        return has(id) ? motion(id)->length_ticks() : 0.0F;
    }
    float loop_from(int id) const override
    {
        if (!has(id))
            return 0.0F;
        const kh2::Motion *m = motion(id);
        return m->loop_from() < m->end() ? m->loop_ticks() : m->length_ticks();
    }
    Vec3 root(int id, float ticks) const override
    {
        if (!has(id))
            return {};
        const kh2::Mat4 r = motion(id)->root_motion(ticks);
        return {r[12], r[13], r[14]};
    }
    // a player set's slot is its number's entry as a motion's (4 N, its mode's order): Final Xemnas's grab hooks Riku's
    // P_EH000_LAST to 251, its entry 1004, whose own entries are numbered so too (his 197, Attack, its 788)
    bool hook(int id) const override
    {
        int entry = id;
        if (player_ && id >= 0) {
            entry = -1;
            for (const int k : kOrder[mode & 3])
                if (!set_->variants(std::size_t(4 * id + k)).empty()) {
                    entry = 4 * id + k;
                    break;
                }
        }
        if (entry < 0 || set_->variants(std::size_t(entry)).empty())
            return false;
        hook_ = entry;
        hook_id_ = id;
        return true;
    }
    void unhook() const override
    {
        hook_ = hook_id_ = -1;
    }
    bool hooked() const override
    {
        return hook_ >= 0;
    }
    int hook_id() const
    {
        return hook_id_;
    }
    // its motion `id` another's part (1:29's capture: KH2's MOTION::start with the capturer's set, the target's own
    // part of its slot), till it plays another (end_capture); the set holding it kept alive
    void capture(int id, std::shared_ptr<const kh2::MotionSet> set, const kh2::MotionSet::Variant *part)
    {
        capture_set_ = std::move(set);
        capture_id_ = id;
        capture_ = part;
    }
    void end_capture()
    {
        capture_set_.reset();
        capture_id_ = -1;
        capture_ = nullptr;
    }
    int capture_id() const
    {
        return capture_id_;
    }
    const std::shared_ptr<const kh2::MotionSet> &set() const
    {
        return set_;
    }
    // the part of slot `slot` named `name`, the first of its with a motion; none when it has none
    const kh2::MotionSet::Variant *part(int slot, const std::string &name) const
    {
        if (slot < 0)
            return nullptr;
        for (const kh2::MotionSet::Variant &v : set_->variants(std::size_t(slot)))
            if (v.name == name && v.motion)
                return &v;
        return nullptr;
    }

private:
    static constexpr int kOrder[4][4] = {{0, 1, 3, 2}, {1, 0, 2, 3}, {2, 3, 1, 0}, {3, 2, 0, 1}};
    const kh2::MotionSet::Variant *hooked_variant(int id) const
    {
        if (hook_ < 0 || id < 0)
            return nullptr;
        const auto &entries = set_->variants(std::size_t(hook_));
        const auto at = [&entries](int n) {
            return n >= 0 && std::size_t(n) < entries.size() && entries[std::size_t(n)].motion
                       ? &entries[std::size_t(n)]
                       : nullptr;
        };
        if (!player_)
            return at(id);
        for (const int k : kOrder[mode & 3])
            if (const kh2::MotionSet::Variant *v = at(4 * id + k))
                return v;
        return nullptr;
    }
    std::shared_ptr<const kh2::MotionSet> set_;
    bool player_ = false;
    mutable int hook_ = -1, hook_id_ = -1; // the hooked slot (1:75): its entry and its number, -1 none
    std::shared_ptr<const kh2::MotionSet> capture_set_;
    int capture_id_ = -1;
    const kh2::MotionSet::Variant *capture_ = nullptr;
};

// The KINGDOM HEARTS II enemies the mods hold: each mod's list (kh2_enemies.txt) in load order -- the installer's
// "Days - KH2 Enemies", or any mod carrying the enemies its missions use, its files under its own kh2/ -- a kind in two
// lists the first one's; `folder` the first such mod (the log's), "" none
struct Mod final {
    bool looked = false;
    std::string folder; // empty: none
    std::vector<Kind> kinds;
    ai::Calls calls;
    ai::Memory memory; // every KH2 enemy's and player's in one address space, as KH2's are
    std::map<std::string, std::unique_ptr<KindFiles>> files; // by name; null: it cannot be read
};

struct Enemy final {
    unsigned days = 0; // the Days enemy (a DS address); an ally's own number (allies, below)
    // an ally's: the Days enemy whose battle it came with -- the room's collision and the players it finds are that
    // one's, and it goes with it; 0 an enemy's
    unsigned anchor = 0;
    unsigned ally_frame = 0; // the frame an ally was last moved on in
    bool started = true;     // its script set going (one another's script made: at its first tick)
    std::set<int> stops;     // KH2's stops put on it (6:46, Demyx's): held, script and motions, while any is
    // a large enemy (its object entry's kind 0xc: the Hydra): its Days enemy's sphere the reach of its body (footprint),
    // its radius now
    bool large = false;
    float body = 0.0F;
    KindFiles *kind = nullptr;
    std::unique_ptr<SetMotions> motions;
    // the part of the slot it is captured in (1:29, capture_motion) its weapon plays: its character's second ("0191"),
    // none when the slot has none
    const kh2::MotionSet::Variant *capture_weapon = nullptr;
    std::unique_ptr<Actor> actor;
    std::unique_ptr<kh2::MotionPlayer> player;
    std::vector<kh2::Mat4> pose; // this frame's, its bones' model-space matrices (KH2's cm)
    // the pose shown when its motion started (a.motion.started then): a blend goes from it (MOTION::exec's Context1)
    std::vector<kh2::Mat4> blend_from;
    unsigned posed_started = 0;
    bool reacting = false; // a Days hit's reaction playing (held: + 0x18c bit 9)
    // its reflected attack's motion playing (its atkp's + 0x15: BTLOBJ::exec_reflect_default), -1 none: a damage motion
    // to its states while it plays (ACT::is_damage_motion counts BTLOBJ::is_reflect_motion's, 140 to 150)
    int reflect_motion = -1;
    bool paused = false; // its script waits while it plays (a kind with no reaction state of its own)
    bool dying = false, released = false;
    bool down = false;        // released and no longer standing: made again if its Days enemy is set going once more
    bool dead_marked = false; // counted as defeated by its own script (1:328): its Days defeat without "dead"
    // the place (KH2's) the shell gave its actor last: another script putting it elsewhere (1:148 on its handle, the
    // cubes' on the Sorcerer they made) is seen by it
    Vec3 given{};
    unsigned end_ticks = 0;
    float fall = 0.0F; // its fall's speed off the ground (KH2's units a tick)
    // the ground under it, KH2's y (down), from the last frame Days found one: a jump or a fall stops there (Days's
    // collision lets a fast one through: the Wight Knight's 5 m leap fell on under the room)
    float floor = 0.0F;
    bool floor_known = false;
    float radius = 0.0F;    // its Days enemy's collision sphere's, last read (Days's units)
    int attack_motion = -1; // the motion its attacks' hits below were made in, and its time then
    float attack_time = 0.0F;
    unsigned took = 0;                                // the player its attack last landed on (its trigger 15)
    std::map<std::size_t, std::set<unsigned>> struck; // by attack range: the players (or enemies) it hit, each once
    std::set<std::size_t> windows_told;               // traced: the attack ranges of this motion told
    int slot = 0;                                     // its stand-in's attack slot its hits use
    bool told_draw = false;
    // its effects running (started by its motions' trigger 1 and its script's 1:87), moved on as it is drawn (KH2's
    // 60 a second): each its element, its ticks run, whether its sound has started
    struct Running final {
        std::unique_ptr<kh2::EffectInstance> fx;
        std::vector<kh2::EffectDraw> draws;
        const Element *element = nullptr;
        float age = 0.0F;
        bool sounded = false;
        unsigned player = 0; // cast by the motion of its set a player it holds plays (his object): on his bones
        // on his weapon (the keyblade in his hand: its bones are its element's), and the files its element is of (none:
        // the enemy's kind's; KH2's keyblade's, W_EX010.a.us, for its swing's trail, YS::WEAPON::locus)
        bool on_weapon = false;
        const KindFiles *files = nullptr;
        std::uint32_t handle = 0; // what the scripts know it by (1:149, 0:44)
        kh2::EffectMatrix last{}; // that one's place when last drawn, kept once he is let go
        bool placed = false;
        // one its script placed in the world (1:22, 0:38: a shot), at a place of KH2's world, turned (0:41) when set
        bool in_world = false, turned = false;
        Vec3 at{}, turn{};
        // its size and colour set by its script (0:39, 0:67), when set: the element's size given way to, its draws'
        // colours times it (1 each as they are)
        bool scaled = false, coloured = false;
        Vec3 scale{};
        std::array<float, 4> colour{1.0F, 1.0F, 1.0F, 1.0F};
        // its fading out (0:52: ryj::EFFECT::fadeout_kill / fadeout_free -- its colour's alpha a share, + 0x20, that
        // falls by + 0x24 a tick, EFFECT::fade, which runs while it is held too; at 0 the effect is freed): its share
        // now and its fall a tick, 0 none. Xemnas's thorns are held (0:66) and faded so: a held effect's age does not
        // move on)
        float fade = 1.0F, fade_step = 0.0F;
        bool paused = false; // held where it is (0:66, ryj::EFFECT::stop(true)): drawn as it stands, not run
        int dome = 0; // one of the laser dome's (LaserDome): 1 its lasers', 2 hits', 3 bounces', 4 bursts' -- 0 none
        // its two points when last drawn (a swing trail's base and tip): the ticks between two draws take theirs
        // between those and the new ones, as KH2's bones give a new pair each tick
        std::vector<kh2::EffectPoint> points_before;
    };
    std::vector<Running> fx;
    float pan = 0.0F; // where it was last drawn, left (-1) to right (1) of the camera: its sounds' pan
    // its voice line heard (khdays_sound_shot_play's number, 0 none) and the priority it was asked with (YS::VOICE's
    // + 0xc): one at a time, a new one taking its place when asked with a priority as high or higher
    int voice_shot = 0, voice_priority = 0;
    unsigned hit_kinds = 0; // the last Days hit's kinds (its packet's high flags): its element (2:85) is of them
    // its battle entry's health over KH2's Soldier's (2:9; 0 none: Days's own HP stands), and from Days's first hit on
    // a KH2 enemy its health in Days's damage (kSoldierBlows x that share blows of that hit's size) and the most of it;
    // -1 until then (Days's own HP stands). Days's fields show its share (shown_health), up to shown_most
    double health_share = 0.0;
    std::int64_t health = -1, health_most = 0;
    int shown_most = 0;
    // its scripts' measure of it (2:9's record: KH2's own numbers, its battle entry's health -- 1:229, 1:231's least,
    // 1:233 are in it, and the scripts keep shares of it from their start), 0 none; and the share 1:233 set before
    // Days's first hit gave its health
    std::int64_t record_most = 0;
    double start_share = 1.0;
    int battle_level = 0;       // its battle entry's level (enmp + 2: YS::LEVELPARAM's for its attacks), 0 none known
    bool held_at_least = false; // the last Days hit's damage kept off by its least health (1:231): a hit all the same
    // what the last hit's damage was kept to (khdays_kh2_enemies_damage) and the frame it was, for its class's own
    // working out of it (khdays_kh2_enemies_dealt); -1 none
    int dealt = -1;
    unsigned dealt_frame = 0;
    std::int64_t health_taken = 0; // the last Days hit's damage on its health (khdays_kh2_enemies_damage), until told
    unsigned fx_frame = 0;
    std::uint32_t fx_seed = 1;
    // where it was last drawn (Days's place) and the projection then, for its end once its Days enemy is gone
    float place[3] = {};
    float home[3] = {}; // where its wave placed it (Days's place): it comes back there after KH2's "leave"
    // made by a script (1:0: Luxord's cards), not by a wave: after KH2's "leave" it is gone (no spawner of KH2's brings
    // it back), its Days enemy sent away once (`sent_away`)
    bool made = false, sent_away = false;
    // its battle's frame, KH2's world, as it was set going: its place and its facing (the way to its player) -- where
    // its kind's companions and walk paths are placed from
    Vec3 battle_at{};
    float battle_turn = 0.0F;
    bool placed = false;    // its place given to Days (e.place) at least once
    float fall_from = 0.0F; // how high above the ground its fall began (KH2's cm)
    bool aloft = false;     // off the ground as KH2's ground class sees it: a jump, a fall past a step, a motion's air
    // a state's name shouted to it with a delay (1:90; + 0x4c0 .. + 0x4d0, the last one shouted), heard once
    // `sent_after` ticks have gone: that state's shout routine run (StateMachine::shout)
    std::string sent_state;
    std::uint32_t sent_from = 0; // the shouter's handle
    float sent_after = 0.0F;
    float pure[16] = {};
    bool seen = false;
    unsigned ghost_frame = 0;
    unsigned drawn_frame = ~0u; // the frame its KH2 model was last drawn in: once a frame, whatever matched it
    unsigned ticks_traced = 0;  // KHDAYS_TRACE_KH2_ENEMIES=2: its ticks, its threads told once a second
    std::string state;          // the state it last entered (traced)
    // the Days player carrying it (1:18: Cerberus on the player in its reaction command), 0 none, or the KH2 enemy or
    // room object (its handle: Oogie Boogie on his tower, F_NM080), 0 none, and how
    unsigned carrier = 0;
    unsigned carried_by = 0;
    bool on_floor_first = false; // set going by a wave: put on its floor at its first tick
    int carry_bone = 0, carry_flags = 0;
    std::uint32_t attack_record = 0;  // trigger 15's attack (its + 0x18 the atkp entry), made on its first hit
    std::uint32_t command_record = 0; // the target record a command taken is sent with (trigger 11's third)
    // a reaction command its zones offer this game's own player now (-1 none), how deep in (the distance over the
    // zone's radius), and the motion one was taken in (none offered again in it)
    int zone = -1;
    float zone_depth = 0.0F;
    int zone_taken_motion = -1;
    int days_damage = 0;       // the last Days hit's damage on it: a KH2 player's blow is worked out from it
    int blow = 0;              // a KH2 player's blow on it to land (its Days damage), 0 none
    bool blow_landing = false; // that blow's hit going through Days now
    double blow_blows = 0.0;   // that blow in KH2's plain hits (its attacks' power over 100)
    // that blow's reaction (its attacks' atkp + 10, the strongest; YS::DAMAGE's REACTION_*: 0 none, 255 ignored as
    // none) and the one of the blow landing now
    int blow_reaction = 0, landing_reaction = 0;
    // that blow's striker when a party member's (his actor's address: Riku's weapon, weapon_tick), 0 the player's; and
    // the one of the blow landing now
    std::uint32_t blow_party = 0, landing_party = 0;
    // that blow's karma (its attacks' atkp + 0x2a, summed) and the one of the blow landing now
    int blow_karma = 0, landing_karma = 0;
    // its karma (YS::ENEMY + 0xaf0 of the PS3 build, the PC's + 0xd48): what each hit adds (its attack's RevengeDamage;
    // a Days hit KH2's player's small hit's, 10), less the frame time each tick its state can act (ENEMY::_OVR_pre_exec,
    // ACT::can_control); a damage motion's trigger 28 starts its "revenge" once it reaches its limit (+ 0xd4c, 100 till
    // 2:76 sets it: ANMATR::ExecTriggerCallback)
    float karma = 0.0F;
    double pending_blows = 0.0; // KH2 player's blows that landed before the area's first Days hit measured a blow
};

inline std::map<unsigned, std::string> kind_of;            // a Days enemy made from a KH2 row -> its KH2 kind
inline std::set<unsigned> appeared;                        // those a script made (1:0): their kind theirs till they go
inline std::map<unsigned, std::unique_ptr<Enemy>> enemies; // those set going, by their Days actor
inline std::map<const Actor *, Enemy *> by_actor;
// KH2's objects that are no enemies (their object entry's kind neither a boss's 3, an enemy's 4 nor a large enemy's
// 0xc: the Hydra's Megara, Phil and Pegasus, 9; Phil's urn, a gimmick, 0x17): no Days enemy stands in for them --
// nothing of Days's hits them, locks on them or counts them; the shell moves them on and draws them itself as the 3D
// list closes (allies_close), on the ground of their battle's Days enemy's room (Enemy::anchor), and they go with it.
// Made by a script (1:0: the Hydra makes Megara) or with their boss, where KH2's area places them (Kind::with)
inline std::map<unsigned, std::unique_ptr<Enemy>> allies;
inline unsigned allies_made = 0;
constexpr unsigned kAllyIds = 0xa1100000u; // allies' own numbers (the traces'): no Days actor's, out of its memory
// accompanied: the Days enemies (and their kinds) whose companions were made, once each
inline std::set<std::pair<unsigned, std::string>> accompanied;
// KH2's vacuums the scripts made (2:77: their records in the scripts' memory), in the order made
inline std::vector<std::uint32_t> vacuums;

// KH2's signals sent (1:41; an enemy's end, 75; the mission's count and timer), told to every script at the next
// frame (YS::SIGNAL::Call queues them, YS::SIGNAL::Exec tells them): their trigger 10 with the id and its argument
inline std::vector<std::pair<int, int>> signals;

// The mission KH2's area runs with a boss's battle (its msn file, OpenKH's MSN page: a BAR -- its entry of type 2 a
// 0x1c-byte header then operations {s16 code, s16 count, count u32 parameters} till 0; 4 a timer and 5 a counter
// (MissionPiece, shell.h); its script, type 3, its triggers 10 the signals, 3 its main, 2 run as it ends):
// Demyx's HE17_MS105, ten water clones in 60 seconds. One at a time: the first boss's
struct Kh2Mission final {
    std::string name, file;
    std::unique_ptr<Actor> actor;
    std::shared_ptr<const ai::Script> script;
    std::vector<MissionPiece> pieces; // its timers and counters, in its header's order
    unsigned ticks = 0;               // KH2 ticks it has run (its pieces' animations)
    bool time_up = false;             // its timer's end told (74)
    bool shown = true;   // its pieces up (4:14 / 4:15; 4:21): up from its start, as KH2's after its start camera
    bool locked = false; // 4:54 / 4:55
    bool over = false;   // completed, failed, its boss dead or left (4:3, 4:11, 4:16, 4:22)
    std::string end;
};
inline std::unique_ptr<Kh2Mission> kh2_mission;
inline unsigned kh2_mission_serial = 0;

inline std::map<unsigned, std::unique_ptr<Object>> players; // each of Days's players as KH2's scripts see them
inline std::map<unsigned, unsigned> player_actors;          // each player's actor (Ov022_ActorUpdate's), by his object
// each player's sheet as KH2's scripts ask it (World::player_sheet): its record and its PartRam's, in their memory
struct PlayerSheet final {
    std::uint32_t sheet = 0, part = 0;
    int told_hp = -1, told_level = -1;
};
inline std::map<unsigned, PlayerSheet> player_sheets;
// A Days hit as KH2's scripts read it, trigger 6's argument: + 0x26 its reaction's kind, 2 a small one (2:8 asks
// whether it is past 1, FUN_1403ce980; the Strafer's and the cubes' revenge turn on it)
inline std::unique_ptr<Object> days_hit;
// a Days hit's attack as KH2's hit gives it (FUN_140411060's third: its + 0xc the attacker's handle), and the Days
// player who struck with it (its owner: 2:24)
inline std::unique_ptr<Object> days_attack;
inline std::uint32_t days_attack_owner = 0;
// the object KH2's reaction camera watches (1:207, dk::ReactionCamera's target), 0 none: the camera's while a KH2 enemy
// holds this game's player (khdays_kh2_enemies_reaction_camera)
inline std::uint32_t watched = 0;
inline bool camera_warped = false; // 1:193 asked the camera to its place at once
inline Lights lights;              // KH2's lights the scripts make (1:181), the area's

// A Days player a KH2 enemy's script has taken in hand (KH2's player is an actor its enemies' scripts move and make
// play their motions, 1:29; Days's are Days's): from the first motion of the enemy's set it plays until the script
// gives it back (1:13, its idle) or the enemy is gone. Days's control of it stands still meanwhile
// (khdays_kh2_enemies_holding, from Ov022_ActorUpdate); the script puts and turns it, the motion's root moves it, its
// ranges offer reaction commands (20), keep it from being hit (27), its triggers play the enemy's sounds and effects.
struct Held final {
    unsigned enemy = 0; // the Days enemy whose set its motion is from
    int slot = -1;      // that slot (an entry of type 0x14: '0010' its body, '0011' its weapon)
    const kh2::MotionSet::Variant *body = nullptr, *weapon = nullptr;
    float time = 0.0F;
    bool ended = false;
    Vec3 place{}; // KH2's, its feet
    float yaw = 0.0F;
    int offered = -1;        // the reaction command its motion offers now (range 20), -1 none
    std::set<int> follows;   // the commands its motion follows on from now (range 26: KH2's + 0x638, + 0x18c bit 21)
    std::set<int> taken;     // the commands taken in this motion
    bool unhittable = false; // range 27
    ai::Machine *machine = nullptr; // its thread (1:112), in its enemy's machine
    std::uint32_t thread = 0;
    std::set<std::size_t> struck; // its weapon's attack ranges that hit its enemy
    float cued = -1.0F;           // its triggers looked at up to this time
    bool idle = false;            // its motion blends back to the player's own idle (range 2): let go
    Vec3 move{};                  // a script's move of him this tick (1:89), KH2's cm
    Vec3 steer{};                 // a script's steering of him, each tick till another (1:111), KH2's cm
    bool own_queued = false;      // his own motion asked for (1:13): taken once this one ends or blends to idle
    int own_motion = 0;           // which (its number)
    // the blend 1:29 asked for (its fourth value: YS::MOTION::start_motion's blend, above 2 a blend), counted as an
    // enemy's motion's is (Playing, actor.h): its length (0 none or over), its count from 2, the pose before's share
    // left; `fresh` the motion started this tick (it stands at its first frame till the next)
    float blend = 0.0F, blend_count = 0.0F, blend_keep = 0.0F;
    bool fresh = false;
    unsigned started = 0;    // which motion this is (a new one each 1:29): his model blends from the pose before
    std::uint32_t trail = 0; // his weapon's swing's trail running (its handle), 0 none
    unsigned carried_by = 0; // the KH2 actor he is put on (1:18: its Days number), where he stands; 0 none
};
inline std::map<unsigned, Held> held; // by the Days player's object
// the Days players a script hid (1:133) by their object, their models' hiding as last told (khdays_kh2_enemies_holding)
inline std::map<unsigned, bool> hidden_players, hidden_told;

// A Days player a KH2 script turned into a form (1:242, YS::CHANGEFORM::Capture: KH2's player's form 12 -- Luxord's
// card M_EX950_CARD and die M_EX950_DICE): the form's object made an ally (allies) at his place and drawn in his stead
// (his Days model hidden), moved by his own pad as KH2's actions move it (capture_tick: Ts::ACTION_CARD and
// Ts::ACTION_DICE); Days's control of him is locked meanwhile, his pad read first (khdays_kh2_enemies_captured), and
// he is put where the form is (khdays_kh2_enemies_holding). Its time is a form's: two of the drive gauge's bars (YS::SORA's constructor for form 12:
// SHEET::start_drive(2, -1), sstm + 0x68 a bar), less a tick each tick (PLAYER::_OVR_pre_exec; half outside a battle,
// sstm + 0x6c, which a KH2 enemy's field never is)
// and his landed blows' AddDrive x sstm + 0x74 (PLAYER::_OVR_attack_callback); he turns back once it is out and he
// stands on the ground (SORA::_OVR_exec: Revert decidable -- not in the air: a die that keeps jumping stays one), or
// when a script turns him back (1:243)
struct Capture final {
    unsigned anchor = 0; // the Days enemy whose script turned him (its room's collision)
    unsigned form = 0;   // the form's ally (allies)
    unsigned actor = 0;  // his Days actor (khdays_kh2_enemies_holding's), 0 not seen yet
    int entry = 0;
    int action = 0;    // 0x2b the die's, 0x2c its jump, 0x2e the card's; 0 none given yet
    Vec3 inertia{};    // the card's velocity (YS::OBJ + 0x20, KH2's cm a tick)
    float time = 0.0F; // the form's left (SHEET + 0x1b4, ticks)
    // this frame's pad (his actor's, as Days's control reads it): the way held (KH2's yaw), none when not; presses
    std::optional<float> heading;
    bool jump = false, command = false;
    bool hidden = false;
    std::map<unsigned, std::size_t> struck; // the enemies his form's attack ranges struck (their count told)
};
inline std::map<unsigned, Capture> captures; // by the Days player's object
// the actions' numbers as the scripts last gave them (6:28, 6:30), else KH2's own (the PS3 build's: those in its
// .bss are 0 till a script gives them): the die's -- its move on the ground's share, its turn (radians a tick), its
// jump's height, its party jump's -- and the card's -- its walk and run speeds, its turn, its inertia's kept share
// moving and its loss still, its height over the floor and its climb toward it a tick
inline std::vector<float> dice_spec = {0.0F, 0.0F, 50.0F, 5.0F};
inline std::vector<float> card_spec = {4.0F, 4.0F, 0.0F, 0.95F, 0.01F, 100.0F, 3.0F};
// each held player's weapon as his KH2 model drew it (held_weapon_drawn: its bones, KH2's cm in his motion's space, y
// up) and the frame it did, by his object
struct DrawnWeapon final {
    std::vector<kh2::Mat4> bones;
    unsigned frame = 0;
};
inline std::map<unsigned, DrawnWeapon> drawn_weapons;
// the states' names shouted this frame (1:90; FUN_1403cacd0's list at DAT_142a23d50), each with its shouter's handle and
// its delay: sent to every enemy at the next frame's first tick (FUN_1403c9d40, once a frame)
struct Shout final {
    std::string name;
    std::uint32_t from = 0;
    float delay = 0.0F;
};
inline std::vector<Shout> shouts;
// a thread a script started on a player before taking it in hand (Cerberus's 1:112 comes before its 1:29), by object
inline std::map<unsigned, std::pair<ai::Machine *, std::uint32_t>> waiting_threads;
inline unsigned local_player = 0; // this game's own player's object (khdays_kh2_enemies_holding sees it)
inline unsigned local_takes = 0;  // the reaction commands this game's own player took (khdays_kh2_enemies_taken)
// the object types whose control a script took away (1:42, YS::ControlFlag): 0 the player -- Days's players locked as
// Days's own lock locks them (khdays_kh2_enemies_control_off, Ov022_TranslateCommandToAction)
inline std::set<int> controls_off;
// a prize drain asked (1:187) for the pickups of this frame and the next: KH2 sets it for one of its ticks, two of
// which a Days frame runs, and Days takes its prizes once a frame (khdays_kh2_enemies_prize_drain)
inline int drain_frames = 0;
// the enemy whose own pictures draw the player's command menu (1:359; 0 KH2's own: khdays_kh2_enemies_command_image)
inline unsigned command_image_of = 0;
// The player's special command menu an object gives (10:6, kn::trap_obj_set_special_command -> YS::STDOBJ::
// set_special_command: PLAYER_COMMAND opens menu 14, SPECIAL_COMMAND::_check makes it from a table of the game's, its
// commands chosen sent to the object as its target; drawn in the object's own "comd" pictures) till 10:7 takes it off:
// the enemy and its commands, slot by slot (0 none). Final Xemnas's 12 (RcDarkRikuTable_USE, the PS3 build's
// 0x728968: Jump 454, Dark Aura 653, Attack 652) while Riku fights for Sora in his hands
struct SpecialMenu final {
    unsigned enemy = 0;
    std::array<int, 4> commands{-1, -1, -1, -1};
};
inline SpecialMenu special_menu;
// KH2's attacks of their own (2:10, YS::ATTACK_MAGIC), by their address in the scripts' memory: the enemy whose
// script made one (its hits go through its shell), whether KH2's player owns it (its hits his blows), its atkp entry,
// its size and growth, its place and the one before (swept between), the effect it follows, the players it hit
struct AttackObject final {
    unsigned enemy = 0;
    std::uint32_t owner = 0; // the object owning it (+ 0x10: what 2:10 was given; 2:24)
    bool players = false;
    int atkp = 0, group = -1, sub = 0, team = -1;
    float radius = 0.0F, height = 0.0F;
    float grow_ticks = 0.0F, grow_for = 0.0F, grow_radius = 0.0F, grow_height = 0.0F;
    Vec3 at{}, before{};
    bool placed = false;
    std::uint32_t effect = 0;
    std::set<unsigned> struck;
    bool ended = false;    // ended by its script (2:13): let go
    bool expired = false;  // grown, or its effect gone: no more hits, its state still asked (2:14, 2:29)
    bool landed = false;   // it hit (2:39)
    bool on_floor = false; // it met the room's floor (2:41: + 8 bit 4)
    std::uint32_t hit = 0; // the object it hit last (2:44)
    std::uint32_t on_hit = 0, on_hit_value = 0; // its script's routine run when it hits (2:38), 0 none
    // its script's routine run when it is reflected (2:74), 0 none: kept, not run -- the port does not see Days's
    // guard send a KH2 attack back
    std::uint32_t on_reflect = 0, on_reflect_value = 0;
};
inline std::map<std::uint32_t, AttackObject> attacks;

// a reaction command an enemy's script offered this game's own player (1:165: renewed each tick it stands) and the
// VBlank it last did
struct ScriptOffer final {
    unsigned enemy = 0;
    int command = -1;
    unsigned frame = 0;
    std::uint32_t attack = 0; // the attack carrying it (2:51), 0 a script's own offer (1:165)
};
inline ScriptOffer script_offer;
// KH2's player's command menu as an enemy's script gives it (1:237: its four top commands, renewed each tick it
// stands) and the VBlank it last did; the enemy each command chosen goes to. Days's command deck shows it: its main
// rows (Attack, Magic, Items) the menu's first three slots, the fourth slot in one of them chosen as the menu comes
// (`extra_row`); the commands its rows were last drawn with (`labels`, 0 none; `shown`). Drawn as KH2's own menu (an
// enemy's own pictures over the deck: ui/reaction_command.cpp, `drawn` the VBlank it last asked), its four slots and a
// cursor of its own over them (`cursor`), a slot on as Days's deck steps a row (`deck_row` the row seen last)
struct TopOffer final {
    unsigned enemy = 0;
    std::array<int, 4> commands{-1, -1, -1, -1};
    unsigned frame = 0;
    int extra_row = 0;
    bool shown = false;
    std::array<int, 4> labels{};
    int cursor = 0, deck_row = -1;
    unsigned drawn = ~0u;
};
inline TopOffer top_offer;
// KHDAYS_SHOT_STATES=name,name... (with KHDAYS_TRACE_KH2_ENEMIES and KHDAYS_SHOTS_HD): a KH2 enemy entering one of
// those states has the window save pictures of the field at once and 15, 30, 60, 120 and 240 frames on (a reported
// moment caught though a walk's timing changes from run to run)
inline std::vector<unsigned> shot_frames;
// each Days player's pad as Ov022_TranslateCommandToAction last read it (khdays_kh2_enemies_captured): the way his
// D-pad holds, as a KH2 yaw from the camera's turn, none when it holds none
inline std::map<unsigned, std::optional<float>> pad_ways;
inline int last_days_damage = 0; // the last Days hit's damage on any KH2 enemy (a blow on one not hit yet)
inline int local_taken = -1;     // the last of them
inline std::map<unsigned, unsigned>
    player_of_node; // each player's model's scene node -> its object (the models' draws)

// KH2's one event player (FUN_1402cc4b0; a sequence started ends the one before): the sequence, the enemy whose script
// started it, the player whose camera it takes, its KH2 ticks since it started; the place (Days's) and the yaw (KH2's)
// its camera is set from
struct Playing final {
    std::shared_ptr<const kh2::Event> event;
    unsigned enemy = 0, player = 0, ticks = 0;
    float base[3] = {};
    float yaw = 0.0F;
    std::size_t next = 0; // its next command on a frame (KH2's loop waits on each in turn)
    // the screen's trail it started (command 0x17, FUN_14015f250 each tick): the picture before over the new at
    // `alpha`, its quad `size` of the screen, turned `angle` (on by its `turn` a tick for `turn_ticks`)
    struct Trail final {
        float alpha = 0.0F, size[2] = {1.0F, 1.0F}, angle = 0.0F, turn = 0.0F;
        int turn_ticks = 1, turned = 0;
    };
    std::optional<Trail> trail;
    // the camera shake it started (command 0x27, FUN_140528920 each tick): its ticks gone, the eye's offset now (cm,
    // the camera's x right, y up, z ahead)
    struct Shake final {
        kh2::EventShake shake;
        int gone = 0;
        std::array<float, 3> offset{};
    };
    std::optional<Shake> shake;
    // its camera's springs (command 0x4c: dk::EventCamera::start took the game's camera's look-at, eye and field of view,
    // and lookAt moves them toward its keys with ObjCamera::springDamp once a field update): KH2's cm in Days's world;
    // the field of view (radians, vertical) beside the roll, sprung as one
    struct Springs final {
        Vec3 at{}, eye{}, at_target{}, eye_target{}, at_before{}, eye_before{};
        std::array<float, 2> fov{}, fov_target{}, fov_before{};
    };
    std::optional<Springs> springs;
    bool give_back = false; // its command 0x31 + 6 0 reached (m_interpolate)
    // the view its camera last showed (Days's world, half the vertical field of view): what the game's camera takes up
    // from when it is given back
    bool shown = false;
    float shown_eye[3] = {}, shown_at[3] = {}, shown_half = 0.0F;
};
inline std::optional<Playing> playing;
// how the last sequence for this game's player ended, for the game's camera (khdays_kh2_enemies_event_end_view): 0
// none, 1 ended, 2 its last view given back
struct EventEnd final {
    int how = 0;
    float eye[3] = {}, at[3] = {}, half = 0.0F;
};
inline EventEnd event_end;

// KH2's scope camera a script made (1:281: dk::ScopeCamera, ObjCamera's mode 7; Xigbar's sniping) while it stands: the
// object it looks from, its eye's point `height` above the object's place (its head), its zoom, close-up distance and
// the point it looks at (KH2's world). ScopeCamera::update (the PS3 build's, read): a point `closeup` from the target
// toward the head, the eye that point times `zoom` plus the head times 1 - `zoom` (Axa::interVector), looking at the
// target; an event's camera comes first, as KH2's event camera does over ObjCamera
struct Scope final {
    std::uint32_t object = 0;
    float height = 0.0F, zoom = 0.5F, closeup = 10.0F;
    Vec3 target{};
};
inline std::optional<Scope> scope;

// KH2's screen shake (6:0: ryj::SHAKE, its fields' names the PS3 build's): ticks left, the x and y waves' periods and
// their counts, their sizes (KH2's dots, 0 to 16) and how much those change a tick; the shown frame's offset this
// tick (dk::DispFrame::setShakeXYoffset, whole dots), x right and y down
struct ScreenShake final {
    float life = 0.0F;
    std::array<float, 2> period{}, count{}, size{}, speed{};
    std::array<float, 2> offset{};
};
inline std::optional<ScreenShake> screen_shake;

// KH2's screen faded to white and back (0:31 func_screen_whiteout, 0:32 func_screen_whitein: its fader, white): how
// white the screen is (0 to 1) and how much that changes a tick, till it is all white or clear
struct ScreenWhite final {
    float level = 0.0F, step = 0.0F;
};
inline ScreenWhite screen_white;

// Final Xemnas's laser dome (ryj::LAST_XEMNUS_LASER, the PS3 build's: 8:3 starts it on its caster, 8:4 fires it, 8:5
// ends it): 405 lasers on 15 rings of a dome round the world's middle (6 + 3 k on ring k, at the angle 85 - 5.67 k
// degrees: 7 m out x its cosine, 7 m - 47 cm k up), each aimed at the point 1 m over the middle. They fade in from the
// outer ring in, 4 a tick; fired, a laser at a time picked by KH2's chance turns to the player's middle and flies 40 cm
// a tick (its interval down from 10 ticks to 0.5, to 0.15 after 15 s), striking (atkp 1143) what its 50 cm sphere
// meets, bounced off a player whose motion is the Reflect's (254 to 256) within 2 m, gone after a second and back in
// its place; after 26 s they burst at a rising rate. Its effects draw at the game's own lists (KH2's are its memory,
// which the effects write back): the lasers at their matrices (element 14), the hits (70), the bounces (69) and the
// bursts (70) at their points
struct LaserDome final {
    struct Laser final {
        Vec3 start{}, start_way{}; // its place and way on the dome
        Vec3 at{}, way{};          // where it is and its way now (its matrix's rows 3 and 2)
        int phase = 0;             // 0 none, 1 fading in, 2 ready, 3 flying, 5 burst, 6 over
        float time = 0.0F;
        std::array<std::uint8_t, 4> bytes{}; // its matrix's w: alpha, frame, -, size x 10
    };
    unsigned caster = 0; // the Enemy (enemies' or allies' key) it was started on
    int phase = 0;       // 0 set up, 1 appearing, 2 waiting, 3 fired, 4 firing, 5 all at once, 6 done, 7 over
    float time = 0.0F, total = 0.0F, interval = 0.0F, burst_interval = 0.0F, burst_count = 0.0F;
    int left = 0; // the lasers still to appear
    std::vector<Laser> lasers;
    std::vector<kh2::EffectPoint> hits = std::vector<kh2::EffectPoint>(10), bounces = std::vector<kh2::EffectPoint>(10),
                                  bursts = std::vector<kh2::EffectPoint>(200);
    std::array<std::uint32_t, 4> fx{}; // the lasers', hits', bounces' and bursts' effects
    int hum = 0;                       // its looping sound (374040)
};
inline std::optional<LaserDome> laser_dome;
// The map hidden (8:6, LAST_XEMNUS_LASER::Optimize: dk::Map::setHideGroup(0), hideSky, Glow::off -- every draw entry
// of its arena's map, eh20, is in group 0, its floor too: the world black but for the objects) till 8:7 shows it again;
// the port leaves the room's models out (khdays_kh2_enemies_map_hidden), whatever room the battle is in
inline bool map_hidden = false;

// KH2's party AI's parameters (the mod's kh2/friend_param.bin: kn::friend_param_data, little-endian, which the tool
// takes out of KINGDOM HEARTS II FINAL MIX.exe -- its first 19 words each member's record's offset; a record
// kn::FriendParam, 0xe0 bytes: its follow ranges, its battle's, its ratios and distances, its actions', hops' and
// specials' counts and offsets from the record)
struct FriendAction final { // kn::FriendActionParam (0x80 bytes)
    int id = 0, motion = 0, need_mp = 0;
    std::uint32_t flag = 0;
    float range_zx[2]{}, range_y[2]{}, jump = 0.0F;
    float check_y[2]{}, disable_y_range = 0.0F, disable_y = 0.0F, cure = 0.0F;
    std::array<std::uint8_t, 0x80> raw{}; // as read: 10:35 gives the scripts a copy
};
struct FriendHop final { // kn::FriendHopParam
    float angle = 0.0F, height = 0.0F, speed = 0.0F, accel = 0.0F, time = 0.0F;
};
struct FriendParams final {
    float follow[4][2]{}; // each move mode's (stand, walk, run) least and most distance, out of battle
    float battle_player[4][2]{}, battle_enemy[4][2]{}; // in battle, to the player and to its target
    float search_range = 0.0F, lost_distance = 0.0F;   // + 0x68, + 0x6c
    float idle_time[2]{}, move_time[2]{};              // + 0x70, + 0x78
    std::vector<FriendAction> actions;
    std::vector<FriendHop> hops;
};

// PREF_PARTY (03system.bin's pref "prty", by the entry's weapon part: YS::PREF::Party, YS::SPEC::set): its walk and
// run speeds (cm a tick), its jump's height, its turn a tick, how long it lies stunned
struct PartySpec final {
    float walk = 2.0F, run = 8.0F, jump = 385.0F, turn = 0.262F, stun_time = 1800.0F;
};

// A member's actions (kn::FriendPersonality's table, its own class's: the PS3 build's kn::Riku's at 0x7f5ed8, nine
// kn::FriendAction records -- its name, attribute, routine, parameter's number, ability, item and script's label)
enum class FriendDo { HpItem, MpItem, Idle, Move, Attack, Cure };
struct FriendRow final {
    const char *name;
    std::uint32_t attribute; // bit 1 an attack, bit 9 one kept on while it can go on
    FriendDo does;
    int param, ability, item;
    const char *label;
};

// A party member's weapon (YS::WEAPON, YS::PARTY::set_weapon): its own model and motions, attached to its member's
// hand's bone (03system.bin's sklt by his skeleton's number: PARTY::hand_to_bone), its motion his own synced
// (MOTION::sync: the same number and time in its set, his mode's), its attack ranges his blows on its shapes
struct PartyWeapon final {
    std::string mdlx; // its model's path (the draw's)
    std::shared_ptr<const kh2::MotionSet> set;
    std::shared_ptr<const kh2::MotionSkeleton> skeleton;
    std::unique_ptr<kh2::MotionPlayer> player;
    std::unique_ptr<SetMotions> motions;
    std::vector<Shape> shapes;
    int hand = -1;
    std::vector<kh2::Mat4> pose; // its bones in its member's model space (on his hand)
    std::map<std::size_t, std::set<unsigned>> struck;
    int motion = -1;
    float time = 0.0F;
};

// A party member KH2's AI moves (kn::Friend): one of the allies (the battle's partner, Kind::with's FRIEND_1)
struct Friend final {
    unsigned ally = 0;
    const FriendParams *params = nullptr;
    PartySpec spec;
    const std::vector<FriendRow> *table = nullptr;
    int mode = 0;             // Friend::moveMode_: 0 stands, 1 walks, 2 runs
    float speed = 0.0F;       // its speed now, cm a tick (movement_'s w)
    Vec3 way{};               // its movement's way
    unsigned target = 0;      // the KH2 enemy it goes for (enemies' key), 0 none
    int action = -1;          // its table's row it does now, -1 none
    float action_time = 0.0F; // Friend::actionTimer_
    std::array<int, 3> history{-1, -1, -1};
    bool attacking = false;
    int attack_motion = -1;
    bool own_turning = true; // Friend::enableSystemWishDir
    std::uint32_t watched = 0;
    int use_mp = 0;
    float mp = 100.0F;
    // its sheet's health (YS::SHEET::AllocFriend: its character's, character_health, full; 0 none known: no enemy's
    // attack reaches it), its character (its part number), whether it lies knocked out (+ 0x7f8 bit 4,
    // BTLOBJ::_OVR__dead) and for how many ticks still (+ 0xb40), the motions of the damage reaction it plays (none:
    // -1) -- its states under 300 held while it plays, KH2's party AI waiting
    int character = 0;
    int hp = 0, hp_most = 0;
    bool knocked = false;
    float knocked_for = 0.0F;
    int hurt_motion = -1;
    std::uint32_t stars = 0; // its knock-out stars (the system effects' 14, + 0xb44), 0 none
    std::map<std::size_t, std::size_t> struck;
    std::shared_ptr<PartyWeapon> weapon; // its weapon, none when its kind names none
};
inline std::map<unsigned, Friend> friends; // by their ally's number
// The KH2 collision type of a shape (its kind's low byte: YS::COLLISION's TYPE_*): 1 TYPE_OBJ, an object's body (what
// LASER::check_obj_hit tests, YS::OBJ::get_collision(1)); 2 TYPE_HIT, the spheres on its bones an attack strikes; 16
// TYPE_STUN, where its knock-out stars stand
constexpr int kTypeObj = 1, kTypeHit = 2, kTypeStun = 16;

// The party members an enemy's attacks may reach: each standing, its health known
template <typename F> void each_member(F &&f)
{
    for (auto &[id, member] : friends) {
        const auto it = allies.find(id);
        if (it == allies.end() || !it->second || it->second->released || !it->second->actor || member.hp_most <= 0)
            continue;
        f(*it->second, member);
    }
}

// KH2's object pictures (1:285: YS::STDOBJ::show_picture, a dk::Layout of its archive's first entry of type 0x2e) by
// handle: the object, its kind (the archive: kh2/obj/<name>.a.us, its pictures' HD pages after its effects'), the
// layout shown, the count of its changes (Tz::Layout::setNum: the layout started again), how far it is moved (KH2's
// dots). ui/kh2_pictures.cpp plays and draws them; a layout over (none of its sequences playing nor looping:
// Tz::Layout::update) is gone, as a dk::Layout ends itself
struct Picture final {
    std::uint32_t object = 0;
    const KindFiles *kind = nullptr;
    int number = 0;
    unsigned changes = 0;
    float x = 0.0F, y = 0.0F;
};
inline std::map<std::uint32_t, Picture> pictures;
inline std::uint32_t next_picture = 0x7c000000u; // handles out of the scripts' memory's addresses

// One of KH2's commands (03system.bin's "cmd", 0x30 bytes each: OpenKH's 03system page): what a reaction command
// sends when taken (its argument), the one it follows (require: a range 26 of that number must be on), its name's text
struct Command final {
    int argument = 0, require = 0, text = 0;
    std::uint32_t flags = 0; // + 0xc: 0x10 offered in battle, 0x40 only while the player's motion follows its require
};
constexpr float kPlayerMiddle = 0.725F; // a player's middle over his feet (Days's units): half kPlayerHeight

// KH2's Soldier's battle entry (M_EX010's script names 33 to 2:9): the measure a KH2 enemy's health is taken in
constexpr int kSoldierEntry = 33;
// How many of the player's blows KH2's Soldier stands, as it falls to Sora's with the keyblades of its level: a tuning
// value (KH2's own tables leave it to his keyblade and his abilities: from 2 to 90 at level 99 by his strength alone)
constexpr double kSoldierBlows = 4.0;
// The size of a Days blow on a KH2 enemy: the first hit's damage in the area (Days's, before KH2's element rate), the
// measure of every KH2 enemy's health there; 0 until then
inline int reference_blow = 0;

// A battle entry (00battle.bin's enmp, OpenKH's 00battle page: a u32 count at + 4, then 92-byte records, + 0 the
// entry's number, + 2 its level -- the Data Organization's 99 --, + 4 its 32 health values, the first its own: KH2
// times it by LVPM's for the level, which the share between two entries leaves out); zeros when the mod's file has
// no such entry
struct BattleRow final {
    int health = 0, level = 0;
};

// KH2's level parameters (00battle.bin's lvpm, YS::LEVELPARAM: 12 bytes a level from 1 -- + 0 its health's
// percentage, + 2 strength, + 4 defence, + 6 the most and + 8 the least of an enemy's strength over a defence, + 10
// experience); none past its levels or without the file
struct LevelParam final {
    int health = 0, strength = 0, defence = 0, most = 0, least = 0;
};

// Enemies whose end ("dead": its effect, its fading) goes on once its Days enemy is gone -- Days defeats it at once and
// reuses it for the next wave -- moved on and drawn where it was as the 3D list closes, until it has faded out
inline std::vector<std::unique_ptr<Enemy>> ghosts;

// KH2's player as its enemies' attacks find him: Sora's shapes (P_EX100's: a cylinder 85 cm high, 35 round, and
// spheres on his bones up to his head) stand about 1.45 m high. Days's player's own sphere (half a metre round, on
// the ground) left the attacks KH2 aims at a standing Sora's chest and head above him (Cerberus's bites).
constexpr float kPlayerHeight = 1.45F;
// how far above and below a held player the ground is looked for (KH2's cm): a step up, a fall down
constexpr float kGroundSearchUp = 100.0F, kGroundSearchDown = 1000.0F;
constexpr float kHeldRadius = 30.0F; // a held player's body round, kept off the walls (KH2's cm)
constexpr float kStep = 30.0F;       // a fall from higher than this (KH2's cm) lands with the landing motion

// KH2's player's files (the mod's kh2/obj/P_EX100.mdlx, Sora's): his skeleton, which the enemies' motions for the
// player are made for (Roxas's P_EX110 has its bodies' rest places), and his body's spheres on his bones (his shapes of
// kind 0x302: his head, chest, waist and arms)
struct PlayerFiles final {
    std::shared_ptr<const kh2::MotionSkeleton> skeleton;
    std::vector<Shape> spheres;
    // his keyblade (the mod's kh2/obj/W_EX010.mdlx and .a.us: the Kingdom Key): its skeleton, which the enemies' motions
    // for his weapon ('0011') are made for, in its own axes on his bone `hand`; its effects (its swing's trail)
    std::shared_ptr<const kh2::MotionSkeleton> weapon;
    int hand = -1;
    KindFiles weapon_fx;
};

// An attack in KH2's battle table (00battle.bin's atkp, 48 bytes each, the PS3 build's YS::ATTACKPARAM: + 2 its
// number, + 6 its power, + 8 its team, + 10 the reaction it gives an enemy, YS::DAMAGE's REACTION_*; OpenKH's
// 00battle page; its first entry of that number): its power, team and reaction, 0 when the mod has no
// kh2/00battle.bin or no such attack
struct AttackParam final {
    int power = 0, team = 0, reaction = 0;
    int hitback = 0x7fff; // + 0xc, a short: how far its hit throws (KH2's cm), 0x7fff the battle constant's
    int add_drive = 0;    // + 0x29 (the PS3 build's ATTACKPARAM::AddDrive): what a landed one takes off a form's time
    int revenge = 0;      // + 0x2a (RevengeDamage): the karma a landed one adds (YS::ENEMY::_OVR_exec_damage)
    int kind = 0;         // + 4 (Type): 5 and 6 a cure's (YS::SHEET::calc_damage, BTLOBJ::add_damage), else damage
    int element = 0;      // + 9: 0 physical, 1 fire, 2 blizzard, 3 thunder ...
    int blow_along = 0;   // + 0xe, a short: a blow's speed along (YS::DAMAGE::get_blow; + 0xc then its speed up)
    // its reflection's (the PS3 build's ATTACKPARAM): what it does itself once reflected (+ 0x13, RefactSelf) and what
    // it makes of the attack it reflects (+ 0x14, RefactOther) -- 0 REFLECT, 1 GUARD, 2 NULL --, its owner's motion
    // then (+ 0x15) and push back (+ 0x16, a short: KH2's cm, under 0 50), the state it enters then (+ 0x18: a name, at
    // its offset from the record; "" none), the cone of the other's ways it reflects (+ 0x22, degrees: 0 any) round
    // its owner's facing turned by + 0x23 (a signed byte, half degrees: ATTACK_WORK::is_reflect_range)
    int refact_self = 2, refact_other = 2;
    int reflect_motion = 0, reflect_hitback = 0;
    std::string reflect_act;
    int reflect_range = 0, reflect_angle = 0;
};

// The KH2 attack a Days player is struck with while Days's hit runs (hit_player): his block answers it within the hit
// (Ov022_TryBlockHit sends its attacker its block packet: khdays_kh2_enemies_reflected), reflected once the hit is
// over. The enemy, the attack's atkp entry and its record (motion_attack's, or its own: 2:10), the attack object
struct Delivering final {
    unsigned enemy = 0;
    int atkp = 0;
    std::uint32_t record = 0;
    AttackObject *object = nullptr;
    unsigned guarded_by = 0; // the player whose block it met, 0 none
};
inline Delivering delivering;

// The players a reflection throws back (YS::PARTY::_OVR_exec_reflect), by their objects: the push (Days's units) his
// actor is given at its next update (khdays_kh2_enemies_holding)
inline std::map<unsigned, std::array<float, 3>> recoils;
// ranges 3, 24 and 30: the motion alone carries it up and down (the air classes' movement, FUN_1403cb100 and
// FUN_1403cb500, adds no pull while + 0x18c has 0x1000020 or bit 6)
constexpr std::uint32_t kMotionAir = 1u << 24; // range 30 (FUN_14040d810), 1:60's "off the ground" with bit 5
constexpr std::uint32_t kNoGravity = (1u << 5) | (1u << 6) | kMotionAir;

// The walls swept by a body's sphere along its way (KH2's cm): its radius this when none is given (no larger than
// Days's own sphere's: Days's collision keeps it that far off the walls, a larger one would start in them), its centre
// this much over its radius above its feet -- a step's height under it, as Days's own mover sweeps its 0.56 m sphere
// 0.65 m up (Actor_ResolveMove) -- and kept this far off what it meets
constexpr float kWallRadius = 45.0F, kWallStepOver = 30.0F, kWallGap = 1.0F;
constexpr float kNearEnough = 0.25F; // Days's units

// The shells' own draws, by render object: a Days enemy's model has no link of its own to its actor that the port
// reads (its node's + 0x24, a battle actor's, is not one: traced), so a draw is its shell's when it is its class's own
// model drawn near the shell's place (kNearEnough; the nearest shell of that class); the room's models and the
// enemies' shared effects are other files. Kept while it still stands there.
inline std::map<unsigned, unsigned> shell_draws;

class Field final : public World {
public:
    std::uint32_t appear(Actor &by, std::uint32_t entry) override;

    void dead_mark(Actor &a) override;

    bool walk_path(Actor &a, int number, std::vector<Vec3> &out) override;

    float system_value(std::uint32_t offset, float fallback) override;

    bool bone_point(Actor &a, int bone, const Vec3 &local, Vec3 &out) override;

    void health(Actor &a) override;

    void battle_entry(Actor &a, int entry) override;

    void set_health(Actor &a, int hp) override;

    float ground(Actor &a) override;

    void broadcast(Actor &a, const std::string &name, float delay) override;

    std::uint32_t effect(Actor &a, int number, int, int) override;

    std::uint32_t effect_on(Actor &a, int number, int, int, Object *target) override;
    void attack_made(std::uint32_t at, Actor &by, Object *owner, int atkp, int group, int sub) override;
    void attack_size(std::uint32_t at, float radius, float height) override;
    void attack_place(std::uint32_t at, const Vec3 &place) override;
    void attack_grow(std::uint32_t at, float ticks, float radius, float height) override;
    void attack_follow(std::uint32_t at, std::uint32_t effect) override;
    void attack_team(std::uint32_t at, int team) override;
    void attack_strike(std::uint32_t at, Object *target) override;
    void attack_end(std::uint32_t at) override;
    bool attack_finished(std::uint32_t at) override;
    bool attack_expired(std::uint32_t at) override;
    bool attack_landed(std::uint32_t at) override;
    std::uint32_t attack_struck(std::uint32_t at) override;
    bool attack_way(std::uint32_t at, Vec3 &out) override;
    bool attack_can_hit(std::uint32_t at, Object *target) override;
    void attack_line(std::uint32_t at, const Vec3 &now, const Vec3 &before) override;

    Enemy::Running *running_effect(std::uint32_t handle, Enemy **of = nullptr);
    std::uint32_t effect_in_world(Actor &a, int number, const Vec3 &place) override;
    bool effect_alive(std::uint32_t handle) override;
    bool effect_where(std::uint32_t handle, Vec3 &out) override;
    void effect_unbind(std::uint32_t handle) override;
    void effect_move(std::uint32_t handle, const Vec3 &place) override;
    void effect_turn(std::uint32_t handle, const Vec3 &angles) override;
    void effect_fade(std::uint32_t handle, float ticks) override;
    void effect_scale(std::uint32_t handle, const Vec3 &scale) override;
    void effect_colour(std::uint32_t handle, const std::array<float, 4> &colour) override;
    void voice(Actor &a, int id, int priority) override;
    int sound(Object &o, int id, bool loop) override;
    void sound_fade(int handle, float ticks) override;
    void control(int type, bool off) override;
    void prize_drain(bool on) override;
    void command_image(Object &of, bool on) override;
    bool camera_view(Vec3 &eye, Vec3 &at) override;
    void scope_begin(Object &o, float height) override;
    void scope_end() override;
    void scope_zoom(float zoom) override;
    void scope_closeup(float distance) override;
    void scope_target(const Vec3 &target) override;
    bool projection(const Vec3 &point, Vec3 &out) override;
    std::uint32_t show_picture(Object &o, int number) override;
    void picture_change(std::uint32_t handle, int number) override;
    void picture_place(std::uint32_t handle, float x, float y) override;
    bool pad_held(int bit) override;
    bool player_movement(Object &o, Vec3 &way, float &push) override;
    void layers(bool on) override;
    bool is_stopped(Object &o) override;
    void effect_pause(std::uint32_t handle, bool paused) override;
    Actor *xemnas(int n) override;
    void laser_start(Actor &caster) override;
    void laser_attack() override;
    void laser_end() override;
    void laser_optimize(bool on) override;
    bool event_playing() const override;
    bool in_battle() override;
    Friend *friend_of(Actor &a);
    void friend_target(Actor &a, int mode, Object *target) override;
    void friend_turning(Actor &a, bool own) override;
    void friend_action_clear(Actor &a) override;
    std::uint32_t friend_action_param(Actor &a, int id) override;
    bool friend_ability(Actor &, int) override;
    float magic_points(Object &o) override;
    void magic_use(Object &o) override;
    void magic_use_set(Object &o, int points) override;
    void friend_watch(Actor &a, std::uint32_t effect) override;
    bool friend_watching(Actor &a, std::uint32_t effect) override;
    bool hooked(Object &o) override;
    void special_command(Object &o, int number) override;
    void special_command_off(Object &o) override;
    void screen_shake(float life, float period_x, float period_y, float size_x, float size_y, float speed_x,
                      float speed_y) override;
    void screen_white(bool out, float frames) override;
    int bg_cross(Actor &a, const Vec3 &from, const Vec3 &to, int kind, Vec3 &hit) override;
    bool bg_normal(int polygon, Vec3 &normal) override;
    void camera_watch(Object &o) override;
    void camera_home() override;
    void camera_warp() override;
    std::uint32_t light_create(int kind) override;
    void light_flag(std::uint32_t handle, int bit) override;
    void light_colour(std::uint32_t handle, int r, int g, int b, float ticks) override;
    void light_fade_out(std::uint32_t handle, float ticks) override;
    void light_fade_in(std::uint32_t handle, float ticks) override;
    bool attack_on_floor(std::uint32_t at) override;
    bool ray(Actor &a, const Vec3 &from, const Vec3 &to, int kind, Vec3 &hit) override;
    bool wall_sweep(Actor &a, const Vec3 &place, float radius, Vec3 &way, bool slide) override;
    bool record_distance(std::uint32_t record, const Vec3 &point, float &out) override;
    void attack_on_hit(std::uint32_t at, Actor &, std::uint32_t routine, std::uint32_t value) override;
    void attack_on_reflect(std::uint32_t at, Actor &, std::uint32_t routine, std::uint32_t value) override;
    bool aim_point(Object &o, Vec3 &out) override;

    void stop_effect(std::uint32_t handle) override;
    void effect_loop_end(std::uint32_t handle) override;
    void offer(Actor &by, int command, Object *source) override;
    bool pad_pressed(int bit) override;
    void override_top(Actor &by, const std::array<int, 4> &commands, Object *target) override;
    std::vector<Actor *> actors() override;
    void leave(Actor &a) override;
    void stop_all(int stop, Object *but, Object *nor) override;
    void signal(int id, int argument) override;
    void set_count(Kh2Mission &m, int index, int n);
    void check_warning(const Kh2Mission &m, MissionPiece &p, int value);
    void mission_warning(int kind, int index, bool on) override;
    void set_gauge(Kh2Mission &m, int index, float level);
    float mission_gauge(unsigned number, int index, float value) override;
    Object *attack_owner(std::uint32_t at) override;
    bool star(const Object &o) override;
    std::uint32_t player_sheet(const Object &o) override;
    int player_health(const Object &o) override;
    int add_player_health(const Object &o, int amount, int bar) override;
    int mission(unsigned number, int first, int second) override;
    void vacuum(std::uint32_t record, bool alive) override;
    void stop_end_all(int stop) override;
    std::vector<Actor *> objects() override;
    std::vector<ShapeAt> shapes(Actor &a, int kind) override;

    bool takes_off_later(Actor &a) override;

    bool on_screen(Actor &a) override;

    Object *player(Actor &a) override;

    bool player_motion(Object &o, Actor &by, int slot, float blend) override;
    bool capture_motion(Actor &target, Actor &by, int slot, float blend) override;
    void player_own_motion(Object &o, int id) override;
    void player_own_motion_now(Object &o, int id) override;
    void player_place(Object &o, const Vec3 &place) override;
    void player_move(Object &o, const Vec3 &way, float speed) override;
    void player_carry(Object &o, Object *by) override;
    void player_hide(Object &o, bool hidden) override;
    void player_steer(Object &o, const Vec3 &way, float speed) override;
    void player_yaw(Object &o, float yaw) override;
    void player_let_go(Object &o, const char *why) override;
    void player_thread(Object &o, ai::Machine &m, std::uint32_t id) override;
    void carry(Actor &carried, Object &by, int bone, int flags) override;
    void uncarry(Actor &carried) override;
    void event(Actor &a, const std::string &name, Object *target) override;
    void capture_form(Actor &by, int entry) override;
    void capture_end(Actor &by) override;
    int player_form(Actor &by) override;
    void form_action(Actor &by, int action) override;
    void form_spec(int action, const std::vector<float> &numbers) override;
};

// tick.cpp -- The shell's life: the Days enemies a KH2 row makes, the area they stand in, each KH2 tick of theirs.
void trace_calls(ai::Calls &calls);
void shot_on_state(const std::string &name);
void allies_close();

// files.cpp -- The mod's files: a kind's model, motions, effects, sounds and events; KH2's system tables.
// (a kind's .a.us -- its effects, its pictures, its command images -- the mod's path to it in UTF-8, "" none)
const std::u8string &kind_archive(const KindFiles &k);
std::vector<std::uint8_t> read_file(const fs::path &p);
fs::path u8(const std::string &s);
fs::path kh2_file(const std::string &rel, const std::string &first = {});
std::string kh2_mod_of(const std::string &rel, const std::string &first = {});
std::vector<Shape> read_shapes(const std::vector<std::uint8_t> &mdlx, std::uint32_t *default_groups = nullptr);
int trigger_sound(const kh2::MotionAttributes::Trigger &t);
const KindFiles &system_effects();
Mod &mod();
KindFiles *files_of(const std::string &name, bool form = false);
std::uint32_t entry_record(const std::string &name);
std::string entry_name(std::uint32_t number);
bool enemy_entry(const std::string &name);
const Command *command(int id);
const std::vector<std::uint8_t> &system_constants();
int weapon_bone(int character);
const PlayerFiles &player_files();
const kh2::MotionSkeleton *player_skeleton();
const AttackParam &attack_param(int atkp);
int attack_key(const Actor &a, int atkp);
int attack_power(int atkp);
int attack_team(int atkp);
bool attack_reacts(int atkp);
const std::vector<DamagePattern> &damage_patterns();

// field.cpp -- The scripts' world as Days's field answers it (Field, World's calls).
Field &the_field();

// enemies.cpp -- An enemy set going, posed and moved a tick: its motions, its walls, its sounds and effects cast.
unsigned shell_of(const Enemy &e);
Enemy *enemy_or_ally(unsigned days);
unsigned days_object(const Object &o);
bool in_home(const Kind &kind, float place[7]);
void forget(unsigned days);
Enemy *start(unsigned days, KindFiles &k, const KhdaysKh2Shell &s, bool made = false);
Enemy *start_ally(KindFiles &k, const Vec3 &place, float yaw, unsigned anchor, bool made = false, int argument = 0);
std::vector<kh2::Mat4> pose_at(kh2::MotionPlayer &player, const kh2::Motion &motion, float ticks);
void pose(Enemy &e);
void days_point(const Enemy &e, const KhdaysKh2Shell &s, int bone, const Vec3 &at, float out[3]);
void sound(const Enemy &e, int id);
std::uint32_t cast(Enemy &e, int number, unsigned player = 0, bool on_weapon = false, const KindFiles *files = nullptr);
void weapon_trail(Enemy &e, unsigned object, Held &h, bool swing);
bool effect_place(const Enemy &e, std::uint32_t handle, Vec3 &out);
bool wall_sweep(unsigned shell, const Vec3 &place, float radius, Vec3 &way, bool slide);
void one_tick(Enemy &e, const KhdaysKh2Shell &s, Vec3 &place);
Enemy *find(unsigned days);

// attacks.cpp -- Attacks: an enemy's own, what they strike, those reflected.
void drop_attacks(unsigned days);
void landed(Enemy &e, unsigned object, int atkp, bool star = false);
std::uint32_t motion_attack(Enemy &e, int atkp);
void attack(Enemy &e, const KhdaysKh2Shell &s);
int hit_pattern_reaction(const Actor &a, int first, bool not_player = false);
void blow_reaction(Enemy &t, int atkp);
void strike_enemies(Enemy &e, const KhdaysKh2Shell &s, std::size_t r, int group, int atkp);
void hitback_push(int atkp, float dx, float dz, float push[3]);
void strike(AttackObject &o, Object &target);
void reflect_motion(Enemy &e, const AttackParam &p);
bool clashed(Enemy &e, unsigned player, Object &who);
int hit_player(Enemy &e, unsigned player, int atkp, const float push[3], AttackObject *object = nullptr);
void attack_objects(Enemy &e);

// health.cpp -- Health: KH2's numbers shown in Days's fields, the hits an enemy takes.
const BattleRow &battle_row(int entry);
int battle_health(int entry);
std::optional<LevelParam> level_param(int level);
int character_health(int character);
int character_defence(int character, int level);
void show_health(const Enemy &e);

// held.cpp -- The players in hand: held in an enemy's motions, carried, given back.
void let_go(unsigned object, const char *why);
void put_on_carrier(Enemy &e, Vec3 &place);
std::uint16_t frame_pressed();
std::uint16_t frame_held();
void hold_tick(Enemy &e, unsigned object, Held &h);

// commands.cpp -- Reaction commands and the scripts' command menus on Days's deck.
std::set<int> groups_on(const Enemy &e);
float footprint(Enemy &e, const KhdaysKh2Shell &s);
void zone_offer(Enemy &e);
void attack_offers(Enemy &e);
int script_offered();
bool top_menu_standing();
bool command_usable(int id);
Enemy *zone_offerer();
void take_command(Enemy &e);
void deck_sync();

// events.cpp -- Events, cameras and the screen: event cameras, the scope, shakes, the white, lights, pictures.
void end_event();
void screen_white_tick();
void screen_shake_tick();
void event_tick(const Enemy &e);

// party.cpp -- A party member (Riku): KH2's party AI, its weapon, its hurts.
void friend_start(Enemy &e);
const FriendAction *friend_action(const Friend &f, int id);
void friend_tick(Friend &f, Enemy &e, Vec3 &place);
void draw_weapon(const Friend &f, const float view[16], const float projection[16], unsigned alpha);
void friend_blows(Friend &f, Enemy &e, const KhdaysKh2Shell &s);
bool member_reached(const Enemy &m, const Vec3 &centre, float radius, float up, int type);
void member_hurt(Enemy &by, Enemy &m, Friend &f, int atkp);

// laser_dome.cpp -- Final Xemnas's laser dome.
void laser_tick();
void give_dome(const Enemy &e, Enemy::Running &r);
void take_dome(Enemy::Running &r);

// capture.cpp -- A player turned into a KH2 object (Luxord's die and card).
Capture *capture_of_form(unsigned form);
void capture_tick(Capture &c, Enemy &e, Vec3 &place);
void capture_blows(Capture &c, Enemy &e, const KhdaysKh2Shell &s);
void keep_off_enemies(const Enemy &form, Vec3 &place);

// draw.cpp -- Drawing: KH2 models and effects in Days's 3D, the players held, the ghosts of the fallen.
float drawn_fade(const Enemy &e);
int model_effects(std::uint32_t record);
void row_matrix_mul(const float a[16], const float b[16], float out[16]);
void inverse_affine(const float m[16], float out[16]);
void model_matrix(const Actor &a, const float place[3], float out[16]);
kh2::EffectMatrix element_world(const std::vector<kh2::Mat4> &pose, const Element &el);
void draw_effects(Enemy &e, const float view[16], const float projection[16]);
bool field_camera(float camera[16]);
float apart(const float a[3], const float b[3]);
void ghosts_close();

// mission.cpp -- KH2's missions: their pieces, counts, gauges and timers.
void mission_damage(std::uint32_t object, std::int64_t damage);
MissionPiece *mission_piece(Kh2Mission &m, int kind, int index);
void start_mission(const std::string &name);

} // namespace khdays::kh2_enemies::shell

#endif
