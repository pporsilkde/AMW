# ArenaMW X031 — Occlusion Stage2

- Terrain occluder rasterizes cached LAND cells directly instead of assembling/copying one giant rebased region mesh.
- Side-plane frustum culling skips terrain occluder cells behind/outside the camera before software rasterization.
- New setting: `[Camera] occlusion terrain frustum cull = true`, exposed in Launcher Advanced/Arena settings.
- Graphics presets enable the new frustum culling automatically.
- Profiler adds `Occl Terrain Rast`.
- X029 atomic profiler snapshots are preserved.
- Reusable scratch LAND vertex storage reduces per-cell allocations.
- `skipOcclusion` named user-data lookup is delayed until after a small-object AABB actually fails software occlusion.

ArenaMP-only RAW config, questIndex, MMO multiplayer defaults and chat/protocol changes are intentionally not included in ArenaMW.

Validation: X031 logic harness passes budget/frustum tests, Qt UI XML parses successfully. Full engine compilation was not run in this environment.
