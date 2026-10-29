/* The originals written out (sound_dump.h). */
#include "sound_dump.h"
#include "../core/window.h"

#include "../core/rom.h"
#include "sound.h"
#include "sound_bridge.h"
#include "../../sound/archive.h"
#include "../../sound/channels.h"
#include "../../sound/render.h"
#include "../../sound/replace.h"
#include "../../sound/snd7_native.h"
#include "../../sound/stream_reader.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#define RENDER_RATE 48000u
#define RENDER_LIMIT 600u /* seconds */

/* Where a render's files go in the DS's main RAM, free as the game does not
 * run (its static module and BSS end far below): the render's command lists
 * and shared work, then the bank, its wave archives and the sequence. */
#define REGION 0x02200000u
#define REGION_SIZE 0x00100000u
#define WORK REGION
#define FILES (REGION + 0x400u)

/* A stream's rings, for the stream reader to bind the channels to: any
 * address will do, nothing is written there. */
#define STREAM_RING 0x02300000u
#define STREAM_RING_BYTES 2048u

static const char *root; /* the directory the dump goes to */
static int written, failed;
static const char *const *only; /* the sounds asked for (khdays_sound_dump), every one with none */
static int only_count;

/* Whether the sound `identifier` is one asked for. */
static int wanted(const char *identifier)
{
    for (int i = 0; i < only_count; ++i) {
        if (strcmp(identifier, only[i]) == 0) {
            return 1;
        }
    }
    return only_count == 0;
}

static uint32_t rd16(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8;
}

static uint32_t rd32(const uint8_t *p)
{
    return rd16(p) | rd16(p + 2) << 16;
}

/* --- Where it goes ------------------------------------------------------------------ */

/* The repository `path` is in (the nearest directory above it, or it, with
 * a .git), into `repository`; 0 if none. */
static int repository_of(const char *path, char *repository, size_t size)
{
    char at[MAX_PATH];
    snprintf(at, sizeof(at), "%s", path);
    for (;;) {
        char git[MAX_PATH + 8];
        char *slash;
        snprintf(git, sizeof(git), "%s\\.git", at);
        if (GetFileAttributesA(git) != INVALID_FILE_ATTRIBUTES) {
            snprintf(repository, size, "%s", at);
            return 1;
        }
        slash = strrchr(at, '\\');
        if (slash == NULL || slash == at || slash[-1] == ':') {
            return 0;
        }
        *slash = '\0';
    }
}

/* Inside a repository only under its saves\ (sound_dump.h). */
int khdays_dump_allowed(const char *directory, char *full, size_t size)
{
    char repository[MAX_PATH], saves[MAX_PATH + 8];
    if (GetFullPathNameA(directory, (DWORD)size, full, NULL) == 0) {
        fprintf(stderr, "dump: %s is not a path\n", directory);
        return 0;
    }
    if (!repository_of(full, repository, sizeof(repository))) {
        return 1;
    }
    snprintf(saves, sizeof(saves), "%s\\saves", repository);
    if (_strnicmp(full, saves, strlen(saves)) == 0 && (full[strlen(saves)] == '\0' || full[strlen(saves)] == '\\')) {
        return 1;
    }
    fprintf(stderr,
            "dump: %s is inside the repository %s: the dump is the game's data and goes under its saves\\ (which "
            "git ignores), or outside it\n",
            full, repository);
    return 0;
}

/* Every directory above the file at `path`. */
static void make_directories(const char *path)
{
    char at[MAX_PATH];
    snprintf(at, sizeof(at), "%s", path);
    for (char *p = at; *p != '\0'; ++p) {
        if ((*p == '/' || *p == '\\') && p != at && p[-1] != ':') {
            const char c = *p;
            *p = '\0';
            CreateDirectoryA(at, NULL);
            *p = c;
        }
    }
}

static void write_wav(const char *identifier, uint32_t rate, int channels, const int16_t *samples, uint64_t frames,
                      int looped, uint64_t loop_start, uint64_t loop_end)
{
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s/%s.wav", root, identifier);
    make_directories(path);
    if (khdays_replacement_write_wav(path, rate, channels, samples, frames, looped, loop_start, loop_end)) {
        ++written;
    } else {
        fprintf(stderr, "dump: cannot write %s\n", path);
        ++failed;
    }
}

/* --- The archive's files ------------------------------------------------------------ */

/* File `file_id` of the archive into `destination` (up to `room` bytes):
 * its size, 0 if it has none or it does not fit. */
