# Creature Terrain Tilt Fix

NVSE plugin for Fallout: New Vegas 1.4.0.525. It refreshes creature terrain alignment in the distant movement path and keeps corpses from sinking into the terrain. The name is historical; the corpse changes were added in 2.0.

It fixes stale support-height data in the distant `MobileObject::Move` path used by horizontal/probe character controllers. The fix is not specific to ants or any other creature type.

## What it changes

### Tilt

At `0x0092FBCD`, the plugin wraps the existing `bhkCharacterController_InitFrontBack` call. For probe controllers it resets the four cached support heights to `FLT_MIN`, forcing `CalculatePitchAndRoll` to sample the current terrain, then calls the preserved target.

### Corpses

A creature that dies with part of its body in the ground used to fall through the terrain, get dragged back stretched, or lie half buried and twitching. The changes below handle a corpse that is in the ground, for example on rough terrain or when loaded from a save.

**Ragdoll penetration recovery is repaired.** `bhkRagdollPenetrationUtil::Update` (`0x00CA1410`) disables collision on a bone that crosses a surface, and on every bone below it in the skeleton, but pushes only the crossing bone. The rest has nothing holding it, so when the crossing bone is the root the whole corpse falls through the terrain. Four changes:

- When a penetration is detected, every bone of that ragdoll is marked, not only the crossing bone and the bones attached below it. Otherwise buried bones nearer the root keep their collision and stay trapped.
- After the stock update, every bone that has collision disabled is carried along the average contact normal at `fDesiredVel`. Motion in other directions is retained, so bullet knockback is not erased. This runs for other actors only, from the detour at `0x009782DA`; the player's own ragdoll keeps the stock marking and push.
- For a dying or dead NPC the detection window is restarted on every update while Havok reports its ragdoll awake (`hkpEntity::isActive`, `0x00C9BFF0`). A corpse that is grabbed, pushed or still recovering therefore keeps being checked, and a corpse at rest costs nothing once the window runs out after it falls asleep.
- For living NPCs and the player, a detour on the detection call at `0x00CA175E` reports "clear" once `fDetectionUpdateTimeSec` (default 5 s) has passed, so the stock code restores collision. The detection still runs; only its result is overridden. Stock keeps going for as long as anything is crossing.

A ragdoll that starts with bones under the terrain is handled by the same recovery: a bone line crosses the surface, those bones lose collision, and all of them are carried up and out. This was confirmed in game on slopes and against a rock. Walls and other objects were not tried.

RTM’s `HookUtils::CallDetour` preserves the existing target so another detour installed earlier can run afterward. A plugin installed later must likewise preserve the current target; compatibility cannot be guaranteed with a later plugin that blindly overwrites the call.

The plugin does not change actor-fade behavior or character-proxy positioning. Those extra operations were removed after a crash report. Non-probe controllers retain the stock update.

The plugin validates all three sites before patching any of them and has no C++ runtime dependency. It writes `logs\CreatureTerrainTiltFix.log` in the game directory: either `All fixes installed.` or, for each site that did not match, its address and first six bytes, followed by `No fix installed`. It imports `KERNEL32`, `USER32` (`wvsprintfA` for the log, and an error message box if a hook site does not match) and the C runtime: `msvcrt.dll` in the CI-built release, `api-ms-win-crt-*` (UCRT) in a build with a UCRT MinGW toolchain.

## Repository contents

- `main.cpp`: NVSE entry points, support-height hook and ragdoll recovery repair.
- `shared/SafeWrite/`: RTM’s `CallDetour` and memory-patching support, vendored under GPL-3.0.
- `nvse/PluginAPI.h`: NVSE ABI declarations used by the plugin.
- `build.sh`: standalone Clang/MinGW build.
- `.github/workflows/release.yml`: builds and publishes a release when `VERSION` changes on `main`. No binary is kept in the repository.
- `CMakeLists.txt`: standalone MSVC-compatible build.
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

While a ragdoll is in recovery, the bones being carried have no collision and move along the recovery normal at `fDesiredVel` (about 1.5 m/s). For a dying or dead NPC this continues until detection is clear; living NPCs and the player stop after at most `fDetectionUpdateTimeSec`. Existing knockback is retained and can change the corpse's path. Bones can pass through anything along the way. The recovery direction comes from the first surface each detection ray reports, which is not always the nearest one. A wrong normal can therefore move a dead NPC continuously. Detection runs every frame while a dead NPC's ragdoll is awake and for `fDetectionUpdateTimeSec` after it falls asleep; a sleeping corpse is not checked, so one moved without being woken is only recovered once something wakes it. Detection ignores other ragdoll bodies, so this recovery does not separate corpse–corpse intersections.

Runtime behavior should be validated with ants moving across slopes at varying distances and with any other plugins that hook the same call site. The supplied executable was used to verify the hook address and calling convention; the emitted hook assembly was also checked.

## Investigation

See [FINDINGS.md](FINDINGS.md):

- Sections 1 to 10: the stale support heights in the distant movement path, the rejected arrow hypothesis, crash history and remaining limitations.
- Section 11: the tilt-angle divisor experiment, its measurements, and the rock regression that caused its rollback.
- Sections 12 and 13: the corpse problems, the runtime logs that identified them, and two remedies that were tried and replaced.
- Section 14: why terrain does not push a buried body out, the repaired recovery, and its risks.

## License

Copyright (C) 2026 Creature Terrain Tilt Fix contributors.

Distributed under **GNU GPL-3.0**. See `LICENSE`, `NOTICE`, and `CREDITS.md`.
