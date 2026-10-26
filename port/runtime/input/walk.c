/* The Walk mod (runtime/mods/installed.h, days-mods/walk.txt): the analog gait below the run threshold drawn with
 * a walk of the game's own (KH 358/2 Days's event walk e_xxx_908, on each character's field skeleton; the mod's
 * animations/walk/<code>.khw), instead of the run slowed down. The clip stores joint rotations and local
 * offsets; root travel stays in gameplay. Everything here runs on the game's thread. */
#include "walk.h"
#include "input.h"
#include "../core/window.h"
#include "../core/clock.h"
#include "../core/origin.h"
#include "../core/rom.h"
#include "../core/session_log.h"
#include "../mods/installed.h"
#include "../sound/sound_bridge.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int QueryActiveStateOrDelegate(void);
extern int GetEntryField20ByIndex(int index);
const char *khdays_link_play_sound(const char *sequence_archive, const char *entry); /* scripts/game_link.h */
static void mix(const float *a, const float *b, float t, float *q);
static void quat_from_matrix(const float *m, float *q);

typedef struct Clip {
    char code[3];
    unsigned frames, joints;
    float fps;
    float *data;
    /* the frames a foot is planted (the clip's STEP trailer), where the footstep is played */
    unsigned steps[8], step_count;
    /* an optional clip for the weapon's own model (<code>_w.khw, two joints: the holding hand in the player
     * model's space frame by frame, and that hand as the idle holds it) -- for the weapons the game places
     * itself as a scene object of their own rather than hangs on a joint of the player's skeleton (Zexion's
     * book, Luxord's cards, Xaldin's lances), so that they ride in the walk's hand as the idle
     * holds them: `rest` is where the game puts the weapon against the model while the character stands */
    float *weapon;
    unsigned weapon_hands; /* the hands the clip gives (two joints each): one, or Xaldin's two lances */
    float rest[2][16];
    int rest_known, standing, weapon_applied, weapon_told;
    /* an optional second clip for the weapon out (<code>_k.khw; the mod ships Roxas's, his Keyblade pointed
     * forward, the plain walk being his unarmed one): its joints that differ from the walk's, and the one that
     * tells the two apart most */
    float *held;
    unsigned char held_joint[128];
    int decider;
    int applied;
    /* a Keyblade's keychain (the HANG trailer, of the armed clip when there are two): node `hang_node` of the
     * models in ba/ch/<hang_folder>/w_.p2 turned on every frame so that it hangs from the pommel */
    char hang_folder[4];
    unsigned hang_node;
    float *hang;
    int hang_applied;
} Clip;
static Clip clips[32];
static unsigned clip_count;
static Clip *drawing, *drawn_clip; /* the clip being applied; the local player's, kept for its weapon's draw */
static unsigned seen_frame;
static int local_actor, local_node, target_walk;
static float weight, pace = 1, phase;
/* since the stick let go, and since the game stands again (the walk's way out) */
static float settle, stand;
/* how much the right arm holds the Keyblade (0 the walk's own swing, 1 the run's hold), easing
 * toward what the game's own state says: the set of motions it plays, the armed ones or Roxas's
 * unarmed ones (khdays_walk_draw_begin). */
static float held, held_target;
static u64 drawn_at;

/* What may follow a KHW1 clip's frames (read_khw): "STEP", a count and that many frames (u32), the footsteps;
 * "HANG", a weapon file's folder (4 bytes: ba/ch/<folder>/w_.p2), a node of its models, the frame count and the
 * node's rotation on every frame (4 f32), a Keyblade's keychain hanging from the pommel. */
typedef struct Trailers {
    unsigned steps[8], step_count;
    char hang_folder[4];
    unsigned hang_node;
    float *hang; /* NULL without one */
} Trailers;

static int read_trailers(FILE *f, unsigned frames, Trailers *t)
{
    char magic[4];
    memset(t, 0, sizeof(*t));
    while (fread(magic, 1, 4, f) == 4) {
        if (!memcmp(magic, "STEP", 4)) {
            if (fread(&t->step_count, 4, 1, f) != 1 || t->step_count > 8)
                return 0;
            for (unsigned i = 0; i < t->step_count; ++i)
                if (fread(&t->steps[i], 4, 1, f) != 1 || t->steps[i] >= frames)
                    return 0;
        } else if (!memcmp(magic, "HANG", 4)) {
            unsigned count;
            if (t->hang || fread(t->hang_folder, 1, 4, f) != 4 || fread(&t->hang_node, 4, 1, f) != 1 ||
                t->hang_node > 63 || fread(&count, 4, 1, f) != 1 || count != frames)
                return 0;
            t->hang = malloc((size_t)frames * 4 * sizeof(float));
            if (!t->hang || fread(t->hang, sizeof(float), (size_t)frames * 4, f) != (size_t)frames * 4)
                return 0;
            for (size_t i = 0; i < (size_t)frames * 4; ++i)
                if (!isfinite(t->hang[i]))
                    return 0;
        } else
            return 0;
    }
    return fgetc(f) == EOF;
}

