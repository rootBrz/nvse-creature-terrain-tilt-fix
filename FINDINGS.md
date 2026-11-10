# Distant creature-orientation investigation

## Executive finding

The reported orientation problem is not an ant-specific animation issue and is not caused by the angular-velocity discontinuity in `0x008CD780`.

The relevant defect is in the distant movement path in `MobileObject::Move` (`0x0092F260`). That path can advance an actor while bypassing normal character-proxy integration and support-contact updates. Probe-based controllers then compute pitch and roll from cached terrain support heights, which may describe an older location.

The current plugin, **Creature Terrain Tilt Fix v106**, addresses that specific defect by invalidating four cached heights before the existing `bhkCharacterController_InitFrontBack` call. The engine then samples current terrain through its normal `FindSupportPoint` path.

The fix is limited to horizontal/probe controllers (`controller +0x414 & 1`). Other controller types and several related defects remain outside its scope.

## 1. Rejected hypothesis: `0x008CD780`

The original suspicious function was presented as `MovementController::UpdateAnimation`. Its local staging names are misleading.

`build/units.json:584156-584158` identifies the actual handlers:

- `0x008CD6D0`: `ArrowProjectile::HandleType2`
- `0x008CD780`: `ArrowProjectile::HandleType3`
- `0x008CDAF0`: `ArrowProjectile::HandleType4`

`ArrowProjectile::ProcessImpact` dispatches impact type 3 to `HandleType3` (`src/staging/blk_008c0000_od.cpp:3961-3993`, `0x008CD210`). `include/ArrowProjectile.h:13-19` declares the same handlers.

The values have projectile-specific meanings:

| Staging expression | Actual meaning |
|---|---|
| `state` | Arrow impact record at `ArrowProjectile +0x88` |
| `state->position` | Collision/contact position |
| `state->direction` | World-space collision plane normal |
| `velocity` | Previous arrow displacement, used as a direction |
| `speed` | Scalar arrow speed |

`ArrowProjectile_Bounce` writes the type-3 state and collision normal at `src/staging/blk_008c0000_o2_od_oy_noo2_variant.cpp:788-821`, `0x008CCCE0`.

### Sign discontinuity assessment

The conditional in `HandleType3` is mathematically discontinuous:

- As `normal.z` approaches zero from above, it approaches `+abs(normal.y)`.
- At exactly zero, it becomes zero.
- As `normal.z` approaches zero from below, it approaches `-abs(normal.y)`.

That discontinuity is real, but the result sets **arrow rigid-body angular velocity**, not actor orientation. No ant, scorpion, actor-distance, or process-level data reaches this code.

The similar missile-impact logic at `src/fallout/ai/combat/missileprojectile.cpp:1086-1114` supports the interpretation that this is a projectile collision-response convention.

`DAT_011cedac` is an angular-velocity gain. Its exact setting name/default was not recovered. Downstream Havok use implies angular velocity units, conventionally radians/second, but that is not proven by a setting definition.

**Conclusion:** this function cannot explain the distant creature problem.

## 2. Actual actor movement and orientation path

The recovered high-process chain is:

```text
ProcessLists_UpdateActorsMovement (0x0096DB30)
  → Actor::UpdateMovement_00886CB0
    → ActorMover path delta or animation GetMovement
    → MobileObject::Move (0x0092F260)
      → distant or normal controller update
        → CalculatePitchAndRoll (0x00C70060)
          → pitch +0x550 / roll +0x554
            → Actor::UpdateNodeTransform (0x0088B150)
```

Important sources:

- Scheduler: `src/fallout/ai/processlists.cpp:8484-8505`.
- Actor movement input: `src/fallout/ai/actor_rtm_variant.cpp:451-485`.
- Path-delta production: `src/fallout/ai/movement/actormover.cpp:1702-1815` and `3565-3670`.
- Distance branch: `src/fallout/ai/mobileobject.cpp:5612-5689`.
- Rendered orientation: `src/fallout/ai/actor.cpp:52797-52826`.

