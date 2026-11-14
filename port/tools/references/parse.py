"""The parser of the game's C: pycparser's, with what mwcc accepts and ISO C17
does not -- a declaration whose specifiers name no type (`const n = 3;`) is
an int, as C89 has it and the decomp's C relies on; a declaration of a
typedef's name alone (`VecFx32;`) declares nothing (MSVC and mwcc warn)."""

from __future__ import annotations

import pycparser
from pycparser import c_ast, c_parser

# GameCParser overrides pycparser 3's own parser; an older one parses the C without them and fails much later
if int(pycparser.__version__.split(".")[0]) < 3:
    raise ImportError(
        f"pycparser {pycparser.__version__} is installed; the game's C needs pycparser 3 or newer: "
        'py -m pip install -U "pycparser>=3"'
    )


class GameCParser(c_parser.CParser):
    def _build_declarations(self, spec, decls, typedef_namespace=False):
        if (
            decls
            and decls[0].get("decl") is None
            and decls[0].get("bitsize") is None
            and len(decls) == 1
            and len(spec["type"]) == 1
            and isinstance(spec["type"][0], c_ast.IdentifierType)
            and len(spec["type"][0].names) == 1
            and self._is_type_in_scope(spec["type"][0].names[0])
            and "typedef" not in spec["storage"]
        ):
            return []
        return super()._build_declarations(spec, decls, typedef_namespace)

    def _parse_declaration_specifiers(self, allow_no_type: bool = False):
        return super()._parse_declaration_specifiers(allow_no_type=True)

    def _parse_unified_string_literal(self) -> c_ast.Node:
        # Adjacent literals kept apart in the value (`"\xbf" "A"`): pycparser
        # joins their text, and a hex escape then runs on into the next
        # literal's first character, another byte.
        tok = self._expect("STRING_LITERAL")
        node = c_ast.Constant("string", tok.value, self._tok_coord(tok))
        while self._peek_type() == "STRING_LITERAL":
            node.value += " " + self._advance().value
        return node

    def _fix_decl_name_type(self, decl, typename):
        if not typename:
            typ = decl
            while not isinstance(typ, c_ast.TypeDecl):
                typ = typ.type
            typename = [c_ast.IdentifierType(["int"], coord=decl.coord)]
        return super()._fix_decl_name_type(decl, typename)


def parse(text: str, filename: str) -> c_ast.FileAST:
    return GameCParser().parse(text, filename=filename)
