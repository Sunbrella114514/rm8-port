# rm8_extract：从你自己的《大富翁8》提取移植版需要的数据

移植版（`port\`）本身**不含**任何游戏素材。运行它需要你**合法拥有《大富翁8》**（支持：旧版繁体安装，以及 Steam 简体正版，https://store.steampowered.com/app/2185870/），
由本工具把游戏数据整理成移植版读取的格式。技术分层与权属清单见 `docs\assets_legal.md`。

## 快速开始（普通用户）

1. 在 Steam 买好并装好游戏（<https://store.steampowered.com/app/2185870/>）。
2. 从发布页下载 `rm8_extract.exe` 和 `rm8-port-src-….zip`，放在同一个文件夹（不要解压 zip）。
3. 双击 `rm8_extract.exe`（Windows 弹出 SmartScreen“已保护你的电脑”时：**更多信息 → 仍要运行**；程序没有付费签名，不是病毒）。
4. 回答 4 个问题（都可以直接回车）：游戏文件夹 → 输出文件夹 → 要什么（1 电脑版 / 2 手机版 / 3 都要 / 4 只要数据）→ 是否迁移存档。
5. 等它做完；屏幕上会写清楚结果在哪里（也保存在输出文件夹的 `结果说明.txt`）。电脑版：双击 `rm8.bat`；手机版：把 `rm8_personal_DO_NOT_SHARE.apk` 复制到手机安装。

**不要分享**输出文件夹和个人 APK，里面有你自己游戏的数据。常见问题（会下载什么、存档在哪、没有网络怎么办……）见仓库根目录的 `README.md`。

### 双击流程（不带参数运行）

程序按下面的顺序问，每一步都有默认值（回车），输错会用中文说明并重新问，Ctrl+C 或关闭窗口随时可以退出，最后一问“开始吗？”之前**不会创建或写入任何东西**：

1. **游戏文件夹**：自动列出找到的安装——Steam 版从注册表、`libraryfolders.vdf`（所有 Steam 库文件夹）和 `appmanifest_2185870.acf` 找；旧版从卸载信息和常见位置（各磁盘根目录、`Games`、`Program Files`、桌面 / 下载 / 文档里名为 RichMan8、大富翁8 的文件夹）找。也可以粘贴路径、输入 `b` 打开选择窗口。选错了（例如选了 `steamapps\common`、`Clump` 子文件夹或 `RichMan8.exe` 本身）会提示“你是不是想选这个”。
2. **输出文件夹**：默认 `C:\Users\你\RM8`；检查不能与游戏文件夹重叠、不能是别人的非空文件夹、磁盘空间够、有写入权限。
3. **要得到什么**：电脑版 / 手机版 / 都要 / 只要数据，并显示每一项还要下载多少工具（已装的不再下载）。选了要下载的内容时会先检查网络；连不上就用中文说明并让你重选（只缺影片用的 ffmpeg 时可以选择跳过影片继续）。
4. **迁移存档**：先只读地看有没有存档；没有就不问。
5. 汇总确认 → 开始。遇到“版本不一致”时，会列出不一致的文件并问你是否仍要试试（等于 `--allow-unknown`）。
6. 做完后打印“你得到了什么、在哪里”，并保存到输出文件夹的 `结果说明.txt`；窗口停在“按回车键关闭”。

命令行选项（`--no-video`、`--no-saves`、`--toolchain-dir` 等）在双击流程里同样有效（先在快捷方式 / 命令行里写上，再不带路径运行）。

## 用法（Windows）

```
rm8_extract.exe  <游戏文件夹>  <输出文件夹>  [选项]
rm8_extract.exe                                   （不带参数：双击即可，一步步用中文问你，见“双击流程”）
rm8_extract.exe  --verify <输出文件夹> [--full]   （只检查已有输出是否完好）
```

* 游戏文件夹：装有 `RichMan8.exe`、`Clump`、`Config` 的那个文件夹（Steam：`steamapps\common\Richman 8`）。**只读，不会被修改**。
* 输出文件夹：新建或上次提取的文件夹。得到
  * `game\`（约 1.0 GB）：移植版直接读取的原版资源包（Clump / Texture / Sound / Music / Pysrc\*.pyc）和一份只含音量等设置的 `Config\init.txt`；
  * `scenes\`（约 160 MB）：由你的文件转换出的数据（地图、界面、表格、转换后的影片）；
  * `scenes\save_tables.txt`（在 scenes 里）：读写原版存档用的字符码表和地块顺序，按安装的版本用 Big5（旧版）或 GBK（Steam）；
  * `manifest.json`：提取器版本、数据格式版本（现在是 2）、游戏版本指纹、所有文件的 SHA-256。
* 再运行一次同样的命令 = 更新：游戏文件和提取器版本都没变时直接提示“已是最新”；否则只替换 `game`、`scenes`、`manifest.json`，
  放在旁边的存档/设置不动。
* 电脑版运行：`rm8_app <输出>\game <输出>\scenes`。安卓：把 `game\`、`scenes\`、`manifest.json` 放进
  `Android/data/com.rm8.port/files/`（`android\push_to_phone.py <输出文件夹>` 用 adb 一键推送，也可以用 USB 文件传输手动复制）。

选项：`--no-video`（不转换开场/制作人员影片，不需要 ffmpeg）；`--ffmpeg <路径>` / `--no-download-ffmpeg`（影片转换需要 ffmpeg，见下）；
`--no-copy-game`（不复制 1 GB 原版文件，运行时直接读原游戏文件夹，仅电脑版）；`--link-game`（硬链接）；`--jobs N`；`--quick`（只比文件大小）；
`--allow-unknown`（版本不在受支持列表时继续，结果可能不对）；`--rebuild`；`--force`。

退出码：0 成功；2 参数错误 / 用户取消；3 游戏文件夹不对、版本不受支持或文件不全；4 提取、校验、磁盘或权限失败；5 内部错误；6 构建失败（没有网络、工具下载失败、编译失败、找不到源码……）；130 按了 Ctrl+C。
出错信息都是中文，并说明下一步怎么办；细节在输出文件夹的 `extract.log`。几种常见失败都有专门的说明：**没有网络**（说明连不上哪个网站、怎么用代理或手动放文件，已下载的部分下次接着用）、
**磁盘空间不够**（说明哪个磁盘、需要多少，可用 `--work-dir` / `--toolchain-dir` 换盘）、**选错文件夹**（提示正确的文件夹）、**版本不受支持**（列出不一致的文件，建议先在 Steam 里“验证游戏文件的完整性”）。

### 手机：生成个人 APK（不需要 Android SDK / JDK）

```
rm8_extract.exe <游戏文件夹> <输出文件夹> --apk              提取并生成 APK
rm8_extract.exe --make-apk <已有的输出文件夹>               只生成 APK（不重新提取）
```

* 需要**基础 APK**（不含任何游戏数据的移植版 APK，发布时叫 `rm8_base.apk`）：放在 `rm8_extract.exe` 旁边，或用 `--base-apk <路径>` 指定。
* 提取器把输出里的 `game\`、`scenes\` 装进基础 APK（`assets/rm8data/…`，与 `build_apk.py --with-data` 的格式相同），对齐并用 **APK Signature Scheme v2** 签名——
  全部是纯 Python 实现，不需要 Android SDK / JDK。得到 `<输出>\rm8_personal_DO_NOT_SHARE.apk`（Steam 版约 570 MB）。
* **签名密钥**在第一次使用时在本机生成，保存在 `%APPDATA%\RM8\apk_key\`（`--key-dir` 可改）。请备份这个文件夹：以后生成的 APK 都用它签名，才能覆盖安装旧版本。
  密钥丢了、换了电脑，或者想在“我们发布的基础 APK / 开发版 APK”（签名不同）和个人 APK 之间切换时，**不能互相覆盖安装，要先卸载另一个**（应用数据会随之删除）。
* 安装：把 APK 复制到手机，点开安装（需要允许“安装未知来源的应用”）；第一次启动时 app 会把里面的数据解压到自己的目录（约 1.1 GB，手机留出 3 GB 以上），之后 APK 文件可以删除。
* 这个 APK 里装的是你自己游戏的数据：**只限你本人使用，不要上传、分享**。
* 检查生成的 APK：`python -I tools\verify_apk.py <apk> --data-dir <输出文件夹>`（结构、数据逐文件比对、我们自己的 v2 验证；有 SDK 时再用 zipalign / apksigner 复核）。
* 进阶：`android\push_to_phone.py <输出文件夹>` 用 adb 把数据直接推到已安装的基础 APK 的数据目录。

### 一站式：同时构建移植版（Windows 文件夹 / 安卓个人 APK）

```
rm8_extract.exe <游戏> <输出> --target win        提取 + 构建 Windows 版：输出文件夹里直接得到 rm8_app.exe、rm8.bat（双击启动）
rm8_extract.exe <游戏> <输出> --target android    提取 + 编译安卓版 + 装入你的数据 -> rm8_personal_DO_NOT_SHARE.apk
rm8_extract.exe <游戏> <输出> --target both       两个都要
rm8_extract.exe --build <已有输出文件夹> --target both    不重新提取，只构建
rm8_extract.exe                                    不带参数：选文件夹后会问要不要构建、构建什么
```

* **源码**：`--source <文件夹或 .zip>`：移植版源码文件夹（里面有 `port\`；git 检出或发布树都行）或发布页上的源码包 `rm8-port-src-*.zip`（`tools\make_publish_tree.py --zip` 生成）；
  不写就依次找 `rm8_extract.exe` 旁边的 `port\`、旁边的 `rm8-port-src-*.zip`（发布页的两个文件放在一起即可），最后才读 `data\source_release.json` 里的下载地址（发布后填入）。源码会先复制到工作文件夹，不会改你的检出。
* **构建工具**：缺什么装什么，全部装进私有缓存文件夹 `%LOCALAPPDATA%\RM8\toolchain`（`--toolchain-dir` 可改；**不装进系统、不需要管理员权限、不改 PATH**），
  固定版本、核对大小和 SHA-256（和 `port\README.md` 里的版本一致）：

  | 工具 | 用途 | 大小 |
  |---|---|---|
  | CMake 4.5.0、Ninja 1.13.2 | 构建系统 | 54 MB、0.3 MB |
  | llvm-mingw 20261104（ucrt，x86_64） | Windows 版的 clang 编译器（只在构建 Windows 版时装） | 182 MB |
  | Temurin JDK 21.0.13+9 | 安卓版的 javac / d8 / keytool（只在构建安卓版时装） | 196 MB |
  | Android NDK r29、build-tools 36.1.0、platform android-35 | 安卓版（来自 dl.google.com，和 sdkmanager 装的是同样的包，校验值固定；直接下载，免装 cmdline-tools） | 795 MB、57 MB、61 MB |

  下载优先用系统自带的 `curl.exe`（Windows 10 / 11 都有，某些机器上比 Python 自带的下载快得多），没有就用 Python；中断后重新运行会接着下。
  断网的话可以自己把文件按原名放进 `<工具缓存>\downloads\`，程序会校验后使用。磁盘空间不够时会提前报错（工具约 4–5 GB，另需约 4 GB 工作空间）。
* **Android SDK 许可协议**：装 NDK / build-tools / platform 之前必须先接受 Google 的 Android SDK 许可协议。交互运行时程序会**显示全文并询问**；
  非交互运行必须自己加 `--accept-android-licenses`（表示你已读过并接受），否则停下并给出许可证链接。不会静默接受。
* **步骤**（可恢复：重新运行时没变的步骤会跳过，记录在工作文件夹的 `state.json`）：复制源码 → 安装缺的工具 → `extern\fetch_deps.py` 下载第三方源码（固定版本、SHA-256 校验，缓存在工具缓存里）→
  配置并编译 → 打包。`--work-dir` 可改工作文件夹（默认 `%LOCALAPPDATA%\RM8\build`）；`--no-install-tools` 缺工具时直接报错而不是安装。日志（含完整的编译输出）在输出文件夹的 `extract.log`。
* **Windows 打包**：就地放进输出文件夹——`rm8_app.exe`（静态链接，只依赖 Windows 自带的系统 DLL；用 `llvm-objdump` 检查导入表，若有需要随带的 DLL 会自动复制）、
  `rm8.bat`（启动器，用当前 app 要求的两个参数 `game` `scenes`；app 支持单个输出文件夹 / manifest.json 后，只需把 `rm8.bat` 最后一行改成 `rm8_app.exe "%~dp0."`）、`README_windows.txt`。
  `--zip` 另外压缩成一个 zip（含你的数据，只限本人使用）。需要 Windows 10 或更新。
* **安卓打包**：编译 `libmain.so`、`libSDL2.so`，用 `build_apk.py` 打出不含数据的基础 APK，再按上一节把你的数据装进去、用你自己的密钥签名，得到 `rm8_personal_DO_NOT_SHARE.apk`。
  已有基础 APK 时用 `--base-apk` 就不再编译。

### 迁移存档（`--saves` / `--no-saves`）

```
rm8_extract.exe <游戏> <输出> --saves                       提取 + 迁移原版存档和进度
rm8_extract.exe --build <输出> --target android --saves     构建安卓个人 APK 时一并装入存档
```

交互运行（不带参数）会问“是否打包存档（迁移原版存档到移植版）”；非交互运行默认**不迁移**，要迁移必须加 `--saves`。**只读**读取游戏文件夹，不会修改它。

* **迁移什么**：
  * `Save\record_NN.sav`（NN = 01..10，故事模式的存档位）和 `Save\record_mNN.sav`（自由模式的存档位，如 `record_m01.sav`）：移植版直接读原版的存档格式，所以是**逐字节复制**；复制前按移植版同样的规则检查文件头（版本 > 6 且有 `r8recrd`），坏文件跳过并说明原因。`record_temp.sav`（正在进行的牌局的自动存档）不迁移。
  * `Config\gamedate.bin`（724 字节：剧情进度、解锁的角色、拼图）→ 移植版的 `rm8_gamedate.bin`：移植版的进度文件和原版**同一格式、同一套魔数**（`collect.cpp` 的 `loadGameDate` / `saveGameDate` 就是照 `GameDate_Load` / `GameDate_Save` 写的），所以也是直接复制；提取器只校验大小并显示摘要（“解锁角色 n 个、拼图 m 块”）。
  * 储藏箱：原版没有单独的文件（在故事存档里），移植版的 `rm8_box.bin` 是移植版自己的文件，没有可迁移的内容。
* **从哪里找**：游戏文件夹的 `Save` 和 `Config`（Steam 版的存档也放在安装目录里，Steam 自动云同步的就是这个目录）；Steam 版还会只读地看 `<Steam>\userdata\<帐号>\2185870\remote\` 里有没有云存档副本。同名文件取最新的有效那份。
* **电脑版**：放到移植版读的数据文件夹——`%RM8_DATA_DIR%`（如果设了）或 `%APPDATA%\RM8`（`--saves-dir` 可改）：`save\record_*.sav`、`rm8_gamedate.bin`。**不会静默覆盖已有的文件**：内容相同就跳过；不同时，交互运行逐个询问（`y` 覆盖并先备份成 `.bak-时间`、`N` 保留、`a` 全部覆盖），非交互运行保留原文件并写进日志。
* **安卓个人 APK**：存档装进 APK 的 `assets/rm8data/import_saves/…`，app 第一次启动解压数据时把它们复制到应用的数据目录（`save/`、`rm8_gamedate.bin`），**只在目标文件不存在时才复制**，所以以后换新 APK 也不会覆盖手机上继续玩出来的进度。这需要 app 里 `unpackBundledData` 的一小段支持（见 `docs/assets_legal.md` 第 15 节）。
* **存档是个人数据**：带存档的 APK 仍然只限本人使用（`rm8_personal_DO_NOT_SHARE.apk`），不要分享、不要上传；提取输出里的 `saves\` 文件夹同理。存档和提取用的是同一个游戏版本（同一个游戏文件夹），所以文字编码（Big5 / GBK）和移植版的数据是对得上的。

### 影片需要 ffmpeg

原版影片是 Bink 格式，要转成移植版播放的 MPEG-1。提取器**不自带** ffmpeg：按顺序找 `--ffmpeg`、`tools\ffmpeg\ffmpeg.exe`（放在提取器旁）、上次下载的缓存、`PATH`；
**都没有时默认自动下载**（下载前会用中文说明来源、大小、校验值和许可证；交互模式先问你一句）：固定地址（gyan.dev 的 essentials 9.0.2，ffmpeg.org 下载页列出的 Windows 构建，
约 115 MB，网速慢时要等一阵），核对大小和 SHA-256（`rm8x\ffmpeg_tool.py` 里固定，不一致就丢弃）后只解出 `ffmpeg.exe`、`ffprobe.exe` 到 `%LOCALAPPDATA%\rm8_extract\`，作为独立程序运行。
不想下载：`--no-download-ffmpeg`（或直接 `--no-video` 不转影片）。没有影片游戏照样能玩。许可证说明见 `THIRD_PARTY_NOTICES.md`（FFmpeg 的 Bink 解码器是 LGPL，这个构建整体是 GPL-3.0；我们不分发它）。

## 开发者

```
python -I rm8_extract.py <游戏> <输出>                  （从源码运行，需要 numpy、pillow）
python -I tools\sync_exporters.py                       把 <workspace>\scripts 里的导出脚本快照到 exporters\（改了导出脚本后必做）
python -I tools\refresh_references.py --tc <旧版> --sc <Steam> --work <空文件夹>   重新记录 data\references\*.json（增加 / 更换版本见 data\references\README.md；Steam 是 primary，旧版 legacy）
python -I tools\build_exe.py                            PyInstaller 打包 -> build\dist\rm8_extract.exe（build\venv 里装 numpy、pillow、pyinstaller）
python -I tools\selftest.py --game <安装> --work <空文件夹> [--exe ...]       端到端自测（错误路径、更新、校验、改过的安装）
python -I tools\check_app_data.py --out <输出>          提取结果 vs port\android\data_files.py + 在 port\app 源码里找没提取的文件名
python -I tools\audit_repo.py / scan_source_content.py / list_source_quotes.py  源码树里有没有游戏文件 / 原文的审计（后者给出该用的字符串编号）
python -I tools\make_publish_tree.py --out <空文件夹>   按 PUBLISH.md 的白名单构建发布树（文档里的原文换成编号）并审计
python -I tools\check_notices.py                       THIRD_PARTY_NOTICES.md 里的下载清单与代码里的 19 个下载地址是否一致
python -I tools\compare_trees.py <A> <B>                比较两个 scenes 文件夹
```

导出脚本一律不改输出格式；提取器在临时目录里用 runpy 运行它们的快照，并用审计钩子确认它们**只读了游戏文件夹**（`prteff.py` 里写死的开发机路径在这里被改指向你的游戏文件夹）。
`data\missions.json` 是闯关任务表（我们自己逆向出的数据，不是从游戏文件里读的），`data\opencc\` 是 OpenCC 词典（Apache-2.0，随附许可证）。
