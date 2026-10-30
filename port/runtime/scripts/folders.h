/* The files the mods' scripts read and write: folders listed, files read whole and written whole,
 * paths in UTF-8 with '/' between their parts (std::filesystem underneath, so the same on every
 * platform). */
#ifndef KHDAYS_SCRIPTS_FOLDERS_H
#define KHDAYS_SCRIPTS_FOLDERS_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Calls `each` with the name of every folder directly in `path`, in byte order of the names.
 * Returns 0 when `path` is not a folder. */
int khdays_folders_list(const char *path, void (*each)(const char *name, void *context), void *context);

/* The same with every file directly in `path`. */
int khdays_folders_list_files(const char *path, void (*each)(const char *name, void *context), void *context);

/* Calls `each` with every file under `path` (all depths), as its path from `path` with '/', in byte
 * order of those paths. Returns 0 when `path` is not a folder. */
int khdays_folders_files(const char *path, void (*each)(const char *relative, void *context), void *context);

/* The time `path` was last written, in the file system's own unit; -1 when it is missing. */
long long khdays_folders_time(const char *path);

/* The file's size in bytes; -1 when it is missing. */
long long khdays_folders_size(const char *path);

int khdays_folders_is_file(const char *path);

/* The whole file, with a 0 after it (not counted in `size`), to free(); NULL when it cannot be read. */
char *khdays_folders_read(const char *path, size_t *size);

/* Writes the whole file through a temporary one beside it, making its folders. Returns 1 on success. */
int khdays_folders_write(const char *path, const void *data, size_t size);

/* Renames `from` over `to` (replacing it). Returns 1 on success. */
int khdays_folders_rename(const char *from, const char *to);

int khdays_folders_remove(const char *path);

#ifdef __cplusplus
}
#endif

#endif
