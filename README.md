# Creature Terrain Tilt Fix

NVSE plugin for Fallout: New Vegas 1.4.0.525. It makes creatures tilt correctly on slopes and keeps corpses from sinking into the terrain. The name is historical; the corpse changes were added in 1.1.

It fixes stale support-height data in the distant `MobileObject::Move` path used by horizontal/probe character controllers. The fix is not specific to ants or any other creature type.

## What it changes

### Tilt

At `0x0092FBCD`, the plugin wraps the existing `bhkCharacterController_InitFrontBack` call. For probe controllers it resets the four cached support heights to `FLT_MIN`, forcing `CalculatePitchAndRoll` to sample the current terrain, then calls the preserved target.

It also corrects the tilt angle itself. `CalculatePitchAndRoll` (`0x00C70060`) computes `atan(height difference / length)`, but `InitFrontBack` places the ground samples only `length - 2 * radius` apart. The two divisions at `0x00C70159` (pitch) and `0x00C702CB` (roll) are replaced with a call that divides by the real distance between the samples. This applies at every distance, not only far from the player.

### Corpses

A creature that dies with part of its body in the ground used to fall through the terrain, get dragged back stretched, or lie half buried and twitching. The tilt changes above remove the usual reason for dying in that pose. The changes below handle a corpse that is in the ground anyway, for example on rough terrain or when loaded from a save.

**Ragdoll penetration recovery is repaired.** `bhkRagdollPenetrationUtil::Update` (`0x00CA1410`) disables collision on a bone that crosses a surface, and on every bone below it in the skeleton, but pushes only the crossing bone. The rest has nothing holding it, so when the crossing bone is the root the whole corpse falls through the terrain. Three changes:

- When a penetration is detected, every bone of that ragdoll is marked, not only the crossing bone and the bones attached below it. Otherwise buried bones nearer the root keep their collision and stay trapped.
- After the stock update, every bone that has collision disabled is given the same velocity: `fDesiredVel` along the average contact normal. The ragdoll leaves the surface as one piece. This runs for other actors only, from the detour at `0x009782DA`; the player's own ragdoll keeps the stock push.
- A detour on the detection call at `0x00CA175E` reports "clear" once `fDetectionUpdateTimeSec` (default 5 s) has passed, so the stock code restores collision. Stock keeps going for as long as anything is crossing.

A ragdoll that starts with bones under the terrain is handled by the same recovery: a bone line crosses the surface, those bones lose collision, and all of them are carried up and out. This was confirmed in game on slopes and against a rock. Walls and other objects were not tried.

RTM’s `HookUtils::CallDetour` preserves the existing target so another detour installed earlier can run afterward. A plugin installed later must likewise preserve the current target; compatibility cannot be guaranteed with a later plugin that blindly overwrites the call.

The plugin does not change actor-fade behavior or character-proxy positioning. Those extra operations were removed after a crash report. Non-probe controllers retain the stock update.

The plugin validates all five sites before patching any of them and has no C++ runtime dependency. It imports `KERNEL32`, `USER32` (an error message box if the hook site does not match) and the C runtime: `msvcrt.dll` in the CI-built release, `api-ms-win-crt-*` (UCRT) in a build with a UCRT MinGW toolchain.

## Repository contents

- `main.cpp`: NVSE entry points, support-height hook and tilt-angle correction.
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

While a ragdoll is in recovery, the bones being carried have no collision and move in a straight line at `fDesiredVel` (about 1.5 m/s) for at most `fDetectionUpdateTimeSec`. They can pass through anything in that direction. The direction comes from the first surface each detection ray reports, which is not always the nearest one.

Runtime behavior should be validated with ants moving across slopes at varying distances and with any other plugins that hook the same call site. The supplied executable was used to verify the hook address and calling convention; the emitted hook assembly was also checked.

## Investigation

See [FINDINGS.md](FINDINGS.md):

- Sections 1 to 10: the stale support heights in the distant movement path, the rejected arrow hypothesis, crash history and remaining limitations.
- Section 11: the tilt-angle divisor, with measurements before and after.
- Sections 12 and 13: the corpse problems, the runtime logs that identified them, and two remedies that were tried and replaced.
- Section 14: why terrain does not push a buried body out, the repaired recovery, and its risks.

## License

Copyright (C) 2026 Creature Terrain Tilt Fix contributors.

Distributed under **GNU GPL-3.0**. See `LICENSE`, `NOTICE`, and `CREDITS.md`.