static uint32_t load_file(const KhdaysSoundArchive *a, uint32_t file_id, uint8_t *destination, uint32_t room)
{
    uint32_t offset, size;
    if (!khdays_sound_archive_file(a, file_id, &offset, &size) || size == 0 || size > room) {
        return 0;
    }
    return khdays_sound_archive_read(a, offset, destination, size) == size ? size : 0;
}

/* A file of the archive in memory of its own (free()): NULL if none. */
static uint8_t *file_copy(const KhdaysSoundArchive *a, uint32_t file_id, uint32_t *size)
{
    uint32_t offset;
    uint8_t *data;
    if (!khdays_sound_archive_file(a, file_id, &offset, size) || *size == 0) {
        return NULL;
    }
    data = malloc(*size);
    if (data != NULL && khdays_sound_archive_read(a, offset, data, *size) != *size) {
        free(data);
        data = NULL;
    }
    return data;
}

static uint32_t record_word(const KhdaysSoundArchive *a, uint32_t record, uint32_t at)
{
    uint8_t b[4];
    return khdays_sound_archive_read(a, record + at, b, 4) == 4 ? rd32(b) : 0;
}

static uint32_t record_half(const KhdaysSoundArchive *a, uint32_t record, uint32_t at)
{
    uint8_t b[2];
    return khdays_sound_archive_read(a, record + at, b, 2) == 2 ? rd16(b) : 0;
}

/* --- Waves -------------------------------------------------------------------------- */

/* A wave archive (SWAR): +0x38 its waves, +0x3c where each is from its
 * start; a wave is SNDWaveParam -- +0 format, +1 loop, +2 rate, +4 timer,
 * +6 the loop's start and +8 its length, in words -- and its data. */
static void dump_waves(const KhdaysSoundArchive *a)
{
    const int count = khdays_sound_archive_count(a, KHDAYS_SOUND_WAVE);
    for (int n = 0; n < count; ++n) {
        const uint32_t record = khdays_sound_archive_record(a, KHDAYS_SOUND_WAVE, n);
        uint32_t size, waves;
        uint8_t *swar;
        if (record == 0) {
            continue;
        }
        swar = file_copy(a, record_word(a, record, 0) & 0xffffffu, &size); /* NNSSndArcWaveArcInfo.fileId */
        if (swar == NULL || size < 0x3c || memcmp(swar, "SWAR", 4) != 0) {
            free(swar);
            continue;
        }
        waves = rd32(swar + 0x38);
        for (uint32_t i = 0; i < waves && 0x3c + 4 * i + 4 <= size; ++i) {
            const uint32_t at = rd32(swar + 0x3c + 4 * i);
            const uint8_t *param = swar + at;
            uint32_t loop_words, words, frames, per_word, header;
            char identifier[256];
            int16_t *samples;
            if (at == 0 || at + 12 > size) {
                continue;
            }
            khdays_sound_archive_identifier(a, KHDAYS_SOUND_WAVE, n, (int)i, identifier, sizeof(identifier));
            if (!wanted(identifier)) {
                continue;
            }
            loop_words = rd16(param + 6);
            words = loop_words + rd32(param + 8);
            if (param[0] > 2 || at + 12 + 4 * (uint64_t)words > size) {
                fprintf(stderr, "dump: wave %u of wave archive %d is not a wave this reads\n", i, n);
                ++failed;
                continue;
            }
            per_word = param[0] == 0 ? 4 : param[0] == 1 ? 2 : 8;
            header = param[0] == 2 ? 8 : 0; /* ADPCM's header word */
            samples = malloc(((size_t)words * per_word + 1) * sizeof(int16_t));
            if (samples == NULL) {
                ++failed;
                continue;
            }
            frames = khdays_sound_decode(param + 12, words, param[0], samples);
            /* a loop from its start (after ADPCM's header) to its end */
            write_wav(identifier, rd16(param + 2), 1, samples, frames,
                      param[1] != 0 && loop_words * per_word >= header && loop_words * per_word - header < frames,
                      loop_words * per_word - header, frames);
            free(samples);
        }
        free(swar);
    }
}

/* --- Streams ------------------------------------------------------------------------ */

typedef struct StreamFile {
    const KhdaysSoundArchive *archive;
    uint32_t offset, size;
} StreamFile;

