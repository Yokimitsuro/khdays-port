/* KH Mod SDK: the plugin's parameters (kh.parameters) -- what its mod lets a modder change without code.
 *
 * The mod describes them in `parameters.ini` beside its mod.ini (or the file mod.ini's `parameters` names): a part a
 * parameter, its type (integer, number, flag, choice, text), its default, its bounds or choices, its label and help
 * in the game's languages. KH Studio shows them as controls -- reading that file, never the plugin -- and keeps the
 * values chosen in `parameters.values.ini` beside it. The port reads both before the plugin starts and answers with
 * the value chosen, else the default, else the fallback the plugin gives (a name it does not know, another type). The
 * format: docs/MODDING_NATIVE.md, "Its parameters". */
#ifndef KH_PARAMETERS_H
#define KH_PARAMETERS_H

#include "kh_mod.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KH_PARAMETERS "kh.parameters"
#define KH_PARAMETERS_VERSION 1

typedef struct KhParameters {
    uint32_t size;

    /* An integer, number or flag (1 yes, 0 no) parameter's value; a choice's index among its choices. */
    int32_t (*integer)(KhPlugin *plugin, const char *name, int32_t fallback);
    double (*number)(KhPlugin *plugin, const char *name, double fallback);

    /* A text parameter's value, or a choice's name (valid until the plugin stops). */
    const char *(*text)(KhPlugin *plugin, const char *name, const char *fallback);
} KhParameters;

#ifdef __cplusplus
}
#endif

#endif