/* A KHW1 file of the mod's (animations/walk/<name>.khw): its data, NULL when none or unusable; `trailers` takes
 * what follows the frames (NULL: read and left). */
static float *read_khw(const char *name, unsigned *frames, unsigned *joints, float *fps, Trailers *trailers)
{
    char relative[128], path[1024], magic[4];
    Trailers t;
    if (trailers)
        memset(trailers, 0, sizeof(*trailers));
    if (!khdays_mod_installed(KHDAYS_MOD_WALK))
        return NULL; /* the game's own run, slowed, as without the mod */
    snprintf(relative, sizeof(relative), "animations/walk/%s.khw", name);
    if (!khdays_mods_find(relative, path, sizeof(path)))
        return NULL;
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    if (fread(magic, 1, 4, f) != 4 || memcmp(magic, "KHW1", 4) || fread(frames, 4, 1, f) != 1 ||
        fread(joints, 4, 1, f) != 1 || fread(fps, 4, 1, f) != 1 || *frames < 2 || *frames > 240 || *joints < 1 ||
        *joints > 128 || !isfinite(*fps) || *fps < 1 || *fps > 120) {
        fclose(f);
        return NULL;
    }
    const size_t count = (size_t)*frames * *joints * 7;
    float *data = malloc(count * sizeof(float));
    if (!data || fread(data, sizeof(float), count, f) != count) {
        free(data);
        fclose(f);
        return NULL;
    }
    const int read = read_trailers(f, *frames, &t);
    fclose(f);
    for (size_t i = 0; read && i < count; ++i)
        if (!isfinite(data[i])) {
            free(t.hang);
            free(data);
            return NULL;
        }
    if (!read) {
        free(t.hang);
        free(data);
        return NULL;
    }
    if (trailers)
        *trailers = t;
    else
        free(t.hang);
    return data;
}

static Clip *clip_of(const char *code)
{
    for (unsigned i = 0; i < clip_count; ++i)
        if (!strcmp(clips[i].code, code))
            return &clips[i];
    if (clip_count == 32)
        return NULL;
    Clip *clip = &clips[clip_count++];
    Trailers t;
    memcpy(clip->code, code, 3);
    clip->decider = -1;
    clip->data = read_khw(code, &clip->frames, &clip->joints, &clip->fps, &t);
    if (!clip->data)
        return clip;
    memcpy(clip->steps, t.steps, sizeof(clip->steps));
    clip->step_count = t.step_count;
    if (t.hang) {
        memcpy(clip->hang_folder, t.hang_folder, 4);
        clip->hang_node = t.hang_node;
        clip->hang = t.hang;
    }
    khdays_log("walk: imported WALK for %s, %u frames on %u DS joints, %u footsteps", code, clip->frames,
               clip->joints, clip->step_count);
    {
        char name[8];
        unsigned frames = 0, joints = 0;
        float fps = 0;
        snprintf(name, sizeof(name), "%s_k", code);
        float *with = read_khw(name, &frames, &joints, &fps, &t);
        if (with && frames == clip->frames && joints == clip->joints) {
            if (t.hang) { /* the keychain as the armed walk holds the Keyblade (the unarmed one has none) */
                free(clip->hang);
                memcpy(clip->hang_folder, t.hang_folder, 4);
                clip->hang_node = t.hang_node;
                clip->hang = t.hang;
                t.hang = NULL;
            }
            float most = 0;
            for (unsigned j = 0; j < joints; ++j) {
                float sum = 0;
                for (unsigned f = 0; f < frames; ++f) {
                    const float *a = clip->data + ((size_t)f * joints + j) * 7,
                                *b = with + ((size_t)f * joints + j) * 7;
                    const float dot = fabsf(a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3]);
                    sum += 2.0f * acosf(fminf(dot, 1.0f));
                }
                clip->held_joint[j] = sum / (float)frames > 0.01f;
                if (clip->held_joint[j] && sum > most) {
                    most = sum;
                    clip->decider = (int)j;
                }
            }
            clip->held = with;
            khdays_log("walk: %s's Keyblade walk read (the arm told by joint %d)", code, clip->decider);
        } else {
            free(t.hang);
            free(with);
        }
        if (clip->hang)
            khdays_log("walk: %s's keychain hangs (node %u of ba/ch/%.2s/w_.p2's models)", code, clip->hang_node,
                       clip->hang_folder);
        snprintf(name, sizeof(name), "%s_w", code);
        float *weapon = read_khw(name, &frames, &joints, &fps, NULL);
        if (weapon && frames == clip->frames && (joints == 2 || joints == 4)) {
            clip->weapon = weapon;
            clip->weapon_hands = joints / 2;
            khdays_log("walk: %s's weapon rides in the hand (its own clip, %u hand%s)", code, clip->weapon_hands,
                       clip->weapon_hands > 1 ? "s" : "");
        } else
            free(weapon);
    }
    return clip;
}

