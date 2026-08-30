# ArenaMW / ArenaMP — дорожная карта оптимизаций по мотивам MGE XE

Версия плана: **X028 / Stage 0**

## 0. Что уже есть в ArenaMW/ArenaMP и не требует повторного порта

Текущий движок уже использует ряд идей, схожих с MGE XE:

- `ObjectPaging` группирует дальние статические объекты, использует `FLATTEN_STATIC_TRANSFORMS`, `REMOVE_REDUNDANT_NODES` и `MERGE_GEOMETRY`.
- Terrain уже построен на quadtree + LOD и имеет отдельный buffer cache.
- Terrain index buffers уже выбирают `DrawElementsUShort` либо `DrawElementsUInt` в зависимости от числа вершин — отдельный «порт narrow indices» не нужен.
- Groundcover уже использует GPU instancing через `setNumInstances` + per-instance attributes/divisors.
- Есть CPU software occlusion через Intel Masked Occlusion Culling (MOC), включая terrain и крупные static occluders.
- SceneManager уже кэширует templates/instances и делит StateSet через shared-state manager.
- OpenMW сам владеет OpenGL render pipeline, поэтому MGE/DXVK-способ «native depth capture вместо повторного D3D replay» здесь не переносится буквально: проблема MGE возникает из-за перехвата чужого D3D8 renderer.

## 1. X028 Stage 0 — ВКЛЮЧЕНО СЕЙЧАС: incremental TerrainOccluder cache

Изменённые engine-файлы:

- `components/terrain/terrainoccluder.hpp`
- `components/terrain/terrainoccluder.cpp`
- `apps/openmw/mwrender/occlusionculling.cpp`

Что изменено:

1. Terrain occluder теперь кэширует грубую occlusion mesh **по отдельным LAND-cell**.
2. Переход в соседнюю ячейку больше не декодирует и не строит заново весь квадрат occluder radius.
3. При стандартном `occlusion terrain radius = 8` область содержит 17×17 = **289 cells**.
4. При перемещении на одну соседнюю ячейку пересекаются 16×17 = **272 cells**, поэтому дорогая часть строится только для новой полосы из **17 cells** вместо 289 — примерно **94% меньше cell rebuild work** на обычной границе ячейки.
5. Кэш ограничен вокруг текущей позиции: старые cells удаляются, поэтому он не растёт бесконечно во время путешествия.
6. Пока игрок остаётся в той же terrain-cell, `SceneOcclusionCallback` больше не очищает и не копирует большие массивы `mPositions/mIndices` каждый кадр. Они обновляются только при реальном изменении региона.
7. При смене occlusion LOD cache полностью инвалидируется.
8. Геометрия occluder и логика conservative min-height сохранены; меняется способ её повторного использования, а не критерий видимости.

Ожидаемый эффект: меньше CPU-spike/stutter при пересечении exterior cell borders с включённым software occlusion. Сам MOC rasterization terrain mesh всё ещё выполняется каждый кадр — это следующий уровень оптимизации.

## 2. Stage 1 — Terrain Horizon Culling перед MOC (высокий приоритет)

Цель: адаптировать идею `horizon-culling.md` под OpenMW/OSG, не переносить D3D/DXVK-код.

План:

- Создать фиксированную max-height grid по LAND, независимо от render LOD.
- Построить 2×2 max-reduction pyramid для быстрых upper-bound запросов.
- Для позиции камеры строить polar horizon table по азимутам/кольцам.
- Перестраивать таблицу только при заметном перемещении глаза; rotation таблицу не инвалидирует.
- Для дальних ObjectPaging chunks сначала делать дешёвый horizon reject по AABB/bounding footprint.
- Только прошедшие horizon gate объекты отдавать в текущий MOC/OSG traversal.
- Fail-open: любое отсутствие terrain data, NaN, неполный footprint или stale table => объект считать видимым.
- Добавить adaptive gate: отключать horizon при быстром движении/низком проценте отсечения.
- Добавить stats: tested / culled / build time / saved traversal.

Почему это полезнее прямого GPU occlusion: нет readback/sync stall и одинаковая архитектура подходит PC + Android.

## 3. Stage 2 — Static occluder template cache (высокий приоритет, низкий/средний риск)

Сейчас simplified occluder для paged statics строится из уже трансформированного экземпляра. Следующий шаг:

- один раз строить simplified local-space occluder mesh на template/model;
- к каждому instance применять только transform;
- cache key: template pointer/model + mesh resolution + shrink factor;
- разделить immutable vertex/index topology и instance world bounds;
- ограничить cache и добавить hit/miss counters.

Это уменьшит CPU при генерации ObjectPaging chunks в городах и модифицированных областях с большим количеством повторяющихся объектов.

## 4. Stage 3 — Cumulative static LOD внутри уже объединённого batch (высокий FPS-потенциал)

MGE XE сохраняет provenance исходных компонентов внутри merged mesh и меняет только число рисуемых faces для Near/Far/VeryFar, не разбивая batch на новые draw calls.

Для ArenaMW/ArenaMP:

- при `MERGE_GEOMETRY` сохранять диапазоны индексов по исходному static component;
- классифицировать component по projected/min-size и дистанционным tier;
- упорядочить index buffer как `very-far | far-only | near-only`;
- хранить три cumulative index counts;
- на chunk LOD выбирать count без создания дополнительного StateSet/draw group;
- billboard/animated/alpha-special objects оставить на существующем fallback.

