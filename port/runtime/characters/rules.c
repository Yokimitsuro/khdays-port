/* The rule of a character's form (rules.h). */
#include "rules.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum Kind { AND, OR, NOT, DUAL, GEAR, LINKS, PANEL, LEVEL, HP, PLUGIN };
enum Op { GE, LE, GT, LT, EQ, NE };

typedef struct Node {
    enum Kind kind;
    int a, b; /* AND, OR: the two sides; NOT: a */
    int item; /* GEAR, LINKS (0: any), PANEL */
    enum Op op;
    long n;
    char *name; /* PLUGIN */
} Node;

struct KhdaysRule {
    Node *nodes;
    int count, root;
};

/* The parser's place in the text and its first error. */
typedef struct Parser {
    const char *text, *at;
    KhdaysRule *rule;
    char *why;
    size_t why_size;
    int failed;
} Parser;

static void fail(Parser *p, const char *what)
{
    if (!p->failed) {
        snprintf(p->why, p->why_size, "%s at character %d", what, (int)(p->at - p->text) + 1);
        p->failed = 1;
    }
}

static int add(Parser *p, Node n)
{
    Node *grown = realloc(p->rule->nodes, (size_t)(p->rule->count + 1) * sizeof(*grown));
    if (grown == NULL) {
        free(n.name);
        fail(p, "out of memory");
        return -1;
    }
    p->rule->nodes = grown;
    grown[p->rule->count] = n;
    return p->rule->count++;
}

static void spaces(Parser *p)
{
    while (isspace((unsigned char)*p->at)) {
        ++p->at;
    }
}

/* The next word (letters, digits, '_', '-', '.'), copied into `out`; 0 when there is none. */
static int word(Parser *p, char *out, size_t size)
{
    size_t n = 0;
    spaces(p);
    while (isalnum((unsigned char)p->at[n]) || p->at[n] == '_' || p->at[n] == '-' || p->at[n] == '.') {
        ++n;
    }
    if (n == 0 || n >= size) {
        return 0;
    }
    memcpy(out, p->at, n);
    out[n] = '\0';
    return 1;
}

/* Takes the word `w` when it is the next one. */
static int take(Parser *p, const char *w)
{
    char got[32];
    if (word(p, got, sizeof(got)) && strcmp(got, w) == 0) {
        spaces(p);
        p->at += strlen(w);
        return 1;
    }
    return 0;
}

static int number(Parser *p, long *out)
{
    char *end;
    spaces(p);
    if (!isdigit((unsigned char)*p->at)) {
        return 0;
    }
    *out = strtol(p->at, &end, 10);
    p->at = end;
    return 1;
}

static int comparison(Parser *p, enum Op *out)
{
    static const struct {
        const char *text;
        enum Op op;
    } ops[] = {{">=", GE}, {"<=", LE}, {"==", EQ}, {"!=", NE}, {">", GT}, {"<", LT}};
    spaces(p);
    for (size_t i = 0; i < sizeof(ops) / sizeof(ops[0]); ++i) {
        const size_t n = strlen(ops[i].text);
        if (strncmp(p->at, ops[i].text, n) == 0) {
            p->at += n;
            *out = ops[i].op;
            return 1;
        }
    }
    return 0;
}

/* `<op> <n>`, required. */
static int compared(Parser *p, Node *n)
{
    if (!comparison(p, &n->op)) {
        fail(p, "a comparison (>=, <=, >, <, ==, !=) expected");
        return 0;
    }
    if (!number(p, &n->n)) {
        fail(p, "a number expected");
        return 0;
    }
    return 1;
}

static int expression(Parser *p);

/* The condition named `w` (already taken) with its operands, into `n`; 0 on a mistake (told). */
static int condition(Parser *p, const char *w, Node *n)
{
    long item;
    if (strcmp(w, "dual") == 0) {
        n->kind = DUAL;
    } else if (strcmp(w, "gear") == 0) {
        n->kind = GEAR;
        if (!number(p, &item)) {
            fail(p, "gear: the gear's item number expected");
            return 0;
        }
        n->item = (int)item;
    } else if (strcmp(w, "links") == 0) {
        n->kind = LINKS;
        if (number(p, &item)) {
            n->item = (int)item;
        }
        return compared(p, n);
    } else if (strcmp(w, "panel") == 0) {
        n->kind = PANEL;
        if (!number(p, &item)) {
            fail(p, "panel: the panel's item number expected");
            return 0;
        }
        n->item = (int)item;
        n->op = GE;
        n->n = 1;
        const char *before = p->at;
        enum Op op;
        if (comparison(p, &op)) {
            p->at = before;
            return compared(p, n);
        }
    } else if (strcmp(w, "level") == 0 || strcmp(w, "hp") == 0) {
        n->kind = strcmp(w, "level") == 0 ? LEVEL : HP;
        return compared(p, n);
    } else if (strcmp(w, "plugin") == 0) {
        char name[128];
        n->kind = PLUGIN;
        if (!word(p, name, sizeof(name))) {
            fail(p, "plugin: the condition's name expected");
            return 0;
        }
        p->at += strlen(name);
        n->name = malloc(strlen(name) + 1);
        if (n->name == NULL) {
            fail(p, "out of memory");
            return 0;
        }
        memcpy(n->name, name, strlen(name) + 1);
    } else {
        p->at -= strlen(w);
        fail(p, "not a condition (dual, gear, links, panel, level, hp, plugin or not)");
        return 0;
    }
    return 1;
}