void khdays_walk_movement(int actor, int speed, int full_speed)
{
    if (actor != GetEntryField20ByIndex(QueryActiveStateOrDelegate()))
        return;
    /* Actor.pNode begins with its owner flags. Scene_DrawNode receives
     * &pNode->anim, four bytes after that header (Ov022_ActorUpdate). */
    local_actor = actor;
    local_node = (int)(*(const u32 *)KHDAYS_W((u32)actor + 0x20u) + 4u);
    const int walk = speed > 0 && speed < full_speed;
    /* a check for the log: how often the walk turns on and off while the stick is held (a walk
     * fading in and out every few frames would shake the body); reported every three seconds */
    {
        static unsigned window, flips;
        static int lowest = 0x7fffffff, highest, last_walk;
        const unsigned now = khdays_input_game_frames();
        if (walk != last_walk)
            ++flips;
        last_walk = walk;
        if (speed < lowest)
            lowest = speed;
        if (speed > highest)
            highest = speed;
        if (now - window >= 180u) {
            if (flips > 4)
                khdays_log("walk: the walk turned on/off %u times in 3 s (speed %d..%d of %d)", flips, lowest, highest,
                           full_speed);
            window = now;
            flips = 0;
            lowest = 0x7fffffff;
            highest = 0;
        }
    }
    /* out of the walk only after three frames at full speed: the stick going back to the centre can
     * pass a frame or two there (the stop is the stale case in khdays_walk_draw_begin) */
    {
        static unsigned not_walking;
        not_walking = walk ? 0u : not_walking + 1u;
        target_walk = walk || (target_walk && not_walking < 3u);
    }
    if (walk || !target_walk)
        pace = full_speed > 0 ? 2.0f * (float)speed / (float)full_speed : 1.0f;
    seen_frame = khdays_input_game_frames();
}

/* The footstep at each frame the clip's feet plant, passed between `from` and `to` (the stride wraps): the
 * game's own footsteps come from the run it still plays underneath, slowed, out of time with the walk's
 * feet, so the bridge keeps them silent while the walk has them (khdays_sound_bridge_walk_steps) and tells
 * the sound the game last asked for (the surface's, FIELD_13FS_<surface>'s run01/run02, alternated here). */
static void footsteps(const Clip *clip, float from, float to)
{
    char archive[64], entry[64];
    static int second;
    int due = 0;
    if (weight <= 0.5f || pace < 0.15f)
        return;
    for (unsigned i = 0; i < clip->step_count; ++i) {
        const float at = (float)clip->steps[i];
        due |= to >= from ? at > from && at <= to : at > from || at <= to;
    }
    if (!due || !khdays_sound_bridge_footstep(archive, (int)sizeof(archive), entry, (int)sizeof(entry)))
        return;
    {
        const size_t n = strlen(entry);
        if (n >= 2 && entry[n - 2] == '0' && (entry[n - 1] == '1' || entry[n - 1] == '2')) {
            entry[n - 1] = second ? '2' : '1';
            second = !second;
        }
    }
    khdays_sound_bridge_expect_footstep();
    khdays_link_play_sound(archive, entry);
}

/* --- 4x4 matrices, row vectors (a point is p * M; the DS's MtxFx43 laid out the same way) --- */
static void mat_from_fixed(const int32_t *m, float *out)
{
    memset(out, 0, 16 * sizeof(float));
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 3; ++c)
            out[r * 4 + c] = (float)m[r * 3 + c] / 4096;
    out[15] = 1;
}

static void mat_mul(const float *a, const float *b, float *out) /* out = a * b */
{
    float t[16];
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            t[r * 4 + c] =
                a[r * 4] * b[c] + a[r * 4 + 1] * b[4 + c] + a[r * 4 + 2] * b[8 + c] + a[r * 4 + 3] * b[12 + c];
    memcpy(out, t, sizeof(t));
}