static uint32_t read_stream(void *context, uint32_t offset, void *destination, uint32_t size)
{
    const StreamFile *f = context;
    if (offset >= f->size) {
        return 0;
    }
    if (size > f->size - offset) {
        size = f->size - offset;
    }
    return khdays_sound_archive_read(f->archive, f->offset + offset, destination, size);
}

/* A stream (STRM) as the stream player makes it from its start, once
 * through: its header's +0x18 format, +0x19 loop, +0x1a channels, +0x1c
 * rate, +0x20 the loop's start, and its blocks (NNSSndStrmData). */
static void dump_streams(const KhdaysSoundArchive *a)
{
    const int count = khdays_sound_archive_count(a, KHDAYS_SOUND_STREAM);
    KhdaysStreams *streams = khdays_streams_new((const int8_t *)KHDAYS_W(KHDAYS_NNS_ADPCM_INDEX_TABLE),
                                                (const int16_t *)KHDAYS_W(KHDAYS_NNS_ADPCM_STEP_TABLE));
    if (streams == NULL) {
        ++failed;
        return;
    }
    for (int n = 0; n < count; ++n) {
        const uint32_t record = khdays_sound_archive_record(a, KHDAYS_SOUND_STREAM, n);
        StreamFile file = {a, 0, 0};
        KhdaysStreamPrepared prepared;
        const uint8_t *h = prepared.header;
        uint32_t blocks, frames;
        int channels, handle[2];
        int16_t *samples;
        char identifier[256];
        if (record == 0 ||
            !khdays_sound_archive_file(a, record_word(a, record, 0), &file.offset, &file.size)) { /* fileId */
            continue;
        }
        khdays_sound_archive_identifier(a, KHDAYS_SOUND_STREAM, n, -1, identifier, sizeof(identifier));
        if (!wanted(identifier)) {
            continue;
        }
        memset(&prepared, 0, sizeof(prepared));
        if (read_stream(&file, 0, prepared.header, sizeof(prepared.header)) != sizeof(prepared.header) ||
            memcmp(h, "STRM", 4) != 0 || h[0x18] > 2 || h[0x1a] < 1) {
            fprintf(stderr, "dump: stream %d is not a stream this reads\n", n);
            ++failed;
            continue;
        }
        channels = h[0x1a] < 2 ? h[0x1a] : 2;
        blocks = rd32(h + 0x2c);
        frames = blocks != 0 ? (blocks - 1) * rd32(h + 0x34) + rd32(h + 0x3c) : 0;
        prepared.player = 0;
        prepared.channels = channels;
        for (int c = 0; c < channels; ++c) {
            prepared.channel[c] = (uint8_t)c;
        }
        prepared.ring = STREAM_RING;
        prepared.ring_bytes = STREAM_RING_BYTES;
        khdays_streams_prepared(streams, &prepared, (KhdaysStreamFile){&file, read_stream});
        for (int c = 0; c < channels; ++c) {
            handle[c] = khdays_streams_bind(streams, c, STREAM_RING + (uint32_t)c * STREAM_RING_BYTES,
                                            STREAM_RING_BYTES / 4, h[0x18] == 0 ? 0 : 1);
        }
        samples = malloc(((size_t)frames * (size_t)channels + 1) * sizeof(int16_t));
        if (samples == NULL || handle[0] < 0 || (channels == 2 && handle[1] < 0)) {
            free(samples);
            ++failed;
            continue;
        }
        for (uint32_t i = 0; i < frames; ++i) {
            for (int c = 0; c < channels; ++c) {
                int32_t s = 0;
                khdays_streams_sample(streams, handle[c], i, &s);
                samples[(size_t)i * (size_t)channels + (size_t)c] = (int16_t)s;
            }
        }
        write_wav(identifier, rd16(h + 0x1c), channels, samples, frames, h[0x19] != 0 && rd32(h + 0x20) < frames,
                  rd32(h + 0x20), frames);
        free(samples);
    }
    /* khdays_streams has no free: its few buffers go with the process */
}

/* --- Sequences ---------------------------------------------------------------------- */

/* What is in the region now, so consecutive entries with the same bank do
 * not load it again. */
static int loaded_bank = -1;
static uint32_t files_end; /* past the bank and its wave archives */

/* Bank `bank` (NNSSndArcBankInfo: its file, its four wave archives) into
 * the region, each wave archive assigned to it as SND_AssignWaveArc does
 * (the bank's +0x18 links: the archive, the next link; the archive's +0x18
 * the first link). 0 if it cannot. */
