from __future__ import annotations

import sys
import unittest
from pathlib import Path


TOOLS = Path(__file__).resolve().parents[1] / "native" / "tools"
sys.path.insert(0, str(TOOLS))
import io_access  # noqa: E402
import io_pointers  # noqa: E402


HEADERS = {
    "REG_DIVCNT": ("vu16", "0x04000280"),
    "REG_IE": ("vu32", "0x04000210"),
}


def rewrite(text: str) -> str:
    return io_access.rewrite(text, HEADERS)


class NamedAndLiteralAccessTests(unittest.TestCase):
    def test_macro_store_and_read(self) -> None:
        text = "void f(void) {\n    REG_DIVCNT = 2;\n    while (REG_DIVCNT & 0x8000) {\n    }\n}\n"
        self.assertEqual(
            rewrite(text),
            "void f(void) {\n    KHDAYS_IO_SET(vu16, 0x04000280, 2);\n"
            "    while (KHDAYS_IO_GET(vu16, 0x04000280) & 0x8000) {\n    }\n}\n",
        )

    def test_literal_store(self) -> None:
        text = "void f(u32 op) {\n    *(volatile u32 *)0x04000400 = op;\n}\n"
        self.assertEqual(
            rewrite(text), "void f(u32 op) {\n    KHDAYS_IO_SET(volatile u32, 0x04000400, op);\n}\n"
        )

    def test_compound_assignment_reads_then_stores(self) -> None:
        text = "void f(u32 m) {\n    REG_IE |= m;\n}\n"
        self.assertEqual(
            rewrite(text),
            "void f(u32 m) {\n    KHDAYS_IO_SET(vu32, 0x04000210, KHDAYS_IO_GET(vu32, 0x04000210) | (m));\n}\n",
        )

    def test_bitwise_and_is_not_an_address(self) -> None:
        text = "u32 f(u32 y) {\n    return y & REG_IE;\n}\n"
        self.assertEqual(rewrite(text), "u32 f(u32 y) {\n    return y & KHDAYS_IO_GET(vu32, 0x04000210);\n}\n")

    def test_file_local_macro(self) -> None:
        text = "#define REG_SQRTCNT (*(volatile u16 *)0x040002b0)\nvoid f(void) {\n    REG_SQRTCNT = 1;\n}\n"
        self.assertEqual(
            rewrite(text),
            "#define REG_SQRTCNT (*(volatile u16 *)0x040002b0)\n"
            "void f(void) {\n    KHDAYS_IO_SET(volatile u16, 0x040002b0, 1);\n}\n",
        )

    def test_forms_left_to_the_trap(self) -> None:
        text = (
            "#define BG_PRIORITY(reg, p) ((reg) = (u16)(((reg) & ~3) | (p)))\n"
            "void f(void) {\n    g(&REG_IE);\n    REG_IE++;\n    h(sizeof(REG_IE));\n"
            "    BG_PRIORITY(REG_DIVCNT, 1);\n}\n"
        )
        self.assertEqual(rewrite(text), text)

    def test_address_outside_the_io_region(self) -> None:
        text = "void f(void) {\n    *(volatile u32 *)0x02000000 = 1;\n}\n"
        self.assertEqual(rewrite(text), text)

    def test_header_name_the_file_declares_itself(self) -> None:
        text = (
            "static volatile unsigned short *const REG_DIVCNT = (volatile unsigned short *)0x04000280;\n"
            "void f(void) {\n    while ((*REG_DIVCNT & 0x8000) != 0) {\n    }\n}\n"
        )
        self.assertEqual(rewrite(text), text)


class PointerVariableTests(unittest.TestCase):
    def test_inline_interrupt_disable(self) -> None:
        text = (
            "unsigned f(void) {\n"
            "    volatile unsigned short *ime = (volatile unsigned short *)0x04000208;\n"
            "    unsigned old = *ime;\n    *ime = 0;\n    (void)*ime;\n    return old;\n}\n"
        )
        self.assertEqual(
            io_pointers.rewrite(text),
            "unsigned f(void) {\n"
            "    volatile unsigned short *ime = (volatile unsigned short *)0x04000208;\n"
            "    unsigned old = KHDAYS_IO_GET(volatile unsigned short, (unsigned int)(ime));\n"
            "    KHDAYS_IO_SET(volatile unsigned short, (unsigned int)(ime), 0);\n"
            "    (void)KHDAYS_IO_GET(volatile unsigned short, (unsigned int)(ime));\n"
            "    return old;\n}\n",
        )

    def test_index_and_offset(self) -> None:
        text = (
            "void f(unsigned n, unsigned s) {\n"
            "    volatile unsigned int *p = (volatile unsigned int *)(0x040000B0 + n * 12);\n"
            "    p[1] = s;\n    *(p + 2) = 0x81400001;\n}\n"
        )
        self.assertEqual(
            io_pointers.rewrite(text),
            "void f(unsigned n, unsigned s) {\n"
            "    volatile unsigned int *p = (volatile unsigned int *)(0x040000B0 + n * 12);\n"
            "    KHDAYS_IO_SET(volatile unsigned int, (unsigned int)((p + (1))), s);\n"
            "    KHDAYS_IO_SET(volatile unsigned int, (unsigned int)((p + 2)), 0x81400001);\n}\n",
        )

    def test_cast_dereference_takes_the_cast_type(self) -> None:
        text = (
            "void f(int a) {\n    volatile unsigned *div = (volatile unsigned *)0x04000280;\n"
            "    *(volatile unsigned short *)div = 1;\n    div[5] = (unsigned)a;\n}\n"
        )
        self.assertEqual(
            io_pointers.rewrite(text),
            "void f(int a) {\n    volatile unsigned *div = (volatile unsigned *)0x04000280;\n"
            "    KHDAYS_IO_SET(volatile unsigned short, (unsigned int)(div), 1);\n"
            "    KHDAYS_IO_SET(volatile unsigned, (unsigned int)((div + (5))), (unsigned)a);\n}\n",
        )

    def test_register_block_member_stores_only(self) -> None:
        text = (
            "#define SQRT_REGISTERS ((SqrtRegisters *)0x040002b0)\n"
            "int f(long long v) {\n    SQRT_REGISTERS->control = 1;\n    SQRT_REGISTERS->parameter = v;\n"
            "    while (SQRT_REGISTERS->control & 0x8000) {\n    }\n    return 0;\n}\n"
        )
        self.assertEqual(
            io_pointers.rewrite(text),
            "#define SQRT_REGISTERS ((SqrtRegisters *)0x040002b0)\n"
            "int f(long long v) {\n    KHDAYS_IO_SET_LV(SQRT_REGISTERS->control, 1);\n"
            "    KHDAYS_IO_SET_LV(SQRT_REGISTERS->parameter, v);\n"
            "    while (SQRT_REGISTERS->control & 0x8000) {\n    }\n    return 0;\n}\n",
        )

    def test_a_shadowing_declaration_leaves_the_pointer_alone(self) -> None:
        text = (
            "void f(void) {\n    volatile u16 *p = (volatile u16 *)0x04000208;\n"
            "    {\n        u16 *p = buffer;\n        *p = 1;\n    }\n}\n"
        )
        self.assertEqual(io_pointers.rewrite(text), text)


if __name__ == "__main__":
    unittest.main()
