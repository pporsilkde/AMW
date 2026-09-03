## Y028s
- Floating damage numbers are smaller (15 px), red, and use a negative HP format such as `-12`.
- Hostile overhead HP is thinner (3 px near / 2 px minimum far) while the docked HUD bar remains 9 px.
- The decorative HP frame fades continuously with dockBlend; it still cannot render without a positive fill.

## Y027s
- Combat HP frame cannot render without a positive fill width.

## Arena Y025s — combat HP cell reset / damage-number recovery

- Hard reset of combat HP slots on CellStore changes.
- Valid-health ownership gate prevents empty combat frames.
- Floating damage numbers no longer depend on transient crosshair visibility or a non-empty weapon Ptr.

## Arena Y024s — packaging sync

- No ArenaMW gameplay change in Y024; the functional change is ArenaMP-only chat behaviour.
- All Y023s combat HP fixes remain unchanged.

## Arena Y023 — direct combat HP fill

- Combat HP no longer uses MyGUI::ProgressBar or Track state.
- Overhead enemy HP is a thin red direct-width fill with no frame.
- The frame is a separate widget shown only after docking into the HUD.
- Last verified HP is retained through the existing short linger/fade window.
- A living actor always gets at least a 1 px fill, preventing an empty framed bar.

## Arena Y022s

- Replaced distance-based combat ProgressBar skin swapping with one permanent `Arena_Progress_Red_Combat` skin.
- Aggressive NPC overhead HP is always a thin 3–4 px red line with no decorative frame.
- The frame is a permanent child widget and fades in only near the end of docking into the HUD stack above stamina.
- Removed combat `changeWidgetSkin()` calls, so MyGUI no longer destroys/recreates ProgressBar Track while a fight is running.
- Combat HP slots are enemy-only; Y021 floating damage numbers remain enabled.

## Arena Y021s

- Added compact floating weapon-damage numbers beside the crosshair.
- Numbers use final physical HP loss after resistance, armor and difficulty scaling.
- Misses, blocks, zero damage and spell damage stay silent.
- Numbers alternate left/right, drift upward/outward and fade in about 0.85 seconds.


# ArenaMW changelog

## Y017 — combat HP readability, healing feedback, ammunition and gold weight

- Kept the overhead enemy HP fill readable at long distance by clamping only the far-distance height to 4 px; close/medium presentation is unchanged.
- Health increases now wake the auto-hidden HP HUD; Magicka and Fatigue still ignore passive increases.
- Successful local bow/crossbow/thrown releases show `-1 <item name> (<remaining>)` in the right-side HUD feed.
- `gold_001` has an engine-enforced weight of `0.0001` per coin.


## Y014s — class-weighted Skill Point costs

- XP skill purchases now keep the existing base 1/2/3/4 SP curve by current skill value, then apply the player class importance multiplier: Major ×1, Minor ×2, Misc ×3.
- The Statistics window tooltip and +1 button use the same class-aware cost as the actual purchase, so displayed and charged SP cannot diverge.
- All Y013s HUD dedupe and Y012s water/XP/death/jail fixes remain included.


## Y013s — HUD item notification dedupe

- Harvest and scripted AddItem changes for the player no longer also create the old centered sNotifyMessage60/61 MessageBox when the right HUD item feed already reports the inventory delta.
- All Y012s water/XP/cooldown fixes are preserved.


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

## Y018 — weather fog / land optimization stability
- Fixed weather fog flicker/popping while adaptive land optimization changes the live view distance.
- Preserve fog depth values above 1.0 instead of clamping them to 1.0 during far-plane updates.
