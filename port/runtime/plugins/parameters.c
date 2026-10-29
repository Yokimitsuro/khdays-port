/* A native plugin's parameters, read and written (parameters.h). */
#include "parameters.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const languages[KHDAYS_PARAMETER_LANGUAGES] = {"en", "fr", "de", "it", "es"};
static const char *const kinds[] = {"integer", "number", "flag", "choice", "text"};

static char *copy_span(const char *s, size_t n)
{
    char *c = malloc(n + 1);
    if (c != NULL) {
        memcpy(c, s, n);
        c[n] = '\0';
    }
    return c;
}

static char *copy(const char *s)
{
    return s != NULL ? copy_span(s, strlen(s)) : NULL;
}

static void tell(KhdaysParametersReport report, void *context, int line, const char *format, const char *a,
                 const char *b)
{
    char why[200];
    if (report != NULL) {
        snprintf(why, sizeof(why), format, a, b);
        report(line, why, context);
    }
}

/* `s` (n bytes) trimmed of spaces at both ends: its start, its length into *n. */
static const char *trimmed(const char *s, size_t *n)
{
    while (*n > 0 && isspace((unsigned char)s[0])) {
        ++s;
        --*n;
    }
    while (*n > 0 && isspace((unsigned char)s[*n - 1])) {
        --*n;
    }
    return s;
}

/* A line's text without its comment: `#` at its start or after a space. */
static size_t without_comment(const char *s, size_t n)
{
    for (size_t i = 0; i < n; ++i) {
        if (s[i] == '#' && (i == 0 || isspace((unsigned char)s[i - 1]))) {
            return i;
        }
    }
    return n;
}

static int parse_number(const char *s, double *out)
{
    char *end;
    const double v = strtod(s, &end);
    if (end == s || *end != '\0' || !isfinite(v)) {
        return 0;
    }
    *out = v;
    return 1;
}

static int parse_flag(const char *s, double *out)
{
    if (strcmp(s, "yes") == 0 || strcmp(s, "on") == 0 || strcmp(s, "1") == 0 || strcmp(s, "true") == 0) {
        *out = 1;
        return 1;
    }
    if (strcmp(s, "no") == 0 || strcmp(s, "off") == 0 || strcmp(s, "0") == 0 || strcmp(s, "false") == 0) {
        *out = 0;
        return 1;
    }
    return 0;
}

static int choice_index(const KhdaysParameter *q, const char *s)
{
    for (int i = 0; i < q->choice_count; ++i) {
        if (strcmp(q->choices[i], s) == 0) {
            return i;
        }
    }
    return -1;
}

/* `value` as parameter `q` takes it, into `number` or `text`; NULL when good, else why not. */
static const char *understood(const KhdaysParameter *q, const char *value, double *number, char **text)
{
    switch (q->kind) {
    case KHDAYS_PARAMETER_INTEGER:
    case KHDAYS_PARAMETER_NUMBER:
        if (!parse_number(value, number)) {
            return "not a number";
        }
        if (q->kind == KHDAYS_PARAMETER_INTEGER && *number != floor(*number)) {
            return "not a whole number";
        }
        if (q->min <= q->max && (*number < q->min || *number > q->max)) {
            return "outside its bounds";
        }
        return NULL;
    case KHDAYS_PARAMETER_FLAG: return parse_flag(value, number) ? NULL : "neither yes nor no";
    case KHDAYS_PARAMETER_CHOICE: {
        const int i = choice_index(q, value);
        if (i < 0) {
            return "not one of its choices";
        }
        *number = i;
        return NULL;
    }
    default: *text = copy(value); return *text != NULL ? NULL : "no memory";
    }
}

static void free_parameter(KhdaysParameter *q)
{
    free(q->name);
    for (int i = 0; i < q->choice_count; ++i) {
        free(q->choices[i]);
    }
    free(q->choices);
    free(q->default_text);
    free(q->text);
    free(q->label);
    free(q->help);
    for (int i = 0; i < KHDAYS_PARAMETER_LANGUAGES; ++i) {
        free(q->labels[i]);
        free(q->helps[i]);
    }
    memset(q, 0, sizeof(*q));
}

void khdays_parameters_free(KhdaysParameters *p)
{
    for (int i = 0; i < p->count; ++i) {
        free_parameter(&p->list[i]);
    }
    free(p->list);
    p->list = NULL;
    p->count = 0;
}

