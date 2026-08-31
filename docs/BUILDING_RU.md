# Сборка ArenaMW Y001s

## Основной release-путь
В очищенном репозитории оставлен один актуальный CI frontend: `.github/workflows/windows.yml` — Windows Server 2022 / MSVC 2022 x64.

```bash
bash CI/before_script.msvc.sh -c Release -p x64 -v 2022 -N -V -i "$PWD/install"
source MSVC2022_64_Ninja/activate_msvc.sh
cmake --build MSVC2022_64_Ninja --config Release --parallel 2
cmake --install MSVC2022_64_Ninja --config Release
```

В standalone-пакете ожидаются `openmw`, `openmw-launcher`, `openmw-wizard`, INI importer, `defaults.bin`, ресурсы и необходимые DLL. TES3MP server/browser в пакет ArenaMW попадать не должны.

## Другие платформы
В основе OpenMW 0.47 остаются Linux/macOS/Android пути и gl4es-опции, но Y001s не заявляет их как полностью проверенные release-цели. Любую такую сборку следует считать отдельным портом и фиксировать точные параметры CMake/компилятора.

## Минимальная проверка изменений
- configure/build затронутой цели, когда зависимости доступны;
- XML/MyGUI parse для изменённых layout/resource файлов;
- для лаунчера: изменить графику → Play → перезапустить лаунчер → проверить `settings.cfg` и UI;
- Для Project Magnus Clustered Lighting отдельно проверяйте desktop OpenGL 4.3 и fallback на shader-compatible lighting на неподдерживаемом оборудовании.
- для X040-порта: несколько переходов CharGen, inventory preview, new/load game и штатный выход на многопоточном OSG;
- явно указывать, если полный native compile в конкретной среде выполнить нельзя.
