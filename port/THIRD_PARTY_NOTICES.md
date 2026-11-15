# Third-party notices / 第三方组件

我们自己的代码是 GPL-3.0-or-later（[LICENSE](LICENSE)）。游戏内容**不包含**在本仓库里，也不受这里任何许可证覆盖，见 [NOTICE.md](NOTICE.md)。
本仓库**不包含**第三方源码：`port/extern/fetch_deps.py` 从各自的官方来源下载固定版本并校验 SHA-256（见该脚本里的固定值和 `port/extern/README.md`），下载到 `port/extern/<名字>/`（被 `.gitignore` 忽略）；我们自己写的、要放进它们里面的文件（CPython 的 `rm8_config.c`、`rm8_getpath.c`、`pyconfig.h`）在 `port/deps_local/`。各组件的许可证文本随下载内容一起放在它们自己的目录或文件头里；都与 GPL-3.0 兼容。

## 链接进移植版 app / APK 的组件

| 组件 | 版本 / 位置 | 用途 | 许可证 |
|---|---|---|---|
| [librw](https://github.com/aap/librw) | 提交 `18532c2e13efbc1aa43f0b00b4459831f9414de0`，下载到 `port/extern/librw`；我们的补丁见 `port/patches/apply_librw_android.py` | RenderWare 模型读取与 OpenGL / GLES 渲染 | MIT |
| [SDL2](https://libsdl.org) | 2.32.10（官方发布的 `SDL2-2.32.10.zip`），`port/extern/SDL2-2.32.10` | 窗口、输入、音频、GL 上下文、安卓 Java 层 | zlib |
| [CPython](https://www.python.org) | 2.4.6（python.org 的 `Python-2.4.6.tgz`，只取解释器核心 208 个文件），`port/extern/cpython24`（静态编译，加上 `port/deps_local/cpython24/` 里我们自己的 `rm8_config.c`、`rm8_getpath.c`、`pyconfig.h`） | 运行用户自己游戏里的 Python 脚本（电脑 AI、卡片、过场） | PSF License |
| stb_truetype | v1.28，提交 `9f755b604db3492df2cbeb39bf84b81e3e3932e3`，`port/extern/stb` | 字体栅格化 | 公有领域 / MIT 二选一 |
| dr_mp3（内含 minimp3） | 提交 `cfdfbf6bb7271a792030ebbb6c2a15c3f2fd001e`，`port/extern/dr_libs` | 解码音乐 | 公有领域 / MIT-0 二选一；minimp3 为 CC0 |
| [pl_mpeg](https://github.com/phoboslab/pl_mpeg) | 提交 `c871f2be022ece7ef4f64230b4fb8e1fb9eb6023`，`port/extern/pl_mpeg` | 播放 MPEG-1 影片 | MIT |
| OpenCC 词典（TSCharacters、TSPhrases） | 下载到 `port/extern/opencc_dict`（固定提交见 `fetch_deps.py`）；提取器自带一份相同的副本 `extractor/data/opencc`（连同许可证） | 生成繁→简字表 `zh_t2s.txt` | Apache-2.0 |

OpenCC 署名：词典来自 [OpenCC](https://github.com/BYVoid/OpenCC) 项目（Apache License 2.0，© OpenCC 贡献者）；`zh_t2s.txt` 是对它的转换结果。许可证全文见 `extractor/data/opencc/LICENSE`。

## 只在开发 / 构建时用

| 组件 | 用途 | 许可证 |
|---|---|---|
| [GLFW](https://www.glfw.org) 3.5.1（官方发布的 `glfw-3.5.1.zip`，`port/extern/glfw-3.5.1`） | 开发用的 `rm8_view` / `rm8_scene` 工具 | zlib |
| Android NDK / SDK build-tools / JDK | 构建 APK（不随发布物） | 各自的许可证 |

## 一站式构建时下载的工具（不随包分发，装进用户的私有缓存文件夹）

`rm8_extract --target ...` 构建移植版时，缺的工具会从官方来源下载到 `%LOCALAPPDATA%\RM8\toolchain`（固定版本，核对大小和 SHA-256；不装进系统）。我们不分发它们：

| 工具 | 版本 / 来源 | 许可证 |
|---|---|---|
| [llvm-mingw](https://github.com/mstorsjo/llvm-mingw) | 20261104（ucrt，x86_64），GitHub 发布文件 | Apache-2.0 with LLVM exception；其中的 mingw-w64 为 ZPL / 公有领域，libc++ 等为 Apache-2.0 with LLVM exception |
| [CMake](https://cmake.org) | 4.5.0，GitHub（Kitware）发布文件 | BSD-3-Clause |
| [Ninja](https://ninja-build.org) | 1.13.2，GitHub 发布文件 | Apache-2.0 |
| [Eclipse Temurin JDK](https://adoptium.net) | 21.0.13+9，GitHub（adoptium）发布文件 | GPL-2.0 with Classpath Exception |
| Android NDK、build-tools、platform | r29（29.0.14206865）、36.1.0、android-35，dl.google.com | Android Software Development Kit License Agreement（用户在构建前必须阅读并接受；NDK 内含 LLVM 等开源部分，各有许可证） |

用这些工具编译出的程序（rm8_app.exe、APK）的许可证仍是我们代码的 GPL-3.0-or-later，加上上面“链接进移植版”的第三方组件各自的许可证；llvm-mingw 的运行库（libc++、libunwind、mingw-w64 CRT）静态链接进 rm8_app.exe，其许可证允许这样做。

## 提取器 `rm8_extract.exe` 内含的组件（PyInstaller 打包，Windows 版 exe 是 Python 3.12 的单文件打包）

exe 里随带（用 `extractor/tools/build_exe.py` 在干净的虚拟环境里打包，版本固定：numpy 2.5.4、Pillow 13.0.0、PyInstaller 6.22.3）：

| 组件 | 用途 | 许可证 |
|---|---|---|
| CPython 3.12 及它随带的 OpenSSL 3、libffi、zlib、bzip2、xz、expat | 运行提取器；OpenSSL 用于下载时的 HTTPS 校验 | PSF License；OpenSSL 为 Apache-2.0；libffi 为 MIT；zlib 为 zlib 许可 |
| Tcl/Tk 8.6（`tcl86t.dll`、`tk86t.dll`） | 选择文件夹的窗口 | Tcl/Tk License（BSD 类） |
| numpy 2.5.4（连同它的 wheel 里随带的 OpenBLAS 运行库 `libscipy_openblas64_*.dll`，含 gfortran 运行库） | 图像数据处理（导出地面贴图、界面贴图） | BSD-3-Clause；OpenBLAS 为 BSD-3-Clause；gfortran 运行库为 GPL-3.0 加 GCC 运行库例外 |
| Pillow 13.0.0（连同 wheel 里随带的 libjpeg-turbo、libpng、libtiff、libwebp、libavif、FreeType、LittleCMS 等） | 图片读写、转换 | MIT-CMU（HPND）；随带库各为 BSD / zlib / IJG / FreeType License 等宽松许可证 |
| Microsoft Visual C++ 运行库（`VCRUNTIME140.dll`、`msvcp140.dll`） | Python / numpy 运行需要 | Microsoft 允许再分发的运行库（随 Python 一起） |
| PyInstaller 6.22.3 的引导程序 | 单文件 exe 的外壳 | GPL-2.0-or-later 加“允许打包任意程序”的例外 |
| OpenCC 词典（`data/opencc`） | 繁→简字表 | Apache-2.0（许可证随附） |

源码方式运行提取器需要 Python 3.10+、numpy、Pillow；移植版源码包 `rm8-port-src-<tag>.zip` 是我们自己的代码（GPL-3.0-or-later，已去掉所有游戏文字）。

## 所有会联网下载的东西（完整地址）

提取器和构建脚本只访问下面这些地址（每一项都固定了大小或 SHA-256，不符就丢弃；固定值写在 `extractor/rm8x/toolchain.py`、`extractor/rm8x/ffmpeg_tool.py`、`port/extern/fetch_deps.py` 里）。
`extractor/tools/check_notices.py` 会检查这里列出的地址是否和代码里的完全一致，所以新增下载项时必须同时更新本节。

构建工具（`toolchain.py`，下载到 `%LOCALAPPDATA%\RM8\toolchain\downloads`）：

* https://github.com/mstorsjo/llvm-mingw/releases/download/20261104/llvm-mingw-20261104-ucrt-x86_64.zip
* https://github.com/Kitware/CMake/releases/download/v4.5.0/cmake-4.5.0-windows-x86_64.zip
* https://github.com/ninja-build/ninja/releases/download/v1.13.2/ninja-win.zip
* https://github.com/adoptium/temurin21-binaries/releases/download/jdk-21.0.13%2B9/OpenJDK21U-jdk_x64_windows_hotspot_21.0.13_9.zip
* https://dl.google.com/android/repository/android-ndk-r29-windows.zip
* https://dl.google.com/android/repository/build-tools_r36.1_windows.zip
* https://dl.google.com/android/repository/platform-35_r02.zip
* https://dl.google.com/android/repository/repository2-3.xml（只读取其中的 Android SDK 许可协议文字，显示给你并等你同意）

第三方源码（`fetch_deps.py`）：

* https://github.com/libsdl-org/SDL/releases/download/release-2.32.10/SDL2-2.32.10.zip
* https://github.com/glfw/glfw/releases/download/3.5.1/glfw-3.5.1.zip
* https://github.com/aap/librw/archive/18532c2e13efbc1aa43f0b00b4459831f9414de0.zip
* https://www.python.org/ftp/python/2.4.6/Python-2.4.6.tgz
* https://raw.githubusercontent.com/mackron/dr_libs/cfdfbf6bb7271a792030ebbb6c2a15c3f2fd001e/dr_mp3.h
* https://raw.githubusercontent.com/phoboslab/pl_mpeg/c871f2be022ece7ef4f64230b4fb8e1fb9eb6023/pl_mpeg.h
* https://raw.githubusercontent.com/nothings/stb/9f755b604db3492df2cbeb39bf84b81e3e3932e3/stb_truetype.h
* https://raw.githubusercontent.com/BYVoid/OpenCC/5c764a5a886f46eb365656ed91a0410d7162fac6/data/dictionary/TSCharacters.txt
* https://raw.githubusercontent.com/BYVoid/OpenCC/9e9d730659fc7db366b43b4437b307444d87e23e/data/dictionary/TSPhrases.txt
* https://raw.githubusercontent.com/BYVoid/OpenCC/c56f6c73281a5e70a46938d1e7f7ca2ec8818a3f/LICENSE

影片转换（`ffmpeg_tool.py`，只在没有可用的 ffmpeg 时）：

* https://www.gyan.dev/ffmpeg/builds/packages/ffmpeg-9.0.2-essentials_build.zip

我们自己的源码包（发布页的 `rm8-port-src-<tag>.zip`）：由你下载后放在 exe 旁边；发布后若在 `extractor/data/source_release.json` 里填了地址，提取器也可以自己下载（同样核对 SHA-256）。

以上之外**没有任何联网行为**：不上传文件、不发送统计、不检查更新。

## 运行时下载（不随包分发）：ffmpeg

把原版影片（Bink）转成 MPEG-1 需要 ffmpeg。提取器**不包含** ffmpeg：先用 `--ffmpeg` 指定的、提取器旁边的、上次下载的缓存、PATH 里的；都没有时，
**默认**从固定地址下载 [gyan.dev](https://www.gyan.dev/ffmpeg/builds/)（ffmpeg.org 下载页列出的 Windows 构建）的 essentials 9.0.2：

* URL：`https://www.gyan.dev/ffmpeg/builds/packages/ffmpeg-9.0.2-essentials_build.zip`（114,768,076 字节，SHA-256 `60f467265b1e312373dbcd92200c2618a74850f98d3d078e94296bb3fa2047ba`，大小或哈希不符就丢弃）
* 只解出 `ffmpeg.exe`、`ffprobe.exe`，保存在 `%LOCALAPPDATA%\rm8_extract\`，作为**独立进程**调用（不链接、不修改、不再分发）。
* 许可证：FFmpeg 是 LGPL-2.1-or-later（含它自带的 Bink 解码器 `libavcodec/bink*.c`）；这个 essentials 构建启用了 GPL 组件，整体按 GPL-3.0 提供。
  源码和许可证见 <https://ffmpeg.org/legal.html>、<https://ffmpeg.org/download.html>、构建发布者页面。FFmpeg 是 Fabrice Bellard 的商标。
* 不想下载：加 `--no-video`（跳过两段影片，游戏照样能玩），或自备 ffmpeg 用 `--ffmpeg` 指定。
* 以后如果把 FFmpeg 的 Bink 解码器编成动态库随移植版发布，会在这里补上 LGPL 要求的声明、源码获取方式和替换库的方法。

## 系统组件（不随包）

* 字体：电脑版读取 Windows 自带的宋体；安卓读取系统的 Noto Serif CJK（SIL OFL 1.1，由系统提供）。
* 游戏自带的 Python 标准库 `.pyc`（`os`、`random`、`codecs` 等 16 个，PSF License）随用户自己的游戏数据一起复制，不由我们分发。

## 游戏内容

《大富翁8》的全部美术、音乐、语音、文字、地图、影片和程序，权利属原权利人。游戏文件不包含在本仓库里，只在运行时读取用户自己合法获得的那一份（或由提取器在用户自己的机器上转换）。
