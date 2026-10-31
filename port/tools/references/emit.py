"""The emission: a typed translation unit (types.py) as C again, every pointer of the game's side a
4-byte reference, each expression a value or a host lvalue (the window added only where a reference
is dereferenced). Variadic and mis-declared functions take their arguments in a block as the DS laid
them out; declarations from the port's own headers keep host pointers (docs/BUILD.md).
"""

from __future__ import annotations

import copy
import posixpath
from dataclasses import dataclass, field

from pycparser import c_ast, c_generator

from .types import (
    DOUBLE,
    INT,
    Array,
    Arithmetic,
    CType,
    Enumeration,
    Function,
    Pointer,
    Record,
    Symbol,
    TypeFailure,
    TypePass,
    Void,
    decayed,
)

REF = "khdays_ref"
PRELUDE = """/* Rewritten by port/tools/references: every pointer of the
 * game's side a 4-byte reference, the DS address it had. */
#include "khdays_references.h"
#pragma section(".khdi$m", read, write)
#pragma section(".khfr$m", read)
#pragma section(".khrt$m", read)
"""

# The module number of the data area's records (runtime/core/data_init.c): copied
# once, when the window is made; -1/-2/-3 are the static module, the ITCM and
# the DTCM, an overlay its id (prepare.py module_id).
DATA_AREA_MODULE = -4
IMAGE_SUFFIX = "__khdays_init"  # prepare.py's initializer images of DS-addressed objects

HOST_DIRECTORIES = ("port/runtime/", "port/host/", "port/gfx/", "port/sound/")
HOST_FILES = ("port/compat/decomp_compat.h",)


def walk(node):
    """Every node under `node`, itself first."""
    yield node
    for _, child in node.children():
        yield from walk(child)


def normalised_path(path: str) -> str:
    """A line marker's path with forward slashes and no `..` (the HAL's
    `hal/../runtime/core/io.h` is the runtime's header)."""
    return posixpath.normpath(path.replace("\\\\", "/").replace("\\", "/"))


def is_host_file(path: str | None) -> bool:
    if not path:
        return False
    p = normalised_path(path)
    return any(d in p for d in HOST_DIRECTORIES) or any(p.endswith(f) for f in HOST_FILES)


@dataclass
class Index:
    """What a file's emission needs to know of the whole program."""

    ds_objects: dict[str, int] = field(default_factory=dict)  # game objects at DS addresses
    game_objects: set[str] | None = None  # external objects the game's side defines (None: unknown)
    functions: dict[str, int] = field(default_factory=dict)  # function references known as numbers
    game_functions: set[str] | None = None  # defined on the game's side (None: all but host)
    # functions a direct call reaches through their DS address, as the game's
    # function references (prepare.py's thunks: the overlay loaded there; the
    # ROM's code with no native form: a stop) -- no symbol at a DS address
    dispatched: dict[str, int] = field(default_factory=dict)
    # the game's functions that take their arguments in a block (the docstring)
    variadic: set[str] = field(default_factory=set)


class EmitFailure(TypeFailure):
    pass


