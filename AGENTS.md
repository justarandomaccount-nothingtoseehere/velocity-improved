# AGENTS.md — Velocity (CS2)

Repository-wide rules for AI agents working on this codebase. This file
overrides the global agent configuration (`~/.config/opencode/AGENTS.md`)
wherever the two conflict.

## 1. Project overview

Velocity is a CS2 game modification that runs as an injected DLL inside the
game process. It is written in C++20 and built with MSVC on Windows only.

The repository is public, but contributions come from a single maintainer.

The codebase is a plain header + translation-unit project. There are no C++20
modules, no precompiled header and no vcpkg manifest. Engine state is read
through the `systems::` access layer, and every engine-facing address is
resolved from byte signatures at startup.

Layout:

```text
velocity/
├── AGENTS.md                  # this file
├── README.md                  # build guide + contributing rules
├── .clang-format              # formatting rules (advisory, partly stale)
├── .clang-tidy                # tidy checks (advisory)
├── .clang-format-ignore
├── .gitignore
├── analysis/                  # LOCAL ONLY, git-ignored: binaries, .i64 DBs, dumps
└── cs2/
    ├── velocity.slnx
    ├── velocity.vcxproj       # the only build project
    ├── bin/                   # build output + intermediates, git-ignored
    └── project/               # all source
```

`analysis/` is local scratch space and is not tracked by git. It holds the
current CS2 binaries with their IDA databases (`analysis/CS2-latest/*.i64`),
generated analysis output under `analysis/CS2-latest/*-analysis/`, and the
maintainer's analysis script.

## 2. Hard rules

1. **Never assume. Verify everything.** Before writing a single line of code,
   confirm every engine-facing fact (function address, struct layout, schema
   field, vtable slot, hook target) against a real source: the IDA databases
   in `analysis/CS2-latest/`, `cspatterns.dev`, `cs2-sdk.com`, the
   `GameTracking-CS2` clone (when present in `analysis/`), or CS2 binaries
   directly. If a fact cannot be verified, find a way to verify it or ask the
   maintainer — never guess.
2. **Make it the best possible change.** The implementation must be the
   smartest, strongest and cleanest option available, not the first one that
   compiles in your head. Prefer the correct engine-faithful approach over a
   shortcut.
3. **No builds on this machine.** The project builds with MSBuild on Windows
   (x64, AVX2). Agents work on Linux: do not attempt `msbuild`/`vcpkg`
   builds, and never claim a change was "built" or "tested". Verification is
   by code review, formatting and static reasoning.
4. **Research forums through subagents.** `unknowncheats.me` and
   `yougame.biz` are for looking up new techniques, updates, leaks and
   research. The main agent should use them only for small lookups; anything
   bigger must be delegated to a dedicated subagent per site so the research
   context does not pollute the implementation context.
5. **`analysis/` is read-only** except for the IDA databases and the
   `GameTracking-CS2` clone. Never modify, move or delete the CS2 binaries,
   dumps or generated analysis output.
6. **Never run `cs2_analyze_*.py`.** That script belongs to the maintainer.
7. **Zero comments in new or edited code.** No line comments, block comments,
   doc comments or JSDoc. Code must be self-explanatory. Leave pre-existing
   comments untouched unless the commented lines themselves change.
8. **Never commit secrets.** No tokens, keys, webhook URLs, credentials or
   private endpoints. Config files stay local.
9. **Commit only when the maintainer asks.** A commit request implies the
   commit and the push (see §9). Nothing else gets committed, pushed, or
   merged on your own initiative.
10. **No swarm workflow in this repo.** Work solo; do not spawn a swarm.
11. **Never add anti-detection / anti-cheat evasion logic** unless the
    maintainer explicitly asks for it.
12. **Never reintroduce the removed architecture.** No `.cppm` module units,
    no `platform/platform.h` precompiled header, no vcpkg manifest, no
    `protection/` directory. The header/source split in §3 is the architecture.
13. **Never ad-hoc scan memory for an address.** Every engine target goes
    through `signatures/patterns.cpp` and the `PATTERN(...)` macro.
