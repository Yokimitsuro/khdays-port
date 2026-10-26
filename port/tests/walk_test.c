/* Exercise the imported gait on the same reference-based memory the game uses. */
#include "../runtime/input/walk.h"
#include "../runtime/core/clock.h"
#include <windows.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

unsigned char *khdays_window;
static unsigned frame;
static uint64_t cycle;
static const char *clip_path;
static int failures;
#define ACTOR 0x02002000
#define NODE 0x02001000
#define MODEL 0x02003000
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            printf("walk_test:%d failed: %s\n", __LINE__, #c);                                                         \
            ++failures;                                                                                                \
        }                                                                                                              \
    } while (0)
int QueryActiveStateOrDelegate(void)
{
    return 0;
}
int GetEntryField20ByIndex(int index)
{
    return index == 0 ? ACTOR : 0;
}
unsigned khdays_input_game_frames(void)
{
    return frame;
}
uint64_t khdays_clock_cycles(void)
{
    return cycle;
}
int khdays_origin_of_ram(unsigned address, const char **file, unsigned *offset, unsigned *run)
{
    *file = "ba/ch/ro/def.p.z";
    *offset = 0;
    *run = 0;
    return address == MODEL;
}
int khdays_mods_find(const char *relative, char *path, int size)
{
    if (strcmp(relative, "animations/walk/ro.khw"))
        return 0;
    snprintf(path, size, "%s", clip_path);
    return 1;
}
void khdays_log(const char *format, ...)
{
    (void)format;
}
int khdays_mod_installed(int mod)
{
    (void)mod;
    return 1;
} /* the Walk mod installed (its clip the test's) */
/* the sound: the footsteps the walk asks for are counted, the game having asked for a stone one */
static int footsteps, owns_steps;
void khdays_sound_bridge_walk_steps(int owns)
{
    owns_steps = owns;
}
int khdays_sound_bridge_footstep(char *archive, int archive_size, char *entry, int entry_size)
{
    snprintf(archive, archive_size, "FIELD_13FS_STONE");
    snprintf(entry, entry_size, "se_13fs_stone_run01");
    return 1;
}
void khdays_sound_bridge_expect_footstep(void)
{
}
const char *khdays_link_play_sound(const char *sequence_archive, const char *entry)
{
    if (!strcmp(sequence_archive, "FIELD_13FS_STONE") && !strncmp(entry, "se_13fs_stone_run0", 18))
        ++footsteps;
    return NULL;
}

static void step(int walk)
{
    ++frame;
    cycle += KHDAYS_CLOCK_HZ / 30;
    khdays_walk_movement(ACTOR, walk ? 50 : 100, 100);
    khdays_walk_draw_begin(NODE);
}

int main(int argc, char **argv)
{
    if (argc != 2)
        return 2;
    clip_path = argv[1];
    if (GetFileAttributesA(clip_path) == INVALID_FILE_ATTRIBUTES)
        return 77;
    khdays_window = calloc(1, 0x02400000);
    if (!khdays_window)
        return 3;
    *(unsigned *)(khdays_window + ACTOR + 0x20) = NODE - 4;
    *(unsigned *)(khdays_window + NODE + 0x24) = MODEL;
    khdays_window[MODEL + 0x17] = 24;
    for (int n = 0; n < 10; ++n) {
        step(1);
        khdays_walk_draw_end();
    }
    step(1);
    int32_t result[22] = {0};
    result[0] = 0x1f;
    khdays_walk_joint(14, result);
    CHECK(!(result[0] & 6));
    for (int column = 0; column < 3; ++column)
        for (int other = 0; other < 3; ++other) {
            double dot = 0;
            for (int r = 0; r < 3; ++r)
                dot += (double)result[10 + column * 3 + r] * result[10 + other * 3 + r] / (4096.0 * 4096.0);
            CHECK(fabs(dot - (column == other ? 1 : 0)) < .002);
        }
    CHECK(result[19] || result[20] || result[21]);
    uint16_t animation[13 + 24] = {0};
    ((unsigned char *)animation)[25] = 24;
    for (unsigned joint = 0; joint < 24; ++joint)
        animation[13 + joint] = (uint16_t)(0x100u + joint);
    animation[13 + 14] = 0x103;
    animation[13 + 3] = 0x10e;
    int32_t mapped[22] = {0};
    mapped[0] = 0x1f;
    khdays_walk_mapped_joint(3, animation, mapped);
    CHECK(!memcmp(result, mapped, sizeof(result)));
    int32_t cache[22] = {0};
    cache[0] = 0x1f;
    khdays_walk_send_begin(14, cache);
    CHECK(!memcmp(result, cache, sizeof(result)));
    khdays_walk_send_end(cache);
    CHECK(cache[0] == 0x1f && cache[10] == 0);
    const int before = result[10];
    khdays_walk_draw_end();
    khdays_walk_joint(14, result);
    CHECK(result[10] == before);
    int32_t untouched[22] = {0};
    untouched[0] = 0x1f;
    khdays_walk_draw_begin(NODE + 0x100);
    khdays_walk_joint(14, untouched);
    CHECK(untouched[0] == 0x1f);
    for (int n = 0; n < 10; ++n) {
        step(0);
        khdays_walk_draw_end();
    }
    step(0);
    khdays_walk_joint(14, untouched);
    CHECK(untouched[0] == 0x1f);
    khdays_walk_draw_end();
    for (int n = 0; n < 40; ++n) {
        step(1);
        khdays_walk_draw_end();
    }
    /* a stride and a third at the walk's own pace: its feet planted, the footsteps asked for */
    CHECK(owns_steps);
    CHECK(footsteps >= 1);
    /* the stick let go: the walk stays while the game stands (no snap to its run), then hands over */
    frame += 3;
    cycle += KHDAYS_CLOCK_HZ / 10;
    int32_t held[22] = {0};
    held[0] = 0x1f;
    khdays_walk_draw_begin(NODE);
    khdays_walk_joint(14, held);
    CHECK(!(held[0] & 6));
    khdays_walk_draw_end();
    for (int n = 0; n < 20; ++n) {
        ++frame;
        cycle += KHDAYS_CLOCK_HZ / 30;
        khdays_walk_draw_begin(NODE);
        khdays_walk_draw_end();
    }
    khdays_walk_draw_begin(NODE);
    khdays_walk_joint(14, untouched);
    CHECK(untouched[0] == 0x1f);
    CHECK(!owns_steps);
    khdays_walk_draw_end();
    free(khdays_window);
    if (!failures)
        printf("walk_test: imported gait, rotation normalization, fade-out, the stop, stale input, actor isolation "
               "and the footsteps passed\n");
    return failures != 0;
}
