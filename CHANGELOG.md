# Changelog

Each release needs a `## <version>` section here matching `VERSION`. The release workflow publishes that section as the GitHub release notes and fails if it is missing.

## 1.0

- Initial release. Fixed: creatures with horizontal (probe) character controllers kept the pitch and roll of terrain they had already left while moving far from the player. The cached support heights are now refreshed before each distant update.
