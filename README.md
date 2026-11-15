# 大富翁8 原生移植（RM8 Port）

把 PC 游戏《大富翁8》（RichMan 8）移植成原生 C++（SDL2 + librw），电脑（Windows）和安卓都能运行。

> **⚠️ 本仓库不含《大富翁8》的任何游戏文件**（资源包、图片、模型、音乐、语音、影片、执行档），也不含由它们转换出的数据。
> 你必须**自己合法拥有《大富翁8》**（推荐 Steam 正版：<https://store.steampowered.com/app/2185870/>）才能运行；用随附的提取器把**你自己的**游戏数据转换到**你自己的设备**上使用。
> 提取出的数据和含有这些数据的安装包只限你本人使用，不得上传、分享、出售或上架。本项目与游戏权利人无关。
> 完整说明见 **[NOTICE.md](NOTICE.md)**；第三方组件见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)；
> 我们的代码依 GPL-3.0-or-later 授权（[LICENSE](LICENSE)），许可证只涵盖代码，不涵盖游戏内容。

支持的游戏版本：**Steam 简体中文版**（主要支持，<https://store.steampowered.com/app/2185870/>）；开发用的旧版繁体安装暂时保持可用（以后会换成干净的旧版参考）。

## 快速开始

### 电脑（Windows 10 / 11）

1. 在 Steam 购买并安装《大富翁8》（商店页：<https://store.steampowered.com/app/2185870/>）。装好后不用运行它。
2. 到本仓库的 **Releases（发布）页**下载转换包 `rm8-port-<版本>-windows.zip`，**解压**到一个文件夹。里面有：
   * `rm8_extract.exe`（提取与构建工具）
   * `rm8-port-src-….zip`（移植版的源码包，工具要用它在你的电脑上编译；**这个不用解压**，和 exe 放在一起即可）
   * `SHA256SUMS.txt`（两个文件的 SHA-256；在 PowerShell 里运行 `Get-FileHash rm8_extract.exe` 对一下）

3. **双击 `rm8_extract.exe`。** 第一次运行 Windows 可能弹出蓝色的“Windows 已保护你的电脑”（SmartScreen）：点“**更多信息**”→“**仍要运行**”。
   这是因为我们的程序没有花钱买数字签名，所以 Windows 不认识它；不是病毒。不放心的话，可以改用源码方式运行（见 [extractor/README.md](extractor/README.md)）。
4. 程序会一步步问你（每一步直接按回车就是默认值）：
   1. **游戏文件夹**：Steam 版会自动找到，直接回车；
   2. **输出文件夹**：默认 `C:\Users\你的名字\RM8`（数据约 1.2 GB，手机版 APK 再加约 0.6 GB；建议留 3 GB 以上）；
   3. **要得到什么**：选 1（Windows 电脑版）；
   4. **迁移存档**：如果找到原版存档，会问你要不要接着原来的进度玩。
5. 等它做完。第一次需要联网下载构建工具（电脑版约 240 MB）和转换影片用的 ffmpeg（约 115 MB），一般十几分钟；之后再运行会跳过已完成的步骤。
6. 完成后，屏幕上会列出你得到的东西。**双击输出文件夹里的 `rm8.bat`** 就能玩。电脑需要支持 OpenGL 3.3 的显卡驱动。

### 安卓手机

1. 同上，准备好 Steam 版游戏和解压好的转换包（在电脑上操作，手机只负责安装）。
2. 双击 `rm8_extract.exe`，第 3 步选 **2（安卓手机版）**（或 3，电脑和手机都要）。程序会显示 **Google 的 Android SDK 许可协议**全文并问你是否接受——要构建安卓版必须接受；不接受就选电脑版。
   第一次要下载约 1.1 GB 的构建工具（Android NDK 等），所以需要较好的网络和约 7 GB 空闲磁盘。
3. 做完后得到 `rm8_personal_DO_NOT_SHARE.apk`（约 600 MB，里面装着**你自己的**游戏数据）。把它复制到手机（USB 数据线、网盘都行），点开安装
   （系统提示“不允许安装未知来源的应用”时，按提示在设置里允许你用的文件管理器 / 浏览器安装应用）。
4. 第一次打开会先解压数据（约 1.1 GB，要等一会儿，手机请留出 3 GB 以上），之后 APK 文件可以删掉。
5. **备份签名密钥**：文件夹 `%APPDATA%\RM8\apk_key`（第一次生成 APK 时自动创建）。以后更新、覆盖安装都要用同一把密钥；密钥丢了就只能先卸载旧版再装新版（应用里的存档会随之删除）。

## 常见问题（FAQ）

**这个工具会联网下载什么？为什么？**
游戏文件本身**不下载**，也不会上传任何东西（没有统计、没有遥测）。联网只是为了下载公开的开发工具，都固定了版本，并核对大小和 SHA-256，不一致就丢弃：

| 什么时候 | 下载什么 | 大小 | 来源 | 为什么 |
|---|---|---|---|---|
| 转换开场 / 制作人员影片（默认；可用 `--no-video` 跳过） | ffmpeg 9.0.2 | 约 115 MB | gyan.dev（ffmpeg.org 下载页列出的 Windows 构建） | 把原版 Bink 影片转成移植版能播放的格式 |
| 构建电脑版 | CMake、Ninja、llvm-mingw（clang 编译器） | 约 240 MB | github.com（各项目的发布页） | 在你的电脑上编译移植版 |
| 构建安卓版 | Temurin JDK 21、Android NDK r29、build-tools、platform | 约 1.1 GB | github.com（adoptium）、dl.google.com | 编译并打包 APK |
| 构建（两个版本都要） | 第三方源码：SDL2、GLFW、librw、CPython 2.4.6、stb_truetype、dr_mp3、pl_mpeg、OpenCC 词典 | 约 25 MB | github.com、raw.githubusercontent.com、python.org | 移植版链接的开源库（仓库里不放它们的源码） |