static void mat_inverse_rigid(const float *m, float *out) /* a rotation and a translation */
{
    float t[16] = {0};
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c)
            t[r * 4 + c] = m[c * 4 + r];
    for (int c = 0; c < 3; ++c)
        t[12 + c] = -(m[12] * t[c] + m[13] * t[4 + c] + m[14] * t[8 + c]);
    t[15] = 1;
    memcpy(out, t, sizeof(t));
}

static void mat_from_pose(const float *q, const float *offset, float *out) /* a clip's quaternion and offset */
{
    const float x = q[0], y = q[1], z = q[2], w = q[3];
    const float m[16] = {1 - 2 * (y * y + z * z), 2 * (x * y + z * w),     2 * (x * z - y * w),     0,
                         2 * (x * y - z * w),     1 - 2 * (x * x + z * z), 2 * (y * z + x * w),     0,
                         2 * (x * z + y * w),     2 * (y * z - x * w),     1 - 2 * (x * x + y * y), 0,
                         offset[0],               offset[1],               offset[2],               1};
    memcpy(out, m, sizeof(m));
}

/* For the log (KHDAYS_TRACE_WALK): each model file drawn as a slot or a node, once. */
static void told_model(const char *file, const char *how)
{
    static char told[64][64];
    static int count;
    if (!getenv("KHDAYS_TRACE_WALK") || !strncmp(file, "UI/", 3) || !strncmp(file, "ttl/", 4))
        return;
    for (int i = 0; i < count; ++i)
        if (!strncmp(told[i], file, sizeof(told[i]) - 1))
            return;
    if (count == 64)
        return;
    snprintf(told[count++], sizeof(told[0]), "%s", file);
    khdays_log("walk: %s drawn: %s", how, file);
}

/* A matrix of the DS's, row vectors, from and to fixed point. */
static void mat_to_fixed(const float *m, int32_t *out)
{
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 3; ++c)
            out[r * 3 + c] = (int32_t)lroundf(m[r * 4 + c] * 4096);
}

/* NNSi_G3dDrawInternal (runtime/mods/models.cpp, from prepare.py's hook), as a model's draw begins, and as it
 * ends: whether the model drawn is the local player's weapon, which the game draws as an object of its own
 * with the player's base matrix, its place on the player being the result it gives the weapon's root joint
 * (weapon_joint, from the sendJnt hook). */
static Clip *weapon_drawing, *keychain_drawing;
static unsigned weapon_placed; /* the placed joints met so far in the weapon's draw */
void khdays_walk_model_draw(unsigned model_resource)
{
    Clip *clip = drawn_clip;
    const char *file = NULL;
    u32 offset, run;
    char want[16];
    weapon_drawing = keychain_drawing = NULL;
    weapon_placed = 0;
    if (!clip || (!clip->weapon && !clip->hang) || !khdays_game_ram(model_resource) ||
        !khdays_origin_of_ram(model_resource, &file, &offset, &run) || !file)
        return;
    told_model(file, "a model");
    snprintf(want, sizeof(want), "ba/ch/%s/w_", clip->code);
    if (clip->weapon && strstr(file, want) != NULL)
        weapon_drawing = clip;
    /* a Keyblade: a model of the weapon file whose last node is the keychain's (the file's other models, its
     * swings' traces and hits, have more) */
    snprintf(want, sizeof(want), "ba/ch/%.2s/w_", clip->hang_folder);
    if (clip->hang && strstr(file, want) != NULL &&
        *(const u8 *)KHDAYS_W(model_resource + 0x17u) == clip->hang_node + 1)
        keychain_drawing = clip;
}

void khdays_walk_model_draw_end(void)
{
    weapon_drawing = keychain_drawing = NULL;
}

/* The keychain's node as the game sends it (`r` as in weapon_joint): with the walk on, turned to hang from the
 * pommel as the clip's pendulum has it this frame, eased by the walk's weight; its place is left as it is.
 * Returns whether `r` was changed. */
