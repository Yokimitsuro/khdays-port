/* The Enemy Profiles' model viewer (enemy_viewer.h): the enemy's model as the game loads it in battle,
 * drawn by the game's own 3D (NNS G3D, all in the main ARM9 code, loaded whatever the overlay) while
 * its Files page runs -- the camp menu draws Roxas so (Ov025_ModelActor_Init, Ov025_Menu_RenderScenePanels).
 *
 * The model: Ms/<class>.p (a P2 archive; the class the enm_&.z record's first byte, as the enemies'
 * overlays open it: Ov114_CreateNamedEntity's "Ms/%02x.p"), its entry 0 a KAPH pack -- the model
 * (section 7) and, for most, its joint animations (section 0); for the others each later entry that
 * holds a BCA alone is one animation. An entry is opened by a handle on the archive's header
 * (Msg_OpenContainerAndReadHeader, Ov107_PackTextureHandle's packing) through RegisterSeqAndInit (the
 * model instance: textures to VRAM, the render object, the animations' table at +0xe0) or
 * Snd_RegisterSeqAndBind (an animation's file into a table of its own); BindAnimTrack plays one,
 * Sequence_UpdateTracks steps it, Scene_DrawNode draws the instance on its matrix (+0x80, a 4x3) with
 * the camera Camera_CommitMatrices made (a block Projection_LoadDefaults fills), between main's
 * G3X_ResetMtxStack and its swap.
 *
 * The page has no texture VRAM (Ov025_Reports_SetupDisplay: banks A-D its BGs and OBJs) and hides the
 * 3D layer. The port shows neither of the main engine's (the lower screen's) BG and OBJ there, so while
 * the viewer is open bank B (the main OBJ's) holds the textures and F+G their palettes, as the camp's
 * menu has them (Ov025_Hub_SetupGraphics), BG0 shows the 3D; on closing, all as it was (bank B's
 * contents too). host/render/onescreen.c takes the 3D's polygons into the window.
 *
 * Some enemies are several models the game draws at one place (Darkside's DS01 with DS02 and DS03 on the
 * actor's matrix, Ov244_TickAndSyncTwoModelXforms; the bosses' wings, swords, arms, tails): their parts
 * (enemy_viewer_anims.h's VIEWER_PARTS) are instances of their own from the same archive, each with the
 * animation of the body's number, on the body's matrix. */
#include "enemy_viewer.h"

#include "../core/io.h"
#include "../input/input.h"
#include "enemy_viewer_anims.h"
#include "../../host/app/host.h"
#include "../core/session_log.h"
#include "../core/vram.h"
#include "../core/window.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#undef PlaySound /* the game's sequence function, not Windows' wave playback macro */

typedef struct KhdaysRomReader KhdaysRomReader; /* rom.h */
KhdaysRomReader *khdays_rom_reader_new(void);
int khdays_rom_file_data(KhdaysRomReader *reader, const char *path, int entry, u8 **data, u32 *size);

/* the game's (DS addresses as u32) */
extern unsigned int NNS_FndAllocFromDefaultExpHeapEx(unsigned int size, int align);
extern void NNSi_FndFreeFromDefaultHeap(unsigned int block);
extern unsigned int Msg_OpenContainerAndReadHeader(unsigned int name, int mode);
extern void RegisterSeqAndInit(unsigned int inst, unsigned int file, int with_textures, int heap);
extern int Snd_RegisterSeqAndBind(unsigned int table, unsigned int inst, unsigned int file, int heap);
extern void BindAnimTrack(unsigned int inst, unsigned int track, unsigned int table, int blend);
extern unsigned short Sequence_UpdateTracks(unsigned int inst, int step);
extern void Scene_DrawNode(unsigned int inst);
extern void Projection_LoadDefaults(unsigned int camera);
extern void Camera_CommitMatrices(unsigned int camera);
extern void ReleaseField74AndCleanup(unsigned int inst);
extern void FreeAllResourceTables(unsigned int table);
extern void ResSlot_ReleaseResource(unsigned int slot);
extern int ResSlot_Release(unsigned int slot);
extern void GX_SetBankForOBJ(int banks);
extern void GX_SetBankForTex(int banks);
extern void GX_SetBankForTexPltt(int banks);
extern void G3X_SetClearColor(int colour, int alpha, int depth, int poly_id, int fog);
extern void NNS_GfdResetFrmTexVramState(void);
extern void NNS_GfdInitFrmTexVramManager(int slots, int as_default);
extern void NNS_GfdResetFrmPlttVramState(void);
extern void NNS_GfdInitFrmPlttVramManager(unsigned int size, int as_default);
extern int GetFrameRateMode(void);
extern int PlaySound(int kind, int sound);

