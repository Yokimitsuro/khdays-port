"""A host for the SDK's boundary, in Python (ctypes): loads a plugin library (.dll, .so) as the port does, asks it for
its descriptor (kh_plugin_query), checks its ABI and size, starts it with a host table whose log prints and whose
find answers none, and stops it. What the port's loader does on every platform, without the port:

    python load_plugin.py <library>

Exits 0 when the plugin went through all of it, 1 otherwise. Works wherever Python runs: a Linux plugin is checked on
Linux (or in WSL) without building the port there."""

import ctypes
import sys

KH_ABI_MAJOR = 1
KH_ABI_MINOR = 0
KH_OK = 0
LEVELS = {0: "info", 1: "warning", 2: "error"}


class Host(ctypes.Structure):
    pass


LOG = ctypes.CFUNCTYPE(None, ctypes.c_void_p, ctypes.c_int, ctypes.c_char_p)
FIND = ctypes.CFUNCTYPE(ctypes.c_void_p, ctypes.c_void_p, ctypes.c_char_p, ctypes.c_uint32)
Host._fields_ = [
    ("size", ctypes.c_uint32),
    ("abi_major", ctypes.c_uint16),
    ("abi_minor", ctypes.c_uint16),
    ("plugin", ctypes.c_void_p),
    ("port_version", ctypes.c_char_p),
    ("log", LOG),
    ("find", FIND),
]

START = ctypes.CFUNCTYPE(ctypes.c_int32, ctypes.POINTER(Host))
STOP = ctypes.CFUNCTYPE(None)


class Descriptor(ctypes.Structure):
    _fields_ = [
        ("size", ctypes.c_uint32),
        ("abi_major", ctypes.c_uint16),
        ("abi_minor", ctypes.c_uint16),
        ("name", ctypes.c_char_p),
        ("start", START),
        ("stop", STOP),
    ]


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__)
        return 1
    said: list[str] = []
    asked: list[str] = []

    @LOG
    def log(plugin, level, text):
        line = f"{LEVELS.get(level, level)}: {text.decode('utf-8', 'replace')}"
        said.append(line)
        print("  plugin", line)

    @FIND
    def find(plugin, name, version):
        asked.append(f"{name.decode()} {version}")
        return None

    library = ctypes.CDLL(sys.argv[1])
    query = getattr(library, "kh_plugin_query", None)
    if query is None:
        print("no kh_plugin_query exported")
        return 1
    query.restype = ctypes.POINTER(Descriptor)
    query.argtypes = []
    d = query().contents
    print(
        f"descriptor: size {d.size} (here {ctypes.sizeof(Descriptor)}), SDK {d.abi_major}.{d.abi_minor}, "
        f"{d.name.decode()!r}"
    )
    if d.abi_major != KH_ABI_MAJOR or d.abi_minor > KH_ABI_MINOR or d.size < ctypes.sizeof(Descriptor):
        print("not loadable by this host")
        return 1
    plugin_handle = ctypes.c_int(7)  # any address: the plugin only hands it back
    host = Host(
        ctypes.sizeof(Host), KH_ABI_MAJOR, KH_ABI_MINOR, ctypes.addressof(plugin_handle), b"load_plugin.py", log, find
    )
    started = d.start(ctypes.byref(host))
    print(f"start answered {started}; it asked for {asked or 'nothing'}")
    if started != KH_OK:
        return 1
    d.stop()
    print(f"stopped; {len(said)} lines logged")
    return 0 if said else 1


if __name__ == "__main__":
    sys.exit(main())
