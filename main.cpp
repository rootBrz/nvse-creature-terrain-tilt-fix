// Creature Terrain Tilt Fix - refresh distant terrain support data and correct the tilt angle.
// Copyright (C) 2026 Creature Terrain Tilt Fix contributors.
// SPDX-License-Identifier: LGPL-2.1-only

#include "nvse/PluginAPI.h"
#include "shared/SafeWrite/SafeWrite.hpp"

#include <cstdint>
#include <cstring>

namespace {

constexpr char kPluginName[] = "Creature Terrain Tilt Fix";
constexpr uint32_t kPluginVersion = 110;

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

// bhkCharacterController::CalculatePitchAndRoll (0x00C70060) turns the height
// difference between two ground samples into an angle: atan(difference / length).
// InitFrontBack (0x00C6E820) puts the samples at +-(length / 2 - radius), so they
// are length - 2 * radius apart, not length. Dividing by the full length makes a
// creature tilt less than the slope it stands on, and its uphill legs go into the
// ground. Controller +0x5F0 is the radius, +0x5F4 the length; ESI is the controller.
constexpr uintptr_t kPitchDivide = 0x00C70159;
constexpr uintptr_t kRollDivide = 0x00C702CB;
constexpr uint8_t kStockDivide[] = {0xD8, 0xB6, 0xF4, 0x05, 0x00, 0x00}; // fdiv dword ptr [esi+0x5F4]
float gMinSpanRatio = 0.1f; // below this fraction of the length the shape is degenerate: keep the stock divisor

// Replaces the fdiv: ST0 = height difference on entry, ST0 = difference / span on return.
// x87 only, so no register the surrounding code may be using is touched.
__declspec(naked) void DivideBySampleSpan() {
    __asm {
        fld dword ptr [esi+0x5F4] // length, difference
        fld dword ptr [esi+0x5F0] // radius, length, difference
        fadd st(0), st(0)
        fsubr st(0), st(1) // span = length - 2 * radius, length, difference
        fld st(1)
        fmul dword ptr [gMinSpanRatio] // length * ratio, span, length, difference
        fcomip st(0), st(1) // carry set when length * ratio < span
        jb use_span
        fstp st(0) // length, difference
        jmp divide
    use_span:
        fstp st(1) // span, difference
    divide:
        fdivp st(1), st(0)
        ret
    }
}

bool IsStockDivide(uintptr_t address) {
    return memcmp(reinterpret_cast<const void*>(address), kStockDivide, sizeof(kStockDivide)) == 0;
}

bool InstallHooks() {
    static bool installed = false;
    if (installed)
        return true;

    // Check every site before changing any of them.
    if (*reinterpret_cast<const uint8_t*>(kInitFrontBackCall) != 0xE8 || !IsStockDivide(kPitchDivide) ||
        !IsStockDivide(kRollDivide))
        return false; // Other executable or another patch; leave everything untouched.

    gInitFrontBackDetour.WriteRelCall(kInitFrontBackCall, InitFrontBackFresh);
    HookUtils::ReplaceVirtualCall(kPitchDivide, DivideBySampleSpan, sizeof(kStockDivide));
    HookUtils::ReplaceVirtualCall(kRollDivide, DivideBySampleSpan, sizeof(kStockDivide));
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