The movement delta is not the arrow’s `state->direction`. It comes from the actor mover’s path delta, with animation movement as fallback. When the scheduler supplies zero elapsed time, the actor update substitutes the global frame delta.

### Normal movement

Normal `bhkCharacterController_Move` (`0x00C73170`):

- Derives desired velocity from `in->displacement / in->dt`.
- Calls `InitFrontBack`.
- Calls `ahkpCharacterProxy_checkSupport` through `0x00C6CEF0`.
- Runs movement actions and proxy integration.
- Calculates pitch and roll afterward.

See `src/fallout/ai/mobileobject_o2_variant_rtm_variant.cpp:900-950` and `991-1030`.

### Distant movement

The distant branch runs when squared camera distance exceeds `DAT_01267bd4`, excluding the player, projectiles, dynamic-motion actors, and immobile actors. After a successful navmesh-height lookup, it:

1. Gets a navmesh normal.
2. Writes controller support normal `+0x450`.
3. Resets the pitch timer.
4. Calls `InitFrontBack`.
5. Calls `CalculatePitchAndRoll`.
6. Applies heading and pitch/roll.
7. Updates the actor/reference position and returns.

It does not perform the normal support check, movement action, or Havok proxy integration.

The final call at `src/fallout/ai/mobileobject.cpp:5686` is virtual slot `+0x2A8`. Disassembly of `FalloutNV.exe` shows it resolves to `0x00931620` for the actor-class vtables (slots `0x4534F0` and `0x8DB810` in two other vtables were not checked). That function:

1. Sets the reference position (`0x00575830`).
2. Gets the physics controller through the process (`this+0x68`, virtual `+0x28C`).
3. Calls `SetPosition_005620E0` (`0x005620E0`), which calls `bhkCharacterController` SetPosition (`0x00C6E390`). That writes the new position to the proxy (`0x00CAC920`) and zeroes the proxy velocity.

This runs after `CalculatePitchAndRoll`, so the distant path does move the proxy, one step behind the pitch calculation. This was established statically; it has not been confirmed at runtime.

## 3. Stale support-height mechanism

For controllers with `controller +0x414 & 1`, `CalculatePitchAndRoll` uses:

| Offset | Meaning |
|---|---|
| `+0x460`, `+0x464` | Front/back support heights |
| `+0x468`, `+0x46C` | Side support heights |
| `+0x550` | Pitch, radians |
| `+0x554` | Roll, radians |
| `+0x558` | Pitch/roll interpolation factor |

It computes an angle from height difference divided by controller length, clamps it, and blends toward it. Fresh support-point raycasts occur only for entries still containing `FLT_MIN`.

Source: `src/staging/blk_00c60000_od_o2_nood_variant.cpp:6085-6208`, `0x00C70060`.

Normal contact processing produces these heights:

- Contact processing resets entries to `FLT_MIN`: `src/staging/blk_00c60000_od_o2_nood_variant_rtm_variant.cpp:6964-6968`.
- Accepted contacts call `AddSupportHeight2`: same file, `7310-7312`.
- `AddSupportHeight2` blends contact Z into the entries: `src/staging/blk_00c60000_od_o2_nood_variant.cpp:6991-7005`, `0x00C6C890`.
- The equivalent accumulator is at `src/staging/blk_00c60000_o2.cpp:1531-1544`, `0x00C6C720`.

`InitFrontBack` writes only the four probe origins at `+0x5B0` through `+0x5E0`; it does not reset `+0x460` through `+0x46C` (`src/staging/blk_00c60000_od_o2_nood_variant.cpp:5643-5680`).

Therefore previously populated heights can remain unchanged while an actor moves through the distant path. Old terrain slope becomes the actor’s current visible pitch or roll.

### Proxy/probe position

`InitFrontBack` transforms probe offsets through the phantom transform, and `FindSupportPoint` (`0x00C6E680`) casts down from those probe origins (`this + (index + 0x5B) * 16`).