static int keychain_joint(unsigned joint, int32_t *r)
{
    Clip *clip = keychain_drawing;
    float original[4] = {0, 0, 0, 1}, hang[4], q[4], m[9];
    if (joint != clip->hang_node || weight <= 0)
        return 0;
    const unsigned f = (unsigned)phase % clip->frames, next = (f + 1) % clip->frames;
    mix(clip->hang + (size_t)f * 4, clip->hang + (size_t)next * 4, phase - floorf(phase), hang);
    if (!(r[0] & 2)) {
        for (int k = 0; k < 9; ++k)
            m[k] = (float)r[10 + k] / 4096;
        quat_from_matrix(m, original);
    }
    mix(original, hang, weight, q);
    {
        const float x = q[0], y = q[1], z = q[2], w = q[3];
        const float rotation[9] = {1 - 2 * (y * y + z * z), 2 * (x * y + z * w),     2 * (x * z - y * w),
                                   2 * (x * y - z * w),     1 - 2 * (x * x + z * z), 2 * (y * z + x * w),
                                   2 * (x * z + y * w),     2 * (y * z - x * w),     1 - 2 * (x * x + y * y)};
        for (int k = 0; k < 9; ++k)
            r[10 + k] = (int32_t)lroundf(rotation[k] * 4096);
    }
    r[0] &= ~2;
    if (!clip->hang_applied) {
        clip->hang_applied = 1;
        khdays_log("walk: %s's keychain hangs from the pommel", clip->code);
    }
    return 1;
}

/* The weapon's joint `joint` as the game sends it (`r`: the joint's result, its rotation at 10..18 unless bit 1
 * of the flags says none, its translation at 19..21 unless bit 2). The root joint carries the weapon's place
 * on the player. Standing, that is where the idle holds it: kept (`rest`). With the walk on it is made over:
 * the same hold against the walk's hand this frame (the clip's), eased by the walk's weight. Returns whether
 * `r` was changed. */
static int weapon_joint(unsigned joint, int32_t *r)
{
    Clip *clip = weapon_drawing;
    float placed[16];
    mat_from_fixed(r + 10, placed);
    if (r[0] & 2)
        for (int k = 0; k < 12; ++k)
            placed[k] = k % 5 == 0 ? 1.0f : 0.0f;
    if (r[0] & 4)
        placed[12] = placed[13] = placed[14] = 0;
    if (joint < 4 && getenv("KHDAYS_TRACE_WALK") && !(clip->weapon_told & 1 << joint)) {
        clip->weapon_told |= 1 << joint;
        khdays_log("walk: %s's weapon joint %u sent at %.2f %.2f %.2f (flags %x)", clip->code, joint, placed[12],
                   placed[13], placed[14], (unsigned)r[0]);
    }
    /* the joints that carry a place: those the game gives a translation, in the order sent (the root's, or its
     * children's over a root left at the identity: Xaldin's two lances), each to one of the clip's hands */
    if (r[0] & 4)
        return 0;
    {
        const unsigned hand_index = weapon_placed++;
        float *rest = clip->rest[hand_index];
        if (hand_index >= clip->weapon_hands)
            return 0;
        if (weight <= 0) {
            /* standing (the game's mode 0 at Actor+0x6bc) with the walk off, for a few frames (the game eases
             * the weapon from where the run had it): the weapon is where the idle holds it, kept */
            const int mode = khdays_game_ram((u32)local_actor + 0x6c0u)
                                 ? *(const int32_t *)KHDAYS_W((u32)local_actor + 0x6bcu)
                                 : -1;
            if (mode != 0) {
                clip->standing = 0;
                return 0;
            }
            if (hand_index == 0)
                ++clip->standing;
            if (clip->standing < 8)
                return 0;
            if (!(clip->rest_known & 1 << hand_index))
                khdays_log("walk: %s's weapon stands at %.2f %.2f %.2f of the model (the idle's hold, joint %u)",
                           clip->code, placed[12], placed[13], placed[14], joint);
            memcpy(rest, placed, 16 * sizeof(float));
            clip->rest_known |= 1 << hand_index;
            return 0;
        }
        clip->standing = 0;
        if (!(clip->rest_known & 1 << hand_index))
            return 0;
        const unsigned f = (unsigned)phase, next = (f + 1) % clip->frames;
        const float t = phase - (float)f;
        const unsigned stride = 2 * clip->weapon_hands;
        const float *a = clip->weapon + ((size_t)f * stride + 2 * hand_index) * 7;
        const float *b = clip->weapon + ((size_t)next * stride + 2 * hand_index) * 7;
        const float *idle = a + 7;
        float q[4], shift[3], hand[16], hand_idle[16], inverse[16], hold[16], want[16], m[9], qa[4], qb[4];
        mix(a, b, t, q);
        for (int k = 0; k < 3; ++k)
            shift[k] = a[4 + k] * (1 - t) + b[4 + k] * t;
        mat_from_pose(q, shift, hand);
        mat_from_pose(idle, idle + 4, hand_idle);
        mat_inverse_rigid(hand_idle, inverse);
        mat_mul(rest, inverse, hold); /* the weapon against the hand, as the idle holds it */
        mat_mul(hold, hand, want);
        if (weight < 1) {
            /* on its way in or out: between the game's and the walk's */
            for (int k = 0; k < 3; ++k)
                memcpy(m + k * 3, placed + k * 4, 3 * sizeof(float));
            quat_from_matrix(m, qa);
            for (int k = 0; k < 3; ++k)
                memcpy(m + k * 3, want + k * 4, 3 * sizeof(float));
            quat_from_matrix(m, qb);
            mix(qa, qb, weight, q);
            for (int k = 0; k < 3; ++k)
                shift[k] = placed[12 + k] * (1 - weight) + want[12 + k] * weight;
            mat_from_pose(q, shift, want);
        }
        mat_to_fixed(want, r + 10);
        r[0] &= ~6;
        if (!clip->weapon_applied) {
            clip->weapon_applied = 1;
            khdays_log("walk: %s's weapon moved to the walk's hand", clip->code);
        }
        return 1;
    }
}

