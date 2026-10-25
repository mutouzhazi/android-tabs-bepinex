# android-tabs-bepinex

**中文** | [English](README.en.md)

对 TABS 国际版（`com.xd.tabs.google`）专用的 BepInEx 注入 APK patch 工具。`resources/` 里是运行库和配套的 so。

## 对上游的修改

- `libmain.so`：编码 `unityVersion=2022.3.62`（`f3c1` 后缀塞不下，9 字节填空）
- `libfusion.so`：补 `DT_NEEDED`，`libc++` 的 vtable 符号在 Android namespace 下解析不到
- 新增 `libtabsopt.so`：导出 `libmain_*` 四个路径 override 符号，写 `fusion.cfg` 后拉起 fusion
- `resources.arsc`：STORED 且 4 字节对齐（Android 11+ 必需）
- 内置 `BepInEx` 与 `dotnet` 到 `assets/`，装完即用，不用往 `files/` 拷
- `unity-libs`：固定 2022.3.62

## 许可证

GNU General Public License v3.0（见 [LICENSE](LICENSE)）

注入链继承自 [BepInEx.Android.Patch.RainWorld](https://github.com/MISTAKE568/BepInEx.Android.Patch.RainWorld)（GPL-3.0），其上游为 [BepInEx.Android.Launcher](https://github.com/NextBep/BepInEx.Android.Launcher)（GPL-3.0）与 [BepInEx.Android](https://github.com/NextBep/BepInEx.Android)（LGPL-2.1）