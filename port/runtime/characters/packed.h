/* The game's packed files (".z": Archive_LoadFile unpacks them by their name), for the characters' module. */
#ifndef KHDAYS_CHARACTERS_PACKED_H
#define KHDAYS_CHARACTERS_PACKED_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A packed file's bytes unpacked, or bytes packed as the game reads them: a new buffer in *out (free it), 1; 0 when
 * the bytes are not packed as the game's (or out of memory). */
int khdays_characters_unpack(const unsigned char *data, size_t size, unsigned char **out, size_t *out_size);
int khdays_characters_pack(const unsigned char *data, size_t size, unsigned char **out, size_t *out_size);

#ifdef __cplusplus
}
#endif

#endif