#define HEAP 0xe         /* the game's default heap (the camp's resources) */
#define ENEMY_MODE 0xb   /* the enemies' archives' (Ov107_OpenCachedResourceByName) */
#define INST_SIZE 0x200  /* a model instance (ModelInst_Init's, its tables to +0x10c) */
#define TABLE_SIZE 0x40  /* an animation file's table (BindAnimTrack's: counts, +0xc its slot, +0x10 the lists) */
#define CAMERA_SIZE 0x40 /* Projection_LoadDefaults' 14 words */
#define INST_RESOURCES 0x74u
#define INST_MODEL 0x78u  /* its NNSG3dResMdl */
#define INST_MATRIX 0x80u /* its 4x3 (Scene_DrawNode) */
#define INST_TABLE 0xe0u  /* its animations' table: s16 counts per track */
#define INST_ACTIVE 0x2u  /* track 0 playing (>= 0), and its channel: the frame in Q12 (Sequence_UpdateTracks) */
#define INST_CHANNEL 0xcu
#define MODEL_BOX 0x2cu /* the model's info (+0x14): its box, six fx16 (min x, y, z, size), then the box's scale */
#define MODEL_BOX_SCALE 0x38u
#define BANK_B 1
#define BANK_B_SIZE 0x20000u
#define VRAM_B 2 /* GX_VRAM_*: B; F and G (the texture palettes) */
#define VRAM_FG 0x60
#define DISPCNT 0x04000000u
#define DISP3DCNT 0x04000060u
#define ANIMS_MOST 40
#define TURN 0.10f      /* a step's turn (radians) at the 60 a second pace, the stick all the way */
#define ZOOM_STEP 1.05f /* a step's zoom, the stick all the way */
#define ZOOM_MOST 3.0f
#define ZOOM_LEAST 0.4f
/* Each enemy's size as the game shows it (enemy_viewer_anims.h: the model posed by its animation, its
 * textures' transparent texels left out, measured offline) as a part of the window's height: the sizes
 * apart by their logarithm (a Shadow 0.42 of the window, a Dusk a half, Darkside the window
 * whole), so the biggest still fit and the smallest are still seen. */
#define VIEW_TAN 0.57735027f /* tan(30): the vertical 60 degrees (Projection_LoadDefaults') */
#define FILL_SIZE 0.7f       /* this size (Roxas is 1.6) ... */
#define FILL_AT 0.28f        /* ... this part of the window's height */
#define FILL_PER_LOG 0.24f   /* and this more each time the size is e times bigger */
#define FILL_LEAST 0.2f
#define FILL_MOST 0.9f
#define FIT 0.92f  /* the model's reach (turning, animated) inside this part of the window */
#define FRAME 1.7f /* (no size measured: the box's sphere this much of the view's height away) */
#define PITCH_MOST 1.3f
/* its controls (the DS's buttons, whatever the player plays them with) */
#define KHDAYS_VIEW_OPEN KHDAYS_KEY_X
#define KHDAYS_VIEW_CLOSE (KHDAYS_KEY_B | KHDAYS_KEY_X)
#define KHDAYS_VIEW_RESET KHDAYS_KEY_Y
#define KHDAYS_VIEW_PREVIOUS KHDAYS_KEY_L
#define KHDAYS_VIEW_NEXT (KHDAYS_KEY_R | VIEW_LOCK_ON)
#define VIEW_LOCK_ON 0x4000u /* (not a DS button: the port's lock-on, R1 when it is apart from the DS's R) */
#define KHDAYS_VIEW_LEFT KHDAYS_KEY_LEFT /* turning: the D-pad (and the left stick, as it is) */
#define KHDAYS_VIEW_RIGHT KHDAYS_KEY_RIGHT
#define KHDAYS_VIEW_IN KHDAYS_KEY_UP /* zooming: the D-pad's up and down (and the right stick) */
#define KHDAYS_VIEW_OUT KHDAYS_KEY_DOWN
#define STICK_DEAD 7000 /* of SDL's 32767 */
#define SLOW_PACE 0.6f  /* an animation the table marks as moving much */
#define SOUND_ERROR 4   /* the menus' sound 4: the error's two beats (an enemy not met) */
/* the menus' own sounds (COMMON_SYS 0-3, PlaySound(0, n)): opening it, another enemy or the view set
 * straight, closing it -- silent, its buttons seemed not to answer */
#define SOUND_CLICK 0  /* se_sys_click: L/R to another enemy, Y */
#define SOUND_OPEN 2   /* se_sys_open: X opens it */
#define SOUND_CANCEL 3 /* se_sys_cancel: B or X closes it */

static float max_f(float a, float b)
{
    return a > b ? a : b;
}
static float min_f(float a, float b)
{
    return a < b ? a : b;
}

