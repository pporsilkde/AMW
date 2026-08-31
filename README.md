# ArenaMW Y002s

[Русский](README_RU.md) · **English**

ArenaMW is the standalone/single-player branch of the Arena engine work. It is based on OpenMW 0.47.0 and carries the compatible gameplay, UI, animation, AI and rendering systems developed for ArenaMP/EncoreMP without TES3MP networking, server authority, player groups or server scripts.

> A legal copy of *The Elder Scrolls III: Morrowind* is required. Bethesda game data is not included.

## Source snapshot

| Component | Value |
| --- | --- |
| ArenaMW source | **Y002s** |
| Engine base | OpenMW 0.47.0 |
| Runtime | Single-player `openmw` |
| Launcher | `openmw-launcher` |
| Network/server layer | Not included |
| Primary release build | Windows x64 |

## Y002s highlights

- Crosshair remains visible throughout CharGen/character registration and returns to normal HUD/camera preference handling immediately after creation is complete.

## Y001s highlights

- Clean source distribution: embedded `.git`, historical cumulative patches, one-off harnesses, old checksum manifests and obsolete Travis/GitLab/AppVeyor files are removed.
- Y001 graphics persistence: manual Water, Terrain, PBR, lighting, shadow, video and FPS-limit changes are merge-saved to the newest `settings.cfg` when Play/close is used.
- Advanced settings are change-tracked too, so untouched overlapping controls cannot overwrite a Graphics quality preset when Play is pressed.
- New HUD FPS presentation: `FPS: N`, enabled once for upgraded profiles; its normal HUD setting remains user-controlled afterwards. F3 is unchanged and remains the HDR hotkey.
- ArenaMP X040 single-player port: the selected `[OSG] threading model` now reaches `osgViewer`, and Character/Inventory/CharGen RTT preview resources are retired after a render-thread safety margin instead of being destroyed while OSG may still reference them.
- Project Magnus clustered lighting is available as an optional desktop OpenGL 4.3 mode; unsupported systems fall back to the existing shader-compatible lighting path.
- Ported the X042a MSVC fix for the Water quality expression that was still malformed in the ArenaMW X033 source.
- Existing ArenaMW standalone systems through the X033 generation are preserved: XP leveling, Refined Alchemy/poisons, equipment requirements, tactical AI, animation systems, Arena dialogue/HUD, QuickLoot, PBR/water/HDR/bloom, shadows and CPU occlusion culling.

## Build

The retained Windows workflow is `.github/workflows/windows.yml`. For a local MSVC/Ninja build:

```bash
bash CI/before_script.msvc.sh -c Release -p x64 -v 2022 -N -V -i "$PWD/install"
source MSVC2022_64_Ninja/activate_msvc.sh
cmake --build MSVC2022_64_Ninja --config Release --parallel 2
cmake --install MSVC2022_64_Ninja --config Release
```

See [Building ArenaMW](docs/BUILDING.md), [feature overview](docs/FEATURES.md), [ArenaMP-to-ArenaMW port boundary](docs/ARENAMP_PORTING.md), and [changelog](CHANGELOG.md).

## Repository layout

| Path | Purpose |
| --- | --- |
| `apps/` | Game, launcher, wizard and OpenMW utilities/source |
| `components/` | Shared engine libraries |
| `files/` | Defaults, shaders, MyGUI layouts, localisation and VFS resources |
| `extern/` | Bundled third-party source dependencies, including masked occlusion code |
| `extras/` | Optional EncoreMP content |
| `CI/` | Build/bootstrap scripts |
| `docs/` | ArenaMW documentation plus retained upstream OpenMW history |

ArenaMW is an independent fork. OpenMW, TES3MP, EncoreMP contributors and Bethesda Softworks do not endorse this project.
