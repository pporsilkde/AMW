# Contributing to ArenaMW

Keep changes focused and preserve the standalone boundary.

A useful change description states: problem/expected behavior, implementation, affected platform/subsystem, save/config compatibility, exact build/test result, and screenshots for visible UI/render changes.

Rules:
- Do not reintroduce TES3MP/RakNet/server-authority dependencies into the standalone runtime.
- Port ArenaMP features only after separating local behavior from multiplayer ownership/transport.
- New Arena-owned player-visible strings must remain available in both EN and RU localisation where applicable.
- Preserve OpenMW/third-party license notices.
- Avoid broad formatting-only rewrites or checked-in build products.
- For render-thread lifetime work, test multithreaded OSG modes and shutdown.
- For launcher settings work, test against an existing profile and a `settings.cfg` modified while the launcher is open.

See `docs/BUILDING.md` and `docs/ARENAMP_PORTING.md`.
