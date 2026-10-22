# AGENTS.md — Velocity (CS2)

Repository-wide rules for AI agents working on this codebase. This file
overrides the global agent configuration (`~/.config/opencode/AGENTS.md`)
wherever the two conflict.

## 1. Project overview

Velocity is a CS2 game modification that runs as an injected DLL inside the
game process. It is written in C++20 and built with MSVC on Windows only.

The repository is public, but contributions come from a single maintainer.

Layout:

```text
velocity/
├── AGENTS.md                  # this file
├── README.md                  # build guide + contributing rules
├── .clang-format              # Google base + include ordering for PCH
├── .clang-tidy                # tidy checks (advisory)
├── .editorconfig              # 2-space indent, LF, 80 cols via clang-format
├── analysis/                  # LOCAL ONLY, git-ignored: binaries, .i64 DBs, dumps
└── cs2/
    ├── velocity-cs2.slnx
    └── velocity-cs2/
        ├── velocity-cs2.vcxproj
        ├── vcpkg.json          # freetype
        └── project/            # all source
```

`analysis/` is local scratch space and is not tracked by git. It holds the
current CS2 binaries with their IDA databases (`analysis/CS2-latest/`), the
`GameTracking-CS2` clone, and the analysis tooling.

## 2. Hard rules

1. **Never assume. Verify everything.** Before writing a single line of code,
   confirm every engine-facing fact (function address, struct layout, schema
   field, vtable slot, hook target) against a real source: the IDA databases
   in `analysis/CS2-latest/`, `cspatterns.dev`, `cs2-sdk.com`, the
   `GameTracking-CS2` clone, or CS2 binaries directly. If a fact cannot be
   verified, find a way to verify it or ask the maintainer — never guess.
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
12. **Uncertainty protocol.** Research first (IDA, the sites above, the
    sources in this repo), and only then ask the maintainer. Come back with
    findings, not with open questions you could have answered yourself.

## 3. Architecture map

All source lives under `cs2/velocity-cs2/project/`.

```text
project/
├── entry.cc                  # DLL entry point + staged initialization thread
├── pch/                      # precompiled header (pch.h / pch.cc)
├── core/
│   ├── features.h            # global instances of every feature (g_*)
│   ├── settings.h            # all user-facing settings + config registration
│   ├── features/
│   │   ├── changer/          # skinchanger: weapons, knives, gloves, agents,
│   │   │                     #   stickers, keychains (econ item system)
│   │   ├── combat/           # ragebot, legitbot, shared targeting, misc
│   │   ├── esp/              # player / item / projectile / other overlays,
│   │   │                     #   chams, glow
│   │   ├── misc/             # camera, hud, impacts, removals, dlight,
│   │   │                     #   trajectory, scoreboard weapons, force server
│   │   ├── movement/         # bhop, airstrafe, edgebug/edgejump/edgestop,
│   │   │                     #   fastladder, jumpbug, slowwalk, test strafer
│   │   └── world/            # weather, scene (skyboxes), smoke
│   ├── systems/              # engine state access layer
│   │   ├── schemas.cc        # runtime schema field lookup (SCHEMA macro)
│   │   ├── entities.cc       # entity cache (add/remove, classification)
│   │   ├── local.cc          # local player/controller snapshot
│   │   ├── input.cc          # usercmd creation, subtick edits, angle writes
│   │   ├── legit_input.cc    # mouse-delta style input manipulation
│   │   ├── prediction.cc     # movement prestate capture
│   │   ├── view.cc           # world-to-screen projection, camera state
│   │   ├── bones.cc          # bone cache access
│   │   ├── bounds.cc         # entity bounding boxes
│   │   ├── hitboxes.cc       # hitbox set query, hitgroup mapping
│   │   ├── tracing.cc        # ray/hull/sphere traces, engine ABI structs
│   │   ├── frame_data.cc     # per-frame origin capture
│   │   ├── materials.cc      # material creation/cloning for chams
│   │   ├── events.cc         # game event listeners
│   │   ├── icons.cc          # VPK/SVG icon extraction to D3D11 textures
│   │   └── preview/          # in-game model preview (panorama based)
│   ├── hooks/                # inline JMP hooks
│   │   ├── cheat.cc          # gameplay/render/input hooks (Present, CreateMove,
│   │   │                     #   FrameStageNotify, glow, smoke, decals, …)
│   │   └── utility.cc        # service read + internal logging hooks
│   └── rendering/            # D3D11 overlay layer
│       ├── context.cc        # device/context/RTV from the swap chain
│       ├── fonts.cc          # Inter / Smallest Pixel 7 via FreeType
│       ├── widgets.cc         # watermark, keybind display
│       └── menu/             # in-game menu (tabs: ragebot, legitbot, player,
│                             #   world, skins, misc, config)
├── utilities/
│   ├── addresses/            # module/global/function address + interface lookup
│   ├── hooking/              # Jmp hook primitives + manager
│   ├── security/             # integrity checks, protected regions, prologues
│   ├── logging/              # console + popup logging
│   ├── memory/               # safe reads/writes, module/export lookup, RTTI
│   ├── math/                 # vector/quaternion/matrix helpers
│   ├── proto/                # protobuf usercmd structures
│   ├── steam/                # Steamworks wrappers (Http, Friends, User, Utils)
│   ├── threadpool/           # worker threads for background work
│   ├── diag.h                # early diagnostics, crash dumps, SEH handling
│   ├── overrides.cc          # global new/delete routed to the game allocator
│   ├── cstypes.h             # game type/constant tables
│   ├── animation.h           # UI tween/easing helpers (xdraw-based)
│   ├── fnv1a.h               # hashing
│   └── game_path.h           # CS2 install path resolution
├── protection/               # byte-signature layer: patterns.cc + address
│                             #   resolution; future home of evasion work
└── external/                 # vendored third-party code (see §5.4)
```