The distant path updates the proxy at the end of `MobileObject::Move` through virtual `+0x2A8` (see section 2). The probes therefore start from the previous update's position, the same lag the stock distant code has. This is not a second stale-input path.

`ApplyPitchAndRoll` copies all four phantom transform rows, including translation, and changes only rotation (`src/staging/blk_00c60000_od_o2_nood_variant_rtm_variant.cpp:8726-8759`, `0x00C6E420`).

An earlier version of this document claimed the distant path never moves the proxy. That was wrong.

## 4. Actor Fade connection

`Actor::UpdateMovement_00886CB0` computes:

```cpp
fDistance = cameraDistanceToBoundCenter - boundRadius * DAT_01021798;
fRatio = fDistance / BSFadeNode_GetFadeOutMultiplier(3);
bFar = fRatio > fadeNode->GetFloatAtOffsetB0();
controller->bhkCharacterController_SetFarAway(bFar);
```

Source: `src/fallout/ai/actor_rtm_variant.cpp:486-525`.

The decompiled `pCombat` name is misleading. Its virtual `FUN_vt28c` is `BaseProcess::GetPhysicsCharacterController`, established by `include/BaseProcess.h:750` and `src/fallout/ai/processlists_private/processlists_BaseProcess.h:312`.

`bhkCharacterController_SetFarAway` (`0x008879D0`) sets controller flag `0x40000` (`src/fallout/ai/actor.cpp:3621-3626`). The distant-orientation gate rejects that flag at `src/fallout/ai/mobileobject.cpp:5666`.

When the flag is set:

- Position can continue changing.
- Probe initialization, pitch calculation, and phantom rotation are skipped.
- Existing `+0x550/+0x554` values are not cleared.

Negative fade multipliers can alter this comparison, but no user-specific INI values were established. The proven relationship is that Actor Fade controls whether distant tilt updates occur.

## 5. Related defects outside the current fix

### Unchecked navmesh-normal failure

`GetPathingLocationNormal` can return false without writing its output (`src/fallout_shared/pathfinding/pathfind.cpp:1009-1023`). The caller ignores the return value and leaves `hitNormal` uninitialized (`src/fallout/ai/mobileobject.cpp:5670-5676`).

This affects the non-probe branch that consumes support normal `+0x450`. It is not the explanation for a confirmed probe-based controller.

`NavMesh::GetPlaneNormal` also returns an unnormalized cross product (`src/fallout_shared/pathfinding/navmesh_util.cpp:85-96`), which matters to that branch.

### Duplicate pitch check

`src/fallout/ai/mobileobject.cpp:5666-5668` calls `QCanPitch()` twice. A non-quadruped roll-only controller therefore misses distant tilt processing. Quadruped eligibility bypasses this defect.

### Process scheduling

Process tiers are selected primarily from cell state, not a direct actor-player distance (`src/fallout/ai/mobileobject.cpp:1160-1181`, `0x009334B0`; `src/fallout/ai/actor.cpp:112146-112204`, `0x008860D0`).

Observed update thresholds:

| Tier | Condition |
|---|---|
| Middle-high | `0.15` game hours, rollback, or nonzero supplied delta |
| Middle-low | `0.30` game hours |
| Low | `1.0` game hour; also budget-limited |

Sources: `src/fallout/ai/processlists.cpp:1219-1242`, `7231-7243`, `7633-7645`.

These schedules can amplify stale transitions but are not required to explain the distant-input defect. Lower tiers also have direct/virtual movement paths, so each sighting must first be confirmed to pass through `0x0092F260`.

A separate frame-skip path at `src/fallout/ai/mobileobject.cpp:5569-5594` directly translates the Ni node and returns without proxy or pitch updates. Its enabling setting and role in the report are unknown.

### Animation culling

`Animation::Update` (`0x00491180`) performs root translation before its `IsCulled()` check. The check at `src/fallout_shared/animation.cpp:6035-6068` gates sound processing, not skeletal/root animation. It does not explain this orientation bug.

