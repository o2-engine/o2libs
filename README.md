# o2libs

Optional modules on top of the [o2 engine](https://github.com/zenkovich/o2). A game project attaches
this repository next to `o2/` and turns on the modules it wants; a module that is off costs nothing —
it is not compiled, not linked, and the lines that mention o2libs in the project compile to nothing.

| Module | Option | What it is |
|---|---|---|
| [Core](Modules/Core) | `O2LIBS_CORE` | Player identity, persistent key-value storage (files; `localStorage` in the browser), service settings, JSON merge patch. Pulled in by the modules that need it. |
| [RemoteConfig](Modules/RemoteConfig) | `O2LIBS_REMOTE_CONFIG` | Remote configs, scheduled launches and A/B tests: the client of the o2 live-ops service. |

## Attaching to a project

```sh
git submodule add https://github.com/o2-engine/o2libs.git o2libs
```

`CMakeLists.txt`, after the engine:

```cmake
add_subdirectory(o2)

set(O2LIBS_REMOTE_CONFIG ON CACHE BOOL "")     # one line per module the game uses
add_subdirectory(o2libs)

# ... the game library and its codegen target, as before, with one addition: the modules'
# reflection caches among the parents of the game's own codegen
#     -parent_projects "<o2 framework cache> ${O2LIBS_CODEGEN_CACHES}"

o2libs_link(GameLib GameLibCodegen)            # links the enabled modules, orders the codegen passes

# in the tests section, after set_target_output_directories() and BuildAssets exist:
o2libs_add_tests()                              # the o2libsTests executable
```

Entry points (`Platforms/*/main`), once — new modules never touch them again:

```cpp
#include "o2libs/o2libs.h"

INITIALIZE_O2;
InitializeTypesGameLib();
O2LIBS_INITIALIZE_TYPES;          // reflection of the modules in this build

auto app = mmake<GameApplication>();
app->Initialize();
O2LIBS_START;                     // the engine is up: modules that start by themselves, start
app->Launch();
```

Both macros live in the entry points and not in the game's application class on purpose: a
script-only game running on a stock runtime gets its modules without a line of C++. `O2LIBS_START`
does its work once, so an application may call it from `OnStarted()` as well (iOS, where `main` has
no such moment). Modules that need a per-frame tick run themselves as an `o2Tasks` task.

A project that may or may not have the submodule guards the include:

```cpp
#if __has_include("o2libs/o2libs.h")
#include "o2libs/o2libs.h"
#else
#define O2LIBS_INITIALIZE_TYPES
#define O2LIBS_START
#endif
```

and the CMake lines with `if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/o2libs/CMakeLists.txt")` /
`if(COMMAND o2libs_link)`.

Requires an o2 checkout with `o2/Network` (HTTP) and `Utils/Tasks`: engine commits from September
2026 on. `O2LIBS_O2_DIR` points at the engine when it is not at `../o2`.

## Layout

```
CMakeLists.txt               the list of modules: one o2libs_declare_module() line each
cmake/o2libs.cmake           the machinery: options, dependencies, targets, codegen, tests
Sources/o2libs/o2libs.h      the umbrella header: knows a module only by its O2LIBS_<MODULE> definition
Tests/TestsMain.cpp          headless gtest runner for the enabled modules
Modules/<Name>/
    Sources/o2libs/<Name>/   the static library o2libs<Name>; included as "o2libs/<Name>/X.h"
    Sources/o2libs<Name>.cpp, CodeToolCache.xml     written by o2CodeTool, committed
    Tests/                   gtest sources
    README.md
```

Everything is in the `o2libs` namespace, which is also what scripts see: a class with `@SCRIPTABLE`
members is `o2libs.<Class>` in JavaScript with no binding code.

## Adding a module

1. `Modules/<Name>/Sources/o2libs/<Name>/…` and `Modules/<Name>/Tests/…`.
2. One line in `CMakeLists.txt`, after the modules it depends on:
   `o2libs_declare_module(<Name> OPTION O2LIBS_<NAME> DESCRIPTION "…" DEPENDS Core)`.
3. In `Sources/o2libs/o2libs.h`: the `extern void InitializeTypeso2libs<Name>();` and its call under
   `#if defined(O2LIBS_<NAME>)`, and a line in `Start()` if the module starts by itself.

Rules that keep modules optional and the portal's server builds working:

- A module depends on the engine and on the modules it names in `DEPENDS`, never the other way round,
  and never on a game. A dependency is switched on automatically.
- Reflection is generated on a desktop build only (`o2CodeTool` rewrites the `// --- META ---` blocks,
  `o2libs<Name>.cpp` and `CodeToolCache.xml` in place). Commit what it writes: cross builds and the
  portal's build server compile the sources as they are.
- No new link flags and no third-party libraries without a very good reason: the portal links games
  against a prebuilt kit, and a new flag means a new kit for everybody. Browser facilities are reached
  with `EM_JS` (see `Core/Storage.cpp`), which needs none.
- Network only through `o2Network`, behind an interface the tests can replace (see
  `RemoteConfig/RemoteConfigTransport.h`): unit tests never touch the network.

## Tests

```sh
cmake --build --preset mac --target o2libsTests
ctest --test-dir build -R '^o2libsTests/' --output-on-failure
```

`RemoteConfigLive.*` runs the client over the engine's real HTTP stack against a running live-ops
service and is skipped otherwise; `o2portal/liveops/tools/live-check.ts` sets the whole thing up.