### 3.1 Initialization sequence

`entry.cc` handles `DLL_PROCESS_ATTACH`, then spawns a dedicated init thread
and never blocks the loader lock. The init thread stages, in order:

1. crash handlers (vectored + unhandled exception filters, `DEV` only)
2. `CoInitializeEx` (multithreaded)
3. `config::initialize()` + `settings::FinalizeBinds()`
4. `security::regions::AddModule()` — protect our own module pages
5. logging (console + popup)
6. integrity checks, thread pool, Steam API, econ item system
7. address resolution (`addresses::modules/globals/functions`)
8. systems (`Materials`, `Events`, `Icons`)
9. skinchanger runtime
10. hooks (`hooks::Utility`, `hooks::Cheat`)
11. hidden cvar unlock, skybox discovery

`DEV` builds treat init failures as warnings and keep going; `Ship` builds
fail hard with a message box. Shutdown order mirrors init (hooks and
features torn down before systems).

### 3.2 Threading

| Context | Where it runs | What belongs there |
| --- | --- | --- |
| Init thread | `entry.cc` | one-time setup, address resolution, hook installation |
| Render thread | `hooks::Cheat::Present` / `ResizeBuffers` | drawing, menu, overlays, chams, glow, text |
| Game thread | `hooks::Cheat::CreateMove`, `FrameStageNotify`, `ProcessInputEvent` | usercmd edits, view angles, movement, prediction |
| Worker threads | `utilities/threadpool` | background work only; never touch engine state without a snapshot |
| Engine threads | `events`, `entity add/remove` callbacks | update caches, keep handlers short |

Rules:

- Engine state read from the render thread must go through the `systems::`
  accessors (they cache snapshots), never by re-reading raw pointers.
- Anything that mutates the user command belongs on the game thread.
- Shared state needs the existing `std::mutex` / `std::shared_mutex` /
  `std::atomic` discipline; never hold a lock across a game call.
- Never block the render thread — the game visibly stutters.

## 4. Build

Authoritative build instructions live in `README.md`. Both configurations
target C++20, x64, AVX2 (`AdvancedVectorExtensions2`), static CRT
(`MultiThreaded`) and are built from a Developer PowerShell for Visual
Studio:

```powershell
msbuild cs2\velocity-cs2\velocity-cs2.vcxproj /m /p:Configuration=Ship /p:Platform=x64
msbuild cs2\velocity-cs2\velocity-cs2.vcxproj /m /p:Configuration=Development /p:Platform=x64
```

