# Creature Terrain Tilt Fix

NVSE plugin for Fallout: New Vegas 1.4.0.525. It fixes stale support-height data in the distant `MobileObject::Move` path used by horizontal/probe character controllers. The fix is not specific to ants or any other creature type.

## What it changes

At `0x0092FBCD`, the plugin wraps the existing `bhkCharacterController_InitFrontBack` call. For probe controllers it resets the four cached support heights to `FLT_MIN`, forcing `CalculatePitchAndRoll` to sample the current terrain, then calls the preserved target.

RTM’s `HookUtils::CallDetour` preserves the existing target so another detour installed earlier can run afterward. A plugin installed later must likewise preserve the current target; compatibility cannot be guaranteed with a later plugin that blindly overwrites the call.

The plugin does not change actor-fade behavior or character-proxy positioning. Those extra operations were removed after a crash report. Non-probe controllers retain the stock update.

The plugin validates that the site still contains a call instruction before patching and has no C++ runtime dependency. It imports `KERNEL32`, `USER32` (an error message box if the hook site does not match) and the C runtime: `msvcrt.dll` in the CI-built release, `api-ms-win-crt-*` (UCRT) in a build with a UCRT MinGW toolchain.

## Repository contents

- `main.cpp`: NVSE entry points and support-height hook.
- `shared/SafeWrite/`: RTM’s `CallDetour` and memory-patching support, vendored under GPL-3.0.
- `nvse/PluginAPI.h`: NVSE ABI declarations used by the plugin.
- `build.sh`: standalone Clang/MinGW build.
- `CMakeLists.txt`: standalone MSVC-compatible build.
- `build/CreatureTerrainTiltFix.dll`: prebuilt Win32 plugin.
- `LICENSE`: GNU General Public License 3.0.
- `NOTICE` and `CREDITS.md`: ownership and attribution.

`shared/SafeWrite/SafeWrite.hpp` and `SafeWrite.cpp` are included from `Fallout-Real-Time-Menus` under GPL-3.0.

## Build

On a Linux environment with Clang and 32-bit MinGW:

```sh
./build.sh
```

Override `CXX`, `MINGW_ROOT`, or `MINGW_GCC_LIB` if your installation differs.

For an MSVC Win32 environment with CMake:

```sh
cmake -S . -B build
cmake --build build --config Release
```

The output is `build/CreatureTerrainTiltFix.dll`. Install it as `Data/NVSE/Plugins/CreatureTerrainTiltFix.dll` in the game directory.

## Scope and cost

The fix refreshes four terrain support samples whenever the stock distant probe update runs. Other movement and fade behavior is unchanged.

Runtime behavior should be validated with ants moving across slopes at varying distances and with any other plugins that hook the same call site. The supplied executable was used to verify the hook address and calling convention; the emitted hook assembly was also checked.

## Investigation

See [FINDINGS.md](FINDINGS.md) for the complete source-level investigation, rejected arrow hypothesis, crash history, remaining limitations, and runtime validation plan.

## License

Copyright (C) 2026 Creature Terrain Tilt Fix contributors.

Distributed under **GNU GPL-3.0**. See `LICENSE`, `NOTICE`, and `CREDITS.md`.
