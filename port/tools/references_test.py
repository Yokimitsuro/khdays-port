"""Unit tests of the rewrite of the game's C (port/tools/references): a C fragment through the type
pass and the emission, compared with the C expected (both normalised by the parser), one case per
form. python port/tools/references_test.py (also ctest: khdays-references)
"""

from __future__ import annotations

import re
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from pycparser import c_generator, c_parser  # noqa: E402

from references import rewrite_text  # noqa: E402
from references.emit import Index  # noqa: E402

PRELUDE = """typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned char u8;
"""
LITERAL = (
    "static const char khdays_image_f_lit0[] = {value};"
    '__declspec(allocate(".khdi$m")) struct khdays_data_init khdays_place_f_lit0 = '
    "{{KHDAYS_DATA(f_lit0), &khdays_image_f_lit0, sizeof(khdays_image_f_lit0), -4}};"
    '_Static_assert(sizeof(khdays_image_f_lit0) == KHDAYS_DATA_SIZE_f_lit0, "f_lit0");'
)
# the names the rewritten C uses, for the parser that normalises it
NORMALISING = """typedef unsigned int khdays_ref;
typedef int khdays_function_pointer;
"""


def without_directives(c_text: str) -> str:
    """The text without its comments and preprocessor lines (#line, #include)."""
    text = re.sub(r"/\*.*?\*/", " ", c_text, flags=re.S)
    text = re.sub(r"__declspec\(allocate\(\"[^\"]*\"\)\)", "", text)
    return "\n".join(line for line in text.splitlines() if not line.lstrip().startswith("#"))


def normalised(c_text: str) -> str:
    text = without_directives(c_text)
    # a type as a macro's argument, which the parser cannot read: as a string
    text = re.sub(r"KHDAYS_OFFSETOF\(([^,()]+),", lambda m: f'KHDAYS_OFFSETOF("{m[1].strip()}",', text)
    ast = c_parser.CParser().parse(NORMALISING + text, filename="<case>")
    return c_generator.CGenerator().visit(ast)


