# Distant creature-orientation investigation

## Executive finding

The reported orientation problem is not an ant-specific animation issue and is not caused by the angular-velocity discontinuity in `0x008CD780`.

The relevant defect is in the distant movement path in `MobileObject::Move` (`0x0092F260`). That path can advance an actor while bypassing normal character-proxy integration and support-contact updates. Probe-based controllers then compute pitch and roll from cached terrain support heights, which may describe an older location.

The current plugin, **Creature Terrain Tilt Fix v106**, addresses that specific defect by invalidating four cached heights before the existing `bhkCharacterController_InitFrontBack` call. The engine then samples current terrain through its normal `FindSupportPoint` path.

The fix is limited to horizontal/probe controllers (`controller +0x414 & 1`). Other controller types and several related defects remain outside its scope.

Version 112 adds two further changes found later with runtime logs: the tilt-angle divisor (section 11) and the repaired ragdoll penetration recovery (section 14, with the investigation behind it in sections 12 and 13). Sections 1 to 10 describe version 106 and are unchanged.

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

## 12. Corpses that sink, stretch and twitch: the penetration recovery

This section records the investigation and a first remedy, switching the recovery off, which was used during testing. The shipped change is the repair in section 14.

### Report

> I killed one stuck in the ground like that near the Dunwich building and it started twitching out like crazy. [...] when I came out I saw the rad scorpion's body go by randomly flying through the sky off in the distance.

Later reports: limbs stretched, the corpse half buried and twitching, most often for radscorpions killed far away.

### The system

Every actor owns a `bhkRagdollPenetrationUtil` at `Actor +0xB0`, meant to pull a ragdoll out of terrain and objects.

| Address | Role |
|---|---|
| `0x00CA2AD0` | `Initialize`: builds the ragdoll entries and starts a detection timer |
| `0x00CA0CD0` | Marks all entries active and restarts the timer when a saved actor is loaded |
| `0x00CA1410` | `Update`: once per frame, called from the player update (`0x0093F8C2`) and the per-actor loop (`0x009782DA`) |
| `0x00D1FB00` | `hkaDetectRagdollPenetration::detectPenetration` |
| `0x00CA1C50` | Ray cast used by the detection |

Detection: for each bone and its parent, a ray is cast between their positions. A hit on anything that is not a ragdoll body (layers 8 and 29) marks the parent "penetrating" and every bone below it "descendant". The ray cast takes the first hit in collector order, not the closest.

Response, each frame while anything is penetrating:

1. Every marked bone gets collision-filter bit `0x4000`. `bhkCollisionFilter::IsCollisionEnabled` (`0x00C84740`) returns false for it, so the bone collides with nothing.
2. Each "penetrating" bone gets an impulse along the hit normal: `mass * (fDesiredVel - velocity . normal)`, clamped to `fImpulseLimit`. Defaults in `[RagdollAnim]`: `fDesiredVel` 15 (about 1.5 m/s; one Havok unit is 6.9991 game units), `fImpulseLimit` 1500, `fDetectionUpdateTimeSec` 5.

There is no time limit while a penetration is still detected.

### What a runtime log showed

A logger recorded, per ragdoll, how many bones were under the land surface and how many had collision disabled. One radscorpion corpse (`00164A53`, 37 bones) after a load, heights in game units relative to the land:

| Time | Bones below land | Bone height | Collision disabled | Recovery |
|---|---|---|---|---|
| 0.0 s | 10 of 37 | -16 to +47 | 0 | active |
| 0.6 s | 11 | -16 to +48 | **37 of 37** | active |
| 1.1 s | 37 | -67 to -9 | 6 | active |
| 3.2 s | 37 | -1506 to -1422 | 0 | inactive |
| 3.7 s | 37 | **-1217 to -239** | 0 | inactive |
| 5.3 s | 36 | **-1698 to +31** | 0 | inactive |
| 5.8 s onward | 30 | -37 to +29 | 0 | inactive |

1. The recovery found a crossing at the root bone and disabled collision on all 37 bones.
2. The corpse fell through the terrain within half a second. The impulse acts on one bone and cannot carry the ragdoll.
3. Once every bone was below the surface, no bone line crossed it, so detection reported "clear" and collision was restored. Terrain does not stop a body from below; it kept falling.
4. Twice, part of the corpse was moved back up while the rest was 950 to 1730 units lower. That is the stretching. The code that does this was not identified.
5. It ended with 30 of 37 bones under the surface, moving at about 2000 units per second without end.

### Fix

The call to `detectPenetration` at `0x00CA175E` is replaced with a stub that empties the contact list and returns false. The stock code then takes its normal "not penetrating" path, which clears filter bit `0x4000` and applies nothing. In the next log no ragdoll had collision disabled and none fell through the terrain.

