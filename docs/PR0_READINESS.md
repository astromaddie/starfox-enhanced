# PR0 — starfox-enhanced

**EXPERIMENTAL.** Already public; no new binary release. See the [README](../README.md) for the port's features, controls, game-data requirements and build instructions.

| Record | Revision |
| --- | --- |
| Hygiene audit | `8c3c3ae587d9e2fc3ce346dc7193f9d7fe896c66` |
| LFS payload audit source | `4f1d5cad23a454c0ca632644bb5f1362a86009b8` |
| Build/test source | No fresh full-build SHA recorded in this audit. |

Target: Linux ARM64 / Steam Frame / OpenXR / Vulkan.

Both branches were mirrored. All 23 inventoried LFS payloads were downloaded through anonymous GitHub access and matched their recorded sizes and SHA-256 hashes: 928,120,303 bytes in total. No new application build is claimed for this audit.

The publication blockers are:

- `Starfox-MSU1.PAK` contains 52 FLAC tracks. Two historical Windows executables each embed 52 FLAC tracks. The music set is credited in the upstream notices, but its separate redistribution terms are unresolved.
- Two PS5 splash images have unresolved asset provenance. The other 18 LFS executables were checked by their file headers; their resources were not exhaustively unpacked.
- Generated `dist/StarFoxEnhanced` outputs and their reachable history need cleanup before a new source-only release. The repository was already public, and its visibility is unchanged.

Supported retail ROM is user supplied. The project guide describes supported revisions. No ROM or commercial asset redistribution right is implied. Keep the original upstream and component notices; they do not grant rights to the commercial game.

[Exact metadata](PR0_METADATA.json) · [Changelog](../CHANGELOG.md)