class Rewrite(unittest.TestCase):
    maxDiff = None

    def check(self, source: str, expected: str, index: Index | None = None, routes: bool = False) -> None:
        got = rewrite_text(PRELUDE + source, "game.c", index, routes=routes)
        self.assertEqual(normalised(got), normalised(PRELUDE + expected))

    def test_declarations(self):
        self.check(
            "struct S { int *a; char b; }; int **f(char *c); void g(void) { int *p; struct S s; }",
            "struct S { khdays_ref a; char b; }; khdays_ref f(khdays_ref c);"
            " void g(void) { khdays_ref p; struct S s; }",
        )

    def test_dereference(self):
        self.check("int f(int *p) { return *p; }", "int f(khdays_ref p) { return (*(int *)KHDAYS_W(p)); }")

    def test_member(self):
        self.check(
            "struct S { int x; int *q; }; int f(struct S *s) { return *s->q + s->x; }",
            "struct S { int x; khdays_ref q; };"
            "int f(khdays_ref s) { return (*(int *)KHDAYS_W(((struct S *)KHDAYS_W(s))->q)) +"
            " ((struct S *)KHDAYS_W(s))->x; }",
        )

    def test_subscript(self):
        self.check(
            "int f(short *p, int i) { return p[i]; }",
            "int f(khdays_ref p, int i) { return (*(short *)KHDAYS_W((khdays_ref)(p + (i) * sizeof(short)))); }",
        )

    def test_local_array(self):
        self.check("int f(int i) { int a[4]; return a[i]; }", "int f(int i) { int a[4]; return a[i]; }")

    def test_arithmetic(self):
        self.check(
            "int *g(int *p) { return p + 2; } int *h(int *p) { return p - 1; }",
            "khdays_ref g(khdays_ref p) { return (khdays_ref)(p + (2) * sizeof(int)); }"
            "khdays_ref h(khdays_ref p) { return (khdays_ref)(p - (1) * sizeof(int)); }",
        )

    def test_difference(self):
        self.check(
            "int d(int *a, int *b) { return a - b; }",
            "int d(khdays_ref a, khdays_ref b) { return (int)(a - b) / (int)sizeof(int); }",
        )

    def test_casts(self):
        self.check(
            "unsigned f(int *p) { return (unsigned)p; } int *g(unsigned x) { return (int *)x; }",
            "unsigned f(khdays_ref p) { return (unsigned int)(p); }"
            "khdays_ref g(unsigned x) { return (khdays_ref)(x); }",
        )

    def test_address_of_local(self):
        self.check(
            "void h(int *x); void f(void) { int x; h(&x); }",
            "void h(khdays_ref x); void f(void) { int x; h(KHDAYS_REF(&x)); }",
        )

    def test_ds_object(self):
        self.check(
            "extern int data_02001000; int f(void) { return data_02001000; } int *g(void) { return &data_02001000; }",
            "int f(void) { return (*(int *)KHDAYS_W(0x02001000u)); } khdays_ref g(void) { return 0x02001000u; }",
            Index(ds_objects={"data_02001000": 0x02001000}),
        )

    def test_member_address(self):
        self.check(
            "struct S { int a; int b; }; int *f(struct S *s) { return &s->b; }",
            "struct S { int a; int b; };"
            " khdays_ref f(khdays_ref s) { return (khdays_ref)(s + KHDAYS_OFFSETOF(struct S, b)); }",
        )

    def test_string_literal(self):
        self.check(
            'const char *f(void) { return "ab"; }',
            "khdays_ref f(void) { return KHDAYS_DATA(f_lit0); }" + LITERAL.format(value='"ab"'),
        )

    def test_char_array_initializer(self):
        self.check(
            'void f(void) { char s[] = "abc"; char *t[] = {"x"}; }',
            'void f(void) { char s[] = "abc"; khdays_ref t[] = {KHDAYS_DATA(f_lit0)}; }' + LITERAL.format(value='"x"'),
        )

    def test_function_reference(self):
        self.check(
            "void cb(void); void f(void) { void (*fp)(void) = cb; void (*gp)(void) = &cb; }",
            "void cb(void); void f(void) { khdays_ref fp = 0x02003001u; khdays_ref gp = 0x02003001u; }",
            Index(functions={"cb": 0x02003001}),
        )

    def test_data_area(self):
        self.check(
            "static int count = 3; int *table[2]; int f(void) { static int n; return count + n; }",
            "static int khdays_image_f_count = 3;"
            ' __declspec(allocate(".khdi$m")) struct khdays_data_init khdays_place_f_count ='
            " {KHDAYS_DATA(f_count), &khdays_image_f_count, sizeof(khdays_image_f_count), -4};"
            ' _Static_assert(sizeof(int) == KHDAYS_DATA_SIZE_f_count, "f_count");'
            ' _Static_assert(sizeof(khdays_ref [2]) == KHDAYS_DATA_SIZE_table, "table");'
            " int f(void) {"
            ' _Static_assert(sizeof(int) == KHDAYS_DATA_SIZE_f_l0_n, "f_l0_n");'
            " return (*(int *)KHDAYS_W(KHDAYS_DATA(f_count))) + (*(int *)KHDAYS_W(KHDAYS_DATA(f_l0_n))); }",
        )

    def test_data_init_record(self):
        self.check(
            "struct khdays_data_init { unsigned int address; const void *init; unsigned int size; int module; };"
            " extern int data_02001000; int data_02001000__khdays_init = 5;"
            " struct khdays_data_init khdays_init_data_02001000 ="
            " { (unsigned int)&data_02001000, (const void *)&data_02001000__khdays_init,"
            " sizeof(data_02001000__khdays_init), -1 };",
            "struct khdays_data_init { unsigned int address; khdays_ref init; unsigned int size; int module; };"
            " int data_02001000__khdays_init = 5;"
            ' __declspec(allocate(".khdi$m")) struct khdays_data_init khdays_init_data_02001000 ='
            " {0x02001000u, (const void *)&data_02001000__khdays_init, sizeof(data_02001000__khdays_init), -1};",
            Index(ds_objects={"data_02001000": 0x02001000}),
        )

    def test_call_through_pointer(self):
        self.check(
            "void f(void (*fp)(int)) { fp(3); (*fp)(4); }",
            "void f(khdays_ref fp) { ((void (*)(int))KHDAYS_FN(fp))(3); ((void (*)(int))KHDAYS_FN(fp))(4); }",
        )

    def test_increment(self):
        self.check(
            "void f(int *p) { p++; ++p; }",
            "void f(khdays_ref p) { (khdays_ref)((p += sizeof(int)) - sizeof(int)); p += sizeof(int); }",
        )

    def test_compound_assignment(self):
        self.check("void f(int *p, int n) { p += n; }", "void f(khdays_ref p, int n) { p += (n) * sizeof(int); }")

    def test_sizeof(self):
        self.check(
            "unsigned f(void) { int a[4]; int *p; return sizeof(a) + sizeof(p) + sizeof(*p); }",
            "unsigned f(void) { int a[4]; khdays_ref p;"
            " return sizeof(a) + sizeof(p) + sizeof((*(int *)KHDAYS_W(p))); }",
        )

    def test_host_function(self):
        source = (
            '#line 1 "Z:/port/port/runtime/x.h"\nvoid *khdays_get(char *s);\n'
            '#line 10 "game.c"\nint *f(char *s) { return khdays_get(s); }'
        )
        got = rewrite_text(PRELUDE + source, "game.c")
        self.assertIn("void *khdays_get(char *s)", got)
        self.assertEqual(
            re.sub(r"\s+", "", without_directives(got).split("f(")[-1]),
            re.sub(r"\s+", "", "khdays_ref s)\n{\n  return KHDAYS_REF(khdays_get(khdays_host(s)));\n}\n"),
        )

    def test_static_function_is_the_games(self):
        # the index names the external functions only: a static one, even
        # defined without `static` after a static declaration, is this file's
        index = Index(game_functions={"f"})
        self.check(
            "static void g(int *p); void f(int *p) { g(p); } void g(int *p) { *p = 1; }",
            "static void g(khdays_ref p); void f(khdays_ref p) { g(p); }"
            " void g(khdays_ref p) { (*(int *)KHDAYS_W(p)) = 1; }",
            index,
        )

    def test_function_declared_as_object(self):
        # `extern int FSi_ReadFileCommand;` and its address in a table: the
        # function's reference
        index = Index(functions={"cmd": 0x02001000}, game_objects=set(), game_functions=set())
        self.check(
            "extern int cmd; void f(void) { void *p = &cmd; }",
            "extern int cmd; void f(void) { khdays_ref p = 0x02001000u; }",
            index,
        )

    def test_type_defined_by_a_window_object_declaration(self):
        index = Index(ds_objects={"r": 0x02000100}, game_objects=set(), game_functions={"f"})
        self.check(
            "extern const struct Ring { int v[2]; } r; int f(void) { struct Ring x = r; return x.v[0]; }",
            "struct Ring { int v[2]; }; int f(void) {"
            " struct Ring x = (*(const struct Ring *)KHDAYS_W(0x02000100u)); return x.v[0]; }",
            index,
        )

    def test_array_parameter(self):
        # adjusted to a pointer (OS_GetLowEntropyData's `u32 buffer[8]`), so a
        # reference -- written so or through a typedef
        self.check(
            "typedef int V[3]; void f(u32 b[8], V v) { b[1] = 2; v[2] = 1; }",
            "typedef int V[3]; void f(khdays_ref b, khdays_ref v) {"
            " (*(unsigned int *)KHDAYS_W((khdays_ref)(b + (1) * sizeof(unsigned int)))) = 2;"
            " (*(int *)KHDAYS_W((khdays_ref)(v + (2) * sizeof(int)))) = 1; }",
        )

    def test_dispatched_call(self):
        # a thunk's or the ROM's code: through its DS address, as a function
        # reference is called
        index = Index(game_functions={"f", "g"}, dispatched={"g": 0x020846C0})
        self.check(
            "int g(int *p); int f(int *p) { return g(p); }",
            "int g(khdays_ref p); int f(khdays_ref p) { return ((int (*)(khdays_ref))KHDAYS_FN(0x020846c0u))(p); }",
            index,
        )

    def test_variable_arguments(self):
        # the DS's layout, whatever the host's convention: the arguments in a
        # block of 4-byte slots, the callee reaching the next past the last
        # named one's address
        index = Index(game_functions={"f", "h"})
        self.check(
            "int f(int a, ...) { return *(&a + 1); } int h(int *p) { return f(1, p, (char)3); }",
            "int f(khdays_ref khdays_va) {"
            " return (*(int *)KHDAYS_W((khdays_ref)(((khdays_ref)(khdays_va + 0u)) + (1) * sizeof(int)))); }"
            " int h(khdays_ref p) { khdays_ref khdays_va_0[3];"
            " return ((*(int *)((unsigned char *)khdays_va_0 + 0)) = (int)(1),"
            " (*(khdays_ref *)((unsigned char *)khdays_va_0 + 4)) = (khdays_ref)(p),"
            " (*(int *)((unsigned char *)khdays_va_0 + 8)) = (int)((char)(3)),"
            " f(KHDAYS_REF(khdays_va_0))); }",
            index,
        )

    def test_routes(self):
        # a routine a native plugin may replace (kh.engine replace): its body renamed, a pointer to it, its name
        # calling through the pointer, a record of the three; a static one and a variadic one as they were
        self.check(
            "int f(int a, int *p) { return a + *p; } static int s(int a) { return a; } void v(void) { }"
            " int w(int a, ...) { return a; }",
            "int f(int a, khdays_ref p); int khdays_impl_f(int a, khdays_ref p) { return a + (*(int *)KHDAYS_W(p)); }"
            " int (*khdays_route_f)(int a, khdays_ref p) = khdays_impl_f;"
            " int f(int a, khdays_ref p) { return khdays_route_f(a, p); }"
            ' const struct khdays_route_record khdays_route_record_f = {"f", (void **)&khdays_route_f,'
            " (void *)khdays_impl_f};"
            " static int s(int a) { return a; }"
            " void v(void); void khdays_impl_v(void) { } void (*khdays_route_v)(void) = khdays_impl_v;"
            " void v(void) { khdays_route_v(); }"
            ' const struct khdays_route_record khdays_route_record_v = {"v", (void **)&khdays_route_v,'
            " (void *)khdays_impl_v};"
            " int w(khdays_ref khdays_va) { return (*(int *)KHDAYS_W(khdays_va + 0u)); }",
            routes=True,
        )

    def test_pointer_comparison(self):
        self.check(
            "int f(int *p, int *q) { return p < q && p != 0; }",
            "int f(khdays_ref p, khdays_ref q) { return p < q && p != 0; }",
        )


class Program(unittest.TestCase):
    """What the build decides from every file's declarations (build.py)."""

    @staticmethod
    def found(shapes: dict[str, list]) -> list:
        return [("a.c", [], [], shapes, "")]

    def test_block_for_a_struct_read_as_words(self):
        from references.build import block_abi

        found = self.found(
            {
                "f": [
                    ["def", [["w", 1], ["w", 1], ["w", 1]], False],
                    ["decl", [["s", 2], ["w", 1]], False],
                    ["decl", None, False],
                ]
            }
        )  # one without a prototype
        self.assertEqual(block_abi(found, {"f"}), {"f"})

    def test_hidden_result_on_one_side(self):
        # Sora's mark point: defined returning a 12-byte struct, declared
        # `void (void *out, int)` -- the ARM's result pointer spelled out
        from references.build import hidden_result_mismatches

        found = self.found(
            {
                "g": [["def", [["w", 1]], True], ["decl", [["w", 1], ["w", 1]], False]],
                "h": [["def", [["w", 1]], True], ["decl", [["w", 1]], True]],
            }
        )
        self.assertEqual(hidden_result_mismatches(found, {"g", "h"}), ["g (defined with one)"])


if __name__ == "__main__":
    unittest.main(verbosity=1)
