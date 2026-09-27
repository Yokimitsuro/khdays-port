/* Copies the game's native data initializers into DS memory when their
 * module is loaded (see KHDAYS_DATA_INIT in compat/decomp_compat.h). The
 * entries are grouped by the linker between these two markers, in section
 * order ($a < $m < $z); it may pad between them with zeros, which is skipped. */
#include "../compat/decomp_compat.h"
#include "runtime.h"

#include <string.h>

#pragma section(".khdi$a", read, write)
#pragma section(".khdi$z", read, write)
__declspec(allocate(".khdi$a")) static struct khdays_data_init first_marker = {0, 0, 0, 0};
__declspec(allocate(".khdi$z")) static struct khdays_data_init last_marker = {0, 0, 0, 0};

void khdays_data_init(int module)
{
    for (const struct khdays_data_init *entry = &first_marker + 1; entry < &last_marker; ++entry) {
        if (entry->address != 0 && entry->module == module) {
            memcpy(entry->address, entry->init, entry->size);
        }
    }
}