/* A parameter being read: its keys as written, checked when its part ends. */
typedef struct Reading {
    KhdaysParameter q;
    char *kind, *default_value, *min, *max, *choices;
    int line;
} Reading;

static void free_reading(Reading *r)
{
    free(r->kind);
    free(r->default_value);
    free(r->min);
    free(r->max);
    free(r->choices);
}

static void split_choices(KhdaysParameter *q, const char *list)
{
    const char *s = list;
    while (*s != '\0') {
        const char *comma = strchr(s, ',');
        size_t n = comma != NULL ? (size_t)(comma - s) : strlen(s);
        const char *t = trimmed(s, &n);
        if (n > 0) {
            char **grown = realloc(q->choices, (size_t)(q->choice_count + 1) * sizeof(char *));
            if (grown == NULL) {
                return;
            }
            q->choices = grown;
            q->choices[q->choice_count++] = copy_span(t, n);
        }
        if (comma == NULL) {
            break;
        }
        s = comma + 1;
    }
}

/* The part read so far checked and kept in `out`, or told and dropped. */
static void finish(KhdaysParameters *out, Reading *r, KhdaysParametersReport report, void *context)
{
    KhdaysParameter *q = &r->q;
    int kind = -1;
    const char *bad = NULL;
    char *text = NULL;
    if (q->name == NULL) {
        free_reading(r);
        return;
    }
    for (int i = 0; i < (int)(sizeof(kinds) / sizeof(kinds[0])); ++i) {
        if (r->kind != NULL && strcmp(r->kind, kinds[i]) == 0) {
            kind = i;
        }
    }
    if (kind < 0) {
        tell(report, context, r->line, "[%s]: its type is missing or unknown (%s)", q->name,
             "integer, number, flag, choice, text");
        free_parameter(q);
        free_reading(r);
        return;
    }
    q->kind = kind;
    q->min = 1;
    q->max = 0; /* no bounds */
    if ((r->min != NULL && !parse_number(r->min, &q->min)) || (r->max != NULL && !parse_number(r->max, &q->max))) {
        tell(report, context, r->line, "[%s]: a bound is not a number%s", q->name, "");
        q->min = 1;
        q->max = 0;
    }
    if (r->min != NULL && r->max == NULL) {
        q->max = HUGE_VAL;
    } else if (r->min == NULL && r->max != NULL) {
        q->min = -HUGE_VAL;
    }
    if (kind == KHDAYS_PARAMETER_CHOICE) {
        split_choices(q, r->choices != NULL ? r->choices : "");
        if (q->choice_count == 0) {
            tell(report, context, r->line, "[%s]: a choice without its choices%s", q->name, "");
            free_parameter(q);
            free_reading(r);
            return;
        }
    }
    if (r->default_value == NULL) {
        q->default_number = kind == KHDAYS_PARAMETER_INTEGER || kind == KHDAYS_PARAMETER_NUMBER
                                ? (q->min <= q->max && isfinite(q->min) ? q->min : 0)
                                : 0;
        q->default_text = kind == KHDAYS_PARAMETER_TEXT ? copy("") : NULL;
    } else if ((bad = understood(q, r->default_value, &q->default_number, &text)) != NULL) {
        tell(report, context, r->line, "[%s]: its default: %s", q->name, bad);
        if (strcmp(bad, "outside its bounds") == 0) {
            q->default_number = q->default_number < q->min ? q->min : q->max;
        } else {
            q->default_number = q->min <= q->max && isfinite(q->min) ? q->min : 0;
        }
    } else {
        q->default_text = text;
    }
    q->number = q->default_number;
    q->text = kind == KHDAYS_PARAMETER_TEXT ? copy(q->default_text != NULL ? q->default_text : "") : NULL;
    {
        KhdaysParameter *grown = realloc(out->list, (size_t)(out->count + 1) * sizeof(KhdaysParameter));
        if (grown != NULL) {
            out->list = grown;
            out->list[out->count++] = *q;
        } else {
            free_parameter(q);
        }
    }
    free_reading(r);
}