void khdays_walk_draw_begin(int node)
{
    drawing = NULL;
    if (!khdays_game_ram((u32)local_node)) {
        /* before the stick first moves the local actor (khdays_walk_movement): the actor, found as the
         * movement finds it, so that its weapon's rest is seen while it stands */
        const int actor = GetEntryField20ByIndex(QueryActiveStateOrDelegate());
        if (khdays_game_ram((u32)actor + 0x20u)) {
            local_actor = actor;
            local_node = (int)(*(const u32 *)KHDAYS_W((u32)actor + 0x20u) + 4u);
        }
    }
    if (node != local_node) {
        if (getenv("KHDAYS_TRACE_WALK") && khdays_game_ram((u32)node + 0x28u)) {
            const u32 model = *(const u32 *)KHDAYS_W((u32)node + 0x24u);
            const char *file = NULL;
            u32 offset, run;
            if (khdays_game_ram(model) && khdays_origin_of_ram(model, &file, &offset, &run) && file)
                told_model(file, "a node");
        }
        return;
    }
    if (!khdays_game_ram((u32)node))
        return;
    const u64 now = khdays_clock_cycles();
    const float dt = drawn_at && now >= drawn_at ? (float)(now - drawn_at) / KHDAYS_CLOCK_HZ : 0;
    drawn_at = now;
    const u32 unseen = khdays_input_game_frames() - seen_frame;
    /* the clip of the model drawn (by its file, ba/ch/<code>/): read now, standing too, so that its weapon's
     * rest is seen before the first step. A character without one (Xemnas, whose run is a float) moves with the
     * game's own run, slowed, and its own footsteps. */
    {
        const u32 mdl = *(const u32 *)KHDAYS_W((u32)node + 0x24u);
        const char *file = NULL;
        u32 offset, run;
        const char *at = NULL;
        drawn_clip = NULL;
        if (khdays_game_ram(mdl) && khdays_origin_of_ram(mdl, &file, &offset, &run) && file)
            at = strstr(file, "ba/ch/");
        if (at && strlen(at + 6) >= 3 && at[8] == '/') {
            const char code[3] = {at[6], at[7], 0};
            Clip *clip = clip_of(code);
            if (clip && clip->data && *(const u8 *)KHDAYS_W(mdl + 0x17u) == clip->joints)
                drawn_clip = clip;
        }
        if (!drawn_clip) {
            weight = 0;
            khdays_sound_bridge_walk_steps(0);
            return;
        }
    }
    if (unseen > 90u) {
        weight = 0;
        settle = stand = 0;
        khdays_sound_bridge_walk_steps(0);
        return;
    } /* no longer moved by the stick (a scene, a menu) */
    if (unseen <= 2u) {
        settle = stand = 0;
        weight += (target_walk ? 1.0f : -1.0f) * fminf(dt, 0.05f) / 0.12f;
    } else if (weight > 0) {
        /* the stick let go: the game turns its moving motion (mode 1 at Actor+0x6bc: the run, or
         * Roxas's unarmed e002) into standing with a 5-frame blend (Ov002_GetModeBlendFrames). The
         * walk stays over it, its stride slowing to a stop, until the game stands; only then it
         * hands over, slowly. Fading it out at once showed the run's pose for an instant. */
        pace *= expf(-fminf(dt, 0.05f) / 0.05f);
        const int mode =
            khdays_game_ram((u32)local_actor + 0x6c0u) ? *(const int32_t *)KHDAYS_W((u32)local_actor + 0x6bcu) : -1;
        settle += dt;
        if (mode == 1 && settle < 0.6f)
            stand = 0;
        else
            stand += dt;
        if (mode != 0 && mode != 1)
            weight -= fminf(dt, 0.05f) / 0.08f; /* an attack, a jump...: its own motion, now */
        else if (stand > 0.2f)
            weight -= fminf(dt, 0.05f) / 0.2f;
    }
    if (weight < 0)
        weight = 0;
    if (weight > 1)
        weight = 1;
    /* toward the target without passing it: at the target it stays (stepping down from 1 every
     * other frame made the armed arm flicker between the hold and the swing) */
    {
        const float step = fminf(dt, 0.05f) / 0.15f;
        if (held < held_target)
            held = fminf(held + step, held_target);
        else if (held > held_target)
            held = fmaxf(held - step, held_target);
    }
    if (weight <= 0)
        phase = 0;
    /* the clip even standing still: the game's motions are seen then too, so the walk starts with
     * the right arm (it is applied only with some weight) */
    Clip *clip = drawn_clip;
    if (weight > 0) {
        const float before = phase;
        phase = fmodf(phase + fminf(dt, 0.05f) * clip->fps * pace, (float)clip->frames);
        footsteps(clip, before, phase);
    }
    khdays_sound_bridge_walk_steps(weight > 0.5f);
    if (clip->held && khdays_game_ram((u32)local_actor + 0x2c30u)) {
        /* which set of motions the character plays: the game plays its first track from the set
         * at Actor+0x2c28 (func_ov022_020a3c78). Roxas's unarmed motions (etc.p: b_ro_e000 standing,
         * e002 moving, e003 putting the Keyblade away, E004...) are the set bound at Actor+0x2c2c
         * (Ov030_SetUpScene), played by Ov030_ApplyModeChange while no Keyblade is out; any other
         * set (0, the main track: b_ro_A000 standing, A001 running... with the Keyblade; combos,
         * reactions) is with the Keyblade in the hand. The game's own state, read as it is. */
        const u32 set = *(const u32 *)KHDAYS_W((u32)local_actor + 0x2c28u);
        const int out = set != (u32)local_actor + 0x2c2cu;
        if ((held_target > 0.5f) != out)
            khdays_log("walk: %s plays its %s motions (set %08x): %s", clip->code, out ? "armed" : "unarmed",
                       (unsigned)set, out ? "the walk holding the weapon" : "the walk's own arms");
        held_target = out ? 1.0f : 0.0f;
    }
    drawing = clip;
}