static SRWLOCK lock = SRWLOCK_INIT;
static KhdaysEnemyViewer shown;

static struct {
    int wanted, closing; /* asked for at the pad's sample, done at the update */
    int open, ready;
    int enemy, anim, anims;
    unsigned serial, opening;
    u16 held, pressed;    /* the pad's, while open */
    int touched;          /* turned by the player */
    int slow;             /* its animation played slower */
    int16_t sticks[2][2]; /* the pad's sticks at the sample */
    u16 swallowed;        /* held when it closed: kept from the page until let go (B would leave it) */
    int step_to;          /* -1/+1: to the enemy before or after, met, in the page's list */
    int row;              /* the page's row shown */
    int rows;
    int16_t *ids; /* the page's rows' enemies, -1 not met: `rows` of them */
    int id_room;
    /* the game's memory */
    u32 inst, camera, name, container, table;
    struct {
        u32 inst, table;
        unsigned first, frames; /* hung on a joint: its matrices in VIEWER_HANGS (frames 0: at the body's place) */
    } part[VIEWER_PARTS_MOST];  /* the parts drawn with it (VIEWER_PARTS) */
    int parts;
    /* the animations: entry 0's own (table at INST_TABLE) or, without any, these entries' one each */
    int entries[ANIMS_MOST];
    float centre[3], radius;
    int anim_entry;    /* its animation's file (0: the model's own) */
    float view_height; /* the view's height at the model's middle (the camera's distance) */
    float yaw, pitch, zoom;
    u32 dispcnt, disp3dcnt;
    u8 bank_b[BANK_B_SIZE];
    int banks; /* the texture banks are the viewer's */
    /* the files page as the update saw it last */
    int on, enemies, cursor;
} vw;

static u32 rd32(u32 a)
{
    return *(volatile u32 *)KHDAYS_W(a);
}
static u16 rd16(u32 a)
{
    return *(volatile u16 *)KHDAYS_W(a);
}

static void publish(void)
{
    AcquireSRWLockExclusive(&lock);
    shown.open = vw.open;
    shown.ready = vw.ready;
    shown.enemy = vw.enemy;
    shown.anim = vw.anim;
    shown.anims = vw.anims;
    shown.serial = vw.serial;
    shown.row = vw.row;
    shown.opening = vw.opening;
    ReleaseSRWLockExclusive(&lock);
}

void khdays_enemy_viewer_read(KhdaysEnemyViewer *out)
{
    AcquireSRWLockExclusive(&lock);
    *out = shown;
    ReleaseSRWLockExclusive(&lock);
}

static u32 alloc_zeroed(u32 size)
{
    const u32 block = NNS_FndAllocFromDefaultExpHeapEx(size, 4);
    if (block != 0) {
        memset(KHDAYS_W(block), 0, size);
    }
    return block;
}

/* An entry of the open archive as a file the resource cache opens (Ov107_PackTextureHandle). */
static u32 entry_handle(int entry)
{
    return 0x80000000u | (((vw.container + 0x8000u) & 0xfffffcu) << 7) | ((u32)entry & 0x1ffu);
}

/* The archive's entries after the first that are an animation alone (a KAPH with joint animations, no
 * model), read from the ROM by the port. */
static int animation_entries(void)
{
    static KhdaysRomReader *reader;
    char path[24];
    int n = 0;
    if (reader == NULL && (reader = khdays_rom_reader_new()) == NULL) {
        return 0;
    }
    snprintf(path, sizeof(path), "Ms/%02X.p", vw.enemy);
    for (int entry = 1; entry < 0x200 && n < ANIMS_MOST; ++entry) {
        u8 *data = NULL;
        u32 size = 0;
        if (!khdays_rom_file_data(reader, path, entry, &data, &size)) {
            free(data);
            break;
        }
        if (size >= 0x28 && memcmp(data, "KAPH", 4) == 0) {
            int32_t joints, model;
            memcpy(&joints, data + 8, 4);
            memcpy(&model, data + 8 + 7 * 4, 4);
            if (joints != -1 && model == -1) {
                vw.entries[n++] = entry;
            }
        }
        free(data);
    }
    return n;
}

/* An animation file's table let go of by the instance playing it. */
static void unbind(u32 inst, u32 *table)
{
    if (*table != 0) {
        const u32 slot = rd32(*table + 0xc);
        BindAnimTrack(inst, 0, *table, -1);
        FreeAllResourceTables(*table);
        if (slot != 0) {
            ResSlot_ReleaseResource(slot);
            ResSlot_Release(slot);
        }
        NNSi_FndFreeFromDefaultHeap(*table);
        *table = 0;
    }
}

static void unbind_table(void)
{
    unbind(vw.inst, &vw.table);
}