14. **Uncertainty protocol.** Research first (IDA, the sites above, the
    sources in this repo), and only then ask the maintainer. Come back with
    findings, not with open questions you could have answered yourself.

## 3. Architecture map

All source lives under `cs2/project/`. Include paths are rooted at `project/`,
so project headers are included as `<core/...>`, `<utilities/...>`,
`<signatures/...>` and `<external/...>`.

```text
project/
├── entry.cpp                 # DLL entry point + staged initialization thread
├── core/
│   ├── settings.hpp          # all user-facing settings + config registration
│   ├── features/
│   │   ├── features.hpp      # global instances of every feature (g_*)
│   │   ├── changer/          # skinchanger: weapons, knives, gloves, agents
│   │   │                     #   changer.hpp + impl/{guns,knives,gloves,agents,
│   │   │                     #   econ_item_system}.cpp
│   │   ├── combat/           # ragebot, legitbot, shared targeting, misc
│   │   │                     #   combat.hpp + impl/{rage,shared,legit,misc}.cpp
│   │   ├── esp/              # player / item / projectile / other overlays,
│   │   │                     #   chams, glow; esp.hpp + impl per target
│   │   ├── misc/             # camera, hud, impacts, removals, dlight,
│   │   │                     #   projectile trajectory, scoreboard weapons,
│   │   │                     #   name changer, autobuy, viewmodel adjust
│   │   ├── movement/         # bhop, airstrafe, edgebug/edgejump/edgestop,
│   │   │                     #   fastladder, jumpbug, slowwalk, test strafer
│   │   └── world/            # weather, scene (skyboxes/lighting), smoke
│   ├── hooks/                # engine detours
│   │   ├── hooks.hpp         # detour declarations + inline hook storage
│   │   └── impl/
│   │       ├── cheat.cpp     # gameplay/render/input hooks (Present, CreateMove,
│   │       │                 #   FrameStageNotify, scene, glow, smoke, decals)
│   │       └── utility.cpp   # service read + internal logging hooks
│   ├── rendering/            # D3D11 overlay layer
│   │   ├── rendering.hpp     # overlay/menu/widget/font interfaces
│   │   └── impl/
│   │       ├── context.cpp   # device/context/RTV from the swap chain
│   │       ├── fonts.cpp     # Inter / Smallest Pixel 7
│   │       ├── widgets.cpp   # watermark, keybind display
│   │       └── menu/         # menu.{core,ragebot,legitbot,player,world,
│   │                         #   skins,misc,config}.cpp
│   ├── resources/            # embedded data
│   │   ├── fonts/            # inter.hpp, pixel7.hpp
│   │   └── particles/        # effects.hpp, weather.hpp
│   └── systems/              # engine state access layer
│       ├── systems.hpp       # accessor interfaces + engine ABI mirrors + SCHEMA
│       └── impl/             # schemas, entities, local, input, legit_input,
│                             #   prediction, view, bones, bounds, hitboxes,
│                             #   tracing, materials, events, icons, frame_data
├── signatures/
│   ├── patterns.hpp          # byte-signature declarations
│   ├── patterns.cpp          # byte-signature definitions
│   └── game_addresses.hpp    # signature_t, SIGNATURE/PATTERN/INTERFACE_/CONVAR
├── utilities/
│   ├── addresses/            # modules/globals/functions + interface lookup
│   │   ├── addresses.hpp     # resolved addresses (inline, filled at init)
│   │   ├── interfaces.hpp    # c_convar / c_engine_cvar ABI mirrors
│   │   └── impl/             # modules.cpp, globals.cpp, functions.cpp,
│   │                         #   interfaces/engine_cvar.cpp
│   ├── hooking/              # MinHook-backed jmp + manager + trampoline alloc
│   ├── security/             # region registration + hook prologue bookkeeping
│   ├── logging/              # console + popup logging, xs() macro
│   ├── memory/               # safe reads/writes, module/export/RTTI, signatures
│   ├── math/                 # vector/quaternion/matrix helpers
│   ├── proto/                # protobuf usercmd structures
│   ├── steam/                # Steamworks wrappers (friends, user, utils)
│   ├── threadpool/           # worker jobs for background work
│   ├── random/               # randomized aim/smoothing helpers
│   ├── diag.hpp              # early diagnostics, crash dumps, SEH handling
│   ├── overrides.cpp         # global new/delete routed to the game allocator
│   ├── cstypes.hpp           # game type/constant tables
│   ├── animation.hpp         # UI tween/easing helpers (xdraw-based)
│   ├── fnv1a.hpp             # hashing + "_hash" literal
│   └── game_path.hpp         # CS2 install path resolution
└── external/                 # vendored third-party code (see §5.4)
```

