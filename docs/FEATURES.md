# ArenaMW feature overview

## Standalone architecture
ArenaMW runs local OpenMW world state only. It does not build or require the TES3MP client, dedicated server, browser, RakNet packet layer or server-side Lua authority.

## Progression and gameplay
- Native XP leveling with level curve, skill-point progression, difficulty scaling and death XP loss.
- Equipment requirements, combat/economy rebalance, training/mercantile/stealth changes.
- Refined-Alchemy-inspired brewing, ingredient knowledge, potion rules and weapon poison charges.
- Automatic local resurrection at a suitable shrine instead of multiplayer respawn handling.

## AI and interaction
- Tactical combat behavior, strongest-attack selection, restorative spell preferences and collision/pathing tuning.
- Cross-door hostile pursuit and local route recovery where supported by the 0.47 world model.
- Dynamic NPC idles/walk styles, persistent poses and contextual item/container/book interactions.

## Interface
- Arena cinematic dialogue with integrated persuasion and keyboard navigation.
- Arena HUD, target information, combat bars, game time, QuickLoot and redesigned character creation.
- EN/RU Arena-owned interface localisation.
- Dedicated HUD FPS counter; F3/F4 remain HDR/Bloom.

## Rendering
- HDR, Bloom and SMAA/native post-process path.
- PBR-compatible material/lighting controls and quality profiles.
- Optional Project Magnus clustered point-lighting path on desktop OpenGL 4.3; it falls back to shader-compatible lighting when unsupported.
- Simple and new/PBR water paths, reflections/refraction/ripples and detailed launcher water controls.
- Dynamic shadows and draw-distance linking.
- CPU masked occlusion culling with conservative terrain/static budgets.
- Y001s/X040: launcher-selected OSG threading model is applied to the viewer, with render-thread-safe CharacterPreview retirement.

## Launcher and setup
- Hardware detection and Auto/Minimum/Low/Balanced/Medium/High/Ultra graphics workflow.
- Merge-safe Y001s manual graphics persistence.
- Portable `build.ini` support for build name, data path, language, content, groundcover and BSA archive order; multiplayer address/port fields are ignored.
- Installation wizard and Morrowind INI importer are retained.

## Experimental/limited areas
Ragdoll, some advanced rendering paths and Android/gl4es support remain experimental. The source contains Android/gl4es-related code, but Y001s release validation is centered on the Windows x64 workflow.

## Native riding (Y003s)

`guar_pack` can be ridden directly by the engine with no plugin content. Activation mounts, Sneak dismounts, movement is applied to the creature before physics, and the rider is reattached after physics. The mount retains normal creature HP/death and receives a dedicated docked HUD health row. This foundation intentionally does not yet serialize mounted state or provide a dedicated seated animation.
