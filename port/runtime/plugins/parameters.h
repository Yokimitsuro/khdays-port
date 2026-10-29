/* A native plugin's parameters: what its mod's author lets a modder change without code (docs/MODDING_NATIVE.md,
 * "Its parameters"). The mod describes them in a file (`parameters.ini` beside mod.ini, or mod.ini's `parameters`):
 * a part per parameter, its kind, its default, its bounds or choices, its label and help in the game's languages.
 * The values chosen are kept apart (`<that file without .ini>.values.ini`): KH Studio writes them, the port gives
 * them to the plugin (kh.parameters). Read by both without loading the plugin. Plain C: no game, no files. */
#ifndef KHDAYS_PLUGINS_PARAMETERS_H
#define KHDAYS_PLUGINS_PARAMETERS_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KHDAYS_PARAMETER_LANGUAGES 5 /* en fr de it es */

typedef enum KhdaysParameterKind {
    KHDAYS_PARAMETER_INTEGER,
    KHDAYS_PARAMETER_NUMBER,
    KHDAYS_PARAMETER_FLAG,
    KHDAYS_PARAMETER_CHOICE,
    KHDAYS_PARAMETER_TEXT
} KhdaysParameterKind;

typedef struct KhdaysParameter {
    char *name;
    int kind;        /* KhdaysParameterKind */
    double min, max; /* integer, number: the bounds (min > max: none) */
    char **choices;  /* choice */
    int choice_count;
    double default_number; /* integer, number, flag (0/1), choice (its index) */
    char *default_text;    /* text */
    double number;         /* its value: as default_number */
    char *text;            /* text: its value */
    char *label, *help;    /* with no language; NULL none */
    char *labels[KHDAYS_PARAMETER_LANGUAGES], *helps[KHDAYS_PARAMETER_LANGUAGES];
} KhdaysParameter;

typedef struct KhdaysParameters {
    KhdaysParameter *list;
    int count;
} KhdaysParameters;

/* A problem found reading: its line (0 none) and what. */
typedef void (*KhdaysParametersReport)(int line, const char *why, void *context);

/* The description `text` read into `out` (emptied first), each value its default; what is wrong told through `report`
 * and left out (a parameter without a known kind, a default out of its bounds brought into them). How many. */
int khdays_parameters_read(KhdaysParameters *out, const char *text, size_t size, KhdaysParametersReport report,
                           void *context);

/* The values `text` (`name = value` lines) given to the parameters they name; unknown names and bad values told and
 * left as they were. */
void khdays_parameters_values(KhdaysParameters *p, const char *text, size_t size, KhdaysParametersReport report,
                              void *context);

/* Parameter `name` given `value` as a person writes it (a number, yes/no, a choice's name, a text); NULL when done,
 * else why not (into `why`). */
const char *khdays_parameters_set(KhdaysParameters *p, const char *name, const char *value, char *why, size_t why_size);

/* The parameter named `name`, NULL none. Its value as a person reads it into `out`. */
const KhdaysParameter *khdays_parameters_find(const KhdaysParameters *p, const char *name);
void khdays_parameters_value_text(const KhdaysParameter *q, char *out, size_t size);

/* The values file for the parameters (a comment line, then each parameter's `name = value`): a new string (free it). */
char *khdays_parameters_write_values(const KhdaysParameters *p);

/* The values file's name for the description file `descriptor` ("parameters.ini" -> "parameters.values.ini"), into
 * `out`. */
void khdays_parameters_values_name(const char *descriptor, char *out, size_t size);

void khdays_parameters_free(KhdaysParameters *p);

#ifdef __cplusplus
}
#endif

#endif
