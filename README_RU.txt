ArenaMW cumulative Update MW + AndroidLocalMap + CellStore build fix

База: AMW(20260818-202335)

Перенесено из Update MW только применимое к single-player ArenaMW:
- горизонтальный компас / HUD-маркеры;
- Detect Key / Enchantment / Creature;
- маркеры входов в интерьеры снаружи;
- строка времени/локации;
- подавление голосовых субтитров поверх активного диалога;
- show minimap = false;
- символы/шрифт для новых маркеров.

AndroidLocalMap:
- на Android/NG-GL4ES PBO для динамической fog-of-war локальной карты отключён;
- desktop/Windows путь PBO не меняется.

FIX30:
- apps/openmw/mwgui/hud.cpp теперь явно подключает ../mwworld/cellstore.hpp.
- исправляет MSVC C2027: use of undefined type 'MWWorld::CellStore' в hud.cpp.

MP/TES3MP-сетевая логика в single-player AMW не переносилась.