### 3.1 Initialization sequence

`entry.cpp` exports `entry` (the linker entry point, not `DllMain`), handles
`DLL_PROCESS_ATTACH`, calls `_CRT_INIT`, then spawns a dedicated init thread
and never blocks the loader lock. The init thread stages, in order:

1. `diag::initialize_crash_dumps()` (minidump support, `DEV` only)
2. unhandled + vectored exception filters
3. `CoInitializeEx(COINIT_MULTITHREADED)`
4. `config::initialize()` + `settings::finalize_binds()`
5. `security::regions::add_module()` — register our module bounds
6. logging (console + popup)
7. thread pool, Steam API (friends, user, utils)
8. address resolution (`addresses::modules/globals/functions`)
9. systems (`materials`, `events`, `icons`)
10. econ item system (`features::changer::g_econ_item_system`)
11. hooks (`hooks::utility`, then `hooks::cheat`)
12. hidden cvar unlock
13. skybox discovery

Init failures call `INIT_FAIL` and abort initialization with `0`. In `Debug`
(`DEV`) the failure is only written to the diagnostic log; in `Release` it also
shows a message box. `hooks::cheat::initialize` installs Present/ResizeBuffers
fatally, then attempts each feature hook individually and logs the ones whose
pattern is unavailable instead of aborting.

Shutdown on `DLL_PROCESS_DETACH` is compiled only for `DEV` builds: remove
exception handlers, release weather particles, shut down the menu, events and
both hook sets, then `CoUninitialize`, then `diag::shutdown`.

### 3.2 Threading

| Context | Where it runs | What belongs there |
| --- | --- | --- |
| Init thread | `entry.cpp` | one-time setup, address resolution, hook installation |
| Render thread | `hooks::cheat::present` / `resize_buffers`, `cmd_interpreter` | drawing, menu, overlays, chams, glow, text |
| Game thread | `hooks::cheat::create_move`, `frame_stage_notify`, `handle_view_angles`, `process_input_event`, `override_view` | usercmd edits, view angles, movement, prediction |
| Worker threads | `utilities/threadpool` | background work only; never touch engine state without a snapshot |
| Engine threads | `events`, entity add/remove, level init/shutdown callbacks | update caches, keep handlers short |

Rules:

- Engine state read from the render thread must go through the `systems::`
  accessors (they cache snapshots), never by re-reading raw pointers.
- Anything that mutates the user command belongs on the game thread.
- Shared state needs the existing `std::mutex` / `std::shared_mutex` /
  `std::atomic` discipline; never hold a lock across a game call.
- Never block the render thread — the game visibly stutters.

## 4. Build

Both configurations target C++20 (`stdcpplatest`), x64, AVX2
(`AdvancedVectorExtensions2`), use the `v145` toolset, define an exported
`entry` point and link against the Windows subsystem. FreeType is vendored
inside `external/xdraw/dependencies/`; there is no vcpkg restore step.

From a Developer PowerShell at the repository root:

```powershell
msbuild cs2\velocity.vcxproj /m /p:Configuration=Debug /p:Platform=x64
msbuild cs2\velocity.vcxproj /m /p:Configuration=Release /p:Platform=x64
```

