from __future__ import annotations

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "native" / "tools"))
import prepare  # noqa: E402


class NameAnonymousObjectsTest(unittest.TestCase):
    def names(self, text: str) -> list[str]:
        return [d[2] for d in prepare.definitions(prepare.name_anonymous_objects(text))]

    def test_unnamed_struct_object_is_found(self) -> None:
        text = "struct {\n    short a;\n    struct { int b; } inner[2];\n} data_x = {\n    0, {{1}, {2}},\n};\n"
        self.assertEqual(self.names(text), ["data_x"])

    def test_const_union_and_arrays(self) -> None:
        text = "const union { int i; float f; } data_y[2][3] = {0};\n"
        out = prepare.name_anonymous_objects(text)
        self.assertIn("typedef union { int i; float f; } data_y__khdays_type;", out)
        self.assertIn("const data_y__khdays_type data_y[2][3] = {0};", out)
        self.assertEqual(self.names(text), ["data_y"])

    def test_statics_locals_and_named_types_stay(self) -> None:
        text = ("static struct { int a; } s = {1};\n"
                "void f(void) {\nstruct { int b; } t = {2};\n}\n"
                "struct Named { int c; };\n")
        self.assertEqual(prepare.name_anonymous_objects(text), text)


if __name__ == "__main__":
    unittest.main()
