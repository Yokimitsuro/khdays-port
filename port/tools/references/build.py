"""The rewrite over the whole game's side, for prepare.py: what each file defines, then each file's
emission with that index and the decomp's symbols, across processes; then the data area and the
function references' numbers in khdays_data_area.h. A source's results are kept in gen/references/ and
redone only when its files, the index or the rewrite's code changed (docs/BUILD.md, "The tools").
"""

from __future__ import annotations

import hashlib
import json
import os
import re
import traceback
import zlib
from concurrent.futures import ProcessPoolExecutor
from dataclasses import dataclass, field
from pathlib import Path

# The window's areas above the DS's ranges (data-model.md): the register
# model's memory (64 KB for each I/O region), then the data area.
IO_VIEW = 0x0B100000
IO2_VIEW = 0x0B110000
DATA_AREA = 0x0B200000
FUNCTION_REFERENCES = 0xFE000000  # numbers only, never memory: 4 apart


@dataclass
class Program:
    absolute: dict[str, int]  # objects and symbols at DS addresses (prepare.py)
    functions: dict[str, int]  # functions reached by their DS address (Thumb's with bit 0)
    # the game's functions no C file defines: prepare.py's thunks (khdays_ovcall_*,
    # the overlay loaded at an address several share), called as the game calls
    game_functions: set[str] = field(default_factory=set)
    # what a direct call reaches through its DS address (references/emit.py Index)
    dispatched: dict[str, int] = field(default_factory=dict)


@dataclass
class Result:
    source: str
    output: str = ""
    defined: dict[str, tuple[int, int]] = field(default_factory=dict)
    used: set[str] = field(default_factory=set)
    frefs: set[str] = field(default_factory=set)
    port_objects: set[str] = field(default_factory=set)  # the port's objects the file reaches
    variadic_addresses: set[str] = field(default_factory=set)  # variadic-defined functions taken by address
    error: str = ""


_env = _cl = _flags = None
_cache: Path | None = None
_tools = ""
_flags_key = ""
_stats: dict[str, list[int] | None] = {}


def _init(cache: str, tools: str, arch: str):
    global _env, _cl, _flags, _cache, _tools, _flags_key
    import sys

    sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
    import build as native_build
    from .preprocess import Flags, compiler

    _env = native_build.msvc_env(arch)  # preprocessed as that compiler sees it
    _cl = compiler(_env)
    _flags = Flags.game_side()
    _flags_key = hashlib.sha1("\n".join(_flags.arguments()).encode()).hexdigest()
    _cache = Path(cache)
    _tools = tools


def tools_key() -> str:
    """The rewrite's own code: a change to it emits every file again."""
    h = hashlib.sha1()
    for p in sorted(Path(__file__).parent.glob("*.py")):
        h.update(p.name.encode() + p.read_bytes())
    return h.hexdigest()


ROOT = Path(__file__).resolve().parents[3]


def canonical(source: str) -> str:
    """A source's path the same however it was given: from the repository's
    root when it is under it."""
    path = Path(source).resolve()
    try:
        path = path.relative_to(ROOT)
    except ValueError:
        pass
    return path.as_posix().lower()


def file_key(source: str) -> str:
    """A short, stable name for a source's own data (its statics, literals)."""
    return "f" + hashlib.sha1(canonical(source).encode()).hexdigest()[:10]


def _stat(path: str) -> list[int] | None:
    if path not in _stats:
        try:
            st = os.stat(path)
            _stats[path] = [st.st_size, st.st_mtime_ns]
        except OSError:
            _stats[path] = None
    return _stats[path]


LINE_MARKER = re.compile(r'^#line \d+ "((?:[^"\\]|\\.)*)"', re.M)


def _sources_of(text: str) -> list[str]:
    """The files a preprocessed text came from, by its line markers."""
    return sorted({m[1].replace("\\\\", "\\") for m in LINE_MARKER.finditer(text)})


def _entry(source: str) -> tuple[Path, Path]:
    name = hashlib.sha1(canonical(source).encode()).hexdigest()
    return _cache / f"{name}.json", _cache / f"{name}.pp.z"


