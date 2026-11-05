"""prepare.py's text passes on C written here, without the decomp."""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import prepare  # noqa: E402


def defined(text: str) -> list[str]:
    return [d[2] for d in prepare.definitions(text)]


class ObjectTypes(unittest.TestCase):
    def test_unnamed_struct_object_is_a_definition(self):
        text = "struct {\n    short a;\n    void (*step)(void);\n} data_x = {\n    1, 0,\n};\n"
        self.assertEqual(defined(text), [])
        named = prepare.name_object_types(text)
        self.assertEqual(defined(named), ["data_x"])
        self.assertIn("typedef struct {\n    short a;\n    void (*step)(void);\n} khdays_type_data_x;\n", named)
        self.assertIn("khdays_type_data_x data_x = {\n    1, 0,\n};", named)

    def test_qualifiers_tag_and_dimensions_kept(self):
        named = prepare.name_object_types("const struct Row { int a; } data_rows[16] = { 0 };\n")
        self.assertEqual(defined(named), ["data_rows"])
        self.assertIn("typedef struct Row { int a; } khdays_type_data_rows;", named)
        self.assertIn("const khdays_type_data_rows data_rows[16] = { 0 };", named)

    def test_several_in_one_file_and_bss(self):
        text = "struct { int a; } data_a = { 1 };\nunion { int a; char b; } data_b;\nint data_c = 3;\n"
        self.assertEqual(defined(prepare.name_object_types(text)), ["data_a", "data_b", "data_c"])

    def test_left_alone(self):
        for text in (
            "struct Row { int a; };\n",  # a type, no object
            "typedef struct { int a; } Row;\n",
            "static struct { int a; } s_local = { 1 };\n",  # not a DS object
            "struct { int a; } data_a, data_b;\n",  # two declarators
            "void f(void) {\nstruct { int a; } local = { 1 };\n}\n",  # in a function
        ):
            self.assertEqual(prepare.name_object_types(text), text)


if __name__ == "__main__":
    unittest.main()
