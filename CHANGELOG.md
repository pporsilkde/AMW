# ArenaMW changelog

## Y012s — shader-water ripple isolation + XP HUD/penalties

- Legacy osgParticle movement rings are now disabled whenever shader water is active; turning shader ripples off no longer re-enables the old effect.
- XP cards are iconless and localized. Gains use green text/backing, losses red, neutral XP statuses black.
- Death/jail wipe all current-level XP only. Levels, Skill Points and skills are preserved; XP-mode jail skips vanilla random skill changes.
- A death at exactly zero XP adds current level × 5 seconds to the ordinary respawn delay.

## Y011s — unified HUD notifications

- Replaced the three-band Y010s event-card shadow with one uniform medium-opacity `BlackBG` backing (`0.22` alpha).
- Arena XP gameplay rewards now appear in the same right-side event feed, with same-reason coalescing and fractional XP support.
- XP level/system notifications also use the feed during gameplay; XP feedback produced from an open GUI remains a MessageBox so menu interactions stay visible. Vanilla/scripted game MessageBoxes are unchanged.
- Riding remains rolled back; the cumulative gameplay base is still Y002s.


## Y010s — HUD event-feed visual polish

- Moved event cards to a 2 px right-edge margin instead of inheriting the stamina bar's horizontal inset.
- Removed the framed `HUD_Box_Transparent` skin from event cards.
- Added a resource-free three-step `BlackBG` shadow, fading from lightly transparent on the left to semi-transparent at the right edge.
- Pickup and gold cards now use `+delta`; when the stack already existed, the committed inventory total is appended as `+5 (10)`.
- Coalesced repeated pickups keep accumulating the delta while refreshing the displayed committed total.


## Y009s — HUD/event-feed stability

- Ported the pooled combat-bar render-time Track reassertion from ArenaMP, including hiding a newly reused/re-skinned slot until verified HP exists for that actor.
- MyGUI 3.2.2 range/position setters are reasserted directly at render time; no fake intermediate range is required.
- Magic-effect notification durations now count down live and identify the exact stacked ActiveSpells entry by id, caster and timestamp.
- Inventory clear/refill protection now triggers only after a strong loss-of-existing-items signature, so large legitimate Take All batches are not suppressed merely for containing many item kinds.
- HUD event-feed fallback coordinates are relative to `mGameplayHud`.
- Riding remains rolled back; gameplay baseline stays Y002s plus cumulative HUD improvements.

## Y007s — HUD event feed

- Added a fixed six-slot RPG-style HUD event feed above the stamina/combat-bar stack.
- Positive inventory changes show the real item icon, localized name and gained quantity.
- Repeated item/gold gains aggregate into the existing live card.
- Newly applied lasting spell/potion effects show their magic-effect icon and remaining duration.
- The feed automatically moves above docked NPC combat bars and allocates no widgets during gameplay.
- Riding remains rolled back; this branch stays on the Y002s gameplay baseline plus the HUD feed.

This file consolidates ArenaMW development notes that previously existed as separate root-level patch, review, checksum and harness files. Original OpenMW 0.47 history is preserved in `docs/upstream/OPENMW_CHANGELOG.md`.

## Y006s — riding rollback

- Rolled back the experimental Y003s-Y005s riding subsystem.
- Restored the clean Y002s gameplay/source baseline while riding is redesigned separately.
- Removed all riding-only C++ hooks and bundled riding NIF/KF/DDS resources.

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
