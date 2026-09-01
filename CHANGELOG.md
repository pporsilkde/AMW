# ArenaMW changelog

This file consolidates ArenaMW development notes that previously existed as separate root-level patch, review, checksum and harness files. Original OpenMW 0.47 history is preserved in `docs/upstream/OPENMW_CHANGELOG.md`.

## Y003s — native guar riding foundation

### Added
- Engine-level riding for any live creature reference whose base/ref ID is `guar_pack`; no ESP, OMWAddon or OpenMW Lua runtime is required.
- Activating `guar_pack` mounts it, redirects forward/back/run/autorun and A/D steering to the guar, and keeps mouse/stick yaw steering available.
- Sneak is the dedicated dismount action. A release latch prevents the same press from immediately leaving the player crouched after dismounting.
- The rider remains an ordinary player actor and can use the existing weapon/spell combat path while mounted.
- The ridden guar remains an ordinary damageable creature: its existing health/death mechanics are authoritative, and death automatically forces a safe dismount.
- A green mount-health row is pinned directly above the stamina area while riding, even when ordinary NPC combat bars are disabled.

### Engine behavior
- The guar's existing AI sequence is preserved. Rider locomotion/steering overrides AI movement after actor AI and immediately before physics, so Wander/Combat resumes naturally on dismount; the player is attached to the guar after physics.
- The player's internal movement collision/gravity is disabled while mounted but its external collision body stays enabled, so mounted combat does not make the rider non-targetable.
- Doors/containers are not activated while mounted in this first foundation build; dismount first. Exterior cell traversal remains possible.
- Y003s intentionally reuses the normal player/guar models and animations. Dedicated seated animations/models, mounted inventory actions and save-game persistence of the mounted state are later layers, not required by this engine-only foundation.

## Y002s — CharGen crosshair continuity

### Changed
- The HUD crosshair is forced visible for the complete single-player character-creation/registration flow while `chargenstate != -1`, including modal CharGen GUI stages that normally suppress the camera reticle.
- As soon as character creation finishes (`chargenstate == -1`), crosshair visibility returns to the normal camera, GUI-mode and `[HUD] crosshair` preference rules.

## Y001s — clean standalone source + launcher/FPS/preview safety

### Added
- Clean EN/RU landing documentation and standalone build/feature/porting documents.
- `ARENAMW_SOURCE_VERSION.txt` as an explicit source snapshot marker.
- ArenaMP X040 render safety: apply the configured OSG viewer threading model and retire CharacterPreview/InventoryPreview/CharGen RTT graphs through a delayed render-safe queue.
- Optional Project Magnus clustered-lighting backend, compute shaders and shader integration from the current ArenaMP renderer, with OpenGL 4.3 capability fallback.
- X042a MSVC compile correction for the launcher Water quality expression.

### Changed
- Graphics page save is merge-safe: only controls changed relative to the page snapshot are replayed over the newest `settings.cfg`.
- Advanced page saving is baseline-aware as well: only controls actually changed on Advanced are replayed, preventing stale overlapping values from undoing Graphics presets on Play.
- HUD FPS caption is explicit (`FPS: N`) and the first Y001s run migrates older profiles to show the new HUD counter once.
- Stale localisation text claiming F3 displays FPS was replaced; F3 remains HDR and F4 remains Bloom.
- Root documentation was reorganized; upstream OpenMW history is under `docs/upstream/`.

### Removed from the source distribution
- Embedded `.git` history.
- Historical `X0xx_*.patch`, patch-info, changed/deleted-file manifests, SHA manifests and one-off logic harnesses.
- Legacy Travis CI, GitLab CI, AppVeyor and ReadTheDocs root configuration unused by the ArenaMW release workflow.

## X030–X033 — launcher quality, occlusion, shadows and water
- Added hardware-aware quality profiles and standalone XP controls.
- Added conservative CPU occlusion budgets/caching and launcher controls.
- Corrected Auto preset/shadow behavior and linked shadow reach to viewing distance.
- Added complete manual water controls and quality-dependent PBR/simple water presets.

## X021–X029 — Arena UI/combat presentation and rendering optimization
- Refined compact combat/health bars, dialogue docking/pose timing and CharGen presentation.
- Ported compatible combat AI improvements from ArenaMP without network authority code.
- Established CPU occlusion culling and terrain-occluder optimization stages.

## X012–X020 — standalone journal/topic and interaction hardening
- Added local topic recovery and journal robustness for the Arena UI flow.
- Continued conversion of multiplayer-origin systems to direct local single-player ownership.

## Foundation through X011
- Established ArenaMW as a standalone OpenMW 0.47 branch.
- Ported Arena gameplay balance, XP leveling, equipment requirements, Refined Alchemy/poisons, interaction animations, Arena dialogue/HUD, QuickLoot, HDR/Bloom/PBR/water work, object placement and related single-player systems.