/* A model instance and its animation's table given back to the heap. */
static void free_instance(u32 *inst, u32 *table)
{
    if (*inst != 0) {
        unbind(*inst, table);
        if (rd32(*inst + INST_RESOURCES) != 0) {
            ReleaseField74AndCleanup(*inst);
        }
        NNSi_FndFreeFromDefaultHeap(*inst);
        *inst = 0;
    }
}

static const ViewerParts *parts_of(int enemy)
{
    for (size_t i = 0; i < sizeof(VIEWER_PARTS) / sizeof(VIEWER_PARTS[0]); ++i) {
        if (VIEWER_PARTS[i].enemy == enemy) {
            return &VIEWER_PARTS[i];
        }
    }
    return NULL;
}

/* The enemy's other models (VIEWER_PARTS), each with its animation (the body's number); those that cannot be
 * loaded are left out, the body shown all the same. */
static void load_parts(void)
{
    const ViewerParts *p = parts_of(vw.enemy);
    vw.parts = 0;
    for (int i = 0; p != NULL && i < p->count && i < VIEWER_PARTS_MOST; ++i) {
        u32 inst = alloc_zeroed(INST_SIZE), table = alloc_zeroed(TABLE_SIZE);
        if (inst == 0 || table == 0) {
            if (table != 0) {
                NNSi_FndFreeFromDefaultHeap(table);
            }
            if (inst != 0) {
                NNSi_FndFreeFromDefaultHeap(inst);
            }
            khdays_log("enemy viewer: no room for Ms/%02X.p's part %d", vw.enemy, p->part[i].model);
            break;
        }
        RegisterSeqAndInit(inst, entry_handle(p->part[i].model), 1, HEAP);
        if (rd32(inst + INST_RESOURCES) == 0 || rd32(inst + INST_MODEL) == 0) {
            khdays_log("enemy viewer: Ms/%02X.p's part %d not loaded", vw.enemy, p->part[i].model);
            NNSi_FndFreeFromDefaultHeap(table);
            table = 0;
            free_instance(&inst, &table);
            continue;
        }
        if (p->part[i].anim != 0 && !(p->part[i].anim & 0x8000u)) {
            Snd_RegisterSeqAndBind(table, inst, entry_handle(p->part[i].anim), HEAP);
            BindAnimTrack(inst, 0, table, 0);
        } else {
            /* the model's own animation (Riku's B13_02w, Lumiere's flames), or none (a held weapon) */
            const int own = p->part[i].anim & 0x7fff;
            NNSi_FndFreeFromDefaultHeap(table);
            table = 0;
            if (p->part[i].anim != 0 && own < (int16_t)rd16(inst + INST_TABLE)) {
                BindAnimTrack(inst, 0, inst + INST_TABLE, own);
            }
        }
        vw.part[vw.parts].inst = inst;
        vw.part[vw.parts].table = table;
        vw.part[vw.parts].first = p->part[i].first;
        vw.part[vw.parts].frames = p->part[i].frames;
        ++vw.parts;
    }
}

/* The enemy's one animation (enemy_viewer_anims.h: its idle, or a calmer one when the idle moves much):
 * one of the model's own, or the only one of an entry of its own. */
static void play_chosen(void)
{
    const int known = vw.enemy >= 0 && vw.enemy < 0x76;
    const int entry = known ? VIEWER_ENEMIES[vw.enemy].entry : 0, index = known ? VIEWER_ENEMIES[vw.enemy].index : 0;
    const int own = (int16_t)rd16(vw.inst + INST_TABLE);
    vw.slow = known && VIEWER_ENEMIES[vw.enemy].slow != 0;
    vw.anim_entry = 0;
    if (entry == 0 && own > 0) {
        vw.anim = index < own ? index : 0;
        BindAnimTrack(vw.inst, 0, vw.inst + INST_TABLE, vw.anim);
    } else {
        const int file = entry != 0 ? entry - 1 : (animation_entries() > 0 ? vw.entries[0] : -1);
        int count;
        if (file <= 0 || (vw.table = alloc_zeroed(TABLE_SIZE)) == 0) {
            return; /* (none: the model at rest) */
        }
        Snd_RegisterSeqAndBind(vw.table, vw.inst, entry_handle(file), HEAP);
        count = (int16_t)rd16(vw.table);
        vw.anim = entry != 0 && index < count ? index : 0;
        vw.anim_entry = file;
        BindAnimTrack(vw.inst, 0, vw.table, vw.anim);
    }
    vw.anims = 1;
}