static int load_bank(const KhdaysSoundArchive *a, int bank)
{
    const uint32_t record = khdays_sound_archive_record(a, KHDAYS_SOUND_BANK, bank);
    const uint32_t sbnk = FILES;
    uint8_t *const region = (uint8_t *)KHDAYS_W(REGION);
    uint32_t at, size;
    if (bank == loaded_bank) {
        return 1;
    }
    loaded_bank = -1;
    if (record == 0 || (size = load_file(a, record_word(a, record, 0), region + (sbnk - REGION),
                                         REGION + REGION_SIZE - sbnk)) < 0x3c) {
        return 0;
    }
    at = (sbnk + size + 31) & ~31u;
    for (uint32_t i = 0; i < 4; ++i) {
        const uint32_t wave_archive = record_half(a, record, 4 + 2 * i);
        uint8_t *link = region + (sbnk - REGION) + 0x18 + 8 * i;
        uint32_t wave_record;
        memset(link, 0, 8);
        if (wave_archive == 0xffff) {
            continue; /* NNS_SND_ARC_INVALID_WAVEARC_NO */
        }
        wave_record = khdays_sound_archive_record(a, KHDAYS_SOUND_WAVE, (int)wave_archive);
        if (wave_record == 0 || (size = load_file(a, record_word(a, wave_record, 0) & 0xffffffu, region + (at - REGION),
                                                  REGION + REGION_SIZE - at)) < 0x3c) {
            return 0;
        }
        {
            const uint32_t link_address = sbnk + 0x18 + 8 * i;
            memcpy(link, &at, 4);                                    /* the link's archive; no next */
            memcpy(region + (at - REGION) + 0x18, &link_address, 4); /* the archive's first link */
        }
        at = (at + size + 31) & ~31u;
    }
    loaded_bank = bank;
    files_end = at;
    return 1;
}

/* The player's channels (NNSSndArcPlayerInfo +2, allocChBitFlag). */
static uint16_t player_channels(const KhdaysSoundArchive *a, int player)
{
    const uint32_t record = khdays_sound_archive_record(a, KHDAYS_SOUND_PLAYER, player);
    return record != 0 ? (uint16_t)record_half(a, record, 2) : 0;
}

/* Renders the sequence at `sequence` (+ `offset`) with NNSSndSeqParam
 * `param` (bank +0, channel priority +3, player +5) and writes it. */
static void render(const KhdaysSoundArchive *a, const char *identifier, uint32_t sequence, uint32_t offset,
                   const uint8_t *param)
{
    KhdaysRenderRequest q;
    KhdaysRendered r;
    memset(&q, 0, sizeof(q));
    q.sequence = sequence;
    q.offset = offset;
    q.bank = FILES;
    q.channels = player_channels(a, param[5]);
    q.channel_priority = param[3];
    q.work = WORK;
    q.work_bytes = (uint8_t *)KHDAYS_W(WORK);
    q.memory = khdays_sound_ds_memory;
    q.rate = RENDER_RATE;
    q.limit_seconds = RENDER_LIMIT;
    if (!khdays_render_sequence(&q, &r)) {
        ++failed;
        return;
    }
    if (!r.looped && !r.ended) {
        fprintf(stderr, "dump: %s neither ends nor loops in %u seconds: cut there\n", identifier, RENDER_LIMIT);
    }
    write_wav(identifier, RENDER_RATE, 2, r.samples, r.frames, r.looped, r.loop_start, r.loop_end);
    free(r.samples);
}

/* The sequence file (SSEQ or SSAR) `file_id` after the bank: its address,
 * 0 if it does not fit. */
static uint32_t load_sequence_file(const KhdaysSoundArchive *a, uint32_t file_id, uint32_t *size)
{
    *size = load_file(a, file_id, (uint8_t *)KHDAYS_W(files_end), REGION + REGION_SIZE - files_end);
    return *size >= 0x20 ? files_end : 0;
}

/* Each sequence (NNSSndArcSeqInfo: its file, then NNSSndSeqParam), its
 * data from the file's baseOffset (+0x18), as StartSeq plays it. */
