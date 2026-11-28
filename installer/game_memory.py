"""A running game's own image, read from its memory (Windows). The Xbox app (the Microsoft Store's copies) lets
nothing but the game read its executables' files, while the game it runs for the player can be read as any
program of theirs: what the helper takes from `KINGDOM HEARTS II FINAL MIX.exe` it then takes from there.
Read only; nothing of it is written anywhere."""

from __future__ import annotations

import struct
import sys
from pathlib import Path

PAGE = 0x1000
PROCESS_QUERY_LIMITED_INFORMATION, PROCESS_VM_READ, PROCESS_QUERY_INFORMATION = 0x1000, 0x0010, 0x0400
LIST_MODULES_ALL = 3
ERROR_INSUFFICIENT_BUFFER = 122
PE_OFFSET_AT, SIZE_OF_IMAGE_AT = 0x3C, 24 + 56  # the PE header's place; SizeOfImage in the optional header


def running_image(exe: Path) -> bytes | None:
    """The image of the running program whose file is named as `exe` (the one in `exe`'s folder first), as it
    is in its memory -- its sections where the PE header's addresses say, pages not read left zero -- or None
    when no such program runs, it is not read, or this is not Windows."""
    if sys.platform != "win32":
        return None
    import ctypes
    from ctypes import wintypes as w

    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    psapi = ctypes.WinDLL("psapi", use_last_error=True)
    kernel.OpenProcess.restype = w.HANDLE
    kernel.OpenProcess.argtypes = [w.DWORD, w.BOOL, w.DWORD]
    kernel.CloseHandle.argtypes = [w.HANDLE]
    kernel.QueryFullProcessImageNameW.argtypes = [w.HANDLE, w.DWORD, w.LPWSTR, ctypes.POINTER(w.DWORD)]
    kernel.ReadProcessMemory.argtypes = [
        w.HANDLE,
        ctypes.c_void_p,
        ctypes.c_void_p,
        ctypes.c_size_t,
        ctypes.POINTER(ctypes.c_size_t),
    ]
    psapi.EnumProcesses.argtypes = [ctypes.POINTER(w.DWORD), w.DWORD, ctypes.POINTER(w.DWORD)]
    psapi.EnumProcessModulesEx.argtypes = [
        w.HANDLE,
        ctypes.POINTER(ctypes.c_void_p),
        w.DWORD,
        ctypes.POINTER(w.DWORD),
        w.DWORD,
    ]

    def read(process: int, at: int, size: int) -> bytes:
        out = bytearray(size)
        got = ctypes.c_size_t()
        page = (ctypes.c_char * PAGE)()
        for offset in range(0, size, PAGE):
            want = min(PAGE, size - offset)
            if kernel.ReadProcessMemory(process, ctypes.c_void_p(at + offset), page, want, ctypes.byref(got)):
                out[offset : offset + got.value] = page.raw[: got.value]
        return bytes(out)

    def file_of(process: int) -> str | None:
        """The path of the process's executable (its buffer grown until the whole path fits)."""
        size = 260
        while True:
            name = ctypes.create_unicode_buffer(size)
            length = w.DWORD(size)
            if kernel.QueryFullProcessImageNameW(process, 0, name, ctypes.byref(length)):
                return name.value
            if ctypes.get_last_error() != ERROR_INSUFFICIENT_BUFFER:
                return None
            size *= 2

    count = 1024
    while True:  # the list is whole once it does not fill what it was given
        ids = (w.DWORD * count)()
        filled = w.DWORD()
        if not psapi.EnumProcesses(ids, ctypes.sizeof(ids), ctypes.byref(filled)):
            return None
        if filled.value < ctypes.sizeof(ids):
            break
        count *= 2
    named: list[tuple[bool, int]] = []  # (not in exe's folder, process id): the folder's own sorts first
    for pid in ids[: filled.value // ctypes.sizeof(w.DWORD)]:
        process = kernel.OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, False, pid)
        if not process:
            continue
        file = file_of(process)
        kernel.CloseHandle(process)
        if file is not None and Path(file).name.casefold() == exe.name.casefold():
            named.append((Path(file).parent != exe.parent, pid))
    for _, pid in sorted(named):
        process = kernel.OpenProcess(
            PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, False, pid
        )
        if not process:
            continue
        try:
            first = (ctypes.c_void_p * 1)()
            needed = w.DWORD()
            if not psapi.EnumProcessModulesEx(
                process, first, ctypes.sizeof(first), ctypes.byref(needed), LIST_MODULES_ALL
            ):
                continue
            base = first[0]
            if not needed.value or not base:
                continue  # (a program still starting: its modules not listed yet)
            header = read(process, base, PAGE)
            if header[:2] != b"MZ":
                continue
            pe = struct.unpack_from("<I", header, PE_OFFSET_AT)[0]
            if pe + SIZE_OF_IMAGE_AT + 4 > len(header):
                continue
            size = struct.unpack_from("<I", header, pe + SIZE_OF_IMAGE_AT)[0]
            if size < PAGE:
                continue
            return read(process, base, size)
        finally:
            kernel.CloseHandle(process)
    return None