/* The page's banks lent to the viewer's textures, the 3D shown (see above). */
static void banks_take(void)
{
    memcpy(vw.bank_b, khdays_vram_bank(BANK_B), BANK_B_SIZE);
    vw.dispcnt = khdays_io_load(DISPCNT, 4);
    vw.disp3dcnt = khdays_io_load(DISP3DCNT, 2);
    GX_SetBankForOBJ(0);
    GX_SetBankForTex(VRAM_B);
    GX_SetBankForTexPltt(VRAM_FG);
    NNS_GfdResetFrmTexVramState();
    NNS_GfdInitFrmTexVramManager(1, 1);
    NNS_GfdResetFrmPlttVramState();
    NNS_GfdInitFrmPlttVramManager(0x8000u, 1);
    /* as the menu's: no fog, edges or anti-aliasing; alpha blending; the clear colour transparent */
    khdays_io_store(DISP3DCNT, 2, ((vw.disp3dcnt & 0xcff9u) | 8u));
    G3X_SetClearColor(0, 0, 0x7fff, 0x3f, 0);
    khdays_io_store(DISPCNT, 4, vw.dispcnt | 0x108u); /* BG0 the 3D, shown */
    vw.banks = 1;
}

static void banks_give_back(void)
{
    if (!vw.banks) {
        return;
    }
    GX_SetBankForTex(0);
    GX_SetBankForTexPltt(0);
    GX_SetBankForOBJ(VRAM_B);
    memcpy(khdays_vram_bank_written(BANK_B), vw.bank_b, BANK_B_SIZE);
    khdays_io_store(DISP3DCNT, 2, vw.disp3dcnt);
    khdays_io_store(DISPCNT, 4, vw.dispcnt);
    vw.banks = 0;
}

static void release_model(void)
{
    for (int i = vw.parts - 1; i >= 0; --i) {
        free_instance(&vw.part[i].inst, &vw.part[i].table);
    }
    vw.parts = 0;
    if (vw.inst != 0) {
        unbind_table();
        if (rd32(vw.inst + INST_RESOURCES) != 0) {
            ReleaseField74AndCleanup(vw.inst);
        }
        NNSi_FndFreeFromDefaultHeap(vw.inst);
    }
    if (vw.camera != 0) {
        NNSi_FndFreeFromDefaultHeap(vw.camera);
    }
    if (vw.container != 0) {
        NNSi_FndFreeFromDefaultHeap(vw.container);
    }
    if (vw.name != 0) {
        NNSi_FndFreeFromDefaultHeap(vw.name);
    }
    vw.inst = vw.camera = vw.container = vw.name = 0;
    vw.ready = 0;
    vw.anims = vw.anim = 0;
}

static void release(int page_on)
{
    release_model();
    if (page_on) {
        banks_give_back();
    }
    vw.banks = 0; /* (the page gone: the next screen sets its own) */
    vw.open = 0;
    vw.swallowed = vw.held;
}

static int load(void)
{
    char path[16];
    u32 model;
    vw.inst = alloc_zeroed(INST_SIZE);
    vw.camera = alloc_zeroed(CAMERA_SIZE);
    vw.name = alloc_zeroed(16);
    if (vw.inst == 0 || vw.camera == 0 || vw.name == 0) {
        khdays_log("enemy viewer: no room in the game's heap");
        return 0;
    }
    snprintf(path, sizeof(path), "Ms/%02x.p", vw.enemy); /* as the enemies' overlays name it */
    memcpy(KHDAYS_W(vw.name), path, strlen(path) + 1);
    vw.container = Msg_OpenContainerAndReadHeader(vw.name, ENEMY_MODE);
    if (vw.container == 0) {
        khdays_log("enemy viewer: %s not opened", path);
        return 0;
    }
    if (!vw.banks) {
        banks_take();
    } else {
        /* (another enemy: the last one's textures gone with it, their VRAM from the start again) */
        NNS_GfdResetFrmTexVramState();
        NNS_GfdInitFrmTexVramManager(1, 1);
        NNS_GfdResetFrmPlttVramState();
        NNS_GfdInitFrmPlttVramManager(0x8000u, 1);
    }
    RegisterSeqAndInit(vw.inst, entry_handle(0), 1, HEAP);
    model = rd32(vw.inst + INST_MODEL);
    if (rd32(vw.inst + INST_RESOURCES) == 0 || model == 0) {
        khdays_log("enemy viewer: %s's model not loaded", path);
        return 0;
    }
    if (vw.enemy >= 0 && vw.enemy < 0x76 && VIEWER_ENEMIES[vw.enemy].size > 0.0f) {
        /* its size measured: the middle of its reach, the view's height for its size, the reach inside */
        const ViewerEnemy *v = &VIEWER_ENEMIES[vw.enemy];
        const float fill = min_f(FILL_MOST, max_f(FILL_LEAST, FILL_AT + FILL_PER_LOG * logf(v->size / FILL_SIZE)));
        vw.centre[0] = v->centre[0];
        vw.centre[1] = v->centre[1];
        vw.centre[2] = v->centre[2];
        vw.view_height = max_f(v->size / fill, max_f(v->height / FIT, 2.0f * v->reach / (FIT * 4.0f / 3.0f)));
        vw.radius = sqrtf(v->reach * v->reach + v->height * v->height / 4.0f);
    } else {
        /* its box: the middle, the sphere round it (the camera's distance) */
        const float k = (float)(int32_t)rd32(model + MODEL_BOX_SCALE) / (4096.0f * 4096.0f);
        float box[6], size2 = 0;
        for (int i = 0; i < 6; ++i) {
            box[i] = (float)(int16_t)rd16(model + MODEL_BOX + 2u * (u32)i) * k;
        }
        for (int i = 0; i < 3; ++i) {
            vw.centre[i] = box[i] + box[3 + i] / 2;
            size2 += box[3 + i] * box[3 + i];
        }
        vw.radius = sqrtf(size2) / 2;
        if (!(vw.radius > 0.05f && vw.radius < 500.0f)) {
            vw.radius = 1.0f;
        }
        vw.view_height = 2.0f * VIEW_TAN * vw.radius / 0.5f * FRAME;
    }
    memset(vw.entries, 0, sizeof(vw.entries));
    play_chosen();
    load_parts();
    khdays_log("enemy viewer: Ms/%02X.p, its animation %d of entry %d (%s%s), %d parts, its reach %.2f, the view "
               "%.2f high",
               vw.enemy, vw.anim, vw.anim_entry, vw.table != 0 ? "a file of its own" : "the model's",
               vw.slow ? ", slower" : "", vw.parts, (double)vw.radius, (double)vw.view_height);
    return 1;
}

