# SystemNNN-Emberfall · Tyranor-Next 接入

Tyranor-Next 维护者可从 Emberfall 源码在本地导出接入材料。运行库从对应版本的已签名 Emberfall ARM64 APK 提取，播放器沿用原 Java 包名和 JNI 入口。运行需要 Android 8.0 及以上、ARM64，以及已解压到宿主可读路径的游戏目录。

## 包内文件

- `overlay/engine/` 放入 TYN 的 `:engine` 模块。两种接入包都含播放器 Java、`SystemNnnHost` 和许可资产。`split-so` 含 `libsystemnnn.so` 与五个 FFmpeg 共享库。`single-so` 将同一套 FFmpeg 功能静态链接到唯一的 `libsystemnnn.so`。宿主选用其中一套。
- `integration/` 包含 Manifest 片段、R8 规则、Gradle 与游戏库接入示例。按说明合并到宿主现有文件。[宿主接入示例](integration/host-hooks.md)
- `source/` 是 Emberfall 可公开的构建源码范围。包含核心、渲染、音频、JNI、独立播放器和构建脚本。
- `third_party/ffmpeg/` 提供 APK 对应的 FFmpeg 8.1.3 源码压缩包和构建脚本。[第三方许可与源码说明](https://github.com/PorisRora/SystemNNN-Emberfall/blob/main/THIRD_PARTY_NOTICES.md)

## 启动与存档

`SystemNnnHost.canOpen(File)` 以不区分 ASCII 大小写的 `spt/sys.spt` 作为目录识别标记。播放器启动后会校验脚本和资源。宿主先将游戏库目录解析成可访问的绝对文件系统路径，再用 `SystemNnnHost.createLaunchIntent(context, folder, title)` 构造同应用内的显式 Intent。Intent 沿用 `LAUNCH`、`FOLDER_PATH`、`GAME_TITLE` 和 `PROTOCOL_VERSION=1` 协议。标题文案仍由游戏自己的配置和贴图决定。

独立版和内置 TYN 都使用游戏目录下的 `sav`，兼容安装中已有的 `SAV` 等大小写。宿主可通过 `SystemNnnHost.saveDirectory(context, folder)` 获取该路径，播放器启动时会按需创建目录。剧情档使用 `dataN.sav` 或 `tdataN.sav`，全局进度使用对应的 `sysfile*.sav` 或 `tsysfile*.sav`。启用原版结果表的游戏同时使用 `result.sav`。宿主可通过 `SystemNnnHost.isPcGlobalSave` 筛选 PC 全局档。备份旧版未包含 PC 全局档时，恢复应保留现有 PC 全局档。切换播放器前请先退出当前游戏。

游戏的存档格式保存在 `emberfall-system.cfg`。启动先查 `init2`，再查 `init`。首次初始化全局档版号时，依次使用启用的 EXE 分析、通过区块校验的原版 `.org` 模板、游戏文件回退。配置记录最终版号、正式或试玩标记和判定来源，之后据此打开对应的全局档。配置写入失败时，本次游戏仍复用首次确定的存档格式。已有 `init2` 优先存放，其次是 `init`，两处都没有时创建 `init`。宿主可在单游戏引擎设置调用 `SystemNnnHost.saveLayoutConfig` 查看配置或手动重新分析。重生成的结果在下次打开游戏时使用。

首次用新版打开原游戏路径时，播放器会从当前应用的旧私有目录复制可迁移的全局进度。旧档保留，`sav` 中已有的 PC 全局档优先。要把旧独立版的进度带到 TYN，先用新版独立 APK 在原路径打开一次，迁移完成后退出，再用 TYN 启动同一游戏目录。应用设置各自保存。

宿主可在替换 Emberfall 存档前调用 `SystemNnnHost.canReplaceSaveSnapshot` 校验解包后的受管文件。校验只读取内容，覆盖输入日志顺序、存档文本和原生快照结构。游戏脚本和资源的一致性在实际读档时核对。仅含有效全局进度的备份可恢复到没有剧情档和流程图检查点的目录。迁移旧私有全局进度优先选择有效主档及与它匹配的扩展。

## 重新导出与重建

在 Emberfall 的 Git 工作区保留与播放器 Manifest 版本对应的签名 APK 及 FFmpeg 源码包，使用独立播放器构建所用的 Android SDK 和 JDK 17。

```powershell
python platform/android/package_tyn.py
python platform/android/package_tyn.py --layout single
```

第一条命令从正式分离 APK 导出 `split-so`，第二条从 `*-single-arm64.apk` 导出 `single-so`。每套都在 `build/tyranor/` 生成同名的 `SystemNNN-Emberfall-<版本>-<布局>-so-tyn-integration` 目录及 ZIP。导出会验证 APK 的签名和版本、编译 Java 副本并检查 JNI 符号。

从随包源码重建时，将 `third_party/ffmpeg/ffmpeg-8.1.3-source.tar.gz` 复制到 `source/build/tyranor/`，再从 `source/` 运行 `python platform/android/build_player.py` 构建分离版。加上 `--movie-codec-linkage static` 构建单库版。[Android 构建说明](https://github.com/PorisRora/SystemNNN-Emberfall/blob/main/platform/android/BUILDING.md)

## 用修改过的 FFmpeg 重新链接单库

在 Emberfall 源码仓库根目录用 Linux 或 WSL 运行以下命令。使用本地导出包时，先进入其中的 `source/` 并按上一节放置源码归档。将 `SYSTEMNNN_NDK` 指向 NDK r28 的目录，并将 `ffmpeg_archive` 指向完整的 FFmpeg 源码压缩包。保留源码归档的顶层目录结构。 [原版 FFmpeg n8.1.3 源码](https://codeload.github.com/FFmpeg/FFmpeg/tar.gz/refs/tags/n8.1.3)

```bash
ffmpeg_archive="$PWD/build/tyranor/ffmpeg-8.1.3-source.tar.gz"
relink_dir=$(mktemp -d)
bash platform/android/build_movie_codec.sh "$ffmpeg_archive" "$SYSTEMNNN_NDK" "$relink_dir/ffmpeg" static
cmake -S platform/android/player -B "$relink_dir/player" \
  -DCMAKE_TOOLCHAIN_FILE="$SYSTEMNNN_NDK/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-26 -DANDROID_STL=c++_static \
  -DCMAKE_BUILD_TYPE=Release -DMOVIE_CODEC_ROOT="$relink_dir/ffmpeg" -DMOVIE_CODEC_LINKAGE=static
cmake --build "$relink_dir/player" --parallel 6 --target systemnnn
```

用 `$relink_dir/player/libsystemnnn.so` 替换宿主接入目录中的单库，再由宿主构建。FFmpeg 许可证随包附在 `overlay/engine/src/main/assets/licenses/systemnnn/ffmpeg-lgpl-2.1.txt`。 [第三方声明](https://github.com/PorisRora/SystemNNN-Emberfall/blob/main/THIRD_PARTY_NOTICES.md)