void khdays_walk_draw_end(void)
{
    drawing = NULL;
}

/* Apply after the game's final blend, including its cached poses. Restore
 * the cached result after the matrix send so fades never accumulate on it. */
static int32_t saved_joint[22];
static int saved_joint_valid;
void khdays_walk_send_begin(unsigned int joint, void *result)
{
    if (weapon_drawing && result) {
        memcpy(saved_joint, result, sizeof(saved_joint));
        saved_joint_valid = weapon_joint(joint, result);
        return;
    }
    if (keychain_drawing && result) {
        memcpy(saved_joint, result, sizeof(saved_joint));
        saved_joint_valid = keychain_joint(joint, result);
        return;
    }
    saved_joint_valid = drawing && weight > 0 && joint < drawing->joints && result;
    if (!saved_joint_valid)
        return;
    memcpy(saved_joint, result, sizeof(saved_joint));
    khdays_walk_joint(joint, result);
}
void khdays_walk_send_end(void *result)
{
    if (saved_joint_valid && result)
        memcpy(result, saved_joint, sizeof(saved_joint));
    saved_joint_valid = 0;
}

/* NSBCA tracks need not have the same order as the model's joints. The DS
 * animation object has its joint-to-track map at byte 26 (four-byte refs). */
void khdays_walk_mapped_joint(unsigned int data_index, const void *animation, void *result)
{
    if (!drawing || !animation)
        return;
    const unsigned char *object = animation;
    const uint16_t *map = (const uint16_t *)(object + 26);
    for (unsigned joint = 0; joint < object[25]; ++joint) {
        if ((map[joint] & 0x100u) && (map[joint] & 0xffu) == data_index) {
            khdays_walk_joint(joint, result);
            return;
        }
    }
}