## 6. Current fix

The current plugin replaces the call at `0x0092FBCD` with `InitFrontBackFresh`.

For probe controllers it writes `FLT_MIN` to:

```text
controller +0x460
controller +0x464
controller +0x468
controller +0x46C
```

It then calls the preserved detour target. `CalculatePitchAndRoll` treats those sentinel values as missing data and calls `FindSupportPoint` for each required support height.

The controller test is:

```asm
mov ecx, dword ptr [ebp-0x20]
test dword ptr [ecx+0x414], 1
```

- `ebp-0x20` is the `bhkCharacterController*` local in `MobileObject::Move`.
- `ecx` passes that pointer as `this` for `thiscall` functions.
- `test` performs a bitwise AND with `1`; it does not modify the controller.
- A set bit selects the horizontal/probe update. A clear bit skips directly to the preserved target.

This is a controller-shape test, not an ant test.

### Detour compatibility

The project uses RTM’s `HookUtils::CallDetour`. It reads and preserves the existing call target before replacing it. Earlier detours therefore remain in the chain.

Later plugins must also preserve the current target. Compatibility cannot be guaranteed with a later plugin that blindly overwrites the call.

## 7. Crash history and version changes

| Version | Relevant change |
|---|---|
| Initial implementation | Added proxy-position synchronization, support-height reset, and fade-gate override |
| Crash report | Crash occurred in the new distant-probe path before `CalculatePitchAndRoll`; the added operations were not isolated conclusively |
| v101 | Removed proxy-position call and fade override; retained only support-height reset |
| v102 | Added preservation of an existing call target |
| v103 | Switched to RTM `HookUtils::CallDetour` |
| v104 | Briefly used an independent detour and LGPL-2.1 packaging |
| v105 | Renamed project to Distant Terrain Alignment Fix |
| v106 | Restored RTM detour, vendored SafeWrite, and standardized on GPL-3.0 |

The v101 behavior was reported as “seems fixed.” Later versions preserve that core behavior while changing detour compatibility and packaging. They have not received equivalent full in-game validation.

## 8. Remaining drawbacks and possible mitigations

### 1. Four support samples per affected update

Refreshing all four heights costs physics queries for every affected actor and can reduce the benefit of distant movement optimization.

Possible mitigations:

- Refresh only after meaningful position or heading changes.
- Refresh after a time/frame threshold rather than every update.
- Accept less frequent updates at the cost of more visible stepping.

### 2. Stale proxy position (withdrawn)

Withdrawn. The distant path updates the proxy through virtual `+0x2A8` (`0x00931620` → `0x005620E0` → `0x00C6E390`) after the pitch calculation, so raycasts start one step behind the actor, as in stock. No mitigation is needed. To confirm at runtime, log phantom translation against reference position inside `InitFrontBackFresh`.

### 3. Fade-gated updates

If flag `0x40000` blocks the update, old pitch/roll can remain visible.

Possible mitigations:

- Clear stale pitch/roll when the gate blocks the update.
- Recompute only when the actor is visible, avoiding work for fully faded actors.
- Do not force updates on fully faded actors.

### 4. Non-probe controllers

The current fix does nothing for non-probe controllers.

Both defects below are confirmed in the binary but low priority. Neither has been reproduced in game.

- `hitNormal` is uninitialized. The "constructor" at `0x006815C0` only returns `this`. `GetPathingLocationNormal` (`0x006D9100`) returns false without writing the output when the pathing location is invalid (`0x006DD6F0`) or no navmesh triangle is found (`0x006DD640`). Only the non-probe branch of `CalculatePitchAndRoll` reads the normal. Possible fix: a `CallDetour` at `0x0092FB8A` that writes `(0, 0, 1)` to the output before calling the original.
- The gate calls `0x009305A0` twice (`0x0092FB27`, `0x0092FB36`). It tests flag `0x2000000` (bit 25, pitch). The roll sibling `0x009305D0` tests `0x4000000` (bit 26). A roll-only, non-quadruped controller misses distant tilt. Possible fix: change the rel32 at `0x0092FB37` from `65 0A 00 00` to `95 0A 00 00`.