static int atom(Parser *p)
{
    char w[64];
    Node n;
    memset(&n, 0, sizeof(n));
    spaces(p);
    if (*p->at == '(') {
        ++p->at;
        const int inside = expression(p);
        spaces(p);
        if (*p->at != ')') {
            fail(p, "')' expected");
            return -1;
        }
        ++p->at;
        return inside;
    }
    if (!word(p, w, sizeof(w))) {
        fail(p, *p->at == '\0' ? "a condition expected (the rule ends too soon)" : "a condition expected");
        return -1;
    }
    p->at += strlen(w);
    if (strcmp(w, "not") == 0) {
        const int a = atom(p);
        if (a < 0) {
            return -1;
        }
        n.kind = NOT;
        n.a = a;
        return add(p, n);
    }
    return condition(p, w, &n) ? add(p, n) : -1;
}

static int conjunction(Parser *p)
{
    int left = atom(p);
    while (left >= 0 && take(p, "and")) {
        const int right = atom(p);
        Node n = {AND, left, right, 0, GE, 0, NULL};
        left = right < 0 ? -1 : add(p, n);
    }
    return left;
}

static int expression(Parser *p)
{
    int left = conjunction(p);
    while (left >= 0 && take(p, "or")) {
        const int right = conjunction(p);
        Node n = {OR, left, right, 0, GE, 0, NULL};
        left = right < 0 ? -1 : add(p, n);
    }
    return left;
}

KhdaysRule *khdays_rule_parse(const char *text, char *why, size_t why_size)
{
    KhdaysRule *rule = calloc(1, sizeof(*rule));
    Parser p = {text, text, rule, why, why_size, 0};
    if (rule == NULL) {
        snprintf(why, why_size, "out of memory");
        return NULL;
    }
    rule->root = expression(&p);
    spaces(&p);
    if (!p.failed && *p.at != '\0') {
        fail(&p, "and, or or the rule's end expected");
    }
    if (p.failed || rule->root < 0) {
        khdays_rule_free(rule);
        return NULL;
    }
    return rule;
}

static int compare(long value, enum Op op, long n)
{
    switch (op) {
    case GE: return value >= n;
    case LE: return value <= n;
    case GT: return value > n;
    case LT: return value < n;
    case EQ: return value == n;
    default: return value != n;
    }
}

static long counted(const unsigned short *items, int count, int item)
{
    long n = 0;
    for (int i = 0; i < count; ++i) {
        n += item == 0 || items[i] == item;
    }
    return n;
}

static int holds(const KhdaysRule *r, int at, const KhdaysFormFacts *f)
{
    const Node *n = &r->nodes[at];
    switch (n->kind) {
    case AND: return holds(r, n->a, f) && holds(r, n->b, f);
    case OR: return holds(r, n->a, f) || holds(r, n->b, f);
    case NOT: return !holds(r, n->a, f);
    case DUAL: return f->dual != 0;
    case GEAR: return f->gear != 0 && f->gear == n->item;
    case LINKS: return compare(counted(f->gear_links, f->gear_link_count, n->item), n->op, n->n);
    case PANEL: return compare(counted(f->panels, f->panel_count, n->item), n->op, n->n);
    case LEVEL: return f->level >= 0 && compare(f->level, n->op, n->n);
    case HP: return f->hp_percent >= 0 && compare(f->hp_percent, n->op, n->n);
    default: return f->plugin != NULL && f->plugin(n->name, f->seat, f->number, f->plugin_user) != 0;
    }
}

int khdays_rule_holds(const KhdaysRule *rule, const KhdaysFormFacts *facts)
{
    return rule != NULL && rule->root >= 0 && holds(rule, rule->root, facts);
}

int khdays_rule_in_field(const KhdaysRule *rule)
{
    for (int i = 0; rule != NULL && i < rule->count; ++i) {
        if (rule->nodes[i].kind == HP || rule->nodes[i].kind == PLUGIN) {
            return 1;
        }
    }
    return 0;
}

void khdays_rule_items(const KhdaysRule *rule, void (*each)(int item, void *context), void *context)
{
    for (int i = 0; rule != NULL && i < rule->count; ++i) {
        const Node *n = &rule->nodes[i];
        if ((n->kind == GEAR || n->kind == PANEL || n->kind == LINKS) && n->item != 0) {
            each(n->item, context);
        }
    }
}

void khdays_rule_plugins(const KhdaysRule *rule, void (*each)(const char *name, void *context), void *context)
{
    for (int i = 0; rule != NULL && i < rule->count; ++i) {
        if (rule->nodes[i].kind == PLUGIN) {
            each(rule->nodes[i].name, context);
        }
    }
}

void khdays_rule_free(KhdaysRule *rule)
{
    if (rule == NULL) {
        return;
    }
#pragma warning(push)
#pragma warning(disable : 6001) /* each node up to count was written whole by add(), its name NULL or its own */
    for (int i = 0; i < rule->count; ++i) {
        free(rule->nodes[i].name);
    }
#pragma warning(pop)
    free(rule->nodes);
    free(rule);
}