| Configuration | Defines | Behavior | Output |
| --- | --- | --- | --- |
| `Development` | `DEV` | extra diagnostics, crash capture hooks (minidump, `TerminateProcess`), init failures are warnings | `cs2/bin/velocity-cs2-dev.dll` |
| `Ship` | `NDEBUG` | release path, init failures are fatal (message box), whole-program optimization | `cs2/bin/cs2.dll` |

Agents cannot build here. When you need a rough sanity check,
`clang -fsyntax-only` is acceptable as an approximation (MSVC stays
authoritative). `clang-format` and `clang-tidy` are available locally; format
is mandatory, tidy is advisory.

Build files (`velocity-cs2.vcxproj`, `vcpkg.json`, `.slnx`) are editable when
a change genuinely requires it — but MSBuild lists sources explicitly, so
every new `.cc` / `.h` must be registered as `ClCompile` / `ClInclude`.

## 5. Code style

### 5.1 Source of truth

The existing code wins. Match the file you are editing: its naming, layout,
comment density (none), include order and idioms. Google C++ Style is the
fallback for anything the local code does not settle — lowercase
`snake_case` filenames, `.cc` sources, `.h` headers, `VELOCITY_..._H_`
path-based header guards, quoted includes relative to `project/`.

### 5.2 Naming

| Kind | Convention | Example |
| --- | --- | --- |
| Global state | `g_` prefix | `g_local`, `g_menu`, `g_context` |
| Members | `m_` prefix, trailing `_` | `m_snapshot_`, `m_trampoline_` |
| Constants / enum values | `k` prefix | `kWeaponGroupCount`, `kInvalid` |
| Types, methods | `PascalCase` | `class Tracing`, `GetViewAngles()` |
| Locals, parameters, fields | `snake_case` | `target_entity`, `skip_entity` |
| Files | lowercase `snake_case` | `shared.cc`, `hitboxes.cc` |

### 5.3 Mandatory invariants

- **No comments** in new or edited code.
- **PCH first**: every translation unit that uses the precompiled header
  includes `"pch/pch.h"` as its first include. New `.cc` files use the
  project PCH by default.
- **`xs()` for strings**: user-visible and engine-facing strings go through
  `xs()` (xorstr) so they are not stored in plaintext. Engine API names and
  Windows ABI declarations keep their exact required spelling.
- **`#if defined(DEV)`** around all development-only diagnostics, crash
  capture and debug output, so `Ship` builds stay clean.
- **ABI mirrors need `static_assert`**: any struct that mirrors an engine
  layout must assert its offsets and size (`static_assert(offsetof(...) == …)`,
  `static_assert(sizeof(...) == …)`), as `systems::Tracing` does.
- **Header guards**: `VELOCITY_` + path with `/` → `_` + `_H_`, e.g.
  `VELOCITY_CORE_SYSTEMS_SYSTEMS_H_`.
- **Unity build is enabled** (`EnableUnitySupport=true`): translation units
  are merged, so file-static and anonymous-namespace symbols must be unique
  across the whole project. Do not rely on same-named statics in different
  files being separate.
- **`settings.h` stays monolithic**: all feature settings live in
  `core/settings.h`. Do not split it.
- **`.clang-format` include order**: `"pch/pch.h"` first, then
  `<external/phnt/phnt_windows.h>`, then other angle includes, then project
  includes.

### 5.4 Vendored code

`project/external/` is third-party code that keeps its upstream style and
filenames (`xdraw`, `nlohmann`, `phnt`, `zydis`, `lz4`, `inline-syscall`,
`stackwalker`, `xorstr`, `poly2d`, `bc7`, `config.hpp`). Never restyle it.
Only apply minimal fixes when a build or ABI requirement forces it.

## 6. Settings and config

`core/settings.h` holds every user-facing option. Each feature owns a
nested struct with two kinds of members:

- `xui::setting{value, bind, "display name", "category"}` — toggles and
  keybinds, rendered by the menu automatically.
- `config::val<T>`, `config::col`, `config::bools<N>`, `config::enm<T>` —
  values that are registered for persistence through `reg(category, name)`
  inside the owning struct's `Init()` method.