Цель: уменьшить triangle throughput дальних городов/архитектуры **без возврата draw calls**.

## 5. Stage 4 — Улучшение state batching без texture atlas (средний приоритет)

Полный MGE-style texture atlas пока не переносить: OpenMW должен учитывать arbitrary texture wrapping, animated textures, normal/specular/PBR maps и пользовательские shaders.

Безопаснее сначала:

- усилить группировку merged geometry по стабильному material/state key;
- заранее сортировать static subsets по StateSet/Program/texture-set;
- не переключать одинаковые states между соседними merged drawables;
- добавить profiler counters: drawable count, unique StateSet, texture/program switches.

После профилирования решить, нужен ли atlas только для строго совместимого distant-static subset.

## 6. Stage 5 — GPU skinning / indexed skinning для актёров (очень высокий потенциал в ArenaMP)

Текущий `SceneUtil::RigGeometry` выполняет CPU skinning видимого mesh каждый кадр, модифицирует positions/normals/tangents и dirty-ит dynamic VBO.

План OpenGL-реализации по идее MGE indexed skinning:

- при загрузке mesh один раз упаковать bone indices + weights в vertex attributes;
- bone matrices передавать в shader palette (uniform array/UBO/SSBO в зависимости от backend);
- vertex shader вычисляет skinned position/normal/tangent;
- оставить CPU `RigGeometry` как строгий fallback;
- capability gate по GL/GL ES;
- отдельный небольшой palette path для Android/ng-gl4es;
- сначала включить только NPC/creatures с совместимыми skin partitions;
- сравнивать CPU frame time в сценах 10/25/50 actors.

Для ArenaMP это один из самых перспективных CPU-пунктов после culling, потому что multiplayer чаще держит много видимых актёров.

## 7. Stage 6 — Persistent/incremental ObjectPaging cache (средний приоритет)

Адаптировать не сам Rust generator MGE, а принцип incremental rebuild:

- fingerprint = load order + ESM timestamps/hashes + model metadata + relevant graphics settings;
- сериализовать готовые distant page descriptors/merge metadata на диск;
- при старте валидировать только dirty cells/models;
- GPU objects всё равно создавать runtime, но тяжёлый ESM scan/merge planning не повторять без причины;
- cache versioning и fail-safe delete/rebuild.

Главная выгода: меньше stutter/loading, особенно на больших mod lists.

## 8. Stage 7 — Distant texture atlas только после профилирования (низший приоритет / высокий риск)

Возможен отдельный atlas generator только для static materials, удовлетворяющих строгим условиям:

- clamp-compatible UV;
- нет animated texture/controller;
- одинаковый shader/material contract;
- согласованные diffuse/normal/specular/PBR pages;
- mip-safe border padding;
- fallback на обычный StateSet для несовместимых объектов.

Это может дополнительно сократить draw/state switches, но внедрять раньше Stage 1–5 нецелесообразно.

## 9. Что из MGE XE НЕ переносить напрямую

- `d3d8.dll`, D3D proxy/interception, Morrowind memory patches.
- custom DXVK `d3d9.dll` и private interop API.
- `.fx` shaders как готовые OpenGL shaders.
- Morrowind-specific fixed-function emulation.
- MWSE GUI/Lua integration.
- MGE BSA/texture loader вместо текущего VFS/ResourceSystem.
- native depth replay workaround — архитектурно другая проблема.

## 10. Порядок реализации

1. **X028** incremental TerrainOccluder cache — сделано.
2. **X029** static occluder template cache + occlusion profiler counters.
3. **X030** terrain horizon prototype, выключенный настройкой по умолчанию для A/B проверки.
4. **X031** horizon adaptive gate + quadtree/ObjectPaging integration.
5. **X032** cumulative static LOD inside merged batches.
6. **X033** state batching profiler/optimizer.
7. **X034+** GPU skinning prototype PC, затем Android-compatible fallback.
8. Persistent paging cache и texture atlas — только после измерений предыдущих этапов.

## 11. Как мерить результат

Для каждого этапа фиксировать одинаковые save/camera paths:

- Balmora/рынок: drawables, draw calls, CPU render/update time.
- Vivec: occlusion tested/culled, ObjectPaging generation spikes.
- горный exterior: эффективность horizon culling.
- 10/25/50 NPC: CPU skinning/update time.
- Android: frame-time 1% low, RAM, GL errors, temperature/throttling.

Не принимать оптимизацию только по среднему FPS: обязательно смотреть frame-time spikes, 1% low, RAM/VRAM и visual correctness.

## Reference-файлы MGE XE внутри cumulative

В `docs/porting/MGE-XE_REFERENCE_GPLv2/` скопирован только релевантный subset: документы horizon/static LOD/indexed skinning/native depth/incremental generation, horizon host sources, visible-set batching, grass instancing, distant statics/terrain и Morrowind indexed-skinning reference.

Файлы помещены **только как reference и не подключены к CMake**. В пользовательском архиве MGE XE лицензия заявлена как GPL v2, тогда как Arena/OpenMW дерево содержит GPL v3; поэтому прямой verbatim перенос MGE кода в компилируемую часть намеренно не сделан. X028 engine-код реализован отдельно на API OpenMW/OSG.
