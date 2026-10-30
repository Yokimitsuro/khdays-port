/* A ZIP archive read from memory: its entries as its central directory lists them, each stored or deflated, its
 * CRC checked. No ZIP64 and no encryption (an archive with them is refused). */
#ifndef KHDAYS_ZIP_READ_H
#define KHDAYS_ZIP_READ_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct KhdaysZipEntry {
    char *name; /* as the archive names it, '/' between its parts; a folder's ends with '/' */
    unsigned method, flags;
    uint32_t crc, packed, size, header; /* `header`: its local header's offset */
} KhdaysZipEntry;

typedef struct KhdaysZip {
    const unsigned char *data; /* the archive (the caller's, kept while the reader is used) */
    size_t size;
    KhdaysZipEntry *entries;
    int count;
} KhdaysZip;

/* Reads the archive's directory. 0 with the reason in `why` when it is not one this reads. */
int khdays_zip_open(KhdaysZip *z, const unsigned char *data, size_t size, const char **why);

/* Entry `i`'s bytes (`*size` of them, to free()); NULL with the reason in `why`. */
unsigned char *khdays_zip_extract(const KhdaysZip *z, int i, size_t *size, const char **why);

/* The entry named `name`; -1 none. */
int khdays_zip_find(const KhdaysZip *z, const char *name);

void khdays_zip_close(KhdaysZip *z);

#ifdef __cplusplus
}
#endif

#endif