Adding a setting therefore means: declare it in the feature's struct, call
`reg(...)` in that struct's `Init()`, and make sure the category string
matches the menu tab that should display it. Binds live in `xui::setting`
and are resolved by `settings::FinalizeBinds()` at startup.

Configs are stored as local JSON files. Nothing is uploaded anywhere.

`config::initialize()` runs before features; `settings.h` depends on
`external/config.hpp` and `external/xdraw/xui/xui.hpp`.

## 7. Adding a feature

1. **Settings**: add the option to the matching struct in `core/settings.h`,
   register it with `reg()` in `Init()`, and pick the category the menu
   should show it under.
2. **State**: give the feature a struct in `core/features/<area>/` and
   expose one global instance `g_<feature>` in `core/features/features.h`
   (inside the matching namespace).
3. **Implementation**: put the logic in `core/features/<area>/impl/<feature>.cc`,
   including `"pch/pch.h"` first.
4. **Engine access**: read engine state through `core/systems/` accessors.
   Resolve schemas with the `SCHEMA(class_name, field_hash)` macro, never
   with hardcoded offsets.
5. **Hooks**: if the feature needs engine callbacks, add the detour to
   `core/hooks/hooks.h` / `core/hooks/impl/`, register it in the matching
   `Initialize()`/`Shutdown()` pair, and use `hooking::Jmp` (never raw
   patching).
6. **Menu**: wire the tab/subtab in `core/rendering/impl/menu/` if the
   feature needs custom UI; plain settings appear automatically.
7. **Build**: register the new files in `velocity-cs2.vcxproj`.
8. **Verify**: work through the checklist in §8 and keep `README.md` in sync
   if build steps or conventions changed.

## 8. Verification (every change)

No build happens on this machine, so verification is explicit and manual:

1. Re-read every changed file completely, including the untouched
   surroundings, and check the change is consistent with it.
2. Check thread affinity of any new hook, callback or worker code against
   §3.2.
3. Verify every engine assumption (address, layout, schema, vtable slot)
   against a real source before trusting it, and re-check ABI structs with
   `static_assert` against the current binaries.
4. Run `clang-format` on every touched file and keep the diff clean.
5. Use `clang -fsyntax-only` as a rough check when it is feasible; treat
   MSVC as authoritative.
6. Run `clang-tidy` where useful; it is advisory, but never ignore a real
   finding.
7. Cross-check anything version-sensitive against `cspatterns.dev`,
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
- Never rewrite published history, never force-push `main`, never commit
   secrets or local analysis artifacts.

## 10. Game update procedure

When the maintainer announces a CS2 update and replaces the contents of
`analysis/CS2-latest/` with the new binaries and IDA databases:

1. Update the `GameTracking-CS2` clone inside `analysis/` (pull the latest
   commit).
2. Check `cspatterns.dev` and `cs2-sdk.com` for changed patterns, structures
   and schema fields.
3. Verify **every** signature in `project/protection/patterns.cc` against the
   new binaries using the IDA databases in `analysis/CS2-latest/`. Update
   patterns that no longer match.
4. Re-check the schema fields used by `SCHEMA(...)` lookups — schema hashes
   can change as well.
5. Re-verify the ABI mirrors in `core/systems/` (and anywhere else) against
   the new builds and update the `static_assert`s.
6. Re-check hook targets, vtable slots and interface pointers that are not
   pattern-based.
7. Report what changed and what still needs the maintainer's attention.

## 11. Research sources

| Source | Use |
| --- | --- |
| `analysis/CS2-latest/*.i64` | IDA databases of the current CS2 binaries — primary source for addresses, layouts, strings, xrefs (open with the IDA MCP server; renames and saves are allowed) |
| `cspatterns.dev` | current byte patterns and offsets |
| `cs2-sdk.com` | schema and structure references |
| `GameTracking-CS2` (in `analysis/`) | dumped game files, schemas, resources |
| `unknowncheats.me`, `yougame.biz` | community research, leaks, new techniques — delegate to a dedicated subagent per site |
| IDA MCP server | decompile, disassemble, xref and rename without leaving the editor |
