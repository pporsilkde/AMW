# ArenaMW Y001s — изменённые исходники

Y001s является полной очищенной source-базой, а не patch-only архивом. Ниже перечислены основные файлы, изменённые относительно загруженной ArenaMW X033-era базы.

## Launcher / settings

- `apps/launcher/graphicspage.cpp/.hpp` — merge-safe сохранение Graphics/Quality, baseline UI, Project Magnus lighting option, X042a Water/MSVC fix.
- `apps/launcher/advancedpage.cpp/.hpp` — baseline-aware сохранение только реально изменённых Advanced-параметров.
- `apps/launcher/maindialog.cpp` — проверка результата merge-safe Graphics save до финальной записи.
- `files/ui/graphicspage.ui` — дополнительный `clustered (Project Magnus)` lighting mode.

## HUD / runtime

- `apps/openmw/mwgui/hud.cpp` — явный HUD caption `FPS: N`.
- `files/mygui/openmw_hud.layout` — размер/позиция нового FPS widget.
- `apps/openmw/engine.cpp` — одноразовая миграция HUD FPS, применение OSG threading model, X040 preview retirement в frame loop/shutdown.
- `files/settings-default.cfg` — Y001s migration marker для HUD FPS.
- `files/vfs/l10n/arenamp/en.ini`, `ru.ini` — корректная подсказка FPS без привязки к F3 и строки Project Magnus.

## X040 render safety

- `apps/openmw/mwrender/characterpreview.cpp/.hpp` — delayed retirement RTT/OSG graph для CharGen/Inventory preview.

## Project Magnus clustered lighting

- `components/sceneutil/lightmanager.cpp/.hpp` — Clustered backend, GL 4.3 capability check/fallback, SSBO/compute light clustering.
- `components/shader/shadermanager.cpp` — compute-only program support (nullable vertex/fragment shader pointer).
- `apps/openmw/mwrender/renderingmanager.cpp` — передача ShaderManager в LightManager и принудительный per-pixel shader path для clustered.
- `apps/openmw/mwgui/settingswindow.cpp` — Clustered в runtime lighting selector только при поддержке backend.
- `files/shaders/CMakeLists.txt` — упаковка Magnus shader resources.
- `files/shaders/magnus_cluster.comp`, `magnus_cull.comp`, `magnus_water.glsl` — новые Magnus resources.
- `files/shaders/groundcover_vertex.glsl`, `groundcover_fragment.glsl`, `lighting.glsl`, `lighting_util.glsl`, `nv_default_fragment.glsl`, `objects_fragment.glsl`, `terrain_fragment.glsl`, `water_fragment.glsl`, `water_pbr_fragment.glsl` — интеграция Clustered lighting в существующие material/water paths.

## Документация / cleanup

- Новые: `README.md`, `README_RU.md`, `ARENAMW_SOURCE_VERSION.txt`, `CREDITS.md`, `THIRD_PARTY_NOTICES.md`, `SECURITY.md`, `docs/BUILDING*.md`, `docs/FEATURES*.md`, `docs/ARENAMP_PORTING.md`, `docs/CLEANUP_Y001s.md`, `docs/VALIDATION_Y001s.md`.
- История upstream OpenMW перенесена в `docs/upstream/`.
- Удалены `.git`, старые root-level `X0xx` patches/notes/harnesses, старые SHA/changed/deleted manifests и legacy Travis/GitLab/AppVeyor/ReadTheDocs конфиги.

## Не переносилось намеренно

TES3MP/RakNet, `mwmp`, server CoreScripts, Quest Studio/server quests, Player Menu/chat/groups, multiplayer authority/position/reconnect logic и party/summon server cleanup в ArenaMW Y001s не добавлялись.