def _meta(source: str) -> dict | None:
    """The source's kept results, when every file its text came from is unchanged."""
    meta_path, pp_path = _entry(source)
    try:
        meta = json.loads(meta_path.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return None
    if meta.get("flags") != _flags_key or not pp_path.exists():
        return None
    for path, stamp in meta["files"].items():
        if _stat(path) != stamp:
            return None
    return meta


def _save(source: str, meta: dict) -> None:
    meta_path, _ = _entry(source)
    meta_path.write_text(json.dumps(meta), encoding="utf-8")


def _source_text(source: str, meta: dict | None) -> tuple[str, str]:
    """The source preprocessed -- as the preprocessor wrote it (kept so, a
    change of the dialect's mapping needs no new run of it) and in ISO C's
    dialect."""
    from .preprocess import iso_dialect, preprocess

    if meta is not None:
        raw = zlib.decompress(_entry(source)[1].read_bytes()).decode("utf-8")
    else:
        raw = preprocess(Path(source), _flags, _env, _cl, raw=True)
    return raw, iso_dialect(raw)


def _parse_text(source: str, text: str):
    from .parse import parse
    from .types import TypePass

    ast = parse(text, source)
    return ast, TypePass().run(ast)


def _hidden_result(layout, t) -> bool:
    """Whether a function returns a struct through a pointer its caller
    passes: on the ARM in r0, natively one the compiler adds (MSVC and the
    System V ABIs return structs of 1, 2, 4 or 8 bytes in registers -- x64
    System V up to 16, which this does not take for registers yet)."""
    from .types import Record

    return isinstance(t.result, Record) and layout.size(t.result) not in (1, 2, 4, 8)


def _shape(layout, t) -> list | None:
    """A function type's parameters as the DS's stack holds them: each one's
    kind and size (a struct by value, a floating value, a word or two), and
    `...`; None when unprototyped. Two declarations of one function whose
    shapes differ (a struct where the definition reads two ints) agree on the
    DS -- and on x86 -- word for word, not on x64 (emit.py's block)."""
    from .types import Arithmetic, Record, decayed

    if t.params is None:
        return None
    shape = []
    for p in t.params:
        p = decayed(p)
        kind = "s" if isinstance(p, Record) else "f" if isinstance(p, Arithmetic) and p.floating else "w"
        shape.append([kind, (layout.size(p) + 3) // 4])  # in the stack's 4-byte words
    return shape + [["...", 0]] if t.variadic else shape


def _definitions(source: str):
    from pycparser import c_ast
    from .layout import Layout
    from .types import Function

    meta = _meta(source)
    if meta is not None and meta.get("tools") == _tools and "functions" in meta:
        return source, meta["functions"], meta["objects"], meta.get("shapes", {}), ""
    try:
        raw, text = _source_text(source, meta)  # kept, when only the rewrite's code changed
        ast, tp = _parse_text(source, text)
    except Exception as e:  # noqa: BLE001
        return source, [], [], {}, f"{type(e).__name__}: {e}"
    functions, objects = [], []
    # every declaration's shape of a function with external linkage -- its
    # definition's first ("def"), then each prototype's ("decl"; None for one
    # without) -- and whether it returns a struct through a hidden pointer
    shapes: dict[str, list] = {}
    layout = Layout(tp)

    def note(name, symbol, defined):
        if symbol is None or "static" in symbol.storage or not isinstance(symbol.type, Function):
            return
        shapes.setdefault(name, []).append(
            ["def" if defined else "decl", _shape(layout, symbol.type), _hidden_result(layout, symbol.type)]
        )

    for ext in ast.ext:
        if isinstance(ext, c_ast.FuncDef):
            symbol = tp.declared[id(ext.decl)]
            if "static" not in symbol.storage:  # a static declaration's linkage kept
                functions.append(ext.decl.name)
            note(ext.decl.name, symbol, True)
        elif isinstance(ext, c_ast.Decl) and ext.name is not None and isinstance(ext.type, c_ast.FuncDecl):
            note(ext.name, tp.declared.get(id(ext)), False)
        elif isinstance(ext, c_ast.Decl) and ext.name is not None and not isinstance(ext.type, c_ast.FuncDecl):
            if "extern" not in ext.storage and "static" not in ext.storage and "typedef" not in ext.storage:
                objects.append(ext.name)
    files = {p: _stat(p) for p in _sources_of(text) + [source] + [str(f) for f in _flags.forced]}
    if all(stamp is not None for stamp in files.values()):
        _entry(source)[1].write_bytes(zlib.compress(raw.encode("utf-8"), 6))
        _save(
            source,
            {
                "flags": _flags_key,
                "tools": _tools,
                "files": files,
                "functions": functions,
                "objects": objects,
                "shapes": shapes,
            },
        )
    return source, functions, objects, shapes, ""


def _emit(job):
    source, output, index, index_key = job
    from .emit import emit

    r = Result(source)
    meta = _meta(source)
    done = meta.get("emitted") if meta is not None and meta.get("tools") == _tools else None
    if done is not None and done["index"] == index_key and done["output"] == output and _stat(output) == done["stamp"]:
        r.output = output
        r.defined = {k: tuple(v) for k, v in done["defined"].items()}
        r.used, r.frefs = set(done["used"]), set(done["frefs"])
        r.port_objects = set(done.get("port_objects", ()))
        r.variadic_addresses = set(done.get("variadic_addresses", ()))
        return r
    try:
        _, text = _source_text(source, meta)
        ast, tp = _parse_text(source, text)
        # the game's own routines (rewritten/src/: not the SDK's libraries, libs/, nor the port's HAL, native_hal/)
        # routed for the plugins
        tree = output.replace("\\", "/").rsplit("/rewritten/", 1)
        out_text, e = emit(ast, tp, index, file_key(source), routes=len(tree) == 2 and tree[1].startswith("src/"))
    except Exception as ex:  # noqa: BLE001
        r.error = f"{type(ex).__name__}: {ex}\n{traceback.format_exc()[-1500:]}"
        return r
    out = Path(output)
    out.parent.mkdir(parents=True, exist_ok=True)
    if not out.exists() or out.read_text(encoding="utf-8") != out_text:
        out.write_text(out_text, encoding="utf-8")
    _stats.pop(output, None)
    r.output = output
    r.defined = dict(e.data_defined)
    r.used = set(e.data_objects)
    r.frefs = set(e.frefs)
    r.port_objects = set(e.unplaced)
    r.variadic_addresses = set(e.variadic_addresses)
    if meta is not None:
        meta["emitted"] = {
            "index": index_key,
            "output": output,
            "stamp": _stat(output),
            "defined": r.defined,
            "used": sorted(r.used),
            "frefs": sorted(r.frefs),
            "port_objects": sorted(r.port_objects),
            "variadic_addresses": sorted(r.variadic_addresses),
        }
        _save(source, meta)
    return r


def block_abi(found, game_functions: set[str]) -> set[str]:
    """The game's functions whose arguments go in a block as the DS laid them
    out (emit.py): one declared or defined with `...` anywhere, or one a file
    declares otherwise than its definition takes (where both name the same
    parameter, a different kind or size)."""
    shapes: dict[str, list] = {}
    for _, _, _, file_shapes, _ in found:
        for name, entries in file_shapes.items():
            shapes.setdefault(name, []).extend(entries)
    block = set()
    for name, entries in shapes.items():
        if name not in game_functions:
            continue
        definition = next((s for kind, s, _ in entries if kind == "def"), None)
        if any(s is not None and ["...", 0] in s for _, s, _ in entries):
            block.add(name)
        elif definition is not None:
            for kind, s, _ in entries:
                n = min(len(s), len(definition)) if s is not None else 0
                if s is not None and s[:n] != definition[:n]:
                    block.add(name)
                    break
    return block


def hidden_result_mismatches(found, game_functions: set[str]) -> list[str]:
    """The game's functions some file declares returning a struct through a
    hidden pointer where the definition does not, or the other way: on the
    ARM both pass that pointer in r0; natively one side passes a host pointer
    the other does not expect (a manual fix, prepare.py FILE_HOOKS)."""
    entries: dict[str, list] = {}
    for _, _, _, file_shapes, _ in found:
        for name, es in file_shapes.items():
            entries.setdefault(name, []).extend(es)
    out = []
    for name, es in sorted(entries.items()):
        defined = next((h for kind, _, h in es if kind == "def"), None)
        if name in game_functions and defined is not None and any(h != defined for _, _, h in es):
            out.append(f"{name} (defined {'with' if defined else 'without'} one)")
    return out


def _index_key(index) -> str:
    h = hashlib.sha1()
    for part in (
        sorted(index.ds_objects.items()),
        sorted(index.game_objects or ()),
        sorted(index.functions.items()),
        sorted(index.game_functions or ()),
        sorted(index.dispatched.items()),
        sorted(index.variadic),
    ):
        h.update(repr(part).encode())
    return h.hexdigest()


def rewrite_all(
    jobs: list[tuple[str, str]], program: Program, gen: Path, processes: int | None = None, arch: str = "x64"
):
    """Each (source, output) rewritten; the numbers into gen/khdays_data_area.h.
    Returns the outputs in the order given and the data area's size; stops
    naming every file that failed."""
    from .emit import Index

    processes = processes or max(1, (os.cpu_count() or 2) - 1)
    cache = gen / "references"
    cache.mkdir(parents=True, exist_ok=True)
    with ProcessPoolExecutor(processes, initializer=_init, initargs=(str(cache), tools_key(), arch)) as pool:
        found = list(pool.map(_definitions, [s for s, _ in jobs], chunksize=32))
        failures = [f"{s}: {err}" for s, _, _, _, err in found if err]
        if failures:
            raise SystemExit("references: the game's side does not parse:\n" + "\n".join(failures[:50]))
        game_functions = {n for _, fs, _, _, _ in found for n in fs} | program.game_functions
        mismatched = hidden_result_mismatches(found, game_functions)
        if mismatched:
            raise SystemExit(
                "references: declarations that return a struct otherwise than their definition "
                "(through a hidden pointer on one side only): " + ", ".join(mismatched)
            )
        variadic = block_abi(found, game_functions)
        report = "".join(f"{n}\n" for n in sorted(variadic))
        target = gen / "references_block_abi.txt"
        if not target.exists() or target.read_text(encoding="utf-8") != report:
            target.write_text(report, encoding="utf-8")
        game_objects = {n for _, _, os_, _, _ in found for n in os_} - set(program.absolute)
        index = Index(
            ds_objects=dict(program.absolute),
            game_objects=game_objects,
            functions=dict(program.functions),
            game_functions=game_functions,
            dispatched=dict(program.dispatched),
            variadic=variadic,
        )
        key = _index_key(index)
        results = list(pool.map(_emit, [(s, o, index, key) for s, o in jobs], chunksize=16))
    failures = [f"{r.source}: {r.error}" for r in results if r.error]
    if failures:
        raise SystemExit("references: the rewrite stopped:\n" + "\n\n".join(failures[:20]))
    defined: dict[str, tuple[int, int]] = {}
    for r in results:
        for k, size in r.defined.items():
            if k in defined and defined[k] != size:
                raise SystemExit(f"references: {k} defined twice with different sizes ({defined[k]}, {size})")
            defined[k] = size
    used = set().union(*(r.used for r in results)) if results else set()
    missing = sorted(used - set(defined))
    if missing:
        raise SystemExit(
            "references: data-area objects the game reaches but no file defines: " + ", ".join(missing[:40])
        )
    header, size = data_area_header(defined, sorted(set().union(*(r.frefs for r in results)) if results else []))
    # What the game's side reaches of the port's own objects: each must be in
    # the window, or stops the game where the game takes its address.
    reached: dict[str, list[str]] = {}
    for r in results:
        for name in r.port_objects:
            reached.setdefault(name, []).append(r.source)
    report = "".join(f"{n}\t{len(f)}\t{f[0]}\n" for n, f in sorted(reached.items()))
    target = gen / "references_port_objects.txt"
    if not target.exists() or target.read_text(encoding="utf-8") != report:
        target.write_text(report, encoding="utf-8")
    # A variadic-defined function taken by address: a call through a pointer
    # whose type has no `...` would pass its arguments the host's way.
    taken: dict[str, list[str]] = {}
    for r in results:
        for name in r.variadic_addresses:
            taken.setdefault(name, []).append(r.source)
    report = "".join(f"{n}\t{len(f)}\t{f[0]}\n" for n, f in sorted(taken.items()))
    target = gen / "references_variadic_addresses.txt"
    if not target.exists() or target.read_text(encoding="utf-8") != report:
        target.write_text(report, encoding="utf-8")
    target = gen / "khdays_data_area.h"
    if not target.exists() or target.read_text(encoding="utf-8") != header:
        target.write_text(header, encoding="utf-8")
    return [r.output for r in results], size


def data_area_header(defined: dict[str, tuple[int, int]], frefs: list[str]) -> tuple[str, int]:
    lines = [
        "/* Generated by port/tools/references: the data area's objects",
        " * and the function references with no DS address, numbered. */",
        "#ifndef KHDAYS_DATA_AREA_H",
        "#define KHDAYS_DATA_AREA_H",
        "",
    ]
    at = DATA_AREA
    for k in sorted(defined):
        size, align = defined[k]
        align = max(align, 1)
        at = (at + align - 1) // align * align
        lines.append(f"#define KHDAYS_DATA_{k} 0x{at:08x}u")
        lines.append(f"#define KHDAYS_DATA_SIZE_{k} {size}")
        at += size
    lines.append("")
    for i, k in enumerate(frefs):
        lines.append(f"#define KHDAYS_FREF_{k} 0x{FUNCTION_REFERENCES + 4 * i:08x}u")
    end = (at + 0xFFF) // 0x1000 * 0x1000
    lines += [
        "",
        f"#define KHDAYS_IO_VIEW 0x{IO_VIEW:08x}u",
        f"#define KHDAYS_IO2_VIEW 0x{IO2_VIEW:08x}u",
        f"#define KHDAYS_DATA_AREA 0x{DATA_AREA:08x}u",
        f"#define KHDAYS_DATA_AREA_END 0x{end:08x}u",
        f"#define KHDAYS_FUNCTION_REFERENCES 0x{FUNCTION_REFERENCES:08x}u",
        f"#define KHDAYS_FUNCTION_REFERENCE_COUNT {len(frefs)}u",
        "",
        "#endif",
        "",
    ]
    return "\n".join(lines), end - DATA_AREA