| Configuration | Defines | Runtime / optimization | Output |
| --- | --- | --- | --- |
| `Debug` | `DEV` | static CRT (`MultiThreaded`), optimization off, intrinsics off, extra diagnostics + crash dumps | `cs2/bin/velocity-debug.dll` |
| `Release` | `NDEBUG` | DLL CRT (`MultiThreadedDLL`), `MinSpace`, whole-program optimization, intrinsics on | `cs2/bin/velocity.dll` |

Intermediate files live under `cs2/bin/intermediates/<Configuration>/`.
`cs2/velocity.slnx` opens `velocity.vcxproj` in Visual Studio.

Sources are listed explicitly in `velocity.vcxproj`; they are **not** globbed.
Every new `.cpp` must be added to the `<ClCompile>` item group, and a new
public `.hpp` should be added to `<ClInclude>`. Include directories are
`project/` and the vendored FreeType directory, which is what makes
`<core/...>`, `<utilities/...>` and `<external/...>` includes work.

Agents cannot build here. `clang -fsyntax-only` is only a rough approximation
and generally cannot resolve the MSVC/Windows headers, so static review and
`clang-format` are the practical checks; MSVC stays authoritative.
`clang-format` and `clang-tidy` are available locally; format is mandatory,
tidy is advisory.

## 5. Code style

### 5.1 Source of truth

The existing code wins. Match the file you are editing: its naming, layout,
brace style, spacing, include order and idioms. The de facto style is tabs for
indentation, Allman braces, a space inside parentheses (`fn( arg )`,
`if ( cond )`), `this->` for member access, and `snake_case` members. Google
C++ Style is only a fallback for questions the local code does not settle.

Files are lowercase `snake_case`, implementation units are `.cpp`, headers are
`.hpp`. Multi-part features use dot-separated suffixes (`player.chams.cpp`,
`item.glow.cpp`, `menu.ragebot.cpp`, `projectile.trajectory.cpp`).

### 5.2 Naming

| Kind | Convention | Example |
| --- | --- | --- |
| Global instances | `g_` prefix | `g_local`, `g_menu`, `g_context` |
| Members | `m_` prefix, no trailing underscore | `m_snapshot`, `m_trampoline` |
| Constants | `k_` prefix, snake_case | `k_max_lagcomp_records`, `k_invalid` |
| Types, methods | `PascalCase` | `class Tracing`, `GetViewAngles()` |
| Locals, parameters, fields | `snake_case` | `target_entity`, `skip_entity` |
| Namespaces | lowercase, nested with `::` | `features::combat`, `hooking` |
| Files | lowercase `snake_case` | `shared.cpp`, `hitboxes.cpp` |

Older files mix in unscoped `constexpr` members and lowercase enum constants
(`level::info`, `weather_type::snow`); match the surrounding file rather than
renaming widely.

### 5.3 Mandatory invariants

- **No comments** in new or edited code.
- **Header/source split**: `.hpp` declares, `.cpp` implements. There are no
  `.cppm` module units and no `module`/`import` declarations anywhere.
- **Implementation units include what they use.** Start a `.cpp` with the
  headers it needs, ordered: Windows/system headers, C++ standard headers,
  `<external/...>`, project headers (`<core/...>`, `<utilities/...>`,
  `<signatures/...>`), then local `"..."` includes. Never add a precompiled
  header include; `platform/platform.h` no longer exists.
- **Header guards**: use `#pragma once` by default. Only the textual
  signature boundary headers keep include guards (`signatures/patterns.hpp`,
  `signatures/game_addresses.hpp`); vendored headers keep their upstream
  guards.
- **`xs()` for strings**: user-visible and log strings go through `xs()`.
  It is currently a pass-through macro in `logging/logging.hpp`, kept so call
  sites stay uniform. Engine API names and Windows ABI declarations keep
  their exact required spelling.
- **`#if defined(DEV)`** around all development-only diagnostics, crash
  capture, teardown and debug output, so `Release` builds stay clean.
- **ABI mirrors need `static_assert`**: any struct that mirrors an engine
  layout must assert its offsets and size (`static_assert(offsetof(...) == …)`,
  `static_assert(sizeof(...) == …)`), as `systems::tracing` and
  `interfaces.hpp` do.
