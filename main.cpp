// Creature Terrain Tilt Fix - refresh distant terrain support data and keep corpses out of the ground.
// Copyright (C) 2026 Creature Terrain Tilt Fix contributors.
// SPDX-License-Identifier: LGPL-2.1-only

#include "nvse/PluginAPI.h"
#include "shared/SafeWrite/SafeWrite.hpp"

#include <cstdarg>
#include <cstdint>
#include <cstring>

namespace {

constexpr char kPluginName[] = "Creature Terrain Tilt Fix";
constexpr uint32_t kPluginVersion = 220; // major * 100 + minor * 10

// MobileObject::Move, distant controller update (Fallout NV 1.4.0.525).
constexpr uintptr_t kInitFrontBackCall = 0x0092FBCD; // call 0x00C6E820
HookUtils::CallDetour gInitFrontBackDetour;

// ECX is the character controller.
// Only horizontal/probe controllers (flag 0x414 bit 0) use the four cached
// support heights. Other controllers retain the stock update.
__declspec(naked) void InitFrontBackFresh() {
    __asm {
        pushad
        pushfd

        test dword ptr [ecx+0x414], 1
        jz call_original

        // Force CalculatePitchAndRoll to refresh all four support heights.
        mov dword ptr [ecx+0x460], 0x00800000 // FLT_MIN
        mov dword ptr [ecx+0x464], 0x00800000
        mov dword ptr [ecx+0x468], 0x00800000
        mov dword ptr [ecx+0x46C], 0x00800000

    call_original:
        mov eax, dword ptr [gInitFrontBackDetour]
        call eax

        popfd
        popad
        ret
    }
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

// hkaDetectRagdollPenetration::detectPenetration, thiscall (result, pose, contacts);
// as __fastcall the unused EDX slot sits between `this` and the stack arguments.
// The util built the detector with itself as the raycast interface (detector +0x0C,
// 0x00CA307B), so it comes from `this` and no register the caller may have changed.
// The detection always runs, so a hook installed before ours still sees every call;
// after the window its result is overridden with "clear", and the stock code then
// restores collision. A detector not owned by a util is left as the detection found it.
constexpr uintptr_t kUtilVtable = 0x010C7324; // bhkRagdollPenetrationUtil::`vftable'
constexpr uintptr_t kPlayerSingleton = 0x011DEA3C; // PlayerCharacter*

// An actor's util is at actor +0xB0 (0x0093F8BC, 0x009782D4).
// The player's ragdoll is not carried (only 0x009782DA is hooked). Marking all of it
// would take collision off bones that nothing then holds up, so it keeps the stock marking.
bool IsPlayerUtil(const uint8_t* util) {
    const uint8_t* player = *reinterpret_cast<const uint8_t* const*>(kPlayerSingleton);
    return player && *reinterpret_cast<const uint8_t* const*>(player + 0xB0) == util;
}
using DetectFn = uint8_t*(__fastcall*)(uint8_t* detector, void*, uint8_t* result, void* pose, uint8_t* contacts);

uint8_t* __fastcall DetectWithinWindow(uint8_t* detector, void*, uint8_t* result, void* pose, uint8_t* contacts) {
    uint8_t* const found = reinterpret_cast<DetectFn>(gDetectDetour.GetOverwrittenAddr())(detector, nullptr, result, pose, contacts);
    const uint8_t* util = *reinterpret_cast<const uint8_t* const*>(detector + 0x0C);
    if (!util || *reinterpret_cast<const uintptr_t*>(util) != kUtilVtable)
        return found;
    if (RecoveryWindowExpired(util)) {
        *reinterpret_cast<int*>(contacts + 4) = 0; // contacts.size
        *found = 0;
    } else if (*found && !IsPlayerUtil(util)) {
        MarkWholeRagdoll(detector);
    }
    return found;
}

// Runs after the stock update. For each ragdoll still in recovery, every bone that
// has collision disabled (filter bit 0x4000) is carried along the average contact
// normal at fDesiredVel. Only that velocity component is replaced: the existing
// tangential motion is retained, so killing-blow knockback survives recovery.
// Replacing the normal component also counters gravity, which would otherwise pull
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
            float velocity[3];
            memcpy(velocity, bone + 0x1B0, sizeof(velocity)); // hkpRigidBody linear velocity
            const float alongNormal = velocity[0] * normal[0] + velocity[1] * normal[1] + velocity[2] * normal[2];
            const float correction = desired - alongNormal;
            velocity[0] += normal[0] * correction;
            velocity[1] += normal[1] * correction;
            velocity[2] += normal[2] * correction;
            memcpy(bone + 0x1B0, velocity, sizeof(velocity));
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

// wvsprintfA, not fprintf: MinGW's printf family needs CRT start-up this DLL does not have.
void Log(FILE* log, const char* format, ...) {
    if (!log)
        return;
    char line[1024]; // the wvsprintfA maximum
    va_list args;
    va_start(args, format);
    wvsprintfA(line, format, args);
    va_end(args);
    fputs(line, log);
}

struct Site {
    const char* name;
    uintptr_t address;
    bool stock;
};

bool InstallHooks(uint32_t runtimeVersion) {
    static bool installed = false;
    if (installed)
        return true;

    // Opened here, not by a global constructor: the DLL has no CRT start-up to run one.
    CreateDirectoryA("logs", nullptr);
    FILE* log = fopen("logs\\CreatureTerrainTiltFix.log", "w");
    Log(log, "%s %u.%u, runtime 0x%08X\n", kPluginName, kPluginVersion / 100, kPluginVersion / 10 % 10, runtimeVersion);

    // Check every site before changing any of them.
    const Site sites[] = {
        {"InitFrontBack call", kInitFrontBackCall, IsCall(kInitFrontBackCall)},
        {"detectPenetration call", kDetectCall, IsCall(kDetectCall)},
        {"ragdoll Update call", kActorUpdateCall, IsCall(kActorUpdateCall)},
    };
    bool allStock = true;
    for (const Site& site : sites) {
        if (site.stock)
            continue;
        allStock = false;
        const uint8_t* code = reinterpret_cast<const uint8_t*>(site.address);
        Log(log, "Unexpected code at 0x%08X (%s): %02X %02X %02X %02X %02X %02X\n", site.address, site.name,
            code[0], code[1], code[2], code[3], code[4], code[5]);
    }
    if (!allStock) {
        // Other executable or another patch; leave everything untouched.
        Log(log, "No fix installed: another plugin or a different executable changed the code above.\n");
        if (log)
            fclose(log);
        return false;
    }

    gInitFrontBackDetour.WriteRelCall(kInitFrontBackCall, InitFrontBackFresh);
    gDetectDetour.WriteRelCall(kDetectCall, DetectWithinWindow);
    gRagdollUpdateDetour.WriteRelCall(kActorUpdateCall, RagdollUpdateHook);
    installed = true;
    Log(log, "All fixes installed.\n");
    if (log)
        fclose(log);
    return true;
}

} // namespace

EXTERN_DLL_EXPORT bool NVSEPlugin_Query(const NVSEInterface* nvse, PluginInfo* info) {
    info->infoVersion = PluginInfo::kInfoVersion;
    info->name = kPluginName;
    info->version = kPluginVersion;
    return !nvse->isEditor;
}

EXTERN_DLL_EXPORT bool NVSEPlugin_Load(NVSEInterface* nvse) {
    return InstallHooks(nvse->runtimeVersion);
}

BOOL WINAPI DllMain(HANDLE, DWORD, LPVOID) {
    return TRUE;
}
