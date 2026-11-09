# MvDK Recomp

A recompilation of *Mario vs. Donkey Kong* (Nintendo Switch) for PC and Android, built on the
[Savage Wind](https://github.com/Revivedx/SavageWindRecomp) recompiler and runtime.

**No game files are included.** You need your own copy of the game (ExeFS + RomFS, main build id
`43ae51ee9766867d`). The app copies the files it needs and recompiles the game code from your copy on
first setup.

## Layout

| Folder     | What it is |
|------------|------------|
| `pc/`      | Desktop port (Linux / Windows / macOS): the recompiler (`tools/gen.py`) and the runtime (HLE kernel and services, NVN→Vulkan). Build with `make`; run `./build/savagewind --play`. |
| `android/` | Android port (app id `io.github.mvdkrecomp`): the same runtime with touch controls and Android performance hints. Build with `./gradlew assembleRelease` (JDK 17, Android NDK). |
| `recomp/`  | An early hand-written static recompilation experiment (sead heap / framework in C++20) and its analysis scripts. The game binary and its disassembly are not included. |

## MvDK-specific changes

- Games other than BotW: the recompiler builds without BotW's sites; the old SDK's audio renderer path; build/RomFS checks for MvDK.
- Shader translator fixes (LDC addressing) for Mario's materials, ultrawide support (wide view), render-target scale fixes.
- Frame pacing: the game steps once per frame, so it always runs at 60.
- Android: CPU performance hints, touch controls, six fingers on the screen open the settings, MvDK Recomp launcher art and icon.

## License

MIT, see `pc/LICENSE` and `android/LICENSE` (Savage Wind Recomp contributors, LAS Recomp). Mario vs. Donkey
Kong is © Nintendo; this project contains none of its code or assets.