### Hypotheses that were tried and dropped

- **The impulse over-corrects because it is applied off-centre.** True as arithmetic: for a thin bone hit at distance `a` from its centre the error is multiplied by `-(12 a^2 / L^2)` each frame, which grows once `a > 0.29 L`. Applying the impulse at the centre of mass (`0x00CA1A76`) did not change what players saw.
- **The pull never stops.** Limiting it to `fDetectionUpdateTimeSec` did not help either. The damage is done in the first half second, by the loss of collision.

### Drawbacks

- A limb that ends up through a wall or an object is no longer pulled out.
- The stub replaces the call; it does not chain to a detour installed there earlier by another plugin.
- Step 4 above is unexplained. Nothing was seen to trigger it once corpses stopped falling through.

## 13. Corpses that stay half buried: trapped bones

This section records the investigation and a second remedy used during testing, lifting a buried ragdoll by land height. It is not in the shipped plugin; the repair in section 14 replaced it.

With the penetration recovery off (section 12), corpses no longer fell through the terrain, but radscorpions killed far away with `killall` still sank and twitched.

### What a runtime log showed

Three that sank, two that came to rest normally. Heights are bone centres relative to the land surface, in game units:

| Actor | Before ragdoll: lowest bone | Bones below land | At rest: lowest bone | Bone speed at rest |
|---|---|---|---|---|
| `00164A53` (sunk) | -16 | 10 of 37 | -22 to -34 | 240 to 960 units/s, never settles |
| `0016174B` (sunk) | -10 | 5 | -22 | 60 to 490 units/s |
| `00161749` (sunk) | -17 | 8 | -18 to -20 | 280 to 470 units/s |
| `00161755` (normal) | +8 | 0 | +7 | 0 |
| `00164BDC` (normal) | +2 | 0 | +8 | 0 |

- The sunk scorpions already had 5 to 10 bones under the land when they died, although the actor's own position was on the surface. Section 11 explains that pose: the body was tilted less than the slope.
- The number of bones below the land never changes afterwards. Terrain collision does not push them out. They stay on the lower side, the other bones on the upper side, and the joints pull between them. That is the twitching.
- A corpse that starts with every bone above the land settles with its lowest bone centre about 7 units above it and stops.

### Fix

A detour on the per-actor ragdoll update call (`0x009782DA`; `EDX` is the actor, `ECX` its `bhkRagdollPenetrationUtil`) runs before the stock call. For an actor whose life state (`Actor +0x108`) is dying or dead, in an exterior (`TES +0x34`, the interior cell, is null), it walks the ragdoll bones (`util +0x08` entries, `entry +0x04` rig, `rig +0x08` bodies, `rig +0x0C` count):

- A bone's height above the land is its centre of mass (`hkpRigidBody +0x140`, times 6.9991255) minus `TES::GetLandHeightAtPosition` (`0x004572E0`).
- If the lowest is more than 4 units under the land, every body is moved up by `8 - lowest` with `hkpRigidBody::setPosition` (`0x00CA8800`), starting from the body origin at `+0x120`.
- All bodies move by the same distance, so no joint is strained.
- It is skipped unless every bone is a dynamic body in the world (motion type at `+0xE8` not 4 or 5), and when the lowest bone is more than 150 units down.

The user reported the sinking and twitching fixed with this change.

### Drawbacks

- **Landscape only.** "Buried" means below the landscape height. Statics are not considered, so a corpse inside a rock is not helped, and a corpse in a place built below the landscape surface would be raised onto it.
- **Cost.** One land-height lookup per ragdoll bone per frame for each dying or dead actor being updated.
- **A safety net, not the cause.** The cause of the buried pose is the under-tilt in section 11. This catches what that does not: rough ground, and corpses already buried in a save.
- **Thresholds are measured, not derived.** 8 units of clearance comes from the two normal corpses above. A much larger creature might need more and would then be lifted repeatedly; this was not seen.

### A change that was tried and removed

Far from the player, `MobileObject::Move` sets an actor's height to the navmesh height (`0x0092F9B7`) without comparing it with the land, unlike the near path. A clamp to the land height was added on that theory. It was active in every logged run, so no log shows it doing anything, and its first version crashed (it called `TES::GetLandHeightAtPosition` without the `TES` object). It was removed. If distant living creatures are seen standing below the surface on flat ground, this is the place to look.

## 14. Repairing the recovery instead of switching it off (version 112)