/* An instance's matrix (rows: x, y, z, then the place). */
static void write_matrix(u32 inst, const float r[9], const float t[3])
{
    for (int i = 0; i < 9; ++i) {
        *(volatile int32_t *)KHDAYS_W(inst + INST_MATRIX + 4u * (u32)i) = (int32_t)lrintf(r[i] * 4096.0f);
    }
    for (int i = 0; i < 3; ++i) {
        *(volatile int32_t *)KHDAYS_W(inst + INST_MATRIX + 36u + 4u * (u32)i) = (int32_t)lrintf(t[i] * 4096.0f);
    }
    *(volatile u16 *)KHDAYS_W(inst) &= (u16)0xffdfu; /* bit 5 off: the matrix as it is, not rebuilt */
}

/* The frame the body's animation is at (its track 0's: Sequence_UpdateTracks' channel, Q12), within `frames`. */
static int body_frame(int frames)
{
    const u32 channel = rd32(vw.inst + INST_CHANNEL);
    int frame = 0;
    if ((int16_t)rd16(vw.inst + INST_ACTIVE) >= 0 && channel != 0) {
        frame = (int)rd32(channel) >> 12;
    }
    return frame < 0 ? 0 : frame >= frames ? frames - 1 : frame;
}

/* The instances' matrices: the body turned, scaled, its middle at the origin; the parts at its place or on its
 * joints. */
static void place_model(void)
{
    const float cy = cosf(vw.yaw), sy = sinf(vw.yaw), cp = cosf(vw.pitch), sp = sinf(vw.pitch);
    /* row vectors: v * Ry * Rx * zoom */
    const float r[9] = {cy * vw.zoom, sy * sp * vw.zoom,  -sy * cp * vw.zoom, 0.0f, cp * vw.zoom, sp * vw.zoom,
                        sy * vw.zoom, -cy * sp * vw.zoom, cy * cp * vw.zoom};
    float t[3];
    for (int j = 0; j < 3; ++j) {
        t[j] = -(vw.centre[0] * r[j] + vw.centre[1] * r[3 + j] + vw.centre[2] * r[6 + j]);
    }
    write_matrix(vw.inst, r, t);
    for (int k = 0; k < vw.parts; ++k) {
        if (vw.part[k].frames == 0) {
            write_matrix(vw.part[k].inst, r, t); /* at the body's place */
        } else {
            /* hung on the body's joint: its matrix at the body's animation's frame, then the body's */
            const int *fx = VIEWER_HANGS[vw.part[k].first + (unsigned)body_frame((int)vw.part[k].frames)];
            float a[12], pr[9], pt[3];
            for (int i = 0; i < 12; ++i) {
                a[i] = (float)fx[i] / 4096.0f;
            }
            for (int i = 0; i < 3; ++i) {
                for (int j = 0; j < 3; ++j) {
                    pr[3 * i + j] = a[3 * i] * r[j] + a[3 * i + 1] * r[3 + j] + a[3 * i + 2] * r[6 + j];
                }
            }
            for (int j = 0; j < 3; ++j) {
                pt[j] = a[9] * r[j] + a[10] * r[3 + j] + a[11] * r[6 + j] + t[j];
            }
            write_matrix(vw.part[k].inst, pr, pt);
        }
    }
}

