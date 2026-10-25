# android-tabs-bepinex

**English** | [中文](README.md)

A BepInEx APK injection patch tool for the TABS international build (`com.xd.tabs.google`). `resources/` holds the runtime and the matching native libs.

## Changes from upstream

- `libmain.so`: `unityVersion` encoded as `2022.3.62` (the `f3c1` suffix does not fit in 9 bytes)
- `libfusion.so`: added `DT_NEEDED` entries, the `libc++` vtable symbol is unresolvable under the Android namespace
- New `libtabsopt.so`: exports the four `libmain_*` path override symbols, writes `fusion.cfg`, then brings up fusion
- `resources.arsc`: stored and 4-byte aligned (required on Android 11+)
- `BepInEx` and `dotnet` bundled into `assets/`, no copying into `files/` needed
- `unity-libs`: pinned to 2022.3.62

## License

GNU General Public License v3.0 (see [LICENSE](LICENSE))

The injection chain is inherited from [BepInEx.Android.Patch.RainWorld](https://github.com/MISTAKE568/BepInEx.Android.Patch.RainWorld) (GPL-3.0), which is based on [BepInEx.Android.Launcher](https://github.com/NextBep/BepInEx.Android.Launcher) (GPL-3.0) and [BepInEx.Android](https://github.com/NextBep/BepInEx.Android) (LGPL-2.1)