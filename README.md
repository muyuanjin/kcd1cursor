# KCD1Cursor

KCD1Cursor keeps custom cursor textures active when Kingdom Come: Deliverance reapplies its built-in cursor. It supports Windows x64 Steam build `1.9.8-404-504czj4` (BuildID `22623230`).

## Install

Download `kcd1cursor.zip` from the [latest release](https://github.com/muyuanjin/kcd1cursor/releases/latest) and extract the plugin.

Put `kcd1cursor.asi` and the x64 Ultimate ASI Loader (`dinput8.dll`) beside `KingdomCome.exe` in `Bin/Win64`. Restart the game. `kcd1cursor.log` in the game root reports whether the hooks and Lua API loaded.

## Use from Lua

Put your cursor textures in the mod's PAK. Paths under `Data/Textures/Cursor*` or `Textures/Cursor*` are recognized when a mod sets `r_MouseCursorTexture`. Declare paths outside those directories before setting the CVar.

```lua
if KCD1Cursor then
    KCD1Cursor.Declare("Textures/MyMod/cursor.dds")
end
System.SetCVar("r_MouseCursorTexture", "Textures/MyMod/cursor.dds")
```

- `KCD1Cursor.Declare(path)` marks a mod cursor path and returns a boolean.
- `KCD1Cursor.Lock(path)` loads and applies the texture immediately. On success it becomes the preferred replacement for built-in cursor resets. If loading fails, the active cursor and lock are preserved.
- `KCD1Cursor.Unlock()` clears the lock and returns whether a lock was active. Built-in resets then use the most recently applied mod cursor.

## Build

Use CMake 3.30+ and Visual Studio 2022 with the MSVC x64 toolchain.

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

The plugin is written to `build/Release/kcd1cursor.asi`.

## Package

Pushing a version tag such as `v0.1.0` triggers GitHub Actions to build and publish `kcd1cursor.zip`. To package a local Release build, run the commands below.

```powershell
Copy-Item -LiteralPath build/_deps/minhook-src/LICENSE.txt -Destination build/MinHook-LICENSE.txt
Compress-Archive -LiteralPath build/Release/kcd1cursor.asi, LICENSE, build/MinHook-LICENSE.txt -DestinationPath build/kcd1cursor.zip -Force
```
