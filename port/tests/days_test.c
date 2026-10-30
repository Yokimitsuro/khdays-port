/* The KH DS Project Editor's mods (runtime/mods/days.h): a .days made here, left in a mods folder, copied out and
 * listed as a mod; a wrong hash and an entry outside the mod refused; the copy taken away once its .days is gone;
 * a mod made for another copy of the game told. */
#include "../runtime/core/sha256.h"
#include "../runtime/mods/days.h"
#include "../runtime/mods/list.h"
#include "../runtime/scripts/folders.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                            \
            ++failures;                                                                                                \
        }                                                                                                              \
    } while (0)

static char said[8192];

void khdays_log(const char *format, ...)
{
    va_list args;
    const size_t n = strlen(said);
    va_start(args, format);
    vsnprintf(said + n, sizeof(said) - n, format, args);
    va_end(args);
    strncat(said, "\n", sizeof(said) - strlen(said) - 1);
}

/* ---- a ZIP made here --------------------------------------------------------------------------- */

typedef struct Entry {
    const char *name;
    const unsigned char *packed; /* deflated (method 8), or the bytes themselves (stored) */
    unsigned packed_n;
    const unsigned char *bytes; /* what it holds */
    unsigned n;
} Entry;

static unsigned crc32_of(const unsigned char *p, unsigned n)
{
    unsigned c = 0xffffffffu;
    for (unsigned i = 0; i < n; ++i) {
        c ^= p[i];
        for (int k = 0; k < 8; ++k) {
            c = c & 1 ? 0xedb88320u ^ (c >> 1) : c >> 1;
        }
    }
    return c ^ 0xffffffffu;
}

static unsigned char zip[8192];
static unsigned zip_n;

static void put16(unsigned v)
{
    zip[zip_n++] = (unsigned char)v;
    zip[zip_n++] = (unsigned char)(v >> 8);
}

static void put32(unsigned v)
{
    put16(v & 0xffff);
    put16(v >> 16);
}

static void put(const void *p, unsigned n)
{
    memcpy(zip + zip_n, p, n);
    zip_n += n;
}

static void make_zip(const Entry *e, int count)
{
    unsigned at[16];
    zip_n = 0;
    for (int i = 0; i < count; ++i) {
        const int deflated = e[i].packed != e[i].bytes;
        at[i] = zip_n;
        put32(0x04034b50u);
        put16(20), put16(0), put16(deflated ? 8 : 0), put16(0), put16(0);
        put32(crc32_of(e[i].bytes, e[i].n)), put32(e[i].packed_n), put32(e[i].n);
        put16((unsigned)strlen(e[i].name)), put16(0);
        put(e[i].name, (unsigned)strlen(e[i].name));
        put(e[i].packed, e[i].packed_n);
    }
    const unsigned dir = zip_n;
    for (int i = 0; i < count; ++i) {
        const int deflated = e[i].packed != e[i].bytes;
        put32(0x02014b50u);
        put16(20), put16(20), put16(0), put16(deflated ? 8 : 0), put16(0), put16(0);
        put32(crc32_of(e[i].bytes, e[i].n)), put32(e[i].packed_n), put32(e[i].n);
        put16((unsigned)strlen(e[i].name)), put16(0), put16(0), put16(0), put16(0), put32(0), put32(at[i]);
        put(e[i].name, (unsigned)strlen(e[i].name));
    }
    const unsigned dir_n = zip_n - dir;
    put32(0x06054b50u);
    put16(0), put16(0), put16((unsigned)count), put16((unsigned)count), put32(dir_n), put32(dir), put16(0);
}

static void hex_of(const void *data, size_t n, char out[65])
{
    unsigned char d[32];
    khdays_sha256(data, n, d);
    for (int i = 0; i < 32; ++i) {
        snprintf(out + 2 * i, 3, "%02x", d[i]);
    }
}

/* ---- the test ---------------------------------------------------------------------------------- */

#define MODS "days_test_mods"

static const unsigned char hello[] = "hello";
static const unsigned char hello_deflated[] = {0xcb, 0x48, 0xcd, 0xc9, 0xc9, 0x07, 0x00};

