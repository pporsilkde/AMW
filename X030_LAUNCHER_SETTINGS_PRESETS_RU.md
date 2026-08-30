# X030 — Launcher Settings / Quality Presets / XP Controls

## Launcher
- Added Arena Settings tab with XP speed profiles: 0.50x / 0.75x / 1.00x / 1.50x / 2.00x / Custom.
- Exposes overall XP rate, skill-use-only XP rate, base XP requirement, skill points per level, death XP loss, difficulty scaling, progressive curve and notifications.
- Exposes X029 `occlusion terrain cell budget` (0..128).
- ArenaMP keeps server authority; `server/scripts/config.lua` now has `arenaXpRateMultiplier` as the matching one-line server knob.

## Graphics quality
- Added explicit Water rendering selector: Simple (legacy/faster) or PBR / New.
- Minimum and Low presets force Simple water. Balanced+ use PBR/New water.
- Presets now tune water ripples/refraction scale and X029 terrain-occluder build budget.
- Minimum/Low disable enhanced PBR lighting; Balanced+ enable it.
- Object shadows start at Medium, terrain shadows at High.

## Shadow/view-distance link
- New `[Shadows] link shadow distance to viewing distance = true`.
- When linked, shadow distance = `min(viewing distance, 16384)`.
- Applies when a graphics preset changes view distance and when Advanced -> Viewing distance is edited manually.
- Renderer also enforces the link at runtime, so adaptive/view-distance updates cannot push shadows beyond 16384.
- Manual shadow-distance spinbox is capped at 16384 as well.

## Notes
- ArenaMP remote servers can override `[XP Leveling]` values by design. The launcher values are local/default; server admins should set `config.arenaXpRateMultiplier` for authoritative XP speed.
