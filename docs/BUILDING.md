# Building ArenaMW Y001s

## Supported release path
The repository keeps one active release CI frontend: `.github/workflows/windows.yml` on Windows Server 2022/MSVC 2022 x64.

The Arena bootstrap script downloads/configures the OpenMW 0.47-era dependencies and creates the Ninja build tree:

```bash
bash CI/before_script.msvc.sh -c Release -p x64 -v 2022 -N -V -i "$PWD/install"
source MSVC2022_64_Ninja/activate_msvc.sh
cmake --build MSVC2022_64_Ninja --config Release --parallel 2
cmake --install MSVC2022_64_Ninja --config Release
```

The installed player package is expected to contain at least `openmw`, `openmw-launcher`, `openmw-wizard`, the INI importer, `defaults.bin`, engine resources and required DLLs. No TES3MP server/browser executable belongs in an ArenaMW package.

## Other platforms
The underlying OpenMW 0.47 CMake tree still contains Linux/macOS/Android paths, including gl4es-related options, but Y001s does not claim them as release-validated targets. Treat platform-specific changes as ports and report the exact configure/build result.

## Validation for changes
- Configure/build the affected target when dependencies are available.
- Parse modified MyGUI/XML files.
- For graphics changes, test launcher save + game launch + launcher reopen with an existing `settings.cfg`.
- For Project Magnus clustered lighting, test both a desktop OpenGL 4.3 path and the unsupported-hardware fallback to shader-compatible lighting.
- For rendering-thread changes, test CharGen transitions, inventory preview creation/destruction, load/new-game cycles and normal exit with multithreaded OSG modes.
- State clearly when a full native compile was not available.