class Emitter(c_generator.CGenerator):
    def __init__(self, tp: TypePass, index: Index, file_key: str = "f", routes: bool = False):
        super().__init__()
        self.tp = tp
        self.index = index
        self.file_key = file_key
        self.routes = routes  # the game's own routines: each called through a pointer a plugin may change (routed)
        self.literals: list[str] = []  # the file's string literals used as pointers, by number
        self.unplaced: set[str] = set()  # objects it took for the port's (no game definition known)
        self.data_objects: dict[str, tuple[int, int]] = {}  # the data area's objects: key -> (size, align)
        self.frefs: dict[str, str] = {}  # function references with no DS address (numbered by the build): key -> name
        self.data_defined: dict[str, tuple[int, int]] = {}  # the data-area objects this file defines
        self.static_keys: dict[int, str] = {}  # a static local's symbol -> its key
        from .layout import Layout

        self.layout = Layout(tp)
        self.plain = c_generator.CGenerator()  # for the port's declarations, as written
        self.line = None
        self.markers = True  # #line before each statement and member
        self.defined_here: set[str] = set()  # the functions the file defines (visit_FileAST)
        # a variable-argument function of the game's being emitted: its
        # parameters' offsets in its block (id of the parameter's Decl)
        self.va_params: dict[int, int] = {}
        # its calls to such functions: (block name, the arguments' slots)
        self.va_calls: dict[int, tuple[str, list[tuple[int, CType]]]] = {}
        self.variadic_addresses: set[str] = set()  # variadic-defined functions whose address it takes

    # --- what a node is ---

    def type(self, node) -> CType:
        t = self.tp.types.get(id(node))
        if t is None:
            raise EmitFailure(node, f"no type for {type(node).__name__}")
        return t

    def symbol(self, node) -> Symbol | None:
        return self.tp.symbols.get(id(node))

    def host_symbol(self, s: Symbol | None) -> bool:
        if s is None or s.kind == "enumerator":
            return False
        if s.kind == "object":
            return self.place(s)[0] == "host"
        coord = getattr(s.node, "coord", None)
        if is_host_file(getattr(coord, "file", None)):
            return True
        if s.kind == "function" and ("static" in s.storage or s.name in self.defined_here):
            return False  # defined in this file (the index holds the external ones)
        if s.kind == "function" and self.index.game_functions is not None:
            return s.name not in self.index.game_functions
        return False

    def place(self, s: Symbol, use: bool = True) -> tuple[str, object]:
        """Where an object lives: ('ds', address), ('data', key) -- the build's
        data area --, ('local', None) -- an automatic object, on the stack --,
        ('image', None) -- prepare.py's initializer image, the port's --, or
        ('host', None) -- an object of the port's side."""
        name = s.name
        if name in self.index.ds_objects:
            return "ds", self.index.ds_objects[name]
        if name.endswith(IMAGE_SUFFIX):
            return "image", None
        if not s.file_scope:
            if "static" in s.storage:
                key = self.static_keys.get(id(s))
                if key is None:
                    key = f"{self.file_key}_l{len(self.static_keys)}_{name}"
                    self.static_keys[id(s)] = key
                return "data", key
            if "extern" not in s.storage:
                return "local", None
        if "static" in s.storage:
            return "data", f"{self.file_key}_{name}"
        if is_host_file(getattr(getattr(s.node, "coord", None), "file", None)):
            return "host", None
        if self.index.game_objects is not None:
            if name in self.index.game_objects:
                return "data", name
            if name in self.index.functions or name in (self.index.game_functions or ()):
                # a function the C declares as an object to take its address
                # (`extern int FSi_ReadFileCommand;`): what it names is code
                return "function", name
            if use:  # a use, not its declaration
                self.unplaced.add(name)
            return "host", None
        # no index: a file-scope object is the game's when this file defines it
        return ("host", None) if "extern" in s.storage else ("data", name)

    def is_function_designator(self, node) -> bool:
        s = self.symbol(node) if isinstance(node, c_ast.ID) else None
        return s is not None and isinstance(s.type, Function)

    # --- types as C ---

    def decl(self, t: CType, inner: str = "", game=True) -> str:
        """`t` declaring `inner` (an abstract declarator when empty); pointers
        as references on the game's side."""
        quals = " ".join(q for q in getattr(t, "quals", ()) if q in ("const", "volatile"))
        q = quals + " " if quals else ""
        sep = " " if inner and not inner.startswith(("[", "(")) else ""
        if isinstance(t, Pointer):
            if game:
                return f"{q}{REF}{sep}{inner}".rstrip()
            return self.decl(t.target, f"*{q}{inner}".rstrip(), game)
        if isinstance(t, Array):
            dim = self.visit(t.length) if t.length is not None else ""
            target = f"({inner})" if inner.startswith("*") else inner
            return self.decl(t.element, f"{target}[{dim}]", game)
        if isinstance(t, Function):
            target = f"({inner})" if inner.startswith("*") else inner
            if t.params is None:
                params = ""
            elif game and t.variadic:
                params = REF  # the block of its arguments (the docstring)
            elif not t.params:
                params = "void"
            else:
                params = ", ".join(self.decl(p, "", game) for p in t.params) + (", ..." if t.variadic else "")
            return self.decl(t.result, f"{target}({params})", game)
        if isinstance(t, Arithmetic):
            return f"{q}{t.spelling}{sep}{inner}".rstrip()
        if isinstance(t, Void):
            return f"{q}void{sep}{inner}".rstrip()
        if isinstance(t, Record):
            spelling = t.definition.spelling
            if spelling is None:
                if t.definition.node is None:
                    raise EmitFailure(None, "a struct with no name and no definition to spell it by")
                # an anonymous struct, written out again -- with no line
                # markers, which may not stand in a macro's arguments
                saved, self.markers = self.markers, False
                try:
                    spelling = self.visit(t.definition.node)
                finally:
                    self.markers = saved
            return f"{q}{spelling}{sep}{inner}".rstrip()
        if isinstance(t, Enumeration):
            return f"{q}{t.spelling or 'int'}{sep}{inner}".rstrip()
        raise EmitFailure(None, f"a type not spelt: {type(t).__name__}")

    def pointer_to(self, target: CType) -> str:
        """The host pointer type to `target` (its own pointers references)."""
        return self.decl(target, "*")

    def size_of(self, target: CType) -> str:
        if isinstance(target, (Void, Function)):
            return "1"
        return f"sizeof({self.decl(target)})"

    # --- values and lvalues ---

    def rv(self, node) -> str:
        """The node's value: a pointer as its reference."""
        method = getattr(self, "rv_" + type(node).__name__, None)
        if method is None:
            raise EmitFailure(node, f"an expression not emitted: {type(node).__name__}")
        return method(node)

    def lv(self, node) -> str:
        """The node as a host lvalue."""
        if isinstance(node, c_ast.ID):
            return self.lv_ID(node)
        if isinstance(node, c_ast.UnaryOp) and node.op == "*":
            t = self.type(node)
            return f"(*({self.pointer_to(t)})KHDAYS_W({self.rv(node.expr)}))"
        if isinstance(node, c_ast.StructRef):
            if node.type == "->":
                base_t = decayed(self.type(node.name))
                return f"(({self.pointer_to(base_t.target)})KHDAYS_W({self.rv(node.name)}))->{node.field.name}"
            return (
                f"{self.lv(node.name)}.{node.field.name}"
                if self.is_lvalue(node.name)
                else f"({self.rv(node.name)}).{node.field.name}"
            )
        if isinstance(node, c_ast.ArrayRef):
            a, b = node.name, node.subscript
            ta, tb = self.type(a), self.type(b)
            if isinstance(tb, (Array, Pointer)) and not isinstance(ta, (Array, Pointer)):
                a, b, ta = b, a, tb
            if isinstance(ta, Array) and self.is_lvalue(a):
                return f"{self.lv(a)}[{self.rv(b)}]"
            target = decayed(ta).target
            return (
                f"(*({self.pointer_to(target)})KHDAYS_W(({REF})({self.rv(a)} + ({self.rv(b)}) * "
                f"{self.size_of(target)})))"
            )
        if isinstance(node, c_ast.CompoundLiteral):
            return self.rv_CompoundLiteral(node)
        if isinstance(node, c_ast.Constant) and node.type == "string":
            return node.value
        if isinstance(node, c_ast.Cast):
            return f"({self.rv(node)})"
        raise EmitFailure(node, f"not an lvalue: {type(node).__name__}")

    def is_lvalue(self, node) -> bool:
        return (
            isinstance(node, (c_ast.ID, c_ast.StructRef, c_ast.ArrayRef, c_ast.CompoundLiteral))
            or (isinstance(node, c_ast.UnaryOp) and node.op == "*")
            or (isinstance(node, c_ast.Constant) and node.type == "string")
        )

    def lv_ID(self, node: c_ast.ID) -> str:
        s = self.symbol(node)
        name = node.name
        if s is None or s.kind != "object":
            return name
        if id(s.node) in self.va_params:
            return f"(*({self.pointer_to(s.type)})KHDAYS_W(khdays_va + {self.va_params[id(s.node)]}u))"
        where, what = self.place(s)
        if where == "ds":
            return f"(*({self.pointer_to(s.type)})KHDAYS_W(0x{what:08x}u))"
        if where == "data":
            self.data_object(s, what)
            return f"(*({self.pointer_to(s.type)})KHDAYS_W(KHDAYS_DATA({what})))"
        if where == "function":
            raise EmitFailure(node, f"{name}, a function's code, read as an object")
        return name

    def data_object(self, s: Symbol, key: str) -> None:
        """A data-area object this file reaches: its DS layout's size and
        alignment, for the build to place it (checked by _Static_assert)."""
        if key not in self.data_objects:
            t = s.type
            if isinstance(t, Array) and t.length is None:
                return  # an array declared without its length: its definition gives it
            self.data_objects[key] = (self.layout.size(t), self.layout.measure(t)[1])

    def address(self, node) -> str | None:
        """The reference of an lvalue as reference arithmetic, when it can be
        written without going through a host address (a constant for an
        object whose place the build knows); None otherwise."""
        if isinstance(node, c_ast.ID):
            s = self.symbol(node)
            if s is None or s.kind != "object":
                return None
            if id(s.node) in self.va_params:
                return f"(({REF})(khdays_va + {self.va_params[id(s.node)]}u))"
            where, what = self.place(s)
            if where == "ds":
                return f"0x{what:08x}u"
            if where == "data":
                self.data_object(s, what)
                return f"KHDAYS_DATA({what})"
            if where == "function":
                return self.named_function_reference(what, s, "&")
            return None
        if isinstance(node, c_ast.UnaryOp) and node.op == "*":
            return self.rv(node.expr)
        if isinstance(node, c_ast.StructRef):
            record = decayed(self.type(node.name)).target if node.type == "->" else self.type(node.name)
            if not isinstance(record, Record) or record.definition.spelling is None:
                return None  # an anonymous struct's member: through its host address
            if node.type == "->":
                return f"(({REF})({self.rv(node.name)} + KHDAYS_OFFSETOF({self.decl(record)}, {node.field.name})))"
            base = self.address(node.name) if self.is_lvalue(node.name) else None
            if base is None:
                return None
            return f"(({REF})({base} + KHDAYS_OFFSETOF({self.decl(record)}, {node.field.name})))"
        if isinstance(node, c_ast.ArrayRef):
            a, b = node.name, node.subscript
            ta, tb = self.type(a), self.type(b)
            if isinstance(tb, (Array, Pointer)) and not isinstance(ta, (Array, Pointer)):
                a, b, ta = b, a, tb
            target = decayed(ta).target
            base = self.rv(a) if isinstance(ta, Pointer) else self.address(a)
            if base is None:
                return None
            return f"(({REF})({base} + ({self.rv(b)}) * {self.size_of(target)}))"
        if isinstance(node, c_ast.Constant) and node.type == "string":
            return self.literal(node)
        return None

    def reference(self, node) -> str:
        """`&node`: the reference of an lvalue."""
        a = self.address(node)
        return a if a is not None else f"KHDAYS_REF(&{self.lv(node)})"

    def literal(self, node: c_ast.Constant) -> str:
        from .layout import literal_bytes

        key = f"{self.file_key}_lit{len(self.literals)}"
        self.literals.append(node.value)
        self.data_objects[key] = self.data_defined[key] = (literal_bytes(node.value), 1)
        return f"KHDAYS_DATA({key})"

    def literal_images(self) -> str:
        """The file's string literals' images and records (emitted at its end)."""
        out = []
        for n, value in enumerate(self.literals):
            key = f"{self.file_key}_lit{n}"
            out.append(f"static const char khdays_image_{key}[] = {value};\n")
            out.append(self.record(key, f"khdays_image_{key}") + ";\n")
            out.append(f'_Static_assert(sizeof(khdays_image_{key}) == KHDAYS_DATA_SIZE_{key}, "{key}");\n')
        return "".join(out)

    @staticmethod
    def record(key: str, image: str) -> str:
        return (
            f'__declspec(allocate(".khdi$m")) struct khdays_data_init khdays_place_{key} = '
            f"{{KHDAYS_DATA({key}), &{image}, sizeof({image}), {DATA_AREA_MODULE}}}"
        )

    def decayed_value(self, node, t: CType) -> str:
        """An array or function lvalue used as a value."""
        if isinstance(t, Function):
            return self.function_reference(node)
        return self.reference(node)

    def function_reference(self, node) -> str:
        name = node.name if isinstance(node, c_ast.ID) else None
        if name is None:
            raise EmitFailure(node, "a function value that is not a function's name")
        return self.named_function_reference(name, self.symbol(node))

    def named_function_reference(self, name: str, s: Symbol | None, native: str = "") -> str:
        if name in self.index.variadic:
            self.variadic_addresses.add(name)
        """`name`'s function reference: its DS address, or a number the build
        gives it (its record holding `native` + name: `&` where the C
        declares it as an object)."""
        if name in self.index.functions:
            return f"0x{self.index.functions[name]:08x}u"
        if name in self.index.ds_objects:
            # a symbol at a fixed address the C declares as a function: the
            # linker's (`extern void SDK_IRQ_STACKSIZE(void);`, 0x800)
            return f"0x{self.index.ds_objects[name]:08x}u"
        key = f"{self.file_key}_{name}" if s is not None and "static" in s.storage else name
        self.frefs[key] = native + name
        return f"KHDAYS_FREF({key})"

    # --- expressions, as values ---

    def rv_ID(self, node: c_ast.ID) -> str:
        s = self.symbol(node)
        if s is None or s.kind == "enumerator":
            return node.name
        t = s.type
        if isinstance(t, Function):
            return self.function_reference(node)
        if self.host_symbol(s):
            if isinstance(t, Pointer) and isinstance(t.target, Function):
                raise EmitFailure(node, "a function pointer of the port's side")
            if isinstance(t, (Pointer, Array)):
                return f"KHDAYS_REF({node.name})"
            return node.name
        if isinstance(t, Array):
            return self.reference(node)
        return self.lv_ID(node)

    def rv_Constant(self, node: c_ast.Constant) -> str:
        if node.type == "string":
            return self.literal(node)
        return node.value

    def rv_UnaryOp(self, node: c_ast.UnaryOp) -> str:
        op = node.op
        if op in ("sizeof", "_Alignof"):
            if isinstance(node.expr, c_ast.Typename):
                return f"{op}({self.decl(self.tp.type_of_declarator(node.expr))})"
            return f"{op}({self.undecayed(node.expr)})"
        if op == "&":
            if self.is_function_designator(node.expr):
                return self.function_reference(node.expr)
            if isinstance(node.expr, c_ast.UnaryOp) and node.expr.op == "*":
                return self.rv(node.expr.expr)
            return self.reference(node.expr)
        if op == "*":
            t = self.type(node)
            if isinstance(t, (Array, Function)):
                return self.rv(node.expr)  # an array or function lvalue decays back to the pointer
            return self.lv(node)
        t = decayed(self.type(node.expr))
        if op in ("++", "--", "p++", "p--"):
            target = self.lv(node.expr)
            if isinstance(t, Pointer):
                step = self.size_of(t.target)
                sign = "+" if "++" in op else "-"
                if op.startswith("p"):
                    back = "-" if sign == "+" else "+"
                    return f"(({REF})(({target} {sign}= {step}) {back} {step}))"
                return f"({target} {sign}= {step})"
            return f"({target}{op[1:]})" if op.startswith("p") else f"({op}{target})"
        return f"{op}({self.rv(node.expr)})"

    def undecayed(self, node) -> str:
        """The operand of sizeof: an lvalue as itself (an array keeps its size)."""
        if isinstance(node, c_ast.Constant) and node.type == "string":
            return node.value
        if self.is_lvalue(node):
            return self.lv(node)
        return self.rv(node)

    def rv_Cast(self, node: c_ast.Cast) -> str:
        target = self.tp.type_of_declarator(node.to_type)
        inner = self.value(node.expr)
        if isinstance(target, Pointer):
            return f"(({REF})({inner}))"
        if isinstance(target, Void):
            return f"((void)({inner}))"
        return f"(({self.decl(target)})({inner}))"

    def value(self, node) -> str:
        """The node's value, an array or a function decayed."""
        t = self.type(node)
        if isinstance(t, (Array, Function)) and not (isinstance(node, c_ast.UnaryOp) and node.op == "*"):
            if isinstance(node, c_ast.ID):
                return self.rv_ID(node)
            if isinstance(node, c_ast.Constant):
                return self.rv_Constant(node)
            return self.decayed_value(node, t)
        return self.rv(node)

    def rv_BinaryOp(self, node: c_ast.BinaryOp) -> str:
        op = node.op
        a, b = decayed(self.type(node.left)), decayed(self.type(node.right))
        left, right = self.value(node.left), self.value(node.right)
        if op in ("+", "-") and isinstance(a, Pointer) and not isinstance(b, Pointer):
            return f"(({REF})({left} {op} ({right}) * {self.size_of(a.target)}))"
        if op == "+" and isinstance(b, Pointer) and not isinstance(a, Pointer):
            return f"(({REF})({right} + ({left}) * {self.size_of(b.target)}))"
        if op == "-" and isinstance(a, Pointer) and isinstance(b, Pointer):
            return f"((int)({left} - {right}) / (int){self.size_of(a.target)})"
        return f"({left} {op} {right})"

    def rv_Assignment(self, node: c_ast.Assignment) -> str:
        t = self.type(node.lvalue)
        target = self.lv(node.lvalue)
        value = self.value(node.rvalue)
        host_target = isinstance(node.lvalue, c_ast.ID) and self.host_symbol(self.symbol(node.lvalue))
        if isinstance(t, Pointer) and node.op in ("+=", "-=") and not host_target:
            return f"({target} {node.op} ({value}) * {self.size_of(t.target)})"
        if host_target and isinstance(t, Pointer):
            if node.op != "=":
                raise EmitFailure(node, "a compound assignment to a port's pointer")
            return f"({target} = KHDAYS_W({value}))"
        return f"({target} {node.op} {value})"

    def rv_TernaryOp(self, node: c_ast.TernaryOp) -> str:
        return f"({self.value(node.cond)} ? {self.value(node.iftrue)} : {self.value(node.iffalse)})"

    def rv_ExprList(self, node: c_ast.ExprList) -> str:
        return "(" + ", ".join(self.value(e) for e in node.exprs) + ")"

    def rv_StructRef(self, node: c_ast.StructRef) -> str:
        t = self.type(node)
        if isinstance(t, Array):
            return self.reference(node)
        return self.lv(node)

    def rv_ArrayRef(self, node: c_ast.ArrayRef) -> str:
        t = self.type(node)
        if isinstance(t, Array):
            return self.reference(node)
        return self.lv(node)

    def rv_FuncCall(self, node: c_ast.FuncCall) -> str:
        callee = node.name
        args = node.args.exprs if node.args is not None else []
        if id(node) in self.va_calls:
            return self.va_call(node, callee, args)
        if (
            isinstance(callee, c_ast.ID)
            and self.is_function_designator(callee)
            and callee.name in self.index.dispatched
        ):
            f = self.symbol(callee).type
            fn = f"(({self.pointer_to(f)})KHDAYS_FN(0x{self.index.dispatched[callee.name]:08x}u))"
            return f"{fn}({', '.join(self.value(a) for a in args)})"
        if isinstance(callee, c_ast.ID) and self.is_function_designator(callee):
            s = self.symbol(callee)
            f = s.type
            if self.host_symbol(s):
                texts = []
                for i, a in enumerate(args):
                    v = self.value(a)
                    param = f.params[i] if f.params is not None and i < len(f.params) else decayed(self.type(a))
                    texts.append(self.to_host(v, param))
                call = f"{callee.name}({', '.join(texts)})"
                if isinstance(f.result, Pointer):
                    if isinstance(f.result.target, Function):
                        raise EmitFailure(node, "a function pointer from the port's side")
                    return f"KHDAYS_REF({call})"
                return call
            return f"{callee.name}({', '.join(self.value(a) for a in args)})"
        target = callee
        if isinstance(callee, c_ast.UnaryOp) and callee.op == "*" and isinstance(self.type(callee), Function):
            target = callee.expr  # `(*fp)(...)`: the function fp points at
        ft = decayed(self.type(target))
        if not (isinstance(ft, Pointer) and isinstance(ft.target, Function)):
            raise EmitFailure(node, "a call through a value that is not a function pointer")
        fn = f"(({self.pointer_to(ft.target)})KHDAYS_FN({self.value(target)}))"
        return f"{fn}({', '.join(self.value(a) for a in args)})"

    # --- variable arguments, as the DS laid them out (the docstring) ---

    def va_slot(self, t: CType) -> CType:
        """The type an argument is stored as: a named parameter's own, a
        variable one's promoted (C11 6.5.2.2p6-7)."""
        if isinstance(t, Arithmetic):
            if t.floating:
                return DOUBLE if t.spelling == "float" else t
            if t.spelling in ("_Bool", "char", "signed char", "unsigned char", "short", "unsigned short"):
                return INT
        if isinstance(t, Enumeration):
            return INT
        return decayed(t)

    def va_layout(self, types: list[CType]) -> tuple[list[int], int]:
        """Each argument's offset in the block (4-byte slots, as on the DS's
        stack and x86's), and the block's size in slots."""
        offsets, at = [], 0
        for t in types:
            offsets.append(at)
            at += (self.layout.size(t) + 3) // 4 * 4
        return offsets, max(1, at // 4)

    def callee_type(self, callee) -> Function | None:
        """The function a call reaches: its type, if it is the game's and
        takes a variable argument list."""
        if isinstance(callee, c_ast.ID) and self.is_function_designator(callee):
            s = self.symbol(callee)
            f = s.type
            if self.host_symbol(s):
                return None
            if callee.name in self.index.variadic:
                return f  # its arguments by the caller's own prototype, laid out as words
        else:
            target = (
                callee.expr
                if isinstance(callee, c_ast.UnaryOp) and callee.op == "*" and isinstance(self.type(callee), Function)
                else callee
            )
            t = decayed(self.type(target))
            if not (isinstance(t, Pointer) and isinstance(t.target, Function)):
                return None
            f = t.target
        return f if f.variadic and f.params is not None else None

    def callee_slots(self, f: Function, args) -> list[CType]:
        """The arguments' types as the block holds them: those the caller's
        prototype names converted to them, the rest promoted."""
        named = [decayed(p) for p in f.params] if f.params is not None else []
        return named[: len(args)] + [self.va_slot(self.type(a)) for a in args[len(named) :]]

    def plan_va_calls(self, body) -> list[str]:
        """The variable-argument calls in a function's body, each given its
        block: the blocks' declarations, for the body's start."""
        declarations = []
        for call in walk(body):
            if not isinstance(call, c_ast.FuncCall):
                continue
            f = self.callee_type(call.name)
            if f is None:
                continue
            args = call.args.exprs if call.args is not None else []
            types = self.callee_slots(f, args)
            offsets, words = self.va_layout(types)
            name = f"khdays_va_{len(self.va_calls)}"
            self.va_calls[id(call)] = (name, list(zip(offsets, types)))
            declarations.append(f"{REF} {name}[{words}];")
        return declarations

    def va_call(self, node, callee, args) -> str:
        """The call: its arguments stored in its block, then the block's
        reference passed."""
        name, slots = self.va_calls[id(node)]
        stores = []
        for a, (offset, t) in zip(args, slots):
            v = self.value(a)
            t = t.qualified(())  # stored as the slot's own object, not its const view
            cast = "" if isinstance(t, Record) else f"({self.decl(t)})"
            stores.append(f"(*({self.pointer_to(t)})((unsigned char *){name} + {offset})) = {cast}({v})")
        if isinstance(callee, c_ast.ID) and self.is_function_designator(callee):
            if callee.name in self.index.dispatched:
                f = self.symbol(callee).type
                block = Function(f.result, [Pointer(Void())], True)  # spelt (khdays_ref)
                fn = f"(({self.pointer_to(block)})KHDAYS_FN(0x{self.index.dispatched[callee.name]:08x}u))"
            else:
                fn = callee.name
        else:
            target = (
                callee.expr
                if isinstance(callee, c_ast.UnaryOp) and callee.op == "*" and isinstance(self.type(callee), Function)
                else callee
            )
            ft = decayed(self.type(target))
            fn = f"(({self.pointer_to(ft.target)})KHDAYS_FN({self.value(target)}))"
        return "(" + ", ".join(stores + [f"{fn}(KHDAYS_REF({name}))"]) + ")"

    def to_host(self, value: str, t: CType) -> str:
        """A value handed to the port's side as `t`: a pointer as its host
        address (0 as NULL), a function pointer as the native function."""
        if not isinstance(t, Pointer):
            return value
        if isinstance(t.target, Function):
            return f"(({self.decl(t, '', game=False)})khdays_host_function({value}))"
        return f"khdays_host({value})"

    def rv_CompoundLiteral(self, node: c_ast.CompoundLiteral) -> str:
        t = self.type(node)
        return f"(({self.decl(t)}){self.initializer(node.init, t)})"

    def rv_InitList(self, node: c_ast.InitList) -> str:
        return self.initializer(node, self.type(node))

    def rv_Typename(self, node: c_ast.Typename) -> str:
        return self.decl(self.tp.type_of_declarator(node))

    # --- initializers ---

    def initializer(self, init, t: CType | None = None) -> str:
        target = self.tp.init_targets.get(id(init), t)
        if isinstance(init, c_ast.InitList):
            parts = []
            for e in init.exprs:
                if isinstance(e, c_ast.NamedInitializer):
                    designators = "".join(
                        f".{d.name}" if isinstance(d, c_ast.ID) else f"[{self.rv(d)}]" for d in e.name
                    )
                    parts.append(f"{designators} = {self.initializer(e.expr)}")
                else:
                    parts.append(self.initializer(e))
            return "{" + ", ".join(parts) + "}"
        if isinstance(init, c_ast.Constant) and init.type == "string" and isinstance(target, Array):
            return init.value  # a char array's initializer, not a pointer
        return self.value(init)

    # --- the generator's expression entry points ---

    def visit_ID(self, n):
        return self.rv(n)

    def visit_Constant(self, n):
        return self.rv(n)

    def visit_UnaryOp(self, n):
        return self.rv(n)

    def visit_BinaryOp(self, n):
        return self.rv(n)

    def visit_Assignment(self, n):
        return self.rv(n)

    def visit_TernaryOp(self, n):
        return self.rv(n)

    def visit_Cast(self, n):
        return self.rv(n)

    def visit_FuncCall(self, n):
        return self.rv(n)

    def visit_StructRef(self, n):
        return self.rv(n)

    def visit_ArrayRef(self, n):
        return self.rv(n)

    def visit_ExprList(self, n):
        return ", ".join(self.value(e) for e in n.exprs) if n.exprs else ""

    def visit_CompoundLiteral(self, n):
        return self.rv(n)

    # --- declarations ---

    def game_declarators(self, node):
        """A copy of a declarator chain with every pointer a reference."""
        if isinstance(node, c_ast.PtrDecl):
            inner = node.type
            while not isinstance(inner, c_ast.TypeDecl) and hasattr(inner, "type"):
                inner = inner.type
            name = inner.declname if isinstance(inner, c_ast.TypeDecl) else None
            return c_ast.TypeDecl(name, list(node.quals), None, c_ast.IdentifierType([REF]), node.coord)
        if isinstance(node, c_ast.TypeDecl):
            new = copy.copy(node)
            if isinstance(node.type, (c_ast.Struct, c_ast.Union)) and node.type.decls is not None:
                new.type = self.game_record(node.type)
            return new
        if isinstance(node, c_ast.ArrayDecl):
            new = copy.copy(node)
            new.type = self.game_declarators(node.type)
            return new
        if isinstance(node, c_ast.FuncDecl):
            new = copy.copy(node)
            new.type = self.game_declarators(node.type)
            if node.args is not None:
                params = []
                if any(isinstance(p, c_ast.EllipsisParam) for p in node.args.params):
                    # a variable argument list: the block (the docstring)
                    new.args = c_ast.ParamList(
                        [
                            c_ast.Decl(
                                "khdays_va",
                                [],
                                [],
                                [],
                                [],
                                c_ast.TypeDecl("khdays_va", [], None, c_ast.IdentifierType([REF])),
                                None,
                                None,
                            )
                        ],
                        node.args.coord,
                    )
                    return new
                for p in node.args.params:
                    if isinstance(p, (c_ast.Decl, c_ast.Typename)):
                        q = copy.copy(p)
                        if self.adjusted_to_pointer(p.type):
                            # `u32 buffer[8]` is a pointer, so a reference
                            q.type = c_ast.TypeDecl(
                                getattr(p, "name", None), [], None, c_ast.IdentifierType([REF]), p.coord
                            )
                        else:
                            q.type = self.game_declarators(p.type)
                        params.append(q)
                    else:
                        params.append(p)
                new.args = c_ast.ParamList(params, node.args.coord)
            return new
        if isinstance(node, (c_ast.Struct, c_ast.Union)):
            return self.game_record(node) if node.decls is not None else node
        if isinstance(node, c_ast.Typename):
            new = copy.copy(node)
            new.type = self.game_declarators(node.type)
            return new
        return node

    def adjusted_to_pointer(self, declarator) -> bool:
        """A parameter declared as an array or a function -- written so, or
        through a typedef -- which C adjusts to a pointer (C11 6.7.6.3p7-8)."""
        if isinstance(declarator, (c_ast.ArrayDecl, c_ast.FuncDecl)):
            return True
        if isinstance(declarator, c_ast.TypeDecl) and isinstance(declarator.type, c_ast.IdentifierType):
            names = declarator.type.names
            s = self.tp.file.ordinary.get(names[-1]) if len(names) == 1 else None
            return s is not None and s.kind == "typedef" and isinstance(s.type, (Array, Function))
        return False

    def game_record(self, node):
        new = copy.copy(node)
        decls = []
        for d in node.decls:
            if isinstance(d, c_ast.Decl):
                q = copy.copy(d)
                q.type = self.game_declarators(d.type)
                decls.append(q)
            else:
                decls.append(d)
        new.decls = decls
        return new

    def host_node(self, node) -> bool:
        return is_host_file(getattr(getattr(node, "coord", None), "file", None))

    def once(self, chain, declname: str | None):
        """A declarator chain whose struct or union definition is written once:
        `typedef struct S {...} A, B;` is two declarations sharing one
        definition -- the second names it (`struct S`, or `A` for an anonymous
        one) instead of defining it again."""
        if isinstance(chain, (c_ast.Struct, c_ast.Union)):
            return self.once_record(chain, declname)
        if not hasattr(chain, "type"):
            return chain
        inner = self.once(chain.type, declname)
        if inner is chain.type:
            return chain
        new = copy.copy(chain)
        new.type = inner
        return new

    def once_record(self, node, declname):
        if node.decls is None:
            return node
        if not hasattr(self, "defined"):
            self.defined = {}
        if id(node) in self.defined:
            first = self.defined[id(node)][0]
            if node.name is not None:
                return type(node)(node.name, None, node.coord)
            if first is None:
                return node  # `struct { ... } a, b;`: an anonymous definition may be written again
            return c_ast.IdentifierType([first], node.coord)
        # the node kept with its name: a copy game_record made would otherwise
        # die and its id be another copy's -- a definition taken for written
        self.defined[id(node)] = (declname, node)
        return node

    def visit_Decl(self, n, no_type=False):
        if n.type is not None:
            chain = self.once(n.type, None)
            if chain is not n.type:
                n = copy.copy(n)
                n.type = chain
        s_function = self.tp.declared.get(id(n))
        if (
            isinstance(n.type, c_ast.FuncDecl)
            and s_function is not None
            and s_function.kind == "function"
            and self.host_symbol(s_function)
        ):
            # a function of the port's, declared in the game's file (prepare.py's
            # hooks): as written, host pointers -- as its calls hand them
            return self.plain.visit_Decl(n, no_type)
        if isinstance(n.type, c_ast.FuncDecl) and n.name in self.index.variadic and not self.host_node(n):
            # the block of its arguments, as its definition takes them
            n = copy.copy(n)
            n.type = copy.copy(n.type)
            n.type.args = c_ast.ParamList([c_ast.EllipsisParam()], None)
        s = self.tp.declared.get(id(n))
        if self.is_data_init_record(n):
            return self.data_init_record(n)
        if s is not None and s.kind == "object" and not isinstance(n.type, c_ast.FuncDecl):
            where, what = self.place(s, use=False)
            if where == "function":
                # a function's name declared as an object, reached by its
                # function reference (whose record, numbered, takes its address)
                return self.plain.visit_Decl(n, no_type)
            if where in ("ds", "data") and "extern" in n.storage:
                return self.type_definition(n)  # declared: the object is in the window
            if where == "ds":
                # a DS-addressed object: its place is its address (prepare.py's image fills it)
                return self.type_definition(n)
            if where == "data":
                return self.data_definition(n, s, what)
            if where == "host":
                return self.plain.visit_Decl(n, no_type)
        if self.host_node(n):
            return self.plain.visit_Decl(n, no_type)
        d = copy.copy(n)
        d.type = self.game_declarators(n.type)
        init = n.init
        d.init = None
        text = super().visit_Decl(d, no_type)
        if init is not None:
            t = self.tp.types.get(id(init))
            if t is None:
                t = self.tp.type_of_declarator(n.type)
            text += " = " + self.initializer(init, t)
        return text

    def type_definition(self, n) -> str:
        """What is left of a declaration the window's object needs no more:
        the struct, union or enum it defines (`extern const struct Ring {...}
        data_ov252_020d4428;`), alone."""
        t = n.type
        while isinstance(t, (c_ast.TypeDecl, c_ast.PtrDecl, c_ast.ArrayDecl, c_ast.FuncDecl)):
            t = t.type
        if (
            isinstance(t, (c_ast.Struct, c_ast.Union))
            and t.decls is not None
            or isinstance(t, c_ast.Enum)
            and t.values is not None
        ):
            return self.visit(t)
        return ""

    def data_definition(self, n, s: Symbol, key: str) -> str:
        """A data-area object's definition: its image (when it has an initial
        value) and the record that copies it into the window, and the check of
        its size. The object itself is the window's."""
        self.data_object(s, key)
        if key in self.data_objects:
            self.data_defined[key] = self.data_objects[key]
        t = s.type
        parts = []
        if n.init is not None:
            image = f"khdays_image_{key}"
            d = copy.copy(n)
            d.name = image
            d.storage = ["static"]
            d.type = self.renamed(self.game_declarators(n.type), image)
            d.init = None
            text = super().visit_Decl(d)
            parts.append(text + " = " + self.initializer(n.init, t))
            parts.append(self.record(key, image))
        if not (isinstance(t, Array) and t.length is None):
            parts.append(f'_Static_assert(sizeof({self.decl(t)}) == KHDAYS_DATA_SIZE_{key}, "{key}")')
        return "; ".join(parts)

    @staticmethod
    def renamed(chain, name):
        node = chain
        while not isinstance(node, c_ast.TypeDecl):
            node = node.type
        node.declname = name
        return chain

    @staticmethod
    def is_data_init_record(n) -> bool:
        t = n.type
        return (
            isinstance(t, c_ast.TypeDecl)
            and isinstance(t.type, c_ast.Struct)
            and t.type.name == "khdays_data_init"
            and n.name is not None
            and n.init is not None
        )

    def data_init_record(self, n) -> str:
        """KHDAYS_DATA_INIT's record (compat/decomp_compat.h): the object's DS
        address as a number, its image (the port's) as it is."""
        exprs = n.init.exprs
        target = exprs[0]
        while isinstance(target, c_ast.Cast):
            target = target.expr
        if not (isinstance(target, c_ast.UnaryOp) and target.op == "&"):
            raise EmitFailure(n, "a data record whose address is not an object's")
        address = self.address(target.expr)
        if address is None:
            raise EmitFailure(n, "a data record for an object with no place")
        rest = ", ".join(self.plain.visit(e) for e in exprs[1:])
        return f'__declspec(allocate(".khdi$m")) struct khdays_data_init {n.name} = {{{address}, {rest}}}'

    def visit_Typedef(self, n):
        chain = self.once(n.type, n.name)
        if chain is not n.type:
            n = copy.copy(n)
            n.type = chain
        if self.host_node(n):
            return self.plain.visit_Typedef(n)
        d = copy.copy(n)
        d.type = self.game_declarators(n.type)
        return super().visit_Typedef(d)

    def visit_Typename(self, n):
        d = copy.copy(n)
        d.type = self.game_declarators(n.type)
        return super().visit_Typename(d)

    def visit_FuncDef(self, n):
        if self.host_node(n):
            return self.plain.visit_FuncDef(n)
        s = self.tp.declared.get(id(n.decl))
        f = s.type if s is not None else None
        saved = self.va_params, self.va_calls
        self.va_params, self.va_calls = {}, {}
        try:
            if isinstance(f, Function) and f.params is not None and (f.variadic or n.decl.name in self.index.variadic):
                named = [p for p in n.decl.type.args.params if isinstance(p, c_ast.Decl) and p.name is not None]
                offsets, _ = self.va_layout([decayed(t) for t in f.params])
                self.va_params = {id(p): o for p, o in zip(named, offsets)}
            blocks = self.plan_va_calls(n.body)
            text = super().visit_FuncDef(n)
            if blocks:
                at = text.index("{") + 1
                text = text[:at] + "\n" + "".join(f"  {b}\n" for b in blocks) + text[at:]
            return text
        finally:
            self.va_params, self.va_calls = saved

    def visit_Struct(self, n):
        if self.host_node(n):
            return self.plain.visit_Struct(n)
        return super().visit_Struct(self.game_record(n) if n.decls is not None else n)

    def visit_Union(self, n):
        if self.host_node(n):
            return self.plain.visit_Union(n)
        return super().visit_Union(self.game_record(n) if n.decls is not None else n)

    # --- line markers ---

    def _generate_stmt(self, n, add_indent=False):
        coord = getattr(n, "coord", None)
        marker = ""
        if self.markers and coord is not None and coord.file and (coord.file, coord.line) != self.line:
            self.line = (coord.file, coord.line)
            marker = f'#line {coord.line} "{normalised_path(coord.file)}"\n'
        return marker + super()._generate_stmt(n, add_indent)

    def visit_FileAST(self, n):
        # the functions this file defines: the game's, whatever their
        # declarations before the definition say
        self.defined_here = {ext.decl.name for ext in n.ext if isinstance(ext, c_ast.FuncDef)}
        out = [PRELUDE]
        for ext in n.ext:
            coord = getattr(ext, "coord", None)
            if coord is not None and coord.file:
                out.append(f'#line {coord.line} "{normalised_path(coord.file)}"\n')
            if isinstance(ext, c_ast.FuncDef):
                text = self.visit(ext)
                out.append(self.routed(ext, text) if self.routes else text)
            elif isinstance(ext, c_ast.Pragma):
                out.append(self.visit(ext) + "\n")
            else:
                text = self.visit(ext)
                if text:
                    out.append(text + ";\n")
        out.append(self.literal_images())
        out.append(self.function_records())
        return "".join(out)

    def route_arguments(self, n: c_ast.FuncDef) -> list[str] | None:
        """The names a routed definition's wrapper passes on, in order; None when it cannot be routed (the port's
        own, a static, inline or variadic one, one taking its arguments in a block, an unnamed parameter)."""
        if self.host_node(n) or n.decl.name.startswith("khdays_") or n.decl.funcspec:
            return None
        s = self.tp.declared.get(id(n.decl))
        if s is None or "static" in s.storage or not isinstance(s.type, Function):
            return None
        if s.type.params is None or s.type.variadic or n.decl.name in self.index.variadic:
            return None
        params = n.decl.type.args.params if n.decl.type.args is not None else []
        names = []
        for p in params:
            if isinstance(p, c_ast.Typename) and len(params) == 1:
                return []  # (void)
            if not isinstance(p, c_ast.Decl) or p.name is None:
                return None
            names.append(p.name)
        return names

    def routed(self, n: c_ast.FuncDef, text: str) -> str:
        """A game routine a native plugin may replace (kh.engine replace): its body renamed khdays_impl_<name>, a
        pointer khdays_route_<name> to it, and <name> itself calling through the pointer -- every call, the game's
        own and a function reference's, so reaching what the pointer holds; a record of the three in .khrt$m."""
        args = self.route_arguments(n)
        name = n.decl.name
        at = text.find("{")
        head = text[:at].rstrip() if at > 0 else ""
        if args is None or head.count(f"{name}(") != 1:
            return text
        s = self.tp.declared[id(n.decl)]
        call = f"khdays_route_{name}({', '.join(args)})"
        body = f"{{\n  {call};\n}}\n" if isinstance(s.type.result, Void) else f"{{\n  return {call};\n}}\n"
        return (
            f"{head};\n"
            + text.replace(f"{name}(", f"khdays_impl_{name}(", 1)
            + head.replace(f"{name}(", f"(*khdays_route_{name})(", 1)
            + f" = khdays_impl_{name};\n"
            + f"{head}\n{body}"
            + f'__declspec(allocate(".khrt$m")) const struct khdays_route_record khdays_route_record_{name} = '
            f'{{"{name}", (void **)&khdays_route_{name}, (void *)khdays_impl_{name}}};\n'
        )

    def function_records(self) -> str:
        """The native function of each function reference the build numbers
        (runtime/core/window.c builds its table from these)."""
        return "".join(
            f'__declspec(allocate(".khfr$m")) const struct khdays_function_record '
            f"khdays_function_{key}_{self.file_key} = {{KHDAYS_FREF({key}), (khdays_function_pointer){name}}};\n"
            for key, name in sorted(self.frefs.items())
        )


def emit(
    ast: c_ast.FileAST, tp: TypePass, index: Index, file_key: str = "f", routes: bool = False
) -> tuple[str, Emitter]:
    e = Emitter(tp, index, file_key, routes)
    return e.visit(ast), e