## 9. Runtime validation plan

Instrument one affected actor while it crosses a slope at different distances.

| Hook | Values |
|---|---|
| `0x0092F260` | Actor ID, elapsed time, movement delta, actor position, destination, camera distance squared, process tier, frame number, controller flags |
| `0x006D9100` | Return value, lookup position, output normal |
| `0x00C6E820` | Phantom translation/rotation, probe origins, actor/reference position |
| `0x00C6C720`, `0x00C6C890`, `0x00C711D0` | Support-height writes and contact Z |
| `0x00C70060` | Cached heights, `FLT_MIN` entries, fresh raycast heights, old/new pitch and roll |
| `0x00C6E420` | Phantom translation before/after |
| `0x00886CB0`, `0x008879D0` | Fade multiplier, `bFar`, controller pointer, `0x40000` state |
| `0x0088B150` | Actor/node position and visible pitch/roll |

### Evidence supporting the stale-height diagnosis

- `+0x460..+0x46C` stays unchanged across distant movement over changing terrain.
- No support-height producer runs until normal controller movement resumes.
- Visible pitch/roll follows those old heights while translation advances.
- Near/far or navmesh-path transitions align with abrupt orientation changes.

### Evidence that would redirect the diagnosis

- Support heights refresh and proxy position tracks the actor during anomalous frames.
- The anomaly occurs without entering the distant branch.
- The actor does not call `0x0092F260`; investigate its process-specific virtual path instead.
- `controller +0x414 & 1` is clear; prioritize navmesh-normal failure rather than cached support heights.

## 10. Current packaging and verification

Current binary:

```text
build/CreatureTerrainTiltFix.dll
```

The repository contains the NVSE entry points, local hook logic, RTM SafeWrite sources, standalone build files, prebuilt binary, GPL-3.0 license, notice, and credits.

Verified during development:

- Standalone builds succeed.
- `NVSEPlugin_Query` and `NVSEPlugin_Load` are exported.
- Emitted hook instructions preserve registers and call the preserved target.
- No `libstdc++` dependency exists.

Full behavior across all controller types, mods, near/far transitions, and fade configurations has not been validated.

## 11. Under-tilt from the wrong divisor (version 110)

Found later with a runtime log, while investigating corpses that sank into the ground and twitched.

A logger recorded, for living actors, the controller's pitch and roll and the slope of the land under the actor. With v106 installed:

- The far-away flag (`0x40000`, section 4) was set in under 5% of samples. The fade gate was not the cause of what follows.
- Tilt was consistently about 0.55 to 0.7 of the ground slope, at every distance, including next to the player.

The cause is in the angle calculation:

- `InitFrontBack` (`0x00C6E820`) places the four ground samples at `+-(length / 2 - radius)` (`+0x5F0` radius, `+0x5F4` length). Opposite samples are `length - 2 * radius` apart.
- `CalculatePitchAndRoll` computes `atan(heightDifference / length)` for both pitch and roll.

The angle is therefore too small by `(length - 2 * radius) / length`. For the small radscorpion controller (radius 3.02, length 10.72) that factor is 0.44. The body stays flatter than the ground and the uphill legs go under the surface.

Fix: the two `fdiv dword ptr [esi+0x5F4]` instructions at `0x00C70159` and `0x00C702CB` are replaced with a call that divides by `length - 2 * radius`. If that is not above 10% of the length, the stock divisor is kept.

Measured after the fix, same save and actors, on slopes steeper than 0.08 rad: tilt divided by slope averaged 1.00 over 750 samples for the small radscorpion controller (individuals 0.9 to 1.35). The user confirmed in game that scorpions sit flush on slopes.

Not checked: the larger controllers (lengths 21.43 and 35.72) were almost always on near-flat ground in that run, so their result is not established.