static void read_key(Reading *r, const char *key, const char *value, int line, KhdaysParametersReport report,
                     void *context)
{
    char **slot = strcmp(key, "type") == 0      ? &r->kind
                  : strcmp(key, "default") == 0 ? &r->default_value
                  : strcmp(key, "min") == 0     ? &r->min
                  : strcmp(key, "max") == 0     ? &r->max
                  : strcmp(key, "choices") == 0 ? &r->choices
                  : strcmp(key, "label") == 0   ? &r->q.label
                  : strcmp(key, "help") == 0    ? &r->q.help
                                                : NULL;
    for (int i = 0; slot == NULL && i < KHDAYS_PARAMETER_LANGUAGES; ++i) {
        char with[32];
        snprintf(with, sizeof(with), "label %s", languages[i]);
        if (strcmp(key, with) == 0) {
            slot = &r->q.labels[i];
        }
        snprintf(with, sizeof(with), "help %s", languages[i]);
        if (strcmp(key, with) == 0) {
            slot = &r->q.helps[i];
        }
    }
    if (slot == NULL) {
        tell(report, context, line, "[%s]: %s is not a key of a parameter", r->q.name, key);
        return;
    }
    free(*slot);
    *slot = copy(value);
}

static int good_name(const char *s)
{
    if (*s == '\0') {
        return 0;
    }
    for (; *s != '\0'; ++s) {
        if (!isalnum((unsigned char)*s) && *s != '_' && *s != '-' && *s != '.') {
            return 0;
        }
    }
    return 1;
}

/* Each line of `text` (its comment and spaces taken off, empty ones skipped) given to `line_fn`. */
static void each_line(const char *text, size_t size, void (*line_fn)(const char *s, size_t n, int line, void *state),
                      void *state)
{
    size_t at = 0;
    int line = 0;
    while (at < size) {
        size_t end = at;
        size_t n;
        const char *s;
        while (end < size && text[end] != '\n') {
            ++end;
        }
        ++line;
        n = without_comment(text + at, end - at);
        s = trimmed(text + at, &n);
        if (n > 0) {
            line_fn(s, n, line, state);
        }
        at = end + 1;
    }
}

typedef struct DescriptionState {
    KhdaysParameters *out;
    Reading r;
    KhdaysParametersReport report;
    void *context;
} DescriptionState;

static void description_line(const char *s, size_t n, int line, void *state)
{
    DescriptionState *d = state;
    if (s[0] == '[') {
        char *name;
        finish(d->out, &d->r, d->report, d->context);
        memset(&d->r, 0, sizeof(d->r));
        if (s[n - 1] != ']') {
            tell(d->report, d->context, line, "a part's name without its ]%s%s", "", "");
            return;
        }
        name = copy_span(s + 1, n - 2);
        if (name == NULL || !good_name(name) || khdays_parameters_find(d->out, name) != NULL) {
            tell(d->report, d->context, line, "[%s]: %s", name != NULL ? name : "",
                 "not a name (letters, digits, _ - .) or a name used before");
            free(name);
            return;
        }
        d->r.q.name = name;
        d->r.line = line;
        return;
    }
    {
        const char *equals = memchr(s, '=', n);
        size_t kn, vn;
        const char *k, *v;
        char *key, *value;
        if (equals == NULL || d->r.q.name == NULL) {
            tell(d->report, d->context, line, "%s%s",
                 equals == NULL ? "not a key = value line" : "a key before any part", "");
            return;
        }
        kn = (size_t)(equals - s);
        vn = n - kn - 1;
        k = trimmed(s, &kn);
        v = trimmed(equals + 1, &vn);
        key = copy_span(k, kn);
        value = copy_span(v, vn);
        if (key != NULL && value != NULL) {
            read_key(&d->r, key, value, line, d->report, d->context);
        }
        free(key);
        free(value);
    }
}

int khdays_parameters_read(KhdaysParameters *out, const char *text, size_t size, KhdaysParametersReport report,
                           void *context)
{
    DescriptionState d;
    khdays_parameters_free(out);
    memset(&d, 0, sizeof(d));
    d.out = out;
    d.report = report;
    d.context = context;
    each_line(text, size, description_line, &d);
    finish(out, &d.r, report, context);
    return out->count;
}

typedef struct ValuesState {
    KhdaysParameters *p;
    KhdaysParametersReport report;
    void *context;
} ValuesState;