static void normalize(float *q)
{
    const float length = sqrtf(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    if (length > 0.00001f)
        for (int k = 0; k < 4; ++k)
            q[k] /= length;
    else {
        q[0] = q[1] = q[2] = 0;
        q[3] = 1;
    }
}

static void mix(const float *a, const float *b, float t, float *q)
{
    const float dot = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
    for (int k = 0; k < 4; ++k)
        q[k] = a[k] * (1 - t) + (dot < 0 ? -b[k] : b[k]) * t;
    normalize(q);
}

static void quat_from_matrix(const float *m, float *q)
{
    const float tr = m[0] + m[4] + m[8];
    if (tr > 0) {
        float s = sqrtf(tr + 1) * 2;
        q[3] = s / 4;
        q[0] = (m[5] - m[7]) / s;
        q[1] = (m[6] - m[2]) / s;
        q[2] = (m[1] - m[3]) / s;
    } else if (m[0] > m[4] && m[0] > m[8]) {
        float s = sqrtf(1 + m[0] - m[4] - m[8]) * 2;
        q[0] = s / 4;
        q[1] = (m[3] + m[1]) / s;
        q[2] = (m[6] + m[2]) / s;
        q[3] = (m[5] - m[7]) / s;
    } else if (m[4] > m[8]) {
        float s = sqrtf(1 + m[4] - m[0] - m[8]) * 2;
        q[1] = s / 4;
        q[0] = (m[3] + m[1]) / s;
        q[2] = (m[7] + m[5]) / s;
        q[3] = (m[6] - m[2]) / s;
    } else {
        float s = sqrtf(1 + m[8] - m[0] - m[4]) * 2;
        q[2] = s / 4;
        q[0] = (m[6] + m[2]) / s;
        q[1] = (m[7] + m[5]) / s;
        q[3] = (m[1] - m[3]) / s;
    }
    normalize(q);
}

void khdays_walk_joint(unsigned int joint, void *result)
{
    if (!drawing || joint >= drawing->joints || !result)
        return;
    if (weight <= 0)
        return;
    if (!drawing->applied) {
        drawing->applied = 1;
        khdays_log("walk: applied the imported WALK to %s's rendered skeleton (%u joints)", drawing->code,
                   drawing->joints);
    }
    int32_t *r = result;
    const unsigned f = (unsigned)phase, next = (f + 1) % drawing->frames;
    const float t = phase - (float)f;
    const float *a = drawing->data + ((size_t)f * drawing->joints + joint) * 7;
    const float *b = drawing->data + ((size_t)next * drawing->joints + joint) * 7;
    float sample[4], original[4] = {0, 0, 0, 1}, q[4], m[9], offset[3];
    mix(a, b, t, sample);
    for (int k = 0; k < 3; ++k)
        offset[k] = a[4 + k] * (1 - t) + b[4 + k] * t;
    if (!(r[0] & 2)) {
        for (int k = 0; k < 9; ++k)
            m[k] = (float)r[10 + k] / 4096;
        quat_from_matrix(m, original);
    }
    if (drawing->held && drawing->held_joint[joint]) {
        /* a holding arm or a weapon: the walk's own, or the walk holding the weapon */
        const float *ha = drawing->held + ((size_t)f * drawing->joints + joint) * 7;
        const float *hb = drawing->held + ((size_t)next * drawing->joints + joint) * 7;
        float hold[4];
        mix(ha, hb, t, hold);
        if (held < 1) {
            float q2[4];
            mix(sample, hold, held, q2);
            memcpy(sample, q2, sizeof(q2));
        } else
            memcpy(sample, hold, sizeof(hold));
        for (int k = 0; k < 3; ++k)
            offset[k] = offset[k] * (1 - held) + (ha[4 + k] * (1 - t) + hb[4 + k] * t) * held;
    }
    mix(original, sample, weight, q);
    const float x = q[0], y = q[1], z = q[2], w = q[3];
    const float rotation[9] = {1 - 2 * (y * y + z * z), 2 * (x * y + z * w),     2 * (x * z - y * w),
                               2 * (x * y - z * w),     1 - 2 * (x * x + z * z), 2 * (y * z + x * w),
                               2 * (x * z + y * w),     2 * (y * z - x * w),     1 - 2 * (x * x + y * y)};
    for (int k = 0; k < 9; ++k)
        r[10 + k] = (int32_t)lroundf(rotation[k] * 4096);
    for (int k = 0; k < 3; ++k) {
        const float from = (r[0] & 4) ? 0 : (float)r[19 + k] / 4096;
        const float to = offset[k];
        r[19 + k] = (int32_t)lroundf((from * (1 - weight) + to * weight) * 4096);
    }
    r[0] &= ~6;
}
