/* A ZIP archive read from memory (zip_read.h). The deflated entries are inflated by stb_image's inflater
 * (compiled in host/render/png_io.c). */
#include "zip_read.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

int stbi_zlib_decode_noheader_buffer(char *obuffer, int olen, const char *ibuffer, int ilen);

#define END_SIGNATURE 0x06054b50u
#define ENTRY_SIGNATURE 0x02014b50u
#define LOCAL_SIGNATURE 0x04034b50u
#define END_SIZE 22
#define COMMENT_MOST 0xffff

static uint32_t rd16(const unsigned char *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8;
}

static uint32_t rd32(const unsigned char *p)
{
    return rd16(p) | rd16(p + 2) << 16;
}

static uint32_t crc32_of(const unsigned char *p, size_t n)
{
    static uint32_t table[256];
    static int made;
    if (!made) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) {
                c = c & 1 ? 0xedb88320u ^ (c >> 1) : c >> 1;
            }
            table[i] = c;
        }
        made = 1;
    }
    uint32_t c = 0xffffffffu;
    for (size_t i = 0; i < n; ++i) {
        c = table[(c ^ p[i]) & 0xff] ^ (c >> 8);
    }
    return c ^ 0xffffffffu;
}

/* The end of the central directory: its signature in the last bytes (a comment may follow it). */
static long end_record(const unsigned char *data, size_t size)
{
    if (size < END_SIZE) {
        return -1;
    }
    for (size_t at = size - END_SIZE;; --at) {
        if (rd32(data + at) == END_SIGNATURE) {
            return (long)at;
        }
        if (at == 0 || size - at >= END_SIZE + COMMENT_MOST) {
            return -1;
        }
    }
}

int khdays_zip_open(KhdaysZip *z, const unsigned char *data, size_t size, const char **why)
{
    memset(z, 0, sizeof(*z));
    z->data = data;
    z->size = size;
    const long end = end_record(data, size);
    if (end < 0) {
        *why = "not a ZIP archive";
        return 0;
    }
    const unsigned char *e = data + end;
    const uint32_t count = rd16(e + 10), dir_size = rd32(e + 12), dir_at = rd32(e + 16);
    if (count == 0xffffu || dir_at == 0xffffffffu) {
        *why = "a ZIP64 archive (not read)";
        return 0;
    }
    if ((uint64_t)dir_at + dir_size > (uint64_t)end) {
        *why = "its directory is past its end";
        return 0;
    }
    z->entries = count > 0 ? calloc(count, sizeof(*z->entries)) : NULL;
    if (count > 0 && z->entries == NULL) {
        *why = "out of memory";
        return 0;
    }
    size_t at = dir_at;
    for (uint32_t i = 0; i < count; ++i) {
        if (at + 46 > (size_t)dir_at + dir_size || rd32(data + at) != ENTRY_SIGNATURE) {
            *why = "its directory is broken";
            khdays_zip_close(z);
            return 0;
        }
        const unsigned char *h = data + at;
        const uint32_t name_n = rd16(h + 28), extra_n = rd16(h + 30), comment_n = rd16(h + 32);
        if (at + 46 + name_n > (size_t)dir_at + dir_size) {
            *why = "its directory is broken";
            khdays_zip_close(z);
            return 0;
        }
        KhdaysZipEntry *en = &z->entries[z->count];
        en->name = malloc(name_n + 1);
        if (en->name == NULL) {
            *why = "out of memory";
            khdays_zip_close(z);
            return 0;
        }
        memcpy(en->name, h + 46, name_n);
        en->name[name_n] = '\0';
        en->flags = rd16(h + 8);
        en->method = rd16(h + 10);
        en->crc = rd32(h + 16);
        en->packed = rd32(h + 20);
        en->size = rd32(h + 24);
        en->header = rd32(h + 42);
        ++z->count;
        at += 46 + name_n + extra_n + comment_n;
    }
    return 1;
}

unsigned char *khdays_zip_extract(const KhdaysZip *z, int i, size_t *size, const char **why)
{
    const KhdaysZipEntry *en = &z->entries[i];
    *size = 0;
    if (en->flags & 1u) {
        *why = "encrypted";
        return NULL;
    }
    if (en->method != 0 && en->method != 8) {
        *why = "packed in a way this does not read (only stored or deflated)";
        return NULL;
    }
    if ((size_t)en->header + 30 > z->size || rd32(z->data + en->header) != LOCAL_SIGNATURE) {
        *why = "its entry is broken";
        return NULL;
    }
    const size_t from = (size_t)en->header + 30 + rd16(z->data + en->header + 26) + rd16(z->data + en->header + 28);
    if (from > z->size || en->packed > z->size - from) {
        *why = "its entry is past the archive's end";
        return NULL;
    }
    if (en->packed > INT_MAX || en->size > INT_MAX) {
        *why = "an entry too big to unpack";
        return NULL;
    }
    unsigned char *out = malloc(en->size > 0 ? en->size : 1);
    if (out == NULL) {
        *why = "out of memory";
        return NULL;
    }
    if (en->method == 0) {
        if (en->packed != en->size) {
            free(out);
            *why = "its entry's sizes differ";
            return NULL;
        }
        memcpy(out, z->data + from, en->size);
    } else if (en->size > 0 &&
               stbi_zlib_decode_noheader_buffer((char *)out, (int)en->size, (const char *)(z->data + from),
                                                (int)en->packed) != (int)en->size) {
        free(out);
        *why = "its entry does not inflate";
        return NULL;
    }
    if (crc32_of(out, en->size) != en->crc) {
        free(out);
        *why = "its entry's CRC differs (a broken archive)";
        return NULL;
    }
    *size = en->size;
    return out;
}

int khdays_zip_find(const KhdaysZip *z, const char *name)
{
    for (int i = 0; i < z->count; ++i) {
        if (strcmp(z->entries[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

void khdays_zip_close(KhdaysZip *z)
{
    for (int i = 0; i < z->count; ++i) {
        free(z->entries[i].name);
    }
    free(z->entries);
    memset(z, 0, sizeof(*z));
}
