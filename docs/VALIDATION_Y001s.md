# ArenaMW Y001s validation

Validation performed on the cleaned Y001s source snapshot before packaging.

## Source/package checks

- Full source tree is present; Y001s is not a thin patch archive.
- Embedded `.git` history is absent.
- Historical root-level `X0xx` patch files, one-off harnesses, old checksum/change manifests and obsolete root CI files were removed.
- No nested ZIP/7z/RAR archives or compiled Windows debug/build artifacts were found in the source tree.
- 106 XML/MyGUI/Qt UI files parsed successfully in the repository validation pass.

## Y001s behavior checks

- Graphics saving uses a baseline plus latest-on-disk merge and writes only changed Graphics keys.
- Advanced saving is baseline-aware and replays only changed Advanced keys, protecting overlapping Graphics preset values from stale UI state.
- HUD FPS widget has the explicit `FPS: --` layout/caption path and the Y001s one-time profile migration key exists in defaults.
- F3 was not reassigned to FPS; the existing F3 HDR toggle remains in `KeyboardManager`.
- ArenaMP X040 viewer-threading selection is applied before engine preparation; CharacterPreview retirement calls are present in the frame loop and shutdown drain path.
- Project Magnus clustered-lighting enum/backend, compute shaders, shader-resource entries, launcher choice and in-game lighting selector are present; the backend contains an OpenGL 4.3 capability fallback.
- The X042a Water-quality parenthesis correction is present in `GraphicsPage`.

## Configure check

CMake configuration was started with optional tools/tests disabled. Compiler detection and the OpenMW configuration stage were reached. The check then stopped because the validation environment does not provide the required OpenGL/GLX development libraries (`OPENGL_opengl_LIBRARY`, `OPENGL_glx_LIBRARY`, `OPENGL_INCLUDE_DIR`). Therefore a complete native compile/link test could not be performed in this environment.

This environment limitation is not recorded as a Y001s source-code error; an actual Windows project build remains the authoritative compile/runtime test.
