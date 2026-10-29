# KH Mod SDK

The headers and build files for native plugins of the khdays port's mods (docs/MODDING_NATIVE.md in the port's
repository says how players turn them on and how a mod carries one).

## Making a plugin

1. A CMake project that includes `cmake/KhModSdk.cmake` (or adds this folder) and calls
   `kh_add_plugin(my_plugin my_plugin.cpp)`.
2. In the source, a descriptor and the one exported function:

```cpp
#include <kh/kh_mod.h>

namespace {
const KhHost *host = nullptr;

KhResult start(const KhHost *given)
{
    host = given;
    host->log(host->plugin, KH_LOG_INFO, "started");
    return KH_OK;
}

void stop()
{
    host->log(host->plugin, KH_LOG_INFO, "stopped");
}

const KhPluginDescriptor descriptor = KH_PLUGIN_DESCRIPTOR("My plugin", start, stop);
} // namespace

extern "C" KH_PLUGIN_EXPORT const KhPluginDescriptor *kh_plugin_query(void)
{
    return &descriptor;
}
```

3. Build: `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build`.
4. Install: the library in `mods/<Mod>/native/<platform>/`, and `mod.ini` with `sdk = 1` and
   `native.<platform> = native/<platform>/my_plugin.dll`.

`examples/HelloWorld` is the same, complete.

## The examples

| Example | What it shows |
|---|---|
| `examples/HelloWorld` | the descriptor, `start` and `stop`, the log, an interface the port has not got |
| `examples/AdvancedNativeMod` | `kh.hooks` (the player's HP, the enemies' hits, sounds), `kh.events` (scenes), `kh.engine` (a routine and a datum by name), `kh.actors` |
| `examples/CustomCharacter` | a mod's own character (its `characters/block/`) given behaviour by `kh.characters`: told of its attacks, 1 HP back for each, its hits doubled through a hook and `kh.actors` |
| `examples/CustomLimit` | a character's Limit of its own: the game's skipped at `Ov022_ActorSetState`, the buttons hidden (`kh.input`), its own clips played (`kh.characters`), its spins' hits dealt through the game's routine (`kh.combat`); `make_files.py` makes its made-up character |
| `examples/CustomEnemy` | the Shadows given a behaviour of their own: they blink away and split, after `Ov107_AiState_ApplyHit`'s hook (`kh.actors`' `set_position`, `make_enemy`) |

Each is a mod folder: copy it to the game's `mods/` with its built library in `native/<platform>/`. The installer
brings them with KH Studio: this SDK in the game's `sdk/`, and each example ready to copy, its Windows plugin built,
in `mods-examples/`.

A mod may describe its plugin's parameters in `parameters.ini` (`examples/CustomLimit` does): KH Studio shows and
saves them without loading the plugin, which reads them through `kh.parameters` (docs/MODDING_NATIVE.md in the
port's repository, "Its parameters").

## The rules of the boundary (`include/kh/kh_mod.h`)

- Only C crosses it: fixed-size integers, UTF-8 strings, opaque handles, structs of function pointers. Catch every
  C++ exception inside the plugin; pass no standard-library type; use no RTTI across it.
- Version: `KH_ABI_MAJOR` / `KH_ABI_MINOR`. The port loads a plugin built for its own major and a minor not above
  its own. Structs given by the port begin with their size and only grow at their end: read a field only when the
  size covers it.
- Interfaces: `host->find(host->plugin, "<name>", <version>)` returns a struct of functions or NULL when the port
  has not got it. A port keeps every version of an interface it ever gave.
- Threads: `start`, `stop` and callbacks run on the game's thread unless an interface says otherwise.
- After `stop` returns the port calls nothing of the plugin and releases its library: undo nothing yourself that
  the port undoes (it drops your registrations), free what you allocated.
- A plugin is not isolated from the game: a fault in it is a fault of the game.

## Checking a plugin without the port

`tests/load_plugin.py <library>` (Python 3, nothing else) loads a plugin as the port's loader does: it resolves
`kh_plugin_query`, checks the descriptor's SDK and size, starts it with a host table whose `log` prints and whose
`find` answers none, and stops it. `tests/abi_check.c` is a plugin with no C library for it, which builds for Linux on
any machine:

```bash
cmake -S sdk -B build/sdk-linux -G Ninja -DCMAKE_TOOLCHAIN_FILE=sdk/cmake/toolchain-linux-x64-clang.cmake
cmake --build build/sdk-linux
python3 sdk/tests/load_plugin.py build/sdk-linux/abi_check.so   # on Linux
```

## Platforms

| Platform | Library | State |
|---|---|---|
| Windows x64 | `.dll` (MSVC or clang-cl; the C/C++ runtime linked in by `kh_add_plugin`) | built and run |
| Linux x64, arm64 (Steam Deck) | `.so` (gcc or clang on Linux; `cmake/toolchain-linux-x64-clang.cmake` from another system) | the boundary checked on Linux x64 (`tests/load_plugin.py` loading `tests/abi_check.c`'s `.so`: its one export, its descriptor, its start and stop through a host table); not yet inside the port, which does not build for Linux yet |
| Platforms without dynamic loading | the same source built into the port (`KHDAYS_STATIC_PLUGINS`: its `kh_plugin_query` renamed, listed by its mod's id) | built in and started by the plugins' test; no such port yet |
