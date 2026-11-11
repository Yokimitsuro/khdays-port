/* The enemies' updates (enemy_updates.h). */
#include "enemy_updates.h"

#include <stdlib.h>

int khdays_enemy_far_updates(void)
{
    static int ds_way = -1;
    if (ds_way < 0) {
        const char *v = getenv("KHDAYS_DS_ENEMY_UPDATES");
        ds_way = v != NULL && v[0] == '1';
    }
    return ds_way;
}
