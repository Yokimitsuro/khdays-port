"""The rewrite of the game's C: every pointer a 4-byte reference, the DS address it had. preprocess (a
source as the build compiles it), parse (with what mwcc accepts), types (every expression's C type),
emit (the C again); build.py runs them over the game (docs/BUILD.md, "The references").
"""

from __future__ import annotations


def rewrite_text(text: str, filename: str, index=None, file_key: str = "f", routes: bool = False) -> str:
    """Preprocessed C text rewritten (the unit tests' entry; the build's is
    the same three steps)."""
    from .emit import Index, emit
    from .parse import parse
    from .types import TypePass

    ast = parse(text, filename)
    tp = TypePass().run(ast)
    out, _ = emit(ast, tp, index or Index(), file_key, routes)
    return out
