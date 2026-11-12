# Changelog

Each release needs a `## <version>` section here matching `VERSION`. The release workflow publishes that section as the GitHub release notes and fails if it is missing.

## 2.2

- Reverted the 2.0 tilt-angle change: it could make distant creatures twitch and rotate sharply on rocks, and did not improve their appearance enough to justify these problems.
- Fixed: creatures could hop instead of being knocked back when killed. Penetration recovery no longer erases the killing blow's knockback.

## 2.1

- Fixed: the player's own ragdoll could lose collision and fall through the ground.
- Better compatibility with other NVSE plugins that change the same parts of the game.
- New log file `logs\CreatureTerrainTiltFix.log` in the game folder. It says whether the fixes were installed and, if another plugin got in the way, where.

## 2.0

- Fixed: a corpse lying partly in the ground could lose all collision, fall through the terrain and be dragged back stretched. The engine's ragdoll penetration recovery now moves the whole ragdoll out together, and stops after `fDetectionUpdateTimeSec`.
- Fixed: a creature that died with part of its body under the terrain stayed half buried and twitched. The repaired recovery now carries it out.
- Fixed: creatures tilted less than the slope they stood on, so their uphill legs sank into the ground. The engine divided the height difference between its ground samples by the controller's full length, not by the distance between the samples.

## 1.0

- Initial release. Fixed: creatures with horizontal (probe) character controllers kept the pitch and roll of terrain they had already left while moving far from the player. The cached support heights are now refreshed before each distant update.
