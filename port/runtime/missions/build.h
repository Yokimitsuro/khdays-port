/* A mod's mission built at the start from its description (build.cpp). */
#ifndef KHDAYS_MISSIONS_BUILD_H
#define KHDAYS_MISSIONS_BUILD_H

#include "registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Mission `m`'s file built from its waves, start, time and texts: malloc'd into *out, *size bytes. 0 with the
 * reason in `why`. */
int khdays_missions_build(const KhdaysMission *m, unsigned char **out, size_t *size, char *why, size_t why_size);

#ifdef __cplusplus
}
#endif

#endif
