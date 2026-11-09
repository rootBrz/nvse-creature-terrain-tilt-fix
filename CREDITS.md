# Credits

## Project

**Creature Terrain Tilt Fix contributors**
- Investigation of the distant creature-orientation defect.
- NVSE plugin, support-height refresh, local detour implementation, and build/package files.

## References and upstream projects

**NVSE team**
- NVSE plugin interfaces and the `NVSEInterface`/`PluginInfo` ABI used by this project.
- Only the declarations required by this plugin are included.

**WallSoGB / Fallout-Real-Time-Menus (RTM)**
- `HookUtils::CallDetour` and the SafeWrite memory-patching implementation used by this plugin.
- Upstream: <https://github.com/WallSoGB/Fallout-Real-Time-Menus>
- RTM is distributed under GPL-3.0. Its `shared/SafeWrite/SafeWrite.hpp` and `SafeWrite.cpp` are vendored here with that license and are compiled into the plugin.

**momusdigital / fnv_decomp**
- Decompiled Fallout: New Vegas engine source used to identify the movement and support-height behavior.
- Upstream: <https://github.com/momusdigital/fnv_decomp>
- No decompiled source code is included.

**Obsidian Entertainment / Bethesda Softworks**
- Fallout: New Vegas and its engine behavior.
- No game assets or executable code are included.

## Tooling

Builds use Clang/LLVM or an MSVC-compatible C++ compiler and MinGW or Windows SDK import libraries. Those tools are not redistributed here.