全部装进私有文件夹（默认 `%LOCALAPPDATA%\RM8\toolchain`、`%LOCALAPPDATA%\rm8_extract`），**不装进系统、不需要管理员权限、不改 PATH**，想删除时直接删文件夹即可。
这些工具的许可证见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)；我们不分发它们。

**游戏文件会被修改吗？**
不会。提取器只读取游戏文件夹，输出写到你选的另一个文件夹。Steam 里的游戏不受影响，不会触发“文件校验失败”。

**许可证是什么？**
我们自己的代码是 GPL-3.0-or-later。这个许可证只涵盖我们写的代码，**不涵盖**《大富翁8》的任何内容（美术、音乐、语音、文字、地图、影片、程序，权利都属原权利人）。第三方库各有自己的许可证（见 THIRD_PARTY_NOTICES.md）。

**为什么“个人 APK”（和提取出来的文件夹）不能分享？**
`rm8_personal_DO_NOT_SHARE.apk` 里装的是你自己游戏的数据（还可能有你的存档）。那些内容是原权利人的，你只能自己用，不能上传、转发、出售或上架。
提取输出文件夹（`game\`、`scenes\`）和 `rm8_windows_*.zip` 同理。我们发布的只有不含任何游戏数据的程序和源码。

**我的存档在哪里？**
* 电脑版：`%APPDATA%\RM8\`（在资源管理器地址栏输入这个就能打开）：`save\record_*.sav`（存档）、`rm8_gamedate.bin`（解锁的角色 / 拼图）、`rm8_box.bin`、`rm8_settings.txt`。**不在输出文件夹里**，所以重新提取或删除输出文件夹都不会丢进度。
* 手机版：应用自己的数据目录 `Android/data/com.rm8.port/files/` 下的 `save/` 等。卸载应用会一并删除，卸载前请备份。
* 原版存档（在游戏文件夹的 `Save\` 里，Steam 版同一位置）可以迁移：交互运行时会问；命令行加 `--saves`。迁移只读取原存档，已有的不同存档不会被悄悄覆盖。
* 请自己备份重要存档：复制 `%APPDATA%\RM8` 文件夹即可。

**提示“版本不受支持 / 与受支持的版本不一致”怎么办？**
说明游戏文件和我们记录的原版不一样（被改过、打过补丁，或是别的版本）。Steam 版可以右键游戏 → 属性 → 已安装文件 → “验证游戏文件的完整性”恢复原样后再试。仍想试试的话，交互运行时选“仍然要试试”，或命令行加 `--allow-unknown`（结果可能不对）。

**没有网络 / 下载失败 / 磁盘空间不够怎么办？**
程序会用中文说明原因，已完成的步骤和已下载的部分下次会接着用，不用重头来。没有网络时可以先选“只提取数据”；也可以自己把工具下载好（地址会显示在提示里），放进工具缓存文件夹的 `downloads` 里，文件名与下载地址里的一致，程序校验后直接使用。需要代理的话，先设置环境变量 `HTTPS_PROXY` 再运行。磁盘空间：提取约 1.2 GB；构建电脑版的工具再约 1.2 GB；构建安卓版的工具再约 4.5 GB（工具缓存里的 `downloads` 文件夹是下载的压缩包，构建完成后可以删掉）；可以用 `--work-dir`、`--toolchain-dir` 换到空间大的磁盘。

**杀毒软件 / SmartScreen 报警？**
Windows 上的程序（`rm8_extract.exe`、`rm8_app.exe`）没有数字签名（这是项目的决定，签名证书要花钱），所以第一次运行会有 SmartScreen 提示，个别杀毒软件对这类打包程序会误报。可以用 `SHA256SUMS.txt` 核对下载没有被改，或者直接从源码运行（Python + 本仓库）。

**怎么卸载 / 清理？**
删除输出文件夹即可去掉提取数据和电脑版；构建缓存在 `%LOCALAPPDATA%\RM8`（工具、构建文件）和 `%LOCALAPPDATA%\rm8_extract`（ffmpeg）；存档在 `%APPDATA%\RM8`（想保留进度就别删它）；APK 签名密钥在 `%APPDATA%\RM8\apk_key`。

**我是游戏的权利人，认为本项目有不妥之处？**
请在**本仓库的 Issues 页面**提出，我们会尽快处理（包括下架相关内容）。

## 目录

| 目录 | 内容 |
|---|---|
| `port/` | 移植版：`app/`（游戏代码）、`android/`（APK 构建与部署）、`extern/`（第三方源码的下载脚本）、`viewer/`（开发用工具）、`README.md`（技术说明） |
| `extractor/` | 提取器 / 构建器 `rm8_extract`（Python，可打包成 exe）、导出脚本快照 `exporters/`、版本参考 `data/references/`；[extractor/README.md](extractor/README.md) 有全部命令行选项 |
| `docs/` | 技术文档（发布版里原版文字已替换为编号或转述）；[docs/assets_legal.md](docs/assets_legal.md) 是素材分离的完整说明 |

逆向分析的中间产物（解包文件、反编译代码、Ghidra 工程、截图等）只保存在开发者本机，不在本仓库里；发布的内容清单见 [PUBLISH.md](PUBLISH.md)。