static void dump_sequences(const KhdaysSoundArchive *a)
{
    const int count = khdays_sound_archive_count(a, KHDAYS_SOUND_SEQUENCE);
    for (int n = 0; n < count; ++n) {
        const uint32_t record = khdays_sound_archive_record(a, KHDAYS_SOUND_SEQUENCE, n);
        uint8_t param[8];
        uint32_t file, size;
        char identifier[256];
        if (record == 0 || khdays_sound_archive_read(a, record + 4, param, 8) != 8) {
            continue;
        }
        khdays_sound_archive_identifier(a, KHDAYS_SOUND_SEQUENCE, n, -1, identifier, sizeof(identifier));
        if (!wanted(identifier)) {
            continue;
        }
        if (!load_bank(a, (int)rd16(param)) || (file = load_sequence_file(a, record_word(a, record, 0), &size)) == 0) {
            fprintf(stderr, "dump: %s: its bank or its file cannot be loaded\n", identifier);
            ++failed;
            continue;
        }
        render(a, identifier, file + rd32((const uint8_t *)KHDAYS_W(file) + 0x18), 0, param);
    }
}

/* Each entry of each sequence archive (SSAR: +0x18 baseOffset, +0x1c how
 * many, +0x20 NNSSndSeqArcSeqInfo each -- its offset, 0xffffffff none, and
 * NNSSndSeqParam), as StartSeqArc plays it. */
static void dump_sequence_archives(const KhdaysSoundArchive *a)
{
    const int count = khdays_sound_archive_count(a, KHDAYS_SOUND_SEQUENCE_ARCHIVE_ENTRY);
    for (int n = 0; n < count; ++n) {
        const uint32_t record = khdays_sound_archive_record(a, KHDAYS_SOUND_SEQUENCE_ARCHIVE_ENTRY, n);
        uint32_t size, entries;
        uint8_t *ssar;
        if (record == 0) {
            continue;
        }
        ssar = file_copy(a, record_word(a, record, 0), &size); /* NNSSndArcSeqArcInfo.fileId */
        if (ssar == NULL || size < 0x20 || memcmp(ssar, "SSAR", 4) != 0) {
            free(ssar);
            continue;
        }
        entries = rd32(ssar + 0x1c);
        for (uint32_t e = 0; e < entries && 0x20 + 12 * e + 12 <= size; ++e) {
            const uint8_t *info = ssar + 0x20 + 12 * e;
            char identifier[256];
            uint32_t file, file_size;
            if (rd32(info) == 0xffffffffu) {
                continue; /* NNS_SND_SEQ_ARC_INVALID_OFFSET */
            }
            khdays_sound_archive_identifier(a, KHDAYS_SOUND_SEQUENCE_ARCHIVE_ENTRY, n, (int)e, identifier,
                                            sizeof(identifier));
            if (!wanted(identifier)) {
                continue;
            }
            if (!load_bank(a, (int)rd16(info + 4)) ||
                (file = load_sequence_file(a, record_word(a, record, 0), &file_size)) == 0) {
                fprintf(stderr, "dump: %s: its bank or its file cannot be loaded\n", identifier);
                ++failed;
                continue;
            }
            render(a, identifier, file + rd32(ssar + 0x18), rd32(info), info + 4);
        }
        free(ssar);
    }
}

/* --- The dump ----------------------------------------------------------------------- */

int khdays_sound_dump(const char *directory, const char *const *only_these, int only_these_count)
{
    static char full[MAX_PATH];
    const KhdaysSoundArchive *a;
    int before;
    if (!khdays_dump_allowed(directory, full, sizeof(full))) {
        return 1;
    }
    root = full;
    only = only_these;
    only_count = only_these_count;
    a = khdays_sound_bridge_archive();
    if (a == NULL || !khdays_sound_bridge_native_segment()) {
        return 1;
    }
    khdays_snd7_native_set_ds_base((uintptr_t)khdays_window); /* the game's memory (window.h) */
    printf("dump: the game's sounds to %s\n", full);
    fflush(stdout);

    before = written;
    dump_waves(a);
    printf("dump: %d waves\n", written - before);
    fflush(stdout);
    before = written;
    dump_streams(a);
    printf("dump: %d streams\n", written - before);
    fflush(stdout);
    before = written;
    dump_sequences(a);
    printf("dump: %d sequences\n", written - before);
    fflush(stdout);
    before = written;
    dump_sequence_archives(a);
    printf("dump: %d sequence archive entries\n", written - before);
    printf("dump: %d files written, %d not\n", written, failed);
    if (only_count != 0 && written + failed < only_count) {
        printf("dump: %d of the %d sounds named are not in the archive\n", only_count - written - failed, only_count);
        failed = 1;
    }
    fflush(stdout);
    return failed != 0 ? 1 : 0;
}
