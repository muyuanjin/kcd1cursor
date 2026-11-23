# Repository Guidelines

## Project Structure

KCD1Cursor is a Windows x64 ASI plugin that keeps custom cursors active when Kingdom Come: Deliverance restores its built-in cursor. `src/Plugin.cpp` contains the plugin entry point, cursor hooks, and Lua bindings. `CMakeLists.txt` configures the build and fetches MinHook v1.3.4. `README.md` documents installation and the Lua API. Cursor textures belong in the consuming mod’s PAK. CMake output goes in the ignored `build/` directory.

## Build and Run

Install CMake 3.30+ and Visual Studio 2022 with the MSVC x64 toolchain. From the repository root, run these commands to configure and compile the Release plugin.

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Copy `build/Release/kcd1cursor.asi` and the x64 Ultimate ASI Loader (`dinput8.dll`) beside `KingdomCome.exe` in `Bin/Win64`, then launch the game.

## Coding Style

Use C++20 and follow `src/Plugin.cpp` for formatting. Indent with four spaces and place function and control-block opening braces on their own lines. Use `snake_case` for internal functions, `kPascalCase` for constants, and `g_` for globals. Preserve the Lua API names `Declare`, `Lock`, and `Unlock`. Keep MSVC `/W4` and `/utf-8` enabled.

## Testing

Validation is manual. Run in-game smoke tests on Steam build `1.9.8-404-504czj4` (BuildID `22623230`). Check the game root’s `kcd1cursor.log` for `Cursor hooks installed` and `KCD1Cursor registered`. For cursor changes, set `r_MouseCursorTexture` to a mod texture and confirm it survives a built-in reset. Exercise `Lock` and `Unlock`, and verify that locking a missing texture preserves the active cursor and lock. Match any engine addresses or structure offsets to the supported build.

## Commits and Pull Requests

Use short, imperative commit subjects, such as `Fix cursor restoration after unlock`. In pull requests, describe the behavior change, link any related issue, and record the build and in-game checks performed. Include a screenshot for visible cursor changes when it helps reviewers.