static void values_line(const char *s, size_t n, int line, void *state)
{
    ValuesState *v = state;
    const char *equals = memchr(s, '=', n);
    size_t kn, vn;
    const char *k, *x;
    char *name, *value, why[120];
    if (equals == NULL) {
        tell(v->report, v->context, line, "not a name = value line%s%s", "", "");
        return;
    }
    kn = (size_t)(equals - s);
    vn = n - kn - 1;
    k = trimmed(s, &kn);
    x = trimmed(equals + 1, &vn);
    name = copy_span(k, kn);
    value = copy_span(x, vn);
    if (name != NULL && value != NULL && khdays_parameters_set(v->p, name, value, why, sizeof(why)) != NULL) {
        tell(v->report, v->context, line, "%s: %s", name, why);
    }
    free(name);
    free(value);
}

void khdays_parameters_values(KhdaysParameters *p, const char *text, size_t size, KhdaysParametersReport report,
                              void *context)
{
    ValuesState v = {p, report, context};
    each_line(text, size, values_line, &v);
}

const KhdaysParameter *khdays_parameters_find(const KhdaysParameters *p, const char *name)
{
    for (int i = 0; p != NULL && name != NULL && i < p->count; ++i) {
        if (strcmp(p->list[i].name, name) == 0) {
            return &p->list[i];
        }
    }
    return NULL;
}

const char *khdays_parameters_set(KhdaysParameters *p, const char *name, const char *value, char *why, size_t why_size)
{
    KhdaysParameter *q = (KhdaysParameter *)khdays_parameters_find(p, name);
    double number = 0;
    char *text = NULL;
    const char *bad;
    if (q == NULL) {
        snprintf(why, why_size, "no such parameter");
        return why;
    }
    if ((bad = understood(q, value, &number, &text)) != NULL) {
        snprintf(why, why_size, "%s", bad);
        return why;
    }
    if (q->kind == KHDAYS_PARAMETER_TEXT) {
        free(q->text);
        q->text = text;
    } else {
        q->number = number;
    }
    return NULL;
}

void khdays_parameters_value_text(const KhdaysParameter *q, char *out, size_t size)
{
    switch (q->kind) {
    case KHDAYS_PARAMETER_INTEGER: snprintf(out, size, "%.0f", q->number); break;
    case KHDAYS_PARAMETER_NUMBER: snprintf(out, size, "%.9g", q->number); break;
    case KHDAYS_PARAMETER_FLAG: snprintf(out, size, "%s", q->number != 0 ? "yes" : "no"); break;
    case KHDAYS_PARAMETER_CHOICE: {
        const int i = (int)q->number;
        snprintf(out, size, "%s", i >= 0 && i < q->choice_count ? q->choices[i] : "");
        break;
    }
    default: snprintf(out, size, "%s", q->text != NULL ? q->text : ""); break;
    }
}

char *khdays_parameters_write_values(const KhdaysParameters *p)
{
    static const char head[] = "# the values chosen for the plugin's parameters (KH Studio writes them)\n";
    size_t size = sizeof(head), used;
    char *out;
    for (int i = 0; i < p->count; ++i) {
        size += strlen(p->list[i].name) + (p->list[i].text != NULL ? strlen(p->list[i].text) : 0) + 64;
        for (int c = 0; c < p->list[i].choice_count; ++c) {
            size += strlen(p->list[i].choices[c]);
        }
    }
    out = malloc(size);
    if (out == NULL) {
        return NULL;
    }
    used = (size_t)snprintf(out, size, "%s", head);
    for (int i = 0; i < p->count && used < size; ++i) {
        char value[64];
        const KhdaysParameter *q = &p->list[i];
        if (q->kind == KHDAYS_PARAMETER_TEXT || q->kind == KHDAYS_PARAMETER_CHOICE) {
            used += (size_t)snprintf(out + used, size - used, "%s = ", q->name);
            khdays_parameters_value_text(q, out + used, size - used);
            used += strlen(out + used);
            used += (size_t)snprintf(out + used, size - used, "\n");
        } else {
            khdays_parameters_value_text(q, value, sizeof(value));
            used += (size_t)snprintf(out + used, size - used, "%s = %s\n", q->name, value);
        }
    }
    return out;
}

void khdays_parameters_values_name(const char *descriptor, char *out, size_t size)
{
    const size_t n = strlen(descriptor);
    if (n >= 4 && strcmp(descriptor + n - 4, ".ini") == 0) {
        snprintf(out, size, "%.*s.values.ini", (int)(n - 4), descriptor);
    } else {
        snprintf(out, size, "%s.values.ini", descriptor);
    }
}