- **Schemas use the macro**: resolve schema fields with
  `SCHEMA(class_name, field_hash)` from `core/systems/systems.hpp`, never with
  hardcoded offsets.
- **Addresses use signatures**: declare and define a signature in
  `signatures/patterns.{hpp,cpp}` with `SIGNATURE(...)`, and obtain it with
  `PATTERN(patterns::name)`. Interface pointers use `INTERFACE_`, modules use
  `MODULE_BASE`/`MODULE_EXPORT`, convars use `CONVAR`.
- **Hooks use the wrapper**: add an inline `hooking::jmp` member, a detour
  declaration and an entry in `hooking::manager::create`; reset every hook in
  the matching `shutdown`. Never patch bytes by hand or use raw MinHook calls.
- **Unity is disabled**: translation units are separate. Keep
  anonymous-namespace helpers unique per file anyway.
- **`core/settings.hpp` stays monolithic**: all feature settings live there.
  Do not split it.
- **`core/features/features.hpp` owns the instances**: every feature is one
  `inline g_<feature>` in the matching namespace.

### 5.4 Vendored code

`project/external/` is third-party code that keeps its upstream style and
filenames (`xdraw`, `xui`, `nlohmann`, `lz4`, `minhook`, `bc7`, `config.hpp`,
and xdraw's bundled FreeType, nanosvg and stb headers). Never restyle it. Only
apply minimal fixes when a build or ABI requirement forces it.

## 6. Settings and config

`core/settings.hpp` holds every user-facing option. Each feature owns a nested
struct with two kinds of members:

- `xui::setting{value, bind, "display name", "category"}` — toggles and
  keybinds, rendered by the menu automatically. These register themselves
  from `xui::binds::all()` inside `config::initialize()`.
- `config::val<T>`, `config::col`, `config::vec3`, `config::enm<T>`,
  `config::bools<N>`, `config::str` — persisted values. Register them with
  `reg(category, name)` from the owning struct's `init()` method, or pass
  `category, name` in the constructor.

Adding a setting therefore means: declare it in the feature's struct, make
sure it is registered (constructor or `init()`/`reg()`), and make sure the
category string matches the menu tab/subtab that should display it. Binds live
in `xui::setting` and are resolved by `settings::finalize_binds()` at startup
(which also enforces mutually exclusive binds such as manual left/right).

Configs are local `.cfg` JSON files under `C:\velocity`
(`external/config.hpp`). Nothing is uploaded anywhere.

`config::initialize()` runs before features; `core/settings.hpp` depends on
`external/config.hpp` and `external/xdraw/xui/xui.hpp`.

## 7. Adding a feature

1. **Settings**: add the option to the matching struct in
   `core/settings.hpp`, register persisted values with `reg()`/the category
   constructor, and pick the category the menu should show it under.
2. **State**: give the feature a class in the matching
   `core/features/<area>/<area>.hpp` and expose one `inline g_<feature>` in
   `core/features/features.hpp` inside the matching namespace.
3. **Implementation**: put the logic in
   `core/features/<area>/impl/<feature>.cpp` and add the file to the
   `<ClCompile>` group in `cs2/velocity.vcxproj`.
4. **Engine access**: read engine state through `core/systems/` accessors.
   Resolve schemas with `SCHEMA(class_name, field_hash)`, never with
   hardcoded offsets.
5. **Hooks**: add the inline `hooking::jmp` member and detour declaration to
   `core/hooks/hooks.hpp`, implement it in `core/hooks/impl/`, add the
   `hooking::manager::create` entry (with `PATTERN(...)`), and reset it in the
   matching `shutdown`.
6. **Menu**: wire the tab/subtab in `core/rendering/impl/menu/` if the feature
   needs custom UI; plain settings appear automatically. Tabs are
   `ragebot, legitbot, player, world, skins, misc, config`; subtab names and
   icons are defined in `rendering::menu::k_subtab_defs`.
7. **Build**: add every new `.cpp` to `velocity.vcxproj`; sources are not
   globbed.
8. **Verify**: work through the checklist in §8 and keep `README.md` in sync
   if build steps or conventions changed.

## 8. Verification (every change)

No build happens on this machine, so verification is explicit and manual:

1. Re-read every changed file completely, including the untouched
   surroundings, and check the change is consistent with it.
2. Check wiring: every new `.cpp` is in `velocity.vcxproj`; every referenced
   header is included; the feature has its `inline g_*` in `features.hpp`;
   every new hook is declared, registered and reset; no module/`platform.h`
   leftovers were introduced.
3. Check thread affinity of any new hook, callback or worker code against
   §3.2.
4. Verify every engine assumption (address, layout, schema, vtable slot)
   against a real source before trusting it, and re-check ABI structs with
   `static_assert` against the current binaries.
5. Run `clang-format` on every touched file and keep the diff clean.
6. Use `clang -fsyntax-only` as a rough check when it is feasible; it
   generally cannot resolve the MSVC/Windows headers, so treat MSVC as
   authoritative.
7. Run `clang-tidy` where useful; it is advisory. Its naming options are
   partly stale (they expect a trailing underscore member suffix and
   `platform`/`protection` paths) — follow the existing code, not a
   conflicting tidy suggestion.
8. Cross-check anything version-sensitive against `cspatterns.dev`,
   `cs2-sdk.com` and `GameTracking-CS2`.

## 9. Git workflow

- Conventional commits with a scope: `feat(combat): …`, `fix(esp): …`,
  `chore(build): …`, `docs: …`.
- Feature branches named `type/short-name` (`feat/ragebot-resolver`).
- The agent owns the full branch flow when asked to deliver a change:
  branch → commit → push → merge to `main` → delete the branch.
- Commits happen only on explicit request from the maintainer. A commit
  request includes the push.
- Keep the working tree clean: no stray files, no unrelated edits in the
  same commit.
- Never rewrite published history and never force-push `main` unless the
  maintainer explicitly asks for a force push. Never commit secrets or local
  analysis artifacts.

## 10. Game update procedure

When the maintainer announces a CS2 update and replaces the contents of
`analysis/CS2-latest/` with the new binaries and IDA databases:

1. Update the `GameTracking-CS2` clone inside `analysis/` if it is present
   (pull the latest commit).
2. Check `cspatterns.dev` and `cs2-sdk.com` for changed patterns, structures
   and schema fields.
3. Verify **every** signature in `cs2/project/signatures/patterns.cpp`
   (declared in `patterns.hpp`) against the new binaries using the IDA
   databases in `analysis/CS2-latest/`. Update patterns that no longer match.
4. Re-check the schema fields used by `SCHEMA(...)` lookups — schema hashes
   can change as well.
5. Re-verify the ABI mirrors in `core/systems/systems.hpp`,
   `utilities/addresses/interfaces.hpp` and `core/systems/impl/` (and
   anywhere else) against the new builds and update the `static_assert`s.
6. Re-check hook targets, vtable slots and interface pointers that are not
   pattern-based, including `utilities/addresses/impl/`,
   `signatures/game_addresses.hpp` and `core/hooks/hooks.hpp`.
7. Report what changed and what still needs the maintainer's attention.

## 11. Research sources

| Source | Use |
| --- | --- |
| `analysis/CS2-latest/*.i64` | IDA databases of the current CS2 binaries — primary source for addresses, layouts, strings, xrefs (open with the IDA MCP server; renames and saves are allowed) |
| `analysis/CS2-latest/*-analysis/` | generated strings, vtables and per-module JSON dumps |
| `cspatterns.dev` | current byte patterns and offsets |
| `cs2-sdk.com` | schema and structure references |
| `GameTracking-CS2` (in `analysis/`, when present) | dumped game files, schemas, resources |
| `unknowncheats.me`, `yougame.biz` | community research, leaks, new techniques — delegate to a dedicated subagent per site |
| IDA MCP server | decompile, disassemble, xref and rename without leaving the editor |
