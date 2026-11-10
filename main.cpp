// Creature Terrain Tilt Fix - correct creature tilt on slopes and keep corpses out of the ground.
// Copyright (C) 2026 Creature Terrain Tilt Fix contributors.
// SPDX-License-Identifier: LGPL-2.1-only

#include "nvse/PluginAPI.h"
#include "shared/SafeWrite/SafeWrite.hpp"

#include <cstdint>
#include <cstring>

namespace {

constexpr char kPluginName[] = "Creature Terrain Tilt Fix";
constexpr uint32_t kPluginVersion = 113;

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

// --- Corpses: ragdoll penetration recovery repaired ----------------------------
// bhkRagdollPenetrationUtil::Update (0x00CA1410) disables collision on a bone that
// crosses a surface and on every bone below it in the skeleton, then pushes only
// the crossing bone. The other bones have nothing holding them, so when the
// crossing bone is the root the whole corpse falls through the terrain. Two
// repairs: every bone with collision disabled is carried out together (see
// CarryRecoveringBones), and the recovery ends after fDetectionUpdateTimeSec even
// if something is still crossing, so a corpse cannot drift without collision forever.
constexpr uintptr_t kDetectCall = 0x00CA175E;      // call hkaDetectRagdollPenetration::detectPenetration
constexpr uintptr_t kPrecisionTimer = 0x00AA4D80;  // BSPrecisionTimer::GetTimer
constexpr uintptr_t kEntityActivate = 0x00C9C1D0;  // hkpEntity::activate
constexpr uintptr_t kDesiredVelocity = 0x0126829C; // fDesiredVel:RagdollAnim, Havok units per second
HookUtils::CallDetour gDetectDetour;

// util +0x20: window start, +0x28: window length, both in timer units.
bool __cdecl RecoveryWindowExpired(const uint8_t* util) {
    int64_t start, length;
    memcpy(&start, util + 0x20, sizeof(start));
    memcpy(&length, util + 0x28, sizeof(length));
    return reinterpret_cast<int64_t(__cdecl*)()>(kPrecisionTimer)() - start > length;
}

// The detection marks a crossing bone and the bones attached below it. Bones above
// it in the skeleton keep their collision, so when those are the buried ones they
// stay trapped under the terrain and the ragdoll is pulled apart. Mark every bone:
// the stock code then disables collision on the whole ragdoll, and all of it is
// carried out together. controller +0x18: per-bone states, +0x1C: bone count;
// 0 = clear, 1 = crossing, 2 = attached below a crossing bone.
void __cdecl MarkWholeRagdoll(const uint8_t* controller) {
    int* states = *reinterpret_cast<int* const*>(controller + 0x18);
    const int count = *reinterpret_cast<const int*>(controller + 0x1C);
    for (int i = 0; states && i < count; ++i)
        if (!states[i])
            states[i] = 2;
}

// thiscall detectPenetration(hkBool* result, pose, contacts). EBX is the util.
// After the window it reports "clear"; the stock code then restores collision.
__declspec(naked) void DetectWithinWindow() {
    __asm {
        push ecx
        push ebx
        call RecoveryWindowExpired
        add esp, 4
        pop ecx
        test al, al
        jnz expired

        push ecx // controller, for MarkWholeRagdoll
        push dword ptr [esp+0x10] // contacts
        push dword ptr [esp+0x10] // pose
        push dword ptr [esp+0x10] // result
        call dword ptr [gDetectDetour] // returns the result pointer in EAX
        pop ecx
        cmp byte ptr [eax], 0
        je done
        push eax
        push ecx
        call MarkWholeRagdoll
        add esp, 4
        pop eax
    done:
        ret 0xC

    expired:
        mov eax, dword ptr [esp+0xC]
        mov dword ptr [eax+4], 0 // contacts.size = 0
        mov eax, dword ptr [esp+4]
        mov byte ptr [eax], 0 // *result = false
        ret 0xC
    }
}

// Runs after the stock update. For each ragdoll still in recovery, every bone that
// has collision disabled (filter bit 0x4000) is given the same velocity: fDesiredVel
// along the average of the contact normals the detection reported. Moving them as
// one keeps the joints unstrained and replaces gravity, which would otherwise pull
// the unsupported bones through the floor.
void __cdecl CarryRecoveringBones(const uint8_t* util) {
    if (!util)
        return;
    uint8_t* const* entries;
    uint32_t entryCount;
    memcpy(&entries, util + 0x08, sizeof(entries));
    memcpy(&entryCount, util + 0x0C, sizeof(entryCount));
    float desired;
    memcpy(&desired, reinterpret_cast<const void*>(kDesiredVelocity), sizeof(desired));

    for (uint32_t i = 0; entries && i < entryCount; ++i) {
        const uint8_t* entry = entries[i];
        if (!entry || !entry[0x20]) // +0x20: a penetration was detected this frame
            continue;
        const uint8_t* rig = *reinterpret_cast<const uint8_t* const*>(entry + 0x04);
        const uint8_t* contacts = *reinterpret_cast<const uint8_t* const*>(entry + 0x14);
        const int contactCount = *reinterpret_cast<const int*>(entry + 0x18);
        if (!rig || !contacts || contactCount <= 0)
            continue;

        float normal[3] = {0.0f, 0.0f, 0.0f};
        for (int c = 0; c < contactCount; ++c) { // 48-byte records, normal at +0x20
            float n[3];
            memcpy(n, contacts + c * 48 + 0x20, sizeof(n));
            normal[0] += n[0], normal[1] += n[1], normal[2] += n[2];
        }
        const float lengthSq = normal[0] * normal[0] + normal[1] * normal[1] + normal[2] * normal[2];
        if (lengthSq < 0.01f) // opposing normals cancel: no direction is better than another, go up
            normal[0] = 0.0f, normal[1] = 0.0f, normal[2] = 1.0f;
        else {
            const float scale = 1.0f / __builtin_sqrtf(lengthSq);
            normal[0] *= scale, normal[1] *= scale, normal[2] *= scale;
        }

        uint8_t* const* bones = *reinterpret_cast<uint8_t* const* const*>(rig + 0x08);
        const int boneCount = *reinterpret_cast<const int*>(rig + 0x0C);
        for (int b = 0; bones && b < boneCount; ++b) {
            uint8_t* bone = bones[b];
            if (!bone || !(*reinterpret_cast<const uint32_t*>(bone + 0x2C) & 0x4000))
                continue;
            const float velocity[3] = {normal[0] * desired, normal[1] * desired, normal[2] * desired};
            memcpy(bone + 0x1B0, velocity, sizeof(velocity)); // hkpRigidBody linear velocity
            reinterpret_cast<void(__fastcall*)(uint8_t*)>(kEntityActivate)(bone);
        }
    }
}

// The per-actor call to bhkRagdollPenetrationUtil::Update. ECX = the actor's util.
constexpr uintptr_t kActorUpdateCall = 0x009782DA;
HookUtils::CallDetour gRagdollUpdateDetour;

// The stock update, then carry whatever it put into recovery.
__declspec(naked) void RagdollUpdateHook() {
    __asm {
        push ecx // util, the argument of CarryRecoveringBones
        call dword ptr [gRagdollUpdateDetour]
        call CarryRecoveringBones
        add esp, 4
        ret
    }
}

bool IsCall(uintptr_t address) {
    return *reinterpret_cast<const uint8_t*>(address) == 0xE8;
}

bool InstallHooks() {
    static bool installed = false;
    if (installed)
        return true;

    // Check every site before changing any of them.
    if (!IsCall(kInitFrontBackCall) || !IsStockDivide(kPitchDivide) || !IsStockDivide(kRollDivide) ||
        !IsCall(kDetectCall) || !IsCall(kActorUpdateCall))
        return false; // Other executable or another patch; leave everything untouched.

    gInitFrontBackDetour.WriteRelCall(kInitFrontBackCall, InitFrontBackFresh);
    HookUtils::ReplaceVirtualCall(kPitchDivide, DivideBySampleSpan, sizeof(kStockDivide));
    HookUtils::ReplaceVirtualCall(kRollDivide, DivideBySampleSpan, sizeof(kStockDivide));
    gDetectDetour.WriteRelCall(kDetectCall, DetectWithinWindow);
    gRagdollUpdateDetour.WriteRelCall(kActorUpdateCall, RagdollUpdateHook);
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