static void place_camera(void)
{
    /* the vertical 60 degrees (Projection_LoadDefaults'): the sphere whole, a little room round it */
    const float distance = vw.view_height / (2.0f * VIEW_TAN);
    const float near_plane = max_f(0.05f, (distance - vw.radius * vw.zoom) * 0.5f);
    volatile int32_t *c = (volatile int32_t *)KHDAYS_W(vw.camera);
    Projection_LoadDefaults(vw.camera);
    c[3] = (int32_t)lrintf(near_plane * 4096.0f);
    c[4] = (int32_t)lrintf(max_f(distance * 4.0f, 50.0f) * 4096.0f);
    c[5] = c[6] = c[7] = 0;                                          /* the target */
    c[8] = 0, c[9] = 0, c[10] = (int32_t)lrintf(distance * 4096.0f); /* the eye */
    c[11] = 0, c[12] = 0x1000, c[13] = 0;                            /* up */
    Camera_CommitMatrices(vw.camera);
}

/* A stick's axis past its dead zone, -1 to 1. */
static float axis(int v)
{
    const float a = v < 0 ? (float)-v : (float)v;
    if (a < STICK_DEAD) {
        return 0.0f;
    }
    return (v < 0 ? -1.0f : 1.0f) * min_f(1.0f, (a - STICK_DEAD) / (32767.0f - STICK_DEAD));
}

static void steer(float pace)
{
    const u16 held = vw.held, pressed = vw.pressed;
    const float turn_stick = axis(vw.sticks[0][0]), zoom_stick = axis(vw.sticks[1][1]);
    const int left_stick_on = turn_stick != 0.0f || axis(vw.sticks[0][1]) != 0.0f;
    float turn = 0.0f, zoom = 0.0f;
    vw.pressed = 0;
    /* turning left and right only: the left stick as far as it is pushed, else the D-pad (which the
     * left stick also presses) */
    if (turn_stick != 0.0f) {
        turn = turn_stick;
    } else {
        turn = (held & KHDAYS_VIEW_RIGHT ? 1.0f : 0.0f) - (held & KHDAYS_VIEW_LEFT ? 1.0f : 0.0f);
    }
    /* zooming: the right stick (up nearer), else the D-pad's up and down */
    if (zoom_stick != 0.0f) {
        zoom = -zoom_stick;
    } else if (!left_stick_on) { /* (the left stick pushed up or down presses the D-pad too: not a zoom) */
        zoom = (held & KHDAYS_VIEW_IN ? 1.0f : 0.0f) - (held & KHDAYS_VIEW_OUT ? 1.0f : 0.0f);
    }
    if (turn != 0.0f) {
        vw.touched = 1;
        vw.yaw += TURN * pace * turn;
    }
    vw.pitch = 0.0f;
    if (zoom > 0.0f) {
        vw.zoom = min_f(ZOOM_MOST, vw.zoom * powf(ZOOM_STEP, pace * zoom));
    } else if (zoom < 0.0f) {
        vw.zoom = max_f(ZOOM_LEAST, vw.zoom / powf(ZOOM_STEP, -pace * zoom));
    }
    if (pressed & KHDAYS_VIEW_PREVIOUS) {
        vw.step_to = -1;
    } else if (pressed & KHDAYS_VIEW_NEXT) {
        vw.step_to = 1;
    }
    if (pressed & KHDAYS_VIEW_RESET) {
        vw.yaw = vw.pitch = 0;
        vw.zoom = 1.0f;
        vw.touched = 0;
        PlaySound(0, SOUND_CLICK);
    }
    if ((pressed & KHDAYS_VIEW_CLOSE) && !vw.closing) {
        vw.closing = 1;
        PlaySound(0, SOUND_CANCEL);
    }
}

/* The met enemy `dir` rows away from the one shown, round the list; -1 none. */
static int next_row(int dir)
{
    for (int k = 1; k <= vw.rows; ++k) {
        const int r = ((vw.row + dir * k) % vw.rows + vw.rows) % vw.rows;
        if (vw.ids[r] >= 0) {
            return r;
        }
    }
    return -1;
}

/* Another enemy in the viewer: the model let go, the next one's loaded (the banks kept). */
static void show_row(int row)
{
    release_model();
    vw.row = row;
    vw.enemy = vw.ids[row];
    vw.yaw = vw.pitch = 0;
    vw.zoom = 1.0f;
    vw.touched = 0;
    ++vw.serial; /* (the screen's name and counter: the page's darkened picture kept) */
    if (load()) {
        vw.ready = 1;
    } else {
        release(1);
    }
}

