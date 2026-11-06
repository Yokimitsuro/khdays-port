/* The new missions a copy brings to a multiplayer room (room.h). */
#include "room.h"

#include <stdlib.h>
#include <string.h>

enum { DIGEST = 32 };

static int offered(const KhdaysMission *m)
{
    return m->number != 0 && m->block != NULL && strlen(m->identity) <= 0xffffu;
}

static void put16(unsigned char *p, unsigned v)
{
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
}

static unsigned get16(const unsigned char *p)
{
    return (unsigned)p[0] | (unsigned)p[1] << 8;
}

unsigned char *khdays_missions_room_list(const KhdaysMissions *r, size_t *size)
{
    size_t total = 0;
    *size = 0;
    for (int i = 0; i < r->count; ++i) {
        if (offered(&r->list[i])) {
            total += 2 + strlen(r->list[i].identity) + DIGEST + 4;
        }
    }
    if (total == 0) {
        return NULL;
    }
    unsigned char *list = malloc(total), *p = list;
    if (list == NULL) {
        return NULL;
    }
    for (int i = 0; i < r->count; ++i) {
        const KhdaysMission *m = &r->list[i];
        if (!offered(m)) {
            continue;
        }
        const size_t n = strlen(m->identity);
        put16(p, (unsigned)n);
        memcpy(p + 2, m->identity, n);
        p += 2 + n;
        memcpy(p, m->digest, DIGEST);
        p += DIGEST;
        put16(p, (unsigned)m->number);
        put16(p + 2, (unsigned)m->file);
        p += 4;
    }
    *size = total;
    return list;
}

int khdays_missions_room_next(const unsigned char *list, size_t size, size_t *at, KhdaysRoomMission *out)
{
    if (list == NULL || *at + 2 > size) {
        return 0;
    }
    const size_t n = get16(list + *at);
    if (n == 0 || *at + 2 + n + DIGEST + 4 > size) {
        return 0;
    }
    const unsigned char *p = list + *at + 2;
    out->identity = (const char *)p;
    out->identity_size = n;
    out->digest = p + n;
    out->number = (int)get16(p + n + DIGEST);
    out->file = (int)get16(p + n + DIGEST + 2);
    *at += 2 + n + DIGEST + 4;
    return 1;
}

static int same(const KhdaysRoomMission *e, const KhdaysMission *m)
{
    return e->identity_size == strlen(m->identity) && memcmp(e->identity, m->identity, e->identity_size) == 0 &&
           memcmp(e->digest, m->digest, DIGEST) == 0;
}

int khdays_missions_room_holds(const unsigned char *list, size_t size, const KhdaysMission *m)
{
    KhdaysRoomMission e;
    size_t at = 0;
    while (offered(m) && khdays_missions_room_next(list, size, &at, &e)) {
        if (same(&e, m)) {
            return 1;
        }
    }
    return 0;
}

const KhdaysMission *khdays_missions_room_local(const KhdaysMissions *r, const unsigned char *list, size_t size,
                                                int number, int *named)
{
    KhdaysRoomMission e;
    size_t at = 0;
    *named = 0;
    while (khdays_missions_room_next(list, size, &at, &e)) {
        if (e.number != number) {
            continue;
        }
        *named = 1;
        for (int i = 0; i < r->count; ++i) {
            if (offered(&r->list[i]) && same(&e, &r->list[i])) {
                return &r->list[i];
            }
        }
        return NULL;
    }
    return NULL;
}
