# ArenaMW Y003s

**Русский** · [English](README.md)

ArenaMW — standalone/single-player ветка Arena на базе OpenMW 0.47.0. В неё входят совместимые геймплейные, интерфейсные, анимационные, AI и графические изменения ArenaMP/EncoreMP, но **без** TES3MP-сети, сервера, authority, групп игроков и серверных Lua-механик.

> Нужна легальная копия *The Elder Scrolls III: Morrowind*. Ресурсы Bethesda в архив не входят.

## Версия

| Компонент | Значение |
| --- | --- |
| Снимок ArenaMW | **Y003s** |
| Основа | OpenMW 0.47.0 |
| Игра | single-player `openmw` |
| Лаунчер | `openmw-launcher` |
| Сеть/сервер | отсутствуют |
| Основная релизная цель | Windows x64 |

## Что изменено в Y003s

- Добавлена нативная езда без ESP/OMWAddon: активируйте живого `guar_pack`, чтобы сесть на него.
- Вперёд/назад, постоянный бег, бег и поворот A/D управляют гуаром; кнопка крадучести используется для спешивания. Обычный бой оружием/магией игрока остаётся доступен верхом.
- Гуар сохраняет обычные HP и смерть существа. Во время езды его зелёная полоса здоровья всегда закреплена над областью stamina; смерть гуара автоматически сбрасывает всадника.
- Это первый движковый слой: используются обычные модели/анимации, а двери и контейнеры верхом пока не активируются. Отдельная сидячая анимация/модель и сохранение состояния «верхом» будут отдельным этапом.

## Что изменено в Y002s

- Прицел принудительно остаётся видимым на всём этапе создания/регистрации персонажа (`chargenstate != -1`), включая модальные окна CharGen. После завершения создания персонажа видимость прицела снова определяется обычной логикой камеры/GUI и настройкой HUD.

## Что изменено в Y001s

- Репозиторий очищен по принципу X057 ArenaMP: удалены вложенная история `.git`, старые cumulative/revert patch-файлы, одноразовые harness'ы, старые SHA/changed-file манифесты и неиспользуемые Travis/GitLab/AppVeyor конфиги.
- Исправлено применение Graphics/Quality: Water, Terrain, PBR, lighting, shadows, video и FPS limit сохраняются при Play/закрытии через merge с актуальным `settings.cfg`, а не через слепую запись старого состояния лаунчера.
- Страница Advanced также сохраняет только реально изменённые параметры: её старое состояние больше не может откатить пересекающиеся настройки Graphics/Quality при нажатии Play.
- Новый HUD FPS показывает `FPS: N`. Для старого профиля счётчик один раз включается миграцией, после чего обычный параметр HUD снова полностью контролируется пользователем. **F3 не менялся и остаётся HDR.**
- Перенесено совместимое исправление ArenaMP X040: выбранный `[OSG] threading model` теперь реально передаётся `osgViewer`, а CharGen/Inventory CharacterPreview больше не освобождает RTT/OSG-граф, пока render thread ещё может держать на него ссылки.
- Перенесён renderer-only Project Magnus Clustered Lighting как дополнительный режим для desktop OpenGL 4.3; на неподдерживаемом GPU/драйвере движок автоматически откатывается на существующий shader-compatible lighting.
- Перенесено исправление X042a для MSVC: в ArenaMW X033 оставалось некорректно закрытое выражение Water quality.
- Сохранены standalone-системы ArenaMW до поколения X033: XP-прокачка, Refined Alchemy и яды, требования экипировки, тактический AI, анимации, Arena Dialogue/HUD, QuickLoot, HDR/Bloom/PBR, новая вода, тени и CPU occlusion culling.

## Сборка

Эталонный Windows workflow: `.github/workflows/windows.yml`.

```bash
bash CI/before_script.msvc.sh -c Release -p x64 -v 2022 -N -V -i "$PWD/install"
source MSVC2022_64_Ninja/activate_msvc.sh
cmake --build MSVC2022_64_Ninja --config Release --parallel 2
cmake --install MSVC2022_64_Ninja --config Release
```

Подробности: [сборка](docs/BUILDING_RU.md), [возможности](docs/FEATURES_RU.md), [граница порта ArenaMP → ArenaMW](docs/ARENAMP_PORTING.md), [история изменений](CHANGELOG.md).

## Анимации верховой езды

Y004s встраивает необходимые модели и анимации Immersive Riding прямо в ресурсы движка. `guar_pack` отображается с седлом и использует авторские Idle/Walk/Run/Turn/Attack циклы; игрок получает анимацию посадки и сидячие Idle/Walk/Gallop/Turn позы. ESP, OMWAddon и современный OpenMW Lua не требуются.
