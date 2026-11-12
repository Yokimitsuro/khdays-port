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

/* The KH2 enemy whose stand-in mission `m`'s built enemy table's row `row` is (the builder's rows: one for each class
 * and KH2 enemy in its place), into `name`; 0 for a row of Days's own, or one the mission does not have. */
int khdays_missions_kh2_row(const KhdaysMission *m, int row, char *name, size_t size);

/* The KH2 enemy every row of Days's class `enemy_class` in mission `m` stands in for, into `name`; 0 when the class
 * has a Days enemy of its own there, two KH2 enemies, or no row. */
int khdays_missions_kh2_class(const KhdaysMission *m, int enemy_class, char *name, size_t size);

#ifdef __cplusplus
}
#endif

#endif
