# ArenaMP → ArenaMW port boundary

ArenaMW ports behavior, not multiplayer ownership.

## Safe/appropriate to port
- Renderer and scene-graph fixes that do not depend on peer/server lifetime.
- Local UI/layout/localisation improvements.
- Local gameplay formulas, AI decisions and interaction behavior after removing server-authority waits.
- Launcher/wizard improvements that operate on OpenMW local configuration.

## Do not port directly
- Packet types, `mwmp`, RakNet, server browser/master server code.
- Server CoreScripts, server JSON ownership, group/party/friendly-fire state.
- Multiplayer quest transport/Quest Studio server authority.
- Player-list/Player Menu chat/group systems.
- Actor/player authority handoff, position packet safety, reconnect/server-restart recovery.

## Y001s decisions after ArenaMP X033
- X034–X039: multiplayer combat-state/quest-authority work — not ported.
- **X040 OSG threading + CharacterPreview render lifetime — ported.** The configured viewer threading model is now honored and preview RTT graphs use delayed renderer-safe retirement.
- **Project Magnus clustered lighting — ported.** The implementation is renderer-only; it is exposed as an optional desktop OpenGL 4.3 lighting method and keeps the existing compatibility fallback.
- X041 launcher restructuring — not copied wholesale because ArenaMW's standalone Arena Settings page is functional and owns local XP controls. The useful graphics-save safety is implemented directly in Y001s.
- X042–X043 quest vocabulary/instances remain server-oriented and are not ported; the independent X042a MSVC graphics-expression compile fix is ported.
- X044 authority/idle synchronization — multiplayer ownership behavior, not copied.
- X045–X048 Quest Studio/quest synchronization and its Player Menu/QuickLoot integration are server-quest features, so they are not copied into standalone ArenaMW.
- X049–X057 Player Menu, groups, chat, restart position, party/summon server cleanup — not applicable to single-player.
- ArenaMP Y001 graphics/HUD corrections — ported in standalone form; F3 remains HDR.
