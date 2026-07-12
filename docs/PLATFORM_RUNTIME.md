# SDL3 platform runtime milestone

This overlay replaces the dependency-free bootstrap executable with the first
native platform runtime.

## Included changes

- SDL3 3.4.12 fetched and pinned by CMake.
- Static SDL3 linking.
- Resizable high-DPI window.
- SDL event loop.
- Escape and window-close handling.
- Two placeholder Nintendo DS screens.
- Non-windowed `--version` CTest smoke test.
- SDL3 third-party notice.

## Install

Extract this ZIP over the repository root.

Delete the previous build directory because the CMake dependency graph changed:

```powershell
cd "E:\KH 3582\khdays-port"

Remove-Item .\build -Recurse -Force -ErrorAction SilentlyContinue

cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
```

The first configuration downloads and compiles the pinned SDL3 release, so it
takes longer than the original bootstrap build.

## Run

```powershell
.\build\Debug\khdays-port.exe
```

A 1280×720 window should open with two Nintendo DS screen placeholders.

Close it using Escape or the window close button.

## Test without opening a window

```powershell
ctest --test-dir build -C Debug --output-on-failure
```

Expected:

```text
100% tests passed
```

## Commit

```powershell
git add CMakeLists.txt include src platform THIRD_PARTY_NOTICES.md
git commit -m "platform: add SDL3 window runtime"
git push
```

Do not add `build`, `_deps`, SDL source files, or generated binaries to Git.