void khdays_enemy_viewer_update(int on, int enemies, int cursor, int rows, const int16_t *ids)
{
    vw.on = on;
    vw.enemies = enemies;
    vw.cursor = cursor;
    if (!vw.open) {
        vw.rows = ids != NULL && rows > 0 ? rows : 0;
        if (vw.rows > vw.id_room) {
            int16_t *grown = (int16_t *)realloc(vw.ids, (size_t)vw.rows * sizeof(*grown));
            if (grown == NULL) {
                vw.rows = vw.id_room; /* the rows it has room for */
            } else {
                vw.ids = grown;
                vw.id_room = vw.rows;
            }
        }
        if (vw.rows > 0) {
            memcpy(vw.ids, ids, (size_t)vw.rows * sizeof(vw.ids[0]));
        }
    }
    if (vw.open && (!on || !enemies)) {
        release(on); /* the page gone under it */
        publish();
        return;
    }
    if (!vw.open && vw.wanted) {
        vw.wanted = 0;
        if (on && enemies && cursor >= 0 && cursor < vw.rows && vw.ids[cursor] >= 0 && vw.ids[cursor] < 0x80) {
            vw.row = cursor;
            vw.enemy = vw.ids[cursor];
            vw.open = 1;
            vw.closing = 0;
            vw.yaw = vw.pitch = 0;
            vw.zoom = 1.0f;
            vw.touched = 0;
            vw.held = vw.pressed = 0;
            ++vw.serial;
            ++vw.opening;
            PlaySound(0, SOUND_OPEN);
            if (!load()) {
                release(on);
            } else {
                vw.ready = 1;
            }
        }
    }
    if (vw.open && vw.ready) {
        const int rate = GetFrameRateMode();
        const int step = rate == 1 ? 0xaaa : rate == 2 ? 0x2000 : 0x1000; /* the menu's (Ov025_ModelActor_Init) */
        steer((float)step / 4096.0f);
        if (!vw.closing && vw.step_to != 0) {
            const int row = next_row(vw.step_to);
            vw.step_to = 0;
            if (row >= 0 && row != vw.row) {
                PlaySound(0, SOUND_CLICK);
                show_row(row);
            }
        }
        vw.step_to = 0;
        if (vw.closing || !vw.open) {
            if (vw.open) {
                release(on);
            }
        } else if (vw.ready) {
            const int pace = vw.slow ? (int)((float)step * SLOW_PACE) : step;
            Sequence_UpdateTracks(vw.inst, pace);
            for (int i = 0; i < vw.parts; ++i) {
                Sequence_UpdateTracks(vw.part[i].inst, pace); /* (in step with the body's: the same numbers) */
            }
            place_model(); /* (the parts on joints at the frame the body is drawn at) */
            place_camera();
            Scene_DrawNode(vw.inst);
            for (int i = 0; i < vw.parts; ++i) {
                Scene_DrawNode(vw.part[i].inst);
            }
        }
    }
    publish();
}

uint16_t khdays_enemy_viewer_buttons(uint16_t buttons)
{
    static u16 last;
    const u16 pressed = (u16)(buttons & ~last);
    const KhdaysInputSnapshot *snap = khdays_input_snapshot();
    last = buttons;
    if (vw.open) {
        vw.held = buttons;
        vw.pressed |= pressed;
        if (snap != NULL) {
            static int lock_was;
            const int lock_on = (snap->actions & KHDAYS_ACTION_LOCK_ON) != 0;
            if (lock_on && !lock_was) {
                vw.pressed |= VIEW_LOCK_ON;
            }
            lock_was = lock_on;
            memcpy(vw.sticks, snap->sticks, sizeof(vw.sticks));
        } else {
            memset(vw.sticks, 0, sizeof(vw.sticks));
        }
        return 0; /* the page waits */
    }
    vw.swallowed &= buttons; /* the buttons that closed it: the page gets them once let go */
    buttons = (uint16_t)(buttons & ~vw.swallowed);
    if ((pressed & (KHDAYS_VIEW_OPEN | KHDAYS_KEY_A)) && vw.on && vw.enemies && vw.cursor >= 0 && vw.cursor < vw.rows &&
        vw.ids[vw.cursor] < 0) {
        PlaySound(0, SOUND_ERROR); /* an enemy not met yet: the error's two beats */
    }
    if ((pressed & KHDAYS_VIEW_OPEN) && vw.on && vw.enemies && vw.cursor >= 0 && vw.cursor < vw.rows &&
        vw.ids[vw.cursor] >= 0) {
        vw.wanted = 1;
        vw.swallowed |= KHDAYS_VIEW_OPEN;
        return (uint16_t)(buttons & ~KHDAYS_VIEW_OPEN);
    }
    return buttons;
}