/* A .days of one game file (root/ba/test.bin, "hello" deflated), its mod.yml giving `hash` for it. */
static void write_days(const char *name, const char *hash, const char *extra_entry)
{
    char yml[512], original[65], path[256];
    hex_of("the game's own", 14, original);
    snprintf(yml, sizeof(yml),
             "# Kingdom Hearts DS Project Editor - mod\ntitle: Test Mod\nauthor: Someone\ngame: YKGP\n"
             "assets:\n  - path: root/ba/test.bin\n    size: 5\n    hash: %s\n    originalHash: %s\n",
             hash, original);
    Entry e[3] = {
        {"mod.yml", (const unsigned char *)yml, (unsigned)strlen(yml), (const unsigned char *)yml,
         (unsigned)strlen(yml)},
        {"root/ba/test.bin", hello_deflated, sizeof(hello_deflated), hello, 5},
        {extra_entry, hello, 5, hello, 5},
    };
    make_zip(e, extra_entry != NULL ? 3 : 2);
    snprintf(path, sizeof(path), MODS "/%s", name);
    CHECK(khdays_folders_write(path, zip, zip_n));
}

static int holds(const char *path, const char *text)
{
    size_t n = 0;
    char *data = khdays_folders_read(path, &n);
    const int same = data != NULL && n == strlen(text) && memcmp(data, text, n) == 0;
    free(data);
    return same;
}

static void clean(void)
{
    static const char *const files[] = {
        MODS "/Test Mod.days",
        MODS "/Bad.days",
        MODS "/Outside.days",
        MODS "/.days/Test Mod/files/ba/test.bin",
        MODS "/.days/Test Mod/mod.yml",
        MODS "/.days/Test Mod/unpacked.txt",
        MODS "/.days/Test Mod/files/ba",
        MODS "/.days/Test Mod/files",
        MODS "/.days/Test Mod",
        MODS "/.days",
        "days_test_game/ba/test.bin",
        "days_test_game/ba",
        "days_test_game",
        MODS,
    };
    for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); ++i) {
        khdays_folders_remove(files[i]);
    }
}

int main(void)
{
    char good[65];
    hex_of(hello, 5, good);
    clean();

    /* copied out and listed */
    write_days("Test Mod.days", good, NULL);
    khdays_days_unpack_all(MODS);
    CHECK(holds(MODS "/.days/Test Mod/files/ba/test.bin", "hello"));
    CHECK(khdays_folders_is_file(MODS "/.days/Test Mod/mod.yml"));
    CHECK(strstr(said, "\"Test Mod\" by Someone: 1 of the game's files") != NULL);
    khdays_mods_scan(MODS, NULL);
    const KhdaysModEntry *m = khdays_mods_by_folder("Test Mod.days");
    CHECK(m != NULL && strcmp(m->path, MODS "/.days/Test Mod") == 0 && !m->scripted && khdays_mods_active(m));
    CHECK(khdays_mods_by_folder(".days") == NULL);

    /* as it was: not copied again */
    said[0] = '\0';
    khdays_days_unpack_all(MODS);
    CHECK(strstr(said, "copied into") == NULL);

    /* a wrong hash, and an entry outside the mod: refused */
    write_days("Bad.days", "00", NULL);
    write_days("Outside.days", good, "root/../../evil.bin");
    said[0] = '\0';
    khdays_days_unpack_all(MODS);
    CHECK(strstr(said, "Bad.days: root/ba/test.bin is not the file mod.yml names") != NULL);
    CHECK(strstr(said, "Outside.days: an entry named outside the mod") != NULL);
    CHECK(!khdays_folders_is_file(MODS "/.days/Bad/unpacked.txt"));
    CHECK(!khdays_folders_is_file("evil.bin") && !khdays_folders_is_file(MODS "/evil.bin"));
    khdays_folders_remove(MODS "/Bad.days");
    khdays_folders_remove(MODS "/Outside.days");

    /* made for another copy of the game: told */
    CHECK(khdays_folders_write("days_test_game/ba/test.bin", "another game's", 14));
    khdays_mods_scan(MODS, NULL);
    khdays_mods_start();
    said[0] = '\0';
    khdays_days_check_originals("days_test_game");
    CHECK(strstr(said, "Test Mod.days: made for another copy of the game: 1 of the 1 files") != NULL);

    /* its .days gone: the copy taken away */
    khdays_folders_remove(MODS "/Test Mod.days");
    said[0] = '\0';
    khdays_days_unpack_all(MODS);
    CHECK(!khdays_folders_is_file(MODS "/.days/Test Mod/files/ba/test.bin"));
    CHECK(!khdays_folders_is_file(MODS "/.days/Test Mod/unpacked.txt"));
    CHECK(strstr(said, "Test Mod.days is gone") != NULL);

    clean();
    if (failures == 0) {
        printf("days mods: all checks passed\n");
    }
    return failures == 0 ? 0 : 1;
}