Sections 12 and 13 work, but they are workarounds: one disables an engine feature, the other corrects a bad state after it appears. This section records what static analysis found about the two open questions and the repair that follows from it. The static findings below were not checked at runtime. The repair itself (version 112) was confirmed in game by the user with the `killall` test on slopes: corpses no longer sink, stretch or twitch. A corpse against a rock was also checked and looked correct. Walls and other objects were not tried.

### Why terrain does not push out a bone that starts below it

- Terrain collision is built in `bhkTriSampledHeightFieldBvTreeShape_Init` (`0x00CB0820`) as a `hkpTriSampledHeightFieldCollection` with a sphere radius and nothing else.
- The collection constructor (`0x00D26700`) sets its triangle extrusion (`+0x30`) to zero. Each triangle it hands out (`0x00D26510`) is therefore a flat sheet with no thickness.
- The triangle query (`0x00D26E50`, extents from `0x00D26940`) clips the body's box to the height range of the whole height field and, when bounds checking is on, returns no triangles if every sampled height is above the box. A body wholly below the surface gets no triangles at all.

So a bone below the surface is either ignored or, where it touches the sheet, pushed to the side it is already on. Havok's remedy is the extrusion, which makes each triangle a prism reaching down. Using it here would need three changes in code that every physics object runs through: set the extrusion, widen the query box by it, and lower the terrain's bounding box. That affects all terrain collision for everything in the game and cannot be checked without a running game, so it was not attempted.

### What moved the fallen corpse back up

Not found. It was only seen after a corpse had fallen through the terrain, which no longer happens.

### The repair

The recovery's design is sound in outline: find bones on the wrong side of a surface, let them pass through it, move them out. Its defect is that it removes collision from a whole subtree and then pushes one bone of it. The subtree's other bones are in free fall with no collision.

- **Carry the whole subtree.** After the stock update, for each ragdoll with a penetration detected this frame (`entry +0x20`), take the average of the contact normals (`entry +0x14` records of 48 bytes, normal at `+0x20`; count at `entry +0x18`) and set the linear velocity (`hkpRigidBody +0x1B0`) of every bone that has filter bit `0x4000` to `fDesiredVel` along it. If the normals cancel out, the direction is straight up. Setting the velocity outright, not adding to it, also removes gravity for these bones, which have no floor under them while their collision is off. The stock impulse on the crossing bone is left in place and simply overwritten.
- **End it after the detection window.** A detour on the detection call (`0x00CA175E`; `EBX` is the util, `+0x20` window start, `+0x28` window length) reports "clear" once the window has passed. The stock code then clears bit `0x4000` on every bone. At 1.5 m/s for 5 s a ragdoll can travel 7.5 m at most.
- **The lift is removed (version 112).** Section 13's lift existed because, with the recovery switched off, nothing got a buried bone out. With the recovery repaired that is its job: a ragdoll with bones on both sides of the terrain has a bone line crossing it, the detection flags those bones, and they are carried up until nothing crosses. Keeping the lift as well would hide whether the recovery works, since the lift acts first. If a corpse is still buried when the window ends, nothing moves it any more.

### Version 113: the whole ragdoll, not a subtree

Version 112 carried the bones the detection had marked: the crossing bone and the bones attached below it in the skeleton. A corpse that was mostly under the terrain, with only a limb above it, was reported still twitching in the ground.

That is the case where the buried part is the root side. The crossing is between a buried parent and a child above the surface; the detection marks that parent and everything below it, but not the parent's own ancestors. Those keep their collision, stay trapped under the terrain, and hold the carried bones back.

Now, when the detection reports a penetration, every bone that it left unmarked is marked as "attached" (state 2, at `controller +0x18`, count at `+0x1C`). The stock code then disables collision on the whole ragdoll and the carry step moves all of it as one rigid body until no bone line crosses anything. This was not confirmed in game at the time of writing.

### Risks

- The carried bones pass through everything along the chosen direction. The direction comes from the ray cast in `0x00CA1C50`, which takes the first hit in collector order, not the nearest. A wrong normal sends the ragdoll the wrong way for up to 5 s.
- Only other actors are carried. The player's ragdoll goes through `0x0093F8C2` and keeps the stock push, now with the time limit.
- The recovery only starts inside the detection window, which opens when the ragdoll is set up or loaded. A corpse that becomes buried later is not helped.

### How to check it

First repeat the `killall` test on slopes: corpses must not sink, stretch or twitch. Then kill something so that it falls against or into a rock, wall or large object, and watch for 5 s. Expected: the part that is inside slides out and the corpse settles. Wrong: the corpse slides away through geometry, hovers, or ends up inside the object. If it is wrong, go back to version 110 (branch `tilt-sample-span`), where the recovery is switched off.
