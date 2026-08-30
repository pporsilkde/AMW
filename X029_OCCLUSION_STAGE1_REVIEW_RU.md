# ArenaMW / ArenaMP X029 — Occlusion Stage 1 (reviewed safe port)

Основа: X028 MGE OPT Stage 0.
Источник идеи/входного патча: ArenaMP_X029_OCCLUSION_STAGE1.zip.
Статус: порт принят, но доработан перед включением в cumulative.

## Оценка

Stage 1 ценен прежде всего для сокращения frame-time spikes при входе в exterior и при появлении большого количества повторяющихся крупных статиков. Это не гарантированный прирост среднего FPS: эффект зависит от сцены и должен измеряться через добавленные Occl-счётчики.

## Что включено

1. Terrain occluder cell budget
- новая настройка `[Camera] occlusion terrain cell budget = 24`;
- на холодном входе radius=8 вместо 289 LAND-cell decode за один cull ограничение составляет максимум 24 новых ячейки за build;
- неполный occluder fail-open: временно отсекает меньше, но не должен скрывать видимое;
- при обычном переходе на соседнюю ячейку после прогрева X028 остаётся около 17 новых ячеек.

2. Occluder template cache
- упрощённый occluder повторяющейся модели строится один раз в model-space;
- на экземпляр выполняется только transform готовых вершин;
- кэш ограничен 4096 шаблонами;
- hit/miss counters доступны в статистике.

3. Исправление Objects::updatePtr
- создание нового Cell Root теперь всегда проходит через общий createCellNode();
- новый узел больше не теряет CellOcclusionCallback при перемещении объекта в ещё не созданную cell-node.

4. Occlusion profiler
- Occl Tested
- Occl Culled
- Occl Occluders
- Occl Tris
- Occl Terrain Cells
- Occl Terrain Built
- Occl Templates
- Occl Tpl Hits
- Occl Tpl Misses

## Что исправлено относительно присланного X029

### A. Template cache ограничен только ESM::Static
В исходном X029 исключались только actors. Это оставляло двери, activator/container и другие объекты с потенциально разным внутренним animation transform. Один экземпляр мог создать шаблон в одном состоянии, а другой получить его в другом. В reviewed port общий шаблон разрешён только для настоящего `ESM::Static`; остальные объекты используют прежний per-instance путь.

### B. Сохранён консервативный AABB
В исходном template-пути instance AABB строился из уже shrink-нутых вершин. Старый путь проверял несжатые границы объекта, а shrink использовал только для rasterized occluder mesh. Reviewed port трансформирует 8 углов исходного model-space AABB и использует их как conservative instance AABB. Это предотвращает слишком раннее отсечение самого объекта.

### C. Thread-safe profiler без atomic в hot path
Исходный X029 читал обычные cull counters и `mCellCache.size()` из stats/update потока. При многопоточном OSG это data race. В reviewed port:
- OcclusionCuller публикует snapshot предыдущего завершённого cull в relaxed atomics один раз на beginFrame();
- инкременты per-AABB остаются обычными unsigned и не получают atomic overhead;
- TerrainOccluder публикует cache-size/last-built snapshots из cull-потока.

### D. Cache pruning при незавершённом регионе
Исходный X029 prune делал только после полного построения региона. При быстрых teleport/cell jumps регион мог долго оставаться incomplete и старые cache entries могли накапливаться. Теперь prune выполняется и при incomplete build; keep radius = radius+2 гарантированно сохраняет все ячейки текущего региона.

### E. Cache key включает shrink factor
Ключ шаблона: model + grid resolution + shrink factor. Это защищает от переиспользования старого шаблона после изменения параметров occluder между вновь созданными cell callbacks.

## Настройка

По умолчанию:

    [Camera]
    occlusion terrain cell budget = 24

- 24 — рекомендуемый desktop старт.
- 12–16 — сильнее сглаживает холодную сборку, но дольше держит occluder неполным.
- 32–48 — быстрее завершает occluder, но повышает разовую CPU-нагрузку.
- 0 или меньше — поведение X028: весь отсутствующий регион строится сразу.

## Как оценивать

Открыть resource stats (обычно F4 / resource page) и смотреть совместно:
- frame time / spikes;
- `Occl Terrain Built`;
- `Occl Tpl Hits` / `Occl Tpl Misses`;
- `Occl Culled / Occl Tested`.

Высокий Templates hit-rate + снижение spikes означает, что Stage1 полезен для данной локации. Если средний FPS не меняется, но пропадают крупные пики при переходе cell — Stage1 всё равно выполняет основную задачу.

## Лицензия reference-файлов X028

В X028 cumulative ранее была добавлена некомпилируемая копия reference-файлов MGE XE. В присланном X029 верно отмечено, что MGE XE помечен GPL-2.0-only, тогда как это дерево распространяется под GPLv3. Поэтому X029 cumulative больше НЕ содержит `docs/porting/MGE-XE_REFERENCE_GPLv2/`.

Если X029 накладывается поверх уже распакованного X028, старую папку `docs/porting/MGE-XE_REFERENCE_GPLv2/` следует удалить вручную. На сборку она не влияет, но в новом cumulative она намеренно исключена.

## Проверки

- входной SHA256 manifest X029 проверен;
- standalone logic harness: radius=8 / budget=24 = 13 build frames; соседняя cell = 17 новых ячеек; budget<=0 = X028; commutation test и clustering conservativeness проходят;
- reviewed port применён к ArenaMP и ArenaMW;
- в ArenaMW изменённые renderer-файлы не содержат mwmp/TES3MP зависимостей;
- сохранены веточные различия ArenaMP/ArenaMW в renderingmanager/settings;
- выполнены structural checks и archive integrity checks.

Полную MSVC/Android сборку в этой среде не запускали.
