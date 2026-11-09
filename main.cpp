// Creature Terrain Tilt Fix - refresh distant terrain support data.
// Copyright (C) 2026 Creature Terrain Tilt Fix contributors.
// SPDX-License-Identifier: LGPL-2.1-only

#include "nvse/PluginAPI.h"
#include "shared/SafeWrite/SafeWrite.hpp"

#include <cstdint>

namespace {

constexpr char kPluginName[] = "Creature Terrain Tilt Fix";
constexpr uint32_t kPluginVersion = 106;

// MobileObject::Move, distant controller update (Fallout NV 1.4.0.525).
constexpr uintptr_t kInitFrontBackCall = 0x0092FBCD; // call 0x00C6E820
HookUtils::CallDetour gInitFrontBackDetour;

// ECX is the character controller. EBP is MobileObject::Move's frame.
// Only horizontal/probe controllers (flag 0x414 bit 0) use the four cached
// support heights. Other controllers retain the stock update.
__declspec(naked) void InitFrontBackFresh() {
    __asm {
        pushad
        pushfd

        mov ecx, dword ptr [ebp-0x20]
        test dword ptr [ecx+0x414], 1
        jz call_original

        // Force CalculatePitchAndRoll to refresh all four support heights.
        mov dword ptr [ecx+0x460], 0x00800000 // FLT_MIN
        mov dword ptr [ecx+0x464], 0x00800000
        mov dword ptr [ecx+0x468], 0x00800000
        mov dword ptr [ecx+0x46C], 0x00800000

    call_original:
        mov ecx, dword ptr [ebp-0x20]
        mov eax, dword ptr [gInitFrontBackDetour]
        call eax

        popfd
        popad
        ret
    }
}

bool InstallHooks() {
    static bool installed = false;
    if (installed)
        return true;

    if (*reinterpret_cast<const uint8_t*>(kInitFrontBackCall) != 0xE8)
        return false; // Not a call instruction; leave it untouched.

    gInitFrontBackDetour.WriteRelCall(kInitFrontBackCall, InitFrontBackFresh);
    installed = true;
    return true;
}

} // namespace

EXTERN_DLL_EXPORT bool NVSEPlugin_Query(const NVSEInterface* nvse, PluginInfo* info) {
    info->infoVersion = PluginInfo::kInfoVersion;
    info->name = kPluginName;
    info->version = kPluginVersion;
    return !nvse->isEditor;
}

EXTERN_DLL_EXPORT bool NVSEPlugin_Load(NVSEInterface*) {
    return InstallHooks();
}

BOOL WINAPI DllMain(HANDLE, DWORD, LPVOID) {
    return TRUE;
}
