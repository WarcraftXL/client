<div align="center">

# WarcraftXL client

**The World of Warcraft 3.3.5a client, compiled from source, with a mod runtime built in**

[![Client](https://img.shields.io/badge/client-3.3.5a%20(12340)-orange.svg)](#-what-it-is)
[![Target](https://img.shields.io/badge/target-x64-lightgrey.svg)](#-building)
[![ABI](https://img.shields.io/badge/ABI-0.1.32-blue.svg)](https://github.com/WarcraftXL/sdk)
[![Runtime](https://img.shields.io/badge/runtime-WarcraftXL%20core-blue.svg)](https://github.com/WarcraftXL/core)

</div>

---

> [!IMPORTANT]
> Building and running this client requires an original World of Warcraft 3.3.5a (build 12340)
> installation. No game data, no game binary and no part of either is in this repository; see
> [NOTICE.md](NOTICE.md) for what that means for the source itself.

## 🚀 Overview

A 64-bit `WowClient64.exe` that plays like the original and loads mods written against the
[WarcraftXL SDK](https://github.com/WarcraftXL/sdk). It is a fork of
[wowdecomp](https://github.com/SatyPardus/wowdecomp), the C++ decompilation of `Wow.exe`, with
[WarcraftXL core](https://github.com/WarcraftXL/core) linked in: the same runtime that loads into
the stock 32-bit client by detour is called here by the client's own source.

### Key features

- **Mods, without addresses.** The runtime is told where the client is by calls the client makes,
  not by hooks into a shipped binary. A mod built for `win-x64` from the SDK's template runs here
  unchanged; the same source built for `win-x86` runs on the stock client.
- **Seams, declared once.** Every function the runtime attaches to is one row in core's seam
  table; this client calls that row's `Before` at the top of the function and `After` where it
  returns. What the seam means (the events it raises) is written once, in core, for both clients.
- **Early.** The runtime comes up from `main`, before the client reads its paths, its archives or
  its settings, so a mod's `WXL_STAGE_EARLY` runs before anything of the game.
- **One build.** `cmake --build` produces the executable and the `WarcraftXL.dll` it links, from
  the core checkout next door, into the same folder.

## 📋 Table of contents

- [Overview](#-overview)
- [Building](#-building)
- [Installing](#-installing)
- [What it is](#-what-it-is)
- [Adding a seam](#-adding-a-seam)
- [Layout](#-layout)
- [Troubleshooting](#-troubleshooting)
- [Contributors](#-contributors)
- [License](#-license)

## ⚡ Building

### Prerequisites

- Visual Studio 2022 or newer with the *Desktop development with C++* workload. Visual Studio
  2026 (MSVC 14.51) builds it as is; the upstream README's request for 2022 is a minimum, not a
  requirement.
- CMake 3.21 or newer (the one that ships with Visual Studio works).
- Checkouts of [`WarcraftXL/core`](https://github.com/WarcraftXL/core) and
  [`WarcraftXL/sdk`](https://github.com/WarcraftXL/sdk) beside this one:

  ```
  WarcraftXL/
      client/     this repository
      core/       the runtime, built by this tree as win-x64
      sdk/        the contract, headers only
  ```

  Anywhere else: pass `-DWXL_CORE_DIR=<path>` and `-DWXL_SDK_DIR=<path>` at configure time.

### x64

```sh
cmake -S . -B build/x64 -G "Visual Studio 18 2026" -A x64
cmake --build build/x64 --config Release --target WowClient --parallel
```

The client and its runtime are written to `bin/Win/`:

| File | What it is |
|---|---|
| `WowClient64.exe` | the client, with the crash logger (`WOWCLIENT_CRASH_LOG`, on by default) |
| `Wow64.exe` | the client without it (`--target Wow`) |
| `WarcraftXL.dll` | the runtime, built from `../core` as `win-x64` |
| `XvidDecoder.dll` | the cinematic decoder |
| `Mods/wxl.lua/` | the runtime of the Lua mods, built from `../sdk/lua`: a Lua mod needs nothing else |

The first configure downloads LuaJIT, which `wxl.lua` embeds, at a fixed commit; core's README
says how to give it one already built instead.

The decompiled sources carry around a thousand warning sites of their own (narrowing conversions,
enumerator values, unreferenced parameters); the fork does not try to silence them. The code under
`WarcraftXL/` is held to `/W4 /permissive-` and compiles clean.

### Win32

The Win32 configuration exists so that struct layouts can be measured against the original binary
(`core/tools/layouts/`). It compiles but **does not link at this fork point**: the Win64 sources
`BotCheck.cpp`, `Battlenet.cpp`, `BattlenetLogin.cpp` and `PatchDownloadGlue.cpp` have no Win32
counterpart, which leaves 8 unresolved symbols. The upstream README's claim that Win32 still builds
is stale. Configuring it also needs NASM (`-DNASM_EXECUTABLE=<path>`). No runtime is linked on
Win32; the seam calls compile to nothing there.

## 📦 Installing

1. Copy the `Data` folder of your 3.3.5a client into `bin/Win/`, so that `bin/Win/Data/` holds
   the `.MPQ` archives, or copy `WowClient64.exe`, `WarcraftXL.dll`, `XvidDecoder.dll` and the
   `Mods/wxl.lua/` folder into a folder that already has one.
2. Point `Data/<locale>/realmlist.wtf` at the server you want to connect to.
3. Copy the mods you want into `Mods/` next to the executable, each as its own folder with a
   `mod.json` and `bin/x64/<name>.dll`. The SDK's template builds exactly that shape. A Lua mod is
   a folder with its `init.lua`, run by `Mods/wxl.lua/`.
4. Start `WowClient64.exe`. The runtime's log is `Logs/WarcraftXL.log` next to it; the client's own
   logs are in the same folder.

D3D9 is the default device. `SET gxApi "Vulkan"` in `WTF/Config.wtf`, `-vulkan` on the command line
or `[gx] api = vulkan` in `WarcraftXL.ini` (which also takes a D3D9Ex request) choose the Vulkan
device, `Engine/Source/Gx/CGxDeviceVk`, an adapter over the runtime's renderer. The window, its
fullscreen (borderless), resizes and minimising, the frame, the world and the game's interface,
the textures the engine draws into (the minimap, portraits, full-screen effects), Dear ImGui and the
pointer go through Vulkan (the software pointer drawn into the frame as on D3D9, so screenshots
show it); the client's shaders are their records translated by the runtime from
DX9 bytecode, once per machine. Screenshots and the portraits read the frame back as on D3D9;
`gxMultisample` multisamples it, the video options offering the counts the Vulkan device has, and
the draws the engine keeps unsmoothed (the quest areas on the world map) stay hard-edged where the
device has sample locations. A
machine without Vulkan 1.3 stays on D3D9, and the log says why. Two console commands serve the renderer's tests: `gxVkSimulateLoss` (the Vulkan device lost and
made again) and `gxCapture` (the next frame of the D3D9 device recorded to `WarcraftXL\captures\`).

The signature checks of the original are kept (`WOWDECOMP_DISABLE_SIGNATURE_CHECKS=OFF`): modified
interface files need the option, or a mod such as core's `wxl.glue-unlock` on the stock client.

## 🔎 What it is

The client calls the runtime at three places, and nowhere else:

| Where | What |
|---|---|
| `WoW/Source/Win64/WinMain.cpp`, `main` | `WarcraftXL::Init()`: hands the runtime the client's accessors and calls `WarcraftXLInit`, before `CommonMain` |
| `WoW/Source/WorldClient/World.cpp`, `World::Preload` | `wxl::seam::World::Preload::Before` as the function starts, `::After` as it returns |
| `CMakeLists.txt` | `add_subdirectory(WarcraftXL)` |

Everything behind those lines is under [`WarcraftXL/`](WarcraftXL/): the glue that builds core,
links it and copies the DLL next to the executable, the accessor table (`wxl_core_init` from
core's `include/wxl/core/init.h`, today the map id through `World::QueryMapId`), and the adapters
the seam calls use (`WarcraftXL::Vec3`, a `C3Vector` as the `wxl_vec3` the boundary carries).

What a mod gets on this client, by the runtime's own account:

| | here (x64) | stock client (x86) |
|---|---|---|
| events | `WorldEnter`, `WorldLeave` | the same |
| `wxl.hook` by name | not published: a seam is two calls inside a function the client compiled, not a function a detour can replace | the seams and the extra points, detourable |
| `wxl.hook.raw`, `wxl.patch` | never: no fixed address and nothing to patch in a program we compile | yes |

`World::Preload` is called with the loading screen for a login and a teleport, and without it for
the preloads around a cinematic camera, a far-sight target or a game object. Only the first kind
is a world change, and only that kind raises the events.

## 🧵 Adding a seam

A seam is a client function the runtime attaches to. Adding one touches core first, then one
function here:

1. In core, add a row to the domain's table under `include/wxl/seams/` (or a new table, included
   from `seams.def`): the domain, the function's name, its qualified name as this tree spells it,
   and its parameters as the C boundary sees them. Write the body in `src/seams/<Domain>.cpp`:
   what `Before` and `After` read, decide and raise. Core's x86 build attaches a detour at the
   function's address from that row alone.
2. Here, include `WarcraftXL/Seams.h` in the function's file and call
   `wxl::seam::<Domain>::<Name>::Before(...)` as the function starts and `::After(...)` where it
   returns, with the same arguments. Those are the only lines the function gains.

The name is the function's, never the event's: `World.Preload`, not `World.Enter`. Where and what
are two different columns.

## 📂 Layout

```
WarcraftXL/              the integration: CMake, Init.h/.cpp (WarcraftXLInit), Seams.h (adapters),
                         GxVulkan.h (the runtime's Vulkan renderer, for ConsoleDevice and CGxDeviceVk),
                         GxCapture.h/.cpp (a frame of the D3D9 device recorded, gxCapture)
WoW/                     the game: objects, world, interface, networking, database, glue screens
Engine/                  the engine: storage, graphics, sound, scripting, frames
Tools/                   WowClient (the executable with the crash logger), the Xvid decoder
ThirdParty/              vendored libraries, each under its own license
README.upstream.md       wowdecomp's own README, as forked
UPSTREAM.md              the fork point and how upstream is tracked
NOTICE.md                provenance and what is, and is not, licensed
```

## 🔧 Troubleshooting

| Symptom | Cause |
|---|---|
| CMake: `WXL_CORE_DIR (...) is not a WarcraftXL checkout` | core or the SDK is not beside this repository: clone them there, or pass `-DWXL_CORE_DIR` / `-DWXL_SDK_DIR` |
| The client does not start, `0xc000007b` or `0xc0000142` | `WarcraftXL.dll` next to the executable is not the 64-bit build, or is missing |
| No `Logs/WarcraftXL.log` | the runtime never came up: the executable was started from a folder without write access, or is not this build |
| A mod is missing from the log | wrong folder shape; it needs `Mods/<id>/mod.json` and `Mods/<id>/bin/x64/<name>.dll` |
| `refused … requires service wxl.hook.raw` or `wxl.patch` | the mod is written for the stock client's addresses; it has nothing to attach to here |
| `refused … requires service wxl.hook` | the mod wants to detour a named point; here they are seams, so subscribe to the events they raise instead |
| Win32: 8 unresolved externals | expected at this fork point, see [Win32](#win32) |

## 🤝 Contributors

Upstream's contributors are credited in [UPSTREAM.md](UPSTREAM.md). Tyrallis brought the model and
appearance seams, skins over 65535 indices, the optional geometry's geoset ids and textures over
4 MB (pull request #1), then models of the world, bone watchers, the additive blend of later
models, retail animation ids, Ogg Vorbis sounds and the geosets and eyes of later bodies (pull
request #4).

## 📄 License

This tree is a decompilation and carries no license; [NOTICE.md](NOTICE.md) says why and what that
implies. The runtime it links is [WarcraftXL core](https://github.com/WarcraftXL/core), MIT; the
contract mods are written against is [WarcraftXL sdk](https://github.com/WarcraftXL/sdk), MIT.

<div align="center">

[⬆ Back to top](#warcraftxl-client)

</div>
