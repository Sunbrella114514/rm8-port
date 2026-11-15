# 素材分离与提取：权属清单、用户流程、提取器

> **声明**：本文只描述技术上的“代码 / 数据分离”，不是法律意见，文中没有任何“合法 / 不侵权”的结论。用户已就许可证、发布方式、受支持版本、ffmpeg、逆向产物等做了决定，落实情况见第 10 节。
> 本文由 `extractor\`（提取器）一并交付；相关工具说明见 `extractor\README.md`。

## 0. 摘要

* 目标：移植版的**源码、APK、安装包都不含《大富翁8》的任何素材**；用户必须自己合法拥有游戏（Steam 正版为主，旧版安装暂时保留），由 `rm8_extract` 在用户自己的电脑上把游戏数据整理成移植版读取的 `game\` + `scenes\`。
* 已实现：`<workspace>\extractor\`（Python，单文件 exe 已打包，30 MB）。对旧版（Big5 繁体）和 Steam 版（GBK 简体）都能提取；旧版结果与现有 `port\scenes` **逐字节一致**（2560 个文件，含转换后的影片）；Steam 版提取结果已用现有 `rm8_app.exe` 跑通（地图、菜单、角色、事件）。
* **用户已决定（第 10 节）**：⓪**手机版 = 电脑上生成的个人 APK**（第 5.4 节，纯 Python 对齐 + 签名，不需要 SDK / JDK，不在手机上提取）；①代码许可证和提取器的发布方式照 richman4-remastered：GPL-3.0-or-later、提取 / 转换工具随源码公开；②文档和源码里的原版文字能去的都去（文档发布版自动去、源码清单交负责人）；③**Steam 版为主要支持版本**，旧版暂时保留，之后换成干净的旧版；④**找不到 ffmpeg 时默认自动下载**（固定地址 + SHA-256），允许 FFmpeg 的 LGPL Bink 解码器；⑤逆向产物（`mod\`、`out\`、`ghidra_proj\`、反编译源码、引用原文的原版文档）只私有保存，发布清单见 `PUBLISH.md`。
* 采用的版权处理模型：**用户指定的姊妹项目 `lhr0909/richman4-remastered`**（第 2 节），再加上我们自己的“提取器”这一层（因为我们的移植版需要转换数据，而它直接读原文件）。
* 现在**不能**放进仓库 / APK / 发布物的东西（第 3.4 节有完整清单）：`port\scenes\`（派生数据）、`mod\`（原版脚本反编译源码与配置文本）、`tools\py24\*.dll`（游戏自带 DLL）、`out\`、`ghidra_proj\`、`port\results\` 与各 `tmp_*`（游戏画面截图 / 转换影片 / 解包文件）、带数据的 APK（`out\apk_full\rm8_full.apk`，`build_apk.py --with-data`）、`rm8_viewer.apk` 以外的任何含 `assets\rm8data` 的包。
* 现在**可以**放：`port\app`、`port\android\src` 等自写代码；`port\extern\fetch_deps.py`（第三方开源库改为脚本下载，固定版本 + SHA-256 校验，见第 13 节）；OpenCC 词典（Apache-2.0）；提取器代码；`docs\` 中去掉原文引用后的技术文档。
* 当前 `rm8_viewer.apk`（5.8 MB）不含游戏素材（第 3.6 节）。源码里仍有约 50 处与原游戏文字重合的短字符串和 5 张从游戏文件抄来的小数据表（第 3.5 节），建议处理。

---

## 1. 术语：三类东西

| 类 | 含义 | 能否随移植版发布 | 在用户机器上怎么得到 |
|---|---|---|---|
| (a) 原版文件，原样使用 | `Clump\clump0000.dat`、`Texture\tex000x.dat`、`Sound\snd0000.dat`、`Music\mus0000.dat`、`Pysrc\*.pyc`（原版 Python 脚本字节码：电脑 AI、卡片、过场），以及用户自己的 `Config\init.txt` 设置 | 否 | 提取器复制到 `<输出>\game\`（`init.txt` 只留下 app 读的 6 个键） |
| (b) 原版数据的派生物 | 地图转成的 `scene.txt` + 光照贴图 PNG；UI（解密后的 `.ui`、`uvlist.txt`、`strings.txt`）；`roles / cards / stocks / npcs / stages / news / questions / story / movie_arts / event_arts / particles .txt`；`minigame\sprites.txt`；Bink 转成的 `start.mpg` / `staff.mpg` | 否（内容仍是原作品的内容，只是换了格式） | 提取器运行导出脚本生成到 `<输出>\scenes\` |
| (c) 我们自己的作品 / 第三方开源 | C++ 源码、导出脚本、提取器、文档；librw、SDL2、CPython 2.4 等；OpenCC 词典生成的 `zh_t2s.txt` | 是（各自带许可证） | 随程序发布 |

---

## 2. 采用的模型：richman4-remastered（`lhr0909/richman4-remastered`）

读过的内容（浅克隆到 `<workspace>\out\ref_richman4\repo`，当作不可信数据，没有运行其中任何代码；`lly8666/richman4-remastered` 的 README、NOTICE 与它完全相同，是同一份内容的镜像）：`README.md`、`NOTICE.md`、`THIRD_PARTY_NOTICES.md`、`game-assets\README.md`、`.gitignore`、`scripts\audit_assets.py`、`scripts\build_android.sh`、`docs\migrations\001-assets.md`、`CONTRIBUTING.md`。引擎代码协议为 GPL-3.0-or-later；它的文字我们**没有复制**，下面是借鉴其做法后用自己的话写的，写进我们的 README / NOTICE 时请继续只借做法、不抄原文。

| 它的做法 | 我们怎么用 |
|---|---|
| README 最开头一段醒目声明：仓库不含素材；必须自己合法拥有游戏；放购买链接；与权利人无关；含你素材的安装包只限本人在自己设备上使用，不得散布或上架；链接 NOTICE / 第三方声明 / 引擎协议 | 照此写移植版 README 的开头（草案见第 11 节）。《大富翁8》Steam 页面作为购买链接（请用户确认商店地址） |
| 独立的 `NOTICE.md`（中英双语）：①非官方项目 ②仓库不含游戏文件 ③不得散布含游戏内容的构建 ④引擎协议只覆盖我们的代码 ⑤第三方 ⑥无担保 ⑦权利人联系方式与“可立即下架” | 同样分七节写 `NOTICE.md`；要点见第 11 节 |
| **坦白列出“文字摘录”例外**：源码里含有从原版转写的对白表、名称表、短提示，权利仍属原作者，不受开源协议覆盖，权利人要求即删 | 我们源码里也有同类内容（第 3.5 节）。要么移走到提取数据，要么照它那样在 NOTICE 里明说 |
| `game-assets\` 目录：除一份 README 外全部 `.gitignore`；README 写清要放哪些文件、必要与否、用途，并给出检查命令 | 我们的“放游戏文件”这一步变成运行提取器；`.gitignore` 抄同样的思路（`*.apk *.exe *.dat *.mpg` 加上输出目录、`extract_*`） |
| `scripts\audit_assets.py`：只读审计用户资料：必需文件清单、参考 EXE 的 SHA-256、版本不符给警告并请用户回报；明确写出“只验证了档案边界，不证明解码内容” | 提取器的 `fingerprint.py` 做同样的事，但更细：每个核心文件的大小 + SHA-256，按版本参考分别比对；版本不符默认拒绝，`--allow-unknown` 才继续；校验范围也写明（第 5.2 节） |
| 构建 APK 时把用户本地素材**打进安装包**（约 190 MB），“只能侧载，绝不要上传 Google Play 或分享”；iOS 同理 | 对应我们的 `build_apk.py --with-data`（已有）：**本机自用包**，文件名/文档里明确“不得分发”；默认发布的 APK 不含数据（第 5.3 节） |
| 第三方组件一律脚本下载 + SHA-256 校验，不入库；`THIRD_PARTY_NOTICES.md` 用表列“组件 / 用途 / 许可证” | 第 3.3 节照此给出清单；`extern\` 目前是入库的上游源码副本，建议改为脚本下载并校验（见第 10 节） |
| “本项目不提供、也不协助取得任何游戏文件” | 采用 |
| 它直接读原文件（MKF 包），**没有转换层** | 区别：我们的数据需要转换（地图、光照贴图、UI 解密、影片…），所以多一个提取器；其余做法一致 |

---

## 3. 清单

### 3.1 移植版运行时读取的文件

（依据 `port\app\*.cpp`：`scene.cpp:81-83`、`audio.cpp:228-232`、`menu.cpp:65`、`menu_extra.cpp:34-143`、`ui.cpp:165-484`、`rm8_app.cpp:145-182`、`minigame.cpp:102`、`story.cpp:50-80` 等，并与 `port\android\data_files.py` 的列表逐项核对：两边都是 2687 个文件，一致。）

| 路径（相对数据目录） | 谁读 | 类 | 说明 |
|---|---|---|---|
| `game/Clump/clump0000.dat` | scene.cpp | a | 全部 3D 模型 / 动画（40 MB） |
| `game/Texture/tex0000.dat`、`tex0001.dat` | scene.cpp | a | 全部贴图（698 MB） |
| `game/Sound/snd0000.dat`、`game/Music/mus0000.dat` | audio.cpp | a | 音效（266 MB）、音乐（62 MB） |
| `game/Pysrc/*.pyc`（121 个） | python.cpp（嵌入 CPython 2.4.6） | a | 电脑 AI（AI_01..16）、卡片（CardPy_*）、过场（da*）；其中 16 个是 Python 标准库（os、random、codecs…，PSF 许可，理论上可由我们自己编译提供） |
| `game/Config/init.txt` | audio.cpp / menu.cpp / menu_extra.cpp | a→生成 | 只读 sound / music / speed / difficuty / character / binkplay；提取器只写这 6 个键 |
| `scenes/<图>/scene.txt`、`lm_*.png`、`alm_*.png`（15 张图，2479 个文件，11 MB） | scene.cpp | b | 地图转出的场景（地形、道路、摆件、地块、事件牌）和烘焙光照图 |
| `scenes/ui/*.ui`（63 个）、`uvlist.txt`、`strings.txt` | ui.cpp | b | 界面布局、图片表、`string.bin` 的全部文字（2905 条） |
| `scenes/roles.txt` `particles.txt` `cards.txt` `stages.txt` `stocks.txt` `npcs.txt` `event_arts.txt` `questions.txt` `story.txt` `movie_arts.txt` `news.txt` | roles / particles / cards / menu / stock / npc / events / story / movie / news .cpp | b | 角色、粒子、卡片、关卡、股票、NPC、事件展示模型、问答题、闯关数据、过场用模型、新闻图与节日表 |
| `scenes/minigame/sprites.txt` | minigame.cpp | b | 小游戏精灵 / 模型表 |
| `scenes/video/start.mpg`、`staff.mpg` | video.cpp | b | Bink 转成 MPEG-1（142 MB）；缺失时菜单自动跳过 |
| `scenes/zh_t2s.txt` | zh.cpp | c | OpenCC 词典生成的繁→简表 |
| 字体 | ui.cpp | — | 电脑：`C:/Windows/Fonts/simsun.ttc`；安卓：系统 `NotoSerifCJK-Regular.ttc`。都是系统字体，**不随包** |
| 仅开发用 | rm8_app.cpp / collect.cpp | — | `scenes/camfile.txt`（相机对照）、`RM8_IMPORT_GAMEDATE`（从原版 `Config/gamedate.bin` 导入进度） |
| 用户数据（运行时写） | collect.cpp / menu.cpp / save.cpp | — | `rm8_settings.txt`、`rm8_gamedate.bin`、`rm8_box.bin`、存档（写在 scenes 的上一级目录） |

### 3.2 `port\scenes\` 的分类

全部是 (b) 类，唯一的 (c) 是 `zh_t2s.txt`。当前内容 2575 个文件 / 157 MB：地图 2479 个（11.1 MB）、UI 66 个（0.5 MB）、共享表 12 个（0.35 MB）、`minigame\sprites.txt`、两个影片（141.6 MB）；另有 15 个 `overview.png`（`export_scene.py` 顺手生成的开发检查图：游戏画面的渲染图，app 不读，提取器会删掉）。**这个目录整体不能入库**，建议加入 `.gitignore`，由提取器生成。

### 3.3 `port\extern\` 与其他第三方

| 组件 | 版本 / 位置 | 用途 | 许可证 | 备注 |
|---|---|---|---|---|
| librw | @18532c2（aap），`extern\librw`，我们的补丁 `patches\apply_librw_android.py` | RenderWare 渲染 / 读取 | MIT（`LICENSE`） | 其 `skeleton\imgui` 带 imgui（MIT，`LICENSE_imgui.txt`），未链接；`premake5.exe` 是上游自带的构建工具（BSD），不随发布物 |
| SDL2 | 2.32.10，`extern\SDL2-2.32.10` | 窗口、输入、音频、GL 上下文、安卓 Java 层 | zlib（`LICENSE.txt`） | 安卓 APK 里含 `libSDL2.so` + SDL 的 Java 类 |
| GLFW | 3.5.1，`extern\glfw-3.5.1` | 只有开发用的 `rm8_view / rm8_scene` 工具用 | zlib（`LICENSE.md`） | 不进 app / APK |
| CPython | 2.4.6，`extern\cpython24`（`cmake\py24.cmake` 静态编译） | 运行原版 Python 脚本（与游戏的 Python24.dll 同版本） | PSF License（`LICENSE`） | 我们自己编译，**没有**使用游戏自带的 `python24.dll` |
| stb_truetype | v1.28，`extern\stb` | 字体栅格化 | 公有领域 / MIT 二选一 | |
| dr_mp3 | `extern\dr_libs\dr_mp3.h` | 解码音乐 | 公有领域 / MIT-0 二选一；内含 minimp3（CC0） | |
| pl_mpeg | `extern\pl_mpeg` | 播放 MPEG-1 影片 | MIT（Dominic Szablewski） | |
| OpenCC 词典 | `extern\opencc_dict`（TSCharacters、TSPhrases），副本在 `extractor\data\opencc` | 繁→简字表 | Apache-2.0（随附 `LICENSE`） | 发布时要附署名和许可证文本；`zh_t2s.txt` 是它的转换结果，可随包发布 |
| ffmpeg | 不随包；用户自备，找不到时提取器默认下载（用户已同意） | Bink→MPEG-1 转换 | 所选构建（gyan.dev essentials 9.0.2）为 GPL-3.0；只作为独立程序运行 | 见 5.4 |
| Noto Serif CJK | 安卓系统自带 | 安卓字体 | SIL OFL 1.1（系统提供） | 我们不分发 |
| Python 标准库 `.pyc`（`Pysrc` 里 16 个） | 来自游戏 | | PSF | 可改由我们编译（`ref\py24lib` 有源码） |

本仓库**没有**把字体、音乐、图片放进代码；`port\app` 中没有嵌入贴图或二进制 blob（扫描：>200 字符的 base64 / 十六进制串 0 处，见 `extractor\tools\scan_source_content.py`）。

### 3.4 仓库里实际存在的“不该外传”的东西（审计结果）

审计命令：`extractor\tools\audit_repo.py`（SHA-256 比对两份安装里的全部文件、认派生文件名、比对原文语料）、`extractor\tools\scan_source_content.py`，结果在 `<workspace>\out\extract_test\reports\audit_repo.txt`、`scan_src.txt`（两份安装的逐文件对比在 `reports\_cmp_installs.txt`，可用 `extractor\tools\compare_installs.py` 重做）。

| 位置 | 内容 | 类 | 建议 |
|---|---|---|---|
| `port\scenes\` | 见 3.2 | b | 不入库，用提取器生成 |
| `mod\`（438 个文件，1.6 MB） | `mod\src` / `mod\baseline`：**210 个原版 Python 脚本的反编译源码**（AI_xx、CardPy、da*…）+ 228 个 `Config\*.bin` 解密后的文本；其中 `festival.bin.txt` 与游戏文件逐字节相同 | a/b | 不入库（这是 mod 工作流的工作区，可由 `scripts\rm8mod.py` 从安装重新生成） |
| `tools\py24\python24.dll`、`msvcr71.dll` | 与游戏里的两个 DLL 逐字节相同 | a | 不入库（python24.dll 是 PSF 许可，msvcr71.dll 是微软运行库；都不要从游戏拷出来再发布） |
| `out\`（8633 个文件，3 GB） | 解包的资源（`out\pak`）、`RichMan8.exe` 副本、**反编译 C**（`out\final*`、`ghidra*`）、`pysrc_clean`（脚本反编译）、`config_*`、六个小游戏 DLL 副本、大量截图、`apk_full\rm8_full.apk`（**595 MB，内含 `assets\rm8data` 全部数据**）、`extract_test` | a/b | 整个 `out\` 不入库 |
| `ghidra_proj\`（203 MB） | 主程序 / 小游戏 DLL 的 Ghidra 工程库 | a 的衍生 | 不入库 |
| `port\results\`（368 个，521 MB）、`tmp_anim\ tmp_npc\ tmp_news\ tmp_skip\ tmp_mg19\`、`work_media\`、`work_save\` | 游戏画面截图 / 渲染图、解出的 TGA / DFF / ANM、转换影片测试文件、存档 json 转储 | a/b | 不入库 |
| `docs\`（39 个文件） | （发布的是 `make_publish_tree.py` 生成的去原文版；下面说的是私有原版）技术文档。约 30 个文件（含 `port\README.md` 等）里含原版文字（卡片名、对白、新闻、股票名、问答…，≥4 个汉字的连续片段约 280 处）和反编译函数地址 | b（文字摘录） | 可入库的前提：①删去或缩短原文引用；②反编译代码片段不要贴大段。至少在 NOTICE 里照 richman4-remastered 的写法声明“文字摘录权利属原作者” |
| `scripts\`（134 个） | 自写分析 / 导出脚本，代码本身是我们的。部分写死开发机路径 `<old-edition-dir>`（`prteff.py`、`convert_videos.py`、`validate_assets.py` 等 14 个）；`gen_cards_md.py`、`make_lg_cards.py` 里有几处原文字符串 | c | 可入库；提取器已绕开写死路径（审计钩子证明导出脚本只读用户给的游戏目录） |
| `port\app\`、`port\android\`、`port\tools\`、`port\viewer\` | 自写 C++ / Java / Python | c，但含 3.5 节所列内容 | 见 3.5 |
| `port\rm8_box.bin`、`rm8_gamedate.bin`、`rm8_settings.txt` | 用户进度 / 设置 | 用户数据 | `.gitignore` |
| `port\rm8_viewer.apk`（5.8 MB） | 见 3.6 | c | 不含素材，可以发布（带调试签名，发布前换正式签名） |
| `port\build-*`、`port\loadtest`、`port\mgtest` | 构建产物 / 测试 | — | `.gitignore`；`build-*\*.so` 不含素材 |
| `port\extern\`（2664 个文件，101 MB） | 第三方上游源码副本 | c | **改为脚本下载**：只入库 `fetch_deps.py` 和 README，其余被 `.gitignore` 忽略；我们自己写的 CPython 配置文件移到 `port\deps_local\`（第 13 节） |
| `ref\`（py23lib / py24lib） | Python 标准库源码与 pyc | c（PSF） | 可入库，附 PSF 许可证 |

> **再次提醒**：`out\apk_full\rm8_full.apk`（和 `build_apk.py --with-data` 生成的任何 APK）内含 1.2 GB 的原版数据，只能放在用户自己的手机上，不得上传、不得入库。建议改名为 `rm8_personal_DO_NOT_SHARE.apk` 并写进 `.gitignore`。

### 3.5 源码里嵌入的内容

扫描范围：`port\app`、`port\android`、`port\tools`、`port\viewer`、`port\loadtest`、`port\mgtest`、`scripts`、`extractor`（`scan_source_content.py`）。

**A. 中文字符串**：代码里共 113 个含汉字的字符串字面量（不含注释），其中只有 **5 个**（3 个文件：`story.cpp`、`port\tools\_patch_dialogs.py`、`_patch_bankrupt.py`）与原版文字语料整句或 ≥3 个汉字的片段重合；另用较宽的口径（注释和字符串里 ≥4 个汉字的连续片段）统计，`port\app` 的 25 个文件里有约 50 行与原版文字重合，大多是菜单提示、事件说明短句，如 `story_host.cpp`（欢迎词 4 条）、`event_bank.cpp`、`game.cpp`、`stock.cpp`、`events.cpp`、`event_power.cpp`、`story.cpp`、`menu.h`、`ui.h`、`boardfx.cpp`。已编进当前 APK 的 `libmain.so` 的 177 个汉字字符串里，与原文**整句相同**的为 0（它们是我们自己写的繁体提示）。
建议：①这些短提示保留，并在 NOTICE 里照 richman4-remastered 的写法声明为“为兼容与再现界面而收录的短文字”；②更稳妥的做法是把需要原文的提示改为从 `scenes\ui\strings.txt` 按编号取（其中已含 2905 条原版文字），C++ 里只留自己的提示。

**B. 从游戏文件抄进源码的数据表**（小，属于“事实性常量”，但确实来自游戏文件）：

| 位置 | 内容 | 来源 | 建议 |
|---|---|---|---|
| `events.cpp:27-28` `CARD_LEVEL[49]`、`CARD_PRICE[49]` | 49 张卡片的等级、点券价 | `Config\card.bin`（`cards.txt` 里已有同样的字段） | 删表，改读 `cards.txt`（注释说是“cards 模块加载前要用”，调整初始化顺序即可） |
| `collect.cpp:27-30` `T_UNLOCK[17]`、`T_CLEAR[17]`、`T_STAGE[17][5]` | 拼图收集用的数值表（118 个数） | 从 exe 逆向得到 | 保留为事实性常量，或让提取器写出 `collect.txt`（exe 不能在提取时解析，所以只能由开发者维护） |
| `news.cpp:497-498` `T1[44]`、`T2[38]` | 新闻候选编号表 | exe | 小、事实性，保留 |
| `menu_anim.cpp:178、250` `UICFG[]`、`FLY[]` | 界面控件编号 / 图片编号 / 飞行动画参数（约 220 个数） | exe 逆向 | 保留 |
| `save_big5.inc`、`save_terra_order.inc`（`export_save_big5.py`、`export_save_terra_order.py` 由游戏数据生成） | 股票名用字的 Big5 码表（618 个数）；15 张图原版地块顺序（725 个数） | `stocks.txt` / 地图文件 | 提取器可用 `--build-tables` 生成同样的两个文件（与 `port\app` 里现有的逐字节相同）。建议 app 改为运行时读 `scenes\` 里的等价文本，不再把游戏数据编进可执行文件。**注意**：Big5 码表只适用旧版；Steam 版存档里的名字是 GBK（见第 8 节） |
| `save_*.inc` 其余 | 存档读写钩子（逻辑代码，非数据） | 逆向 | 无问题 |

没有发现：嵌入的图片 / 音频 / 模型、base64 大块、整本问答题、整张卡片文字表。

### 3.6 APK 的内容

`port\android\build_apk.py` 默认产物 `port\rm8_viewer.apk`（5,779,999 字节），逐项检查：

| 条目 | 大小 | 内容 | 类 |
|---|---|---|---|
| `AndroidManifest.xml`、`resources.arsc` | 2.6 KB / 40 B | 我们自己的清单；应用名“大富翁8 地图浏览” | c |
| `classes.dex` | 85 KB | SDL2 Java 类 + `Rm8Activity` | c（SDL zlib） |
| `lib/arm64-v8a/libSDL2.so` | 1.5 MB | SDL2 | c |
| `lib/arm64-v8a/libmain.so` | 4.2 MB（已 strip） | 我们的 app + librw + 内嵌 CPython 2.4.6 + stb / dr_mp3 / pl_mpeg | c |
| `META-INF\*` | | 调试密钥签名 | — |

扫描 `libmain.so`：PNG / TGA / RIFF / ID3 / OggS 标志 0 处；汉字字符串 177 个，与原文整句相同 0 个。**默认 APK 不含游戏素材**。
变体：`build_apk.py --with-data <游戏目录>`（用 `android\data_files.py` 的清单）把 1.2 GB 数据放进 `assets\rm8data\`，首次启动由 `rm8_app.cpp: unpackBundledData` 解到数据目录——**含素材，仅限本人使用**，与 richman4-remastered 的“本机构建自带素材的 APK”同类。

### 3.7 字体

不含字体文件（扫描全树 `.ttf .ttc .otf`：除 `.venv` 外 0 个）。电脑版读系统的宋体，安卓读系统 Noto Serif CJK。

---

## 4. 两个版本的差异（旧版 vs Steam 版）

两份安装用于对照：旧版 `<old-edition-dir>`（350 个文件，1.26 GB，带 DXVK 补丁文件和一份存档备份）；Steam 版 `<steam-edition-dir>`（331 个文件，1.24 GB，目录名全小写）。全部只读。

| 文件 | 旧版（大小 / SHA-256 前 12 位） | Steam 版 | 关系 |
|---|---|---|---|
| `Clump/clump0000.dat` | 40225934 / 18508af7f5cf | 同 | 相同 |
| `Sound/snd0000.dat` | 265598754 / 9e7350e7bbbb | 同 | 相同 |
| `Music/mus0000.dat` | 62223506 / 90b35de068ab | 同 | 相同 |
| `Texture/tex0000.dat` | 697524730 / d93fbe21b520 | 697262487 / 3f81efb555b5 | **不同**：3057 → 3056 项，58 项内容不同（主要是 `ui\inside\*`、`ui\outside\*` 的带字界面图、两个 logo、鼠标指针、3 张特效 / 模型贴图），旧版多一项带空格的 `a02_body_b_01 .tga` |
| `Texture/tex0001.dat` | 794812 / b60bbe1f36de | 794812 / 4142bf3eecb5 | 同大小，内容不同 |
| `Bink/Start.bik` | 41814276 / 1071d74ee0bb | 同 | 相同 |
| `Bink/Staff.bik` | 38554964 / 24bb0611bd39 | 38554772 / bcdbb6e6087a | 不同（Steam 版转换后视频时长比 Bink 源短 0.22 秒，帧数一致，所以 `convert_videos.py` 加了 `--tolerance`，默认值不变） |
| `Map/*.map`、`*.ltm`（15 张） | 如 `01.map` 209420 / 2935b3afc079 | 同 | 相同 → 场景几何一致 |
| `Pysrc/*.pyc`（121 个） | 如 `AI_01.pyc` 6174 / 881fb46dac4c | 同 | 相同（全部字节码是 Python 2.4） |
| `Config/prt/*.tb`、`Config/UI/*.ui` | | 同 | 相同 |
| `Config/*.bin`（50 个）| 密钥 `tiep8jdg`，文字 **Big5**（繁体） | 密钥 **`seowk48h`**，文字 **GBK**（简体） | 加密密钥和字符编码都变了；`card / stage / stock / string / roleinfo / npcinfo / movie / building / route / street / terra / event / introduce / pass_* / sp_* / drama / role.bin` 等 49 个解密后与旧版不同（简体化；`role.bin` 另有约 4000 字节变化；`drama.bin` 第 8 段对白从 4 句减为 3 句，`num=3`），只有 `landtex.bin` 解密后相同 |
| `Config/questions_normal.ini`（明文） | 26558 | **没有**，只有加密的 `.bin`（24724，同一份题） | 提取器改读 `.bin` |
| `Config/string.bin` | 86350 | 86220 | 2905 条文字，简体 |
| `Config/init.txt` | 160（含 `binkplay`、`softstar=…`） | 128（无 `binkplay`） | 用户设置，不进指纹 |
| `RichMan8.exe` | 2187264 / 5d4753ec0dfe（FileVersion 1.0.0.1117，编译于 2008-01-11） | 2631384 / 7bb1180db25f（带 `python27.dll`） | 不同。**移植版的规则 / 逻辑来自对旧版 exe 的逆向，Steam 版 exe 多了 44 万字节，未做对比** |
| 其他 | `GameInfo.inf`、`Richman.dll`、`Python24.dll`、`Gamelib\ltlgame_*.dll` | Steam 多 `python27.dll`、`bulletin.bmp`、`sound\snd0001.dat`（1 个 20 KB 的 wav，id 9999）、`steam_autocloud.vdf`；`ltlgame_18..23.dll` 与旧版不同（`ltlgame_18` 小了 4 KB，其余同大小不同内容） | app 不读这些；小游戏的 Steam 版 DLL 逻辑未做对比 |

**处理方式**：`scripts\rm8_edition.py`（新）根据 `Config\stage.bin` 判断版本（试两个密钥 + 编码），`export_scene.read_cfg`、`export_story`（`drama.bin`）、`export_questions` 通过它取密钥和编码；旧版结果与改动前**逐字节相同**（用现有 `port\scenes` 对照过）。提取器带两份“参考”（`data\references\tc.json`、`sc.json`，各约 280 个核心文件的大小 + SHA-256），选分数最高的那份校验；`game\` 里的文件名统一用旧版的大小写（`Clump\ Texture\ Pysrc\AI_01.pyc` …），Steam 的小写目录会被映射过去（安卓是区分大小写的文件系统）。

提取结果对比（`scenes\`，两份各 2560 个文件）：2536 个逐字节相同（全部地图的光照贴图和几何、粒子、UI 布局 `.ui`、`uvlist.txt`、小游戏表、`zh_t2s.txt`、`start.mpg`…），24 个不同：`video\staff.mpg`（源 `.bik` 不同），其余 23 个是文本文件：15 张地图的 `scene.txt` 只有建筑名等**文字**不同（去掉非 ASCII 字符后逐行相同），`cards / npcs / roles / stages / stocks / story / questions / ui\strings` 是繁体→简体（去掉汉字后 ASCII 部分只在个别标点上有差：Steam 文本里是半角 `?`，旧版是全角）；`story.txt` 因 `drama 8` 少一句而少一行。数字、编号、贴图 / 模型引用全部一致。
Steam 版的 3314 个模型 / 动画引用全部存在于资源包，233 个贴图引用在资源包里找不到（与旧版同样的 233 个，原版数据本身的缺口，如 `m08s06_1.tga`）。

---

## 5. 用户流程设计

### 5.1 电脑（Windows）

1. 用户下载移植版（不含素材）和 `rm8_extract.exe`（或移植版安装包里带 `提取数据.bat`，内容就是调用它）。
2. 双击 `rm8_extract.exe`（或命令行 `rm8_extract.exe "<游戏文件夹>" "<输出文件夹>"`）：
   * 无参数：先显示声明（只读取你自己合法购买并安装的游戏；输出不要分享），再弹两个文件夹选择框（游戏文件夹、输出文件夹）。
   * 默认建议输出到 `%LOCALAPPDATA%\RM8\data`（或用户选的任意空文件夹）；Steam 版游戏文件夹可由程序在 `steamapps\common\Richman 8` 里自动探测（待加：读取注册表 / `libraryfolders.vdf`；现在由用户选）。
3. 提取器依次：检查输出文件夹 → 识别并校验游戏（第 5.2 节）→ 并行导出 15 张地图和各表 → 转换影片（需要 ffmpeg，见 5.4）→ 复制 / 链接原版大文件 → 校验 → 写 `manifest.json` → 原子替换进输出文件夹。全程约 25 秒（8 进程并行，旧版 1.2 GB 含复制与哈希）。
4. 移植版启动时从 `<输出>\manifest.json` 找数据（第 8 节）。如果没有或版本不符，显示中文提示并指向提取器。
5. **更新 / 重做**：再次运行同样的命令。游戏文件指纹、提取器版本、数据格式、选项都没变且文件完好 → “输出已是最新，无需重新提取”；有变化 → 先在输出文件夹里的 `.staging-*` 做完整个提取，成功后才替换 `game\`、`scenes\`、`manifest.json`（失败则原样保留，不会留下半成品）；旁边的存档 / 设置不动。`rm8_extract --verify <输出> [--full]` 只检查完好性与版本。
6. 不想复制 1 GB 的电脑用户：`--no-copy-game`（manifest 里记下原游戏路径，app 直接读原文件夹；只适合电脑版）或 `--link-game`（同盘硬链接）。

### 5.2 版本校验（“是不是真的支持的版本”）

* 结构检查：必须有 `Clump`、`Config`（大小写不限）。选错层（如选了 `steamapps\common`）会提示“游戏在子文件夹里，请选择：…”。
* 指纹：每个版本的“参考”记录了约 280 个核心文件（所有导出脚本读取的 + 要复制的）的大小 + SHA-256，外加两部影片；`RichMan8.exe`、DLL 只作识别信息。多个参考时，用所有小文件的哈希加大文件的大小打分，选最接近的，再对它做完整校验。
* 结果：全部一致 → “已识别：大富翁8 简体中文版（Steam 正版，GBK），指纹 …”；否则列出不一致的文件并**拒绝**（说明可能是其它语言版本、被修改过资源、复制不完整），`--allow-unknown` 才继续（结果可能不对，manifest 会记录“未识别版本”）。exe 不同只给一条提示（破解 / 补丁 / 兼容性修复版不影响提取）。
* 用户设置 / 存档（`Config\init.txt`、`Config\gamedate.bin`、`Save\`）不进指纹。
* 校验范围的说明（学 audit_assets）：这是“文件与已知安装一致”的证据，不证明游戏玩起来所有细节与原版一致。
* 旧版安装的参考是开发机上的那一份（`RichMan8.exe` 1.0.0.1117）。其他渠道的旧版（光盘版、不同补丁）如果某些资源不同，会被报告为“未识别”，用户回报后可以加新的参考。

### 5.3 复制什么、转换什么、输出到哪里

| 动作 | 内容 | 位置 |
|---|---|---|
| 复制（原样，复制后再校验 SHA-256） | Clump、Texture ×2、Sound、Music、`Pysrc\*.pyc` | `<输出>\game\`（共 1.07 GB） |
| 生成（只留 app 读的键） | `Config\init.txt` | `<输出>\game\Config\init.txt` |
| 转换 | 地图 ×15、UI、各表、小游戏表、过场用模型表、问答、闯关数据 | `<输出>\scenes\` |
| 转换（ffmpeg） | `Bink\Start.bik`、`Staff.bik` → MPEG-1 + MP2 | `<输出>\scenes\video\` |
| 生成（我们的） | 繁→简字表 | `<输出>\scenes\zh_t2s.txt` |
| 记录 | 版本、指纹、所有文件的大小 + SHA-256、读过哪些游戏文件、导出脚本的哈希、警告、耗时 | `<输出>\manifest.json`、`extract.log` |
| 不复制 | 其余原版文件（exe、DLL、Cursor、Gamelib、Save、`snd0001.dat` 等） | — |

输出目录布局 = 现在 `deploy.py` 在手机上用的布局（`game\…` + `scenes\…`），所以电脑版命令行 `rm8_app <输出>\game <输出>\scenes` 和安卓的 `files\game`、`files\scenes` 无需改动。

### 5.4 安卓

安卓没有 Python，所以提取放在电脑上做：

* **用户决定：不在手机上提取。由电脑上的提取器生成手机用的“个人 APK”**（与 richman4-remastered“用你自己的素材在本机构建 APK”同一模式）：
  1. 我们只发布**不含数据的基础 APK**（`port\android\build_apk.py` 的默认产物，发布时可改名 `rm8_base.apk`；5.8 MB）。
  2. 用户在 Windows 上运行 `rm8_extract.exe <游戏> <输出> --apk`（或交互模式里选“是”；已有输出时 `--make-apk <输出文件夹>`）。提取器把基础 APK（`--base-apk` 指定，或放在 exe 旁边的 `rm8_base.apk`）和提取出的 `game\`、`scenes\` 合成一个 APK：`rm8_personal_<steam|classic>_<日期>.apk`（Steam 版约 570 MB）。
  3. 合成方式与 `build_apk.py --with-data` 完全一致：`assets/rm8data/<相对路径>`（`.mp3 .mpg .png` 存储，其余 deflate）+ `assets/rm8data/manifest.txt`（`version <编号>`，然后每行 `<大小>\t<相对路径>`）+ 一份去掉了用户游戏路径的 `manifest.json`；app 第一次启动时由 `unpackBundledData` 解到数据目录。版本编号由所有文件的哈希算出，数据没变就不会重复解压。
  4. **不需要 Android SDK / JDK**：对齐（zipalign：存储项 4 字节对齐，`.so` 16 KiB 对齐，基础 APK 里的项不重新压缩）和签名（APK Signature Scheme v2）都用纯 Python 实现（`rm8x\apk.py`、`rm8x\rsa_x509.py`，只用标准库，随 exe 打包）。评估现成的库（凭印象，没有逐个核实许可证）：纯 Python 的 APK 签名工具有 AGPL 的（如 apksigtool），其余要 JDK 或 `cryptography` 的二进制轮子（还要自己拼 X.509）；v2 的格式很小（约 150 行），RSA-2048 + PKCS#1 v1.5 + 最小自签名证书约 200 行，自己实现更稳、许可证干净（GPL-3.0-or-later）。
  5. **签名密钥**：第一次使用时在本机生成（RSA-2048，自签名证书，有效期到 2049 年），保存在 `%APPDATA%\RM8\apk_key\`（`--key-dir` 可改），以后一直复用，所以新生成的个人 APK 能覆盖安装旧的。密钥丢了、换了电脑、或者与我们发布的基础 APK / 开发版 APK（签名不同）之间都**不能互相覆盖安装**，要先卸载旧的（应用数据会随之删除）——README 和提取器的完成提示里都写了。
  6. 验证（本机，没有碰手机）：用真实 SDK 工具核对生成的 APK：`zipalign -c -P 16 4` 通过；`apksigner verify --min-sdk-version 24` 通过（v2 = true）；`aapt2 dump badging` 能读出包名 `com.rm8.port`；`testzip` 全部 CRC 通过；`manifest.txt` 里 2704 个文件全在且大小对；随机抽 43 个文件与提取输出逐字节相同；篡改一个字节后我们自己的验证器和 `apksigner` 都报错。同一份输出用源码运行和用打包的 exe 运行，得到**逐字节相同**的 APK（打包、签名都是确定性的）。`extractor\tools\verify_apk.py` 可以重复这些检查。
  7. 用 adb 直接推数据的 `extractor\android\push_to_phone.py` 保留为可选的进阶方式。
* 提取器**不在手机上运行**；原先评估的“把导出脚本移植进 app”（约 3–5 人周）和“在 app 里嵌 Python（Chaquopy）”两条路不再需要。
* 影片：安卓 app 里已有 pl_mpeg 播放 MPEG-1；Bink→MPEG-1 的转换需要解码器，选项：
  1. **用户自备 ffmpeg**（`--ffmpeg`）：我们不分发，无许可证问题；缺点是门槛。
  2. **提取时联网下载（现在是默认路径）**：本地没有 ffmpeg 时自动下载（已实现并真实下载测试过：大小和 SHA-256 与固定值一致；本机网络下载 114 MB 用了约 22 分钟，所以下载前会用中文说明来源 / 大小 / 校验 / 许可证并提示要等；`--no-download-ffmpeg` 或 `--no-video` 可关闭，交互模式会先问一句；用这份下载的 ffmpeg 转出的两个 `.mpg` 与用开发机上同一构建转出的逐字节相同）：固定地址 `https://www.gyan.dev/ffmpeg/builds/packages/ffmpeg-9.0.2-essentials_build.zip`（ffmpeg.org 下载页列出的 Windows 构建），核对大小 114,768,076 与 SHA-256 `60f46726…47ba`，只解出 `ffmpeg.exe`、`ffprobe.exe` 放到 `%LOCALAPPDATA%\rm8_extract\`；视为不可信下载，不执行压缩包里的其他东西。这个构建是 GPL-3.0；我们只下载并作为独立进程调用，不分发、不链接。
  3. **内置 Bink 解码器**（长期，用户已允许 LGPL 方案）：把 FFmpeg 的 Bink 解码器（LGPL）编成动态库给 app 用（需附 LGPL 声明、源码获取方式和可替换库的说明，补进 `THIRD_PARTY_NOTICES.md`）；或按公开的格式资料自写（视频约 1,500 行，音频 RDFT 约 1,000 行）；免去转换，也让安卓版可以直接播原 `.bik`。
  4. 跳过影片（`--no-video`，`init.txt` 的 `binkplay=0`）：现在 app 已兼容，缺文件自动跳过。
* 转换后的 MPEG-1 影片是原片的再编码，属于 (b) 类，只在用户机器上生成。

### 5.5 中文错误信息（提取器已实现）

| 情况 | 提示（节选） | 退出码 |
|---|---|---|
| 路径不存在 | 找不到游戏文件夹：… | 3 |
| 选错文件夹 | 这不是大富翁8的安装文件夹：没有找到 Clump 和 Config 子文件夹。请选择装有 RichMan8.exe 的那个文件夹（…Steam 版在 steamapps\common\Richman 8）。看起来游戏在子文件夹里，请选择：… | 3 |
| 缺文件 | 游戏文件不完整，缺少：Texture/tex0001.dat … | 3 |
| 版本不对 | 这个安装与受支持的版本不一致（最接近：…）：<列出文件> 受支持的版本：tc（…）、sc（…）。… 如果确定要试一试，请加 --allow-unknown | 3 |
| 输出夹不能用 | 输出文件夹不能与游戏文件夹相同、也不能互相包含 / 输出文件夹不是空的，而且不是上次提取的结果 | 4 |
| 磁盘不足 | 输出盘空间不足：需要约 …，可用 … | 4 |
| 导出失败 | 导出步骤“卡片”失败（详见 extract.log）：… | 4 |
| 结果校验失败 | 结果校验失败：缺少或为空 / 数据引用了资源包里不存在的模型 / game/… 复制后的 SHA-256 与源文件不同 | 4 |
| 缺 ffmpeg | 警告：没有找到 ffmpeg，跳过开场/制作人员影片（游戏仍可玩，只是没有这两段影片）。… | 0 |
| 中断 | 已取消。输出文件夹里原有的 game / scenes 没有被改动。 | 130 |

失败时不会留下半成品：所有写入先进 `.staging-*`，成功才替换。

---

## 6. 提取器实现（`<workspace>\extractor\`）

```
rm8_extract.py          入口（命令行 / 无参数时图形选择框）
rm8x\                   pipeline.py 流水线 · fingerprint.py 版本识别 · validate.py 校验 · ffmpeg_tool.py · pyc24.py · audit.py · util.py · version.py
exporters\              导出脚本的快照（tools\sync_exporters.py 从 scripts\ 复制，MANIFEST.json 记录哈希）
data\                   references\{tc,sc}.json（+ _outputs.json）· missions.json · opencc\（Apache-2.0）
tools\                  sync_exporters · refresh_references · make_reference · build_exe · selftest · check_app_data · audit_repo · scan_source_content · compare_trees
android\push_to_phone.py
build\dist\rm8_extract.exe   PyInstaller 单文件（30 MB），build\venv 是独立的打包环境
```

要点：

* **不改导出脚本的输出**。只加了：`rm8_edition.py`（新，版本识别）和三处取密钥 / 编码的调用（`export_scene.read_cfg`、`export_story`、`export_questions`），以及 `convert_videos.py --tolerance`（默认 0.1 不变）。改动后旧版结果与 `port\scenes` 逐字节一致。
* 导出脚本在临时目录的快照里用 `runpy` 运行（15 张地图用进程池并行）。`out\story\missions.json`（闯关任务表：来自我们对 exe 的逆向，不是读游戏文件）放在 `data\missions.json`；`movie_stats.json` 需要的“过场用到的模型编号”由 `pyc24.py` 直接从 `da*.pyc` 解析得到（213 个，与原来用反编译源码得到的集合完全相同）；`stage.bin.txt` 由提取器解密生成。
* **只读用户给的游戏文件夹**：审计钩子（`audit.py`）记录每一次打开文件，凡是游戏文件夹、临时目录、Python 安装之外的读取都会让提取失败。`prteff.py` 里写死的 `<old-edition-dir>`（含一个默认参数）由提取器改指向用户的文件夹。验证时用 `subst R:` 把游戏映射成另一个盘符，证明没有隐藏的写死路径。
* **本轮新增（提取器 1.1.0，数据格式 2）**：①`scenes\save_tables.txt`（原来编进 app 的 Big5 码表和地块顺序，改成提取数据，并按版本选 cp950 / gbk）；②找不到 ffmpeg 时默认下载（见 5.4）；③参考文件带 `status`（`sc` = primary，`tc` = legacy）、`data\references\README.md` 说明怎么增加 / 换版本；④`tools\make_publish_tree.py`（白名单发布树 + 文档去原文 + 审计）、`tools\list_source_quotes.py`（源码里的原文引用清单，附字符串编号）、`tools\compare_installs.py`。
* **同一份脚本得到同一结果**：源码运行、PyInstaller exe 运行的 `scenes\`（2560 个文件含影片）逐字节相同。

### 验证结果

| 项 | 结果 |
|---|---|
| 旧版 `<old-edition-dir>` → 新建空文件夹 `<workspace>\out\extract_test\tc` | 用时 30 秒（含复制 1.07 GB、全文件 SHA-256、影片转换）；`scenes\` 2560 个文件与当前 `port\scenes` **逐字节相同**（只多出 15 个 `overview.png`，是被提取器有意删掉的开发检查图）；`game\` 5 类文件复制后哈希校验通过；`--build-tables` 生成的 `save_big5.inc`、`save_terra_order.inc` 与 `port\app` 里的逐字节相同 |
| Steam 版 → `<workspace>\out\extract_test\steam` | 识别为 sc 参考（278 个核心文件全部一致），用时 25 秒；`staff.bik` 转换通过（`--tolerance`） |
| 打包 exe → `steam_exe` | 结果与源码运行逐字节相同 |
| 桌面端 `port\build-sdl2\rm8_app.exe` 只用提取数据运行（`--map 01 --size 640x360 --time 30 --shot`）：旧版 `tc\game` + `tc\scenes`、Steam `steam\game` + `steam\scenes` | 两者都正常启动并出图（地图 191 个摆件 0 失败、4 个角色、卡片 / 股票 / 事件 / 故事数据全部加载）；菜单（主菜单 / 单人 / 拼图页）两版截图正常；日志中除随机数相关行外没有差异（Steam 版文字是简体） |
| `tools\selftest.py`（错误路径、更新、校验、改过的安装、缺文件） | 17 项全部通过（见 `<workspace>\out\extract_test\selftest1`） |
| `tools\check_app_data.py` | 提取结果与 `android\data_files.py` 列表（2687 个文件）完全一致；`port\app` 源码里出现的数据文件名都能在提取结果中找到（排除用户数据 / 开发开关） |
| 源码树 / APK 审计 | 见 3.4–3.6 |

---

## 7. 打包与发布计划

* 提取器：`python -I extractor\tools\build_exe.py` → `extractor\build\dist\rm8_extract.exe`（PyInstaller 6.22.3，numpy 2.5.4、Pillow 13.0.0，30–32 MB，同时写 `SHA256SUMS.txt`）。已用它在两份安装上做完整提取并验证。需要时加图标、版本资源；建议发布前用正式的代码签名。
* 发布物（全部不含素材）：移植版 exe / APK、`rm8_extract.exe`、README + NOTICE + THIRD_PARTY_NOTICES（含 OpenCC 的 Apache-2.0 署名）、`push_to_phone.py`。
* 不发布：任何 `game\`、`scenes\`、`manifest.json`（manifest 里记录了用户的游戏路径和哈希）、带数据的 APK、`mod\`、`out\`。
* 导出脚本增加 / 修改后：`sync_exporters.py` → `refresh_references.py` → 重新打包；`check_app_data.py` 作为发布前检查（防止新加的数据文件被漏掉）。

---

## 8. 需要在 app 里做的修改（交给负责人；我没有改 `port\app`）

数据格式版本现在是 **2**（相对第一版多了 `scenes\save_tables.txt`，见下），提取器版本 1.1.0。

**app 侧状态（2026-11-15，已在 `port\app` 里做完 1–7、9、12；8、10、11 无需 app 改动）**

* 1 首次运行检查：`rm8_app.cpp` 先读 manifest，再开窗口、选字体，数据缺失（打不开 Clump / Texture、没有任何 `scenes\<图>\scene.txt`）、`manifest.json` 缺失 / 损坏、`data_format` 不是 2 时，整屏画出中文说明（`ui.cpp: rm8UiDrawNotice`，stb_truetype + 系统字体，只用 app 内置字符串，路径里的反斜杠显示成斜杠），同时 `rm8Log` 到 stderr / logcat；窗口关闭 / Esc / 返回键退出，退出码 3（`--shot` 时把这一屏存成 PNG 后返回 3）。桌面和安卓同一份代码。已用空文件夹、`data_format 1`、有 manifest 没有 game、两参数空文件夹各截一张图核对。
* 2 manifest：桌面接受单个参数 `<输出文件夹>`（= `<out>\game <out>\scenes`；不给参数 = exe 所在文件夹，双击即用），此时 `manifest.json` 必须存在；读 `data_format`、`source.edition`（存进 `rm8SetDataEdition`，日志里写 sc / tc）、`game_dir_for_runtime`（相对 = `<out>\<值>`，绝对路径 = `--no-copy-game`，已用绝对路径的副本核对）、`scenes_dir_for_runtime`。两个路径的老写法照旧；此时 manifest 可有可无（开发环境没有），有的话仍检查 `data_format`。安卓：`<数据目录>\manifest.json` 可有可无（adb 推送的开发数据没有），有则检查（在 `unpackBundledData` 之后读）。`builder.py` 的 `rm8.bat` 最后一行改成 `rm8_app.exe "%~dp0." %*`。
* 3 字体：`fontCandidates()` 候选表（`RM8_FONT=<文件>[,序号]` 优先；Windows：simsun.ttc、msyh.ttc、msyh.ttf、simhei.ttf、Deng.ttf、simkai.ttf、msjh.ttc、mingliu.ttc，目录取 `%WINDIR%`；安卓：NotoSerifCJK / NotoSansCJK .ttc（SC = 2、TC = 3，沿用原写法）及其它几个文件名 [A]），选第一个能打开且有「大」字形的；全都没有时写日志 + `SDL_ShowSimpleMessageBox`，退出码 5（不再静默）。
* 4 存档表：`save.cpp` 启动时读 `scenes\save_tables.txt`（`encoding`、`code`、`terra`），`save_big5.inc`、`save_terra_order.inc` 已删除，不再编译。核对：旧版数据下，用旧 exe（含 .inc）与新 exe 同种子跑出的 `day0000.bin` 逐字节相同；`scripts\save_roundtrip.py` 在旧版（cp950）和 Steam 版（gbk，`out\extract_test\steam`）上都是“compared 10 records, 0 different”；表本身与旧 .inc 逐项相同（309 个码、15 张图的地块顺序，`port\scenes` 与 `out\extract_test\tc` 的副本）。`port\scenes\save_tables.txt` 已用提取器的 `savetables.py` + `export_save_terra_order.py` 在旧版上生成（cp950，与 `extract_test\tc` 的逐字节相同）；开发环境里 `port\scenes` 是旧版数据，所以 Steam 的 GBK 版本放在 `out\extract_test\steam\scenes\save_tables.txt`（用 Steam 数据跑时用 `steam\scenes`）。缺这个文件时日志提示，存档里的名字写成 `?`、读档报“no scene / plot table”。
* 5 `data_files.py` 早已列出 `save_tables.txt`（`check_app_data.py`：2703 个文件一致）。
* 6 原文引用：`out\app_text_edits.md` 重新生成（86 处：字面量 4、注释 82）。`infoscreens.cpp` 的 3 个字面量（卡片商店 / 两个小游戏名）改为运行时从 `string.bin` 5301 / 5314 / 5315 里按固定位置切出（event.bin 不导出）；`rm8_app.cpp` 自己的一句提示改了措辞；其余注释全部改写成 `<str#编号>`（`q#` 问答、`card#`、`npc#`、`drama#`），保留 [V] / [A] 标签和地址。再生成清单 0 处；`make_publish_tree.py --out <空文件夹>`：`files-with-original-text=0`。
* 7 `CARD_LEVEL` / `CARD_PRICE` 已从 `events.cpp` 删除，改用 `rm8CardLevel` / `rm8CardPrice`（`cards.txt`，`rm8CardLoadDefs` 在 main 里先于第一局加载；未知卡 5 / 0）；两张表与两个版本的 `cards.txt` 逐项核对相同。
* 9 用户数据：桌面写到 `%APPDATA%\RM8`（`scene.cpp: rm8UserDataDir`：`rm8_gamedate.bin`、`rm8_box.bin`、`rm8_settings.txt`、`save\`；`RM8_DATA_DIR` 仍可覆盖，现在设置文件也跟它走）；第一次运行把旧位置（scenes 的上一级）里已有的文件复制过来一次（新目录里写 `migrated_from_old_location.txt` 作标记，已有同名文件不覆盖），已用假的旧位置核对。安卓不变。`rm8.bat` 的说明文字同步。
* 12 带数据的 APK：`manifest.json` 本来就在 `manifest.txt` 的文件清单里，首次启动随数据解到数据目录，上面第 1/2 条的检查在解包之后读它；`manifest.txt` 的版本号 / 逐文件清单格式没有改成读 `manifest.json`（要自己重算版本号，不简单，保留）。个人 APK 默认名改为 `rm8_personal_DO_NOT_SHARE.apk`（`apkcmd.py`、`rm8_extract.py` 的帮助、`builder.py` 的提示、`extractor\README.md`；`build_apk.py --with-data` 不给 `--out` 时同名，避免盖掉基础 APK；`*.apk` 早已在 `.gitignore`）。已重新打包的 `rm8_extract.exe` **没有**重新生成（要发布时 `build_exe.py`）。
* 还没做 / 提醒：安卓只编译（`libmain.so` 已重新构建通过），首次运行提示和字体候选没有上真机；`introduce.txt`（`infoscreens.cpp:763`，别的代理新加的）还不在提取结果里，`check_app_data.py` 因此报 1 个问题；MinGW 的 `std::filesystem` 遍历目录联接点（junction）会得到空列表（只影响测试时用 junction 当 scenes）；用户名含非 ANSI 字符时 `%APPDATA%` 路径的编码没有测。

1. **首次运行检查**（`rm8_app.cpp` 约 143–147 行 `rm8OpenPacks` / `dataOk`，现在是红屏 / 橙屏而没有文字）：数据缺失、`manifest.json` 缺失、`data_format` 不等于 `RM8_DATA_FORMAT`（现在应为 2）时，显示中文提示并在日志 / stderr 输出，例如“找不到游戏数据。请先用 rm8_extract 从你自己的《大富翁8》提取数据（见 README），再把输出文件夹的 game、scenes、manifest.json 放到 …”；“数据版本过旧，请用最新的 rm8_extract 重新提取”。此时 `scenes\ui\strings.txt` 不可用，提示文字用 app 内置的自写字符串，用 stb_truetype 画（字体用系统字体，同现在）。安卓同样（数据目录 `files\`）。
2. **读 `manifest.json`**：电脑版接受单个参数 `<输出文件夹>`（等价于 `<输出>\game <输出>\scenes`）；读取 `data_format`、`source.edition`（`sc` = Steam 简体 GBK，`tc` = 旧版繁体 Big5）、`game_dir_for_runtime`（用 `--no-copy-game` 提取时它是原游戏文件夹的绝对路径，`game\` 不存在）。校验 `data_format`，把 `edition` 存下来给 3、4 用。
3. **字体与语言**：`rm8_app.cpp:152-154` 写死 `C:/Windows/Fonts/simsun.ttc` / `/system/fonts/NotoSerifCJK-Regular.ttc`，索引 `simplified ? 2 : 3`——改成候选列表（simsun.ttc、msyh.ttc、…），都找不到就显示提示而不是静默。**Steam（`sc`）是主要版本，文字〔sp_NN.bin#spl_16〕简体**：繁→简转换（`zh_t2s.txt`）对它无害，但“繁体”选项对它没有效果；旧版 `tc` 才有繁体可选。如果将来要给 Steam 版也提供繁体选项，提取器可以生成简→繁表（OpenCC 的 STCharacters / STPhrases，Apache-2.0）。
4. **存档里的名字编码改读 `scenes\save_tables.txt`**（新）：`save.cpp` 现在用编进可执行文件的 `save_big5.inc`（Big5 码表）和 `save_terra_order.inc`（15 张图的原版地块顺序和奇迹数）。提取器现在在 `scenes\save_tables.txt` 里提供同样内容，并按安装的版本选编码（旧版 `encoding cp950`，Steam `encoding gbk`；两者各 309 个字符）：
   * `encoding <cp950|gbk>`
   * `code <unicode 十六进制> <本地编码的 2 字节码>` —— 股票名用到的非 ASCII 字符
   * `terra <地图目录> <奇迹数> <n> <n 个下标>` —— 和 `RM8TO_TABLE` 一一对应（旧版结果已与 `.inc` 逐项核对相同）
   app 改为运行时读它，删掉两个 `.inc`（以及 `scripts\export_save_*.py` 不再需要进构建）。**Steam 版原存档里的名字是 GBK**，不读这个文件就会写错。
5. **把 `save_tables.txt` 加进 `port\android\data_files.py` 的 `SHARED` 列表**（否则 `deploy.py` / `build_apk.py --with-data` 不会推送它；`extractor\tools\check_app_data.py` 会提示）。
6. **源码里的原文引用**（清单：`<workspace>\out\app_text_edits.md`，私有，含原文；用 `extractor\tools\list_source_quotes.py` 重新生成）：`port\app` 里现在还有约 60 处，其中字符串字面量只有 3 处（`story.cpp:28` 关卡名、`stock.cpp:122` 两个公司名），其余是注释里引用的原句。
   * 字面量：改成运行时查提取数据——关卡名读 `stages.txt`，公司名读 `stocks.txt`，菜单 / 事件提示读 `string.bin` 的编号（`scenes\ui\strings.txt`，清单里每行给出了编号）。
   * 注释：改写成编号或转述（例如 `// 文本：string.bin#2`）。
   * 改完后用 `extractor\tools\make_publish_tree.py --out <空文件夹>` 检查，要求“files-with-original-text=0”（目前只剩 `port\app` 的 9 个文件，docs 已经为 0）。
7. **从游戏文件抄进源码的表**：`events.cpp:27-28` 的 `CARD_LEVEL`、`CARD_PRICE` 删掉，改读 `cards.txt`（字段 level、ticket，注释说“cards 模块加载前要用”，调整初始化顺序即可）。其余（`collect.cpp` 的 `T_UNLOCK / T_CLEAR / T_STAGE`、`news.cpp` 的 `T1 / T2`、`menu_anim.cpp` 的 `UICFG / FLY`）来自对 exe 的逆向，不是游戏数据文件里的内容，保留为事实性常量；注意 Steam 版 exe 与旧版不同，这几张表在 Steam 版上是否仍然成立**没有核对**。
8. **去掉写死的开发机路径**：`port\app` 里已经没有 `E:\`、`<dev>`；`scripts\*.py` 里有 14 个文件写死 `<old-edition-dir>`（不发布，不影响提取器；发布的 `extractor\exporters\` 快照里 `prteff.py` 和 `convert_videos.py` 还带着这个默认值，提取器总是显式传参覆盖它）；`port\android\*.py` 的 SDK / JDK 默认路径是开发机路径（只是构建脚本，可改成环境变量）。
9. **用户数据位置**：`rm8CollectInit(dd ? dd : scenes 的上一级)` 把 `rm8_gamedate.bin`、`rm8_box.bin`、`rm8_settings.txt`、存档写在输出文件夹里。提取器只替换 `game\`、`scenes\`、`manifest.json`、`build_tables\`，所以再次提取不会丢；但建议电脑版改写到 `%APPDATA%\RM8`，以免用户误删输出文件夹时丢进度。
10. **缺影片**：`menu_extra.cpp` 已经在文件不存在时跳过（已核对），无需改；`init.txt` 里没有 `binkplay` 时默认开，不需要。
11. **Steam 数据的运行时问题**：目前没有发现（地图、菜单、角色、事件都正常，见第 6 节）；但 Steam 的 `RichMan8.exe` 与小游戏 DLL 与旧版不同，**游戏规则是否有改动没有对比**（建议对 `Richman8.exe` 做一次 diff 或实测对局）。
12. **`unpackBundledData`（带数据的 APK）**：保持仅供本人使用，文案里提示“不得分发”；它读取的 `manifest.txt` 与提取器的 `manifest.json` 是两回事，可以统一成读后者。APK 文件名建议 `rm8_personal_DO_NOT_SHARE.apk`。

---

## 9. 变更清单（相对现有代码）

* 新增：`<workspace>\extractor\`（全部）；`scripts\rm8_edition.py`；仓库根目录和 `port\` 的 `LICENSE`、`NOTICE.md`、`THIRD_PARTY_NOTICES.md`、`.gitignore`；根目录的 `README.md`、`PUBLISH.md`；`port\README.md` 开头加了声明。
* 修改（不影响旧版输出）：`scripts\export_scene.py`（`read_cfg` 取密钥 / 编码）、`export_story.py`（同）、`export_questions.py`（没有 `.ini` 时读 `.bin`）、`convert_videos.py`（`--tolerance`）。
* 没有修改：`port\app`、`port\android`、`port\build-*`、任何安装文件夹。

---

## 10. 用户已决定的事项，以及还需要用户确认的

| # | 问题 | 决定 | 落实 |
|---|---|---|---|
| 1 | 代码许可证 | **与 richman4-remastered 相同：GPL-3.0-or-later** | 根目录和 `port\` 放了 GPL-3.0 全文 `LICENSE`；`NOTICE.md` 第 4 节写明只涵盖我们的代码；第三方保留各自许可证（`THIRD_PARTY_NOTICES.md`，都与 GPLv3 兼容） |
| 2 | 提取器是否发布 | **与它相同：提取 / 转换工具随源码一起公开**，只读用户自己的游戏，不含任何游戏文件；输出文件夹和带数据的包只限本人使用 | `extractor\` 在发布清单里（`PUBLISH.md`）；`NOTICE.md` 第 2、3 节、`README.md` 开头按它的七节结构用自己的话写成 |
| 3 | docs / 源码里的原文 | **能去掉的都去掉** | docs：构建发布树时自动把 ≥6 字的原文片段替换成 `〔表#编号〕`（发布版里 0 处残留；私有原版 `docs\` 不改）；源码：第 8 节第 6、7 条的清单交负责人改；抄自游戏文件的表改读提取数据（`save_tables.txt`、`cards.txt`） |
| 4 | 受支持版本 | **Steam 版为主要支持**；旧版暂时保留；用户之后会提供一份“干净”的旧版来调试 | 参考文件有 `status`：`sc` = `primary`，`tc` = `legacy`；多个参考同分时 `primary` 优先；`data\references\README.md` 写了怎么加 / 换一个版本；`tools\refresh_references.py` 的 `NAMES` 表一行一个版本 |
| 5 | ffmpeg / Bink 解码 | **允许 FFmpeg 的 LGPL Bink 解码器，也允许下载 ffmpeg** | 找不到本地 ffmpeg 时**默认**自动下载（固定地址 + 大小 + SHA-256 校验，下载前用中文说明下载什么、来自哪里、许可证；`--no-download-ffmpeg` / `--no-video` 可关闭；交互模式先询问）；`--ffmpeg` 与 PATH 仍然优先；LGPL / GPL 声明写在 `THIRD_PARTY_NOTICES.md`（我们不分发 ffmpeg）；以后内置解码器时在那里补 LGPL 要求的内容 |
| 6 | 逆向产物 | **只在私有保存，不发布** | `PUBLISH.md` 的发布 / 不发布清单；根 `.gitignore` 用白名单；`tools\make_publish_tree.py` 按白名单构建发布树并审计 |

仍需要用户确认的（见第 12 节末尾）：Steam 商店页面的准确链接、发布 `rm8_viewer.apk`（基础 APK）前的正式签名、`rm8_extract.exe` 的代码签名；（权利人联系方式已定为本仓库的 Issues 页面，`extern\` 已改为脚本下载）；以及一件需要核实的事：**Steam 版 exe 的规则与旧版是否一致**。

---

## 11. 声明文件（已写好，借鉴 richman4-remastered 的结构，用自己的话）

* `README.md`（根目录，和 `port\README.md` 的开头）：非官方、不含游戏文件、必须自己合法拥有（Steam）、提取器在你自己的电脑上转换、输出只限本人使用、链接 NOTICE / 第三方声明 / 许可证。
* `NOTICE.md`（中文，附英文摘要），七节：1 非官方项目与商标 · 2 本仓库不含游戏文件 · 3 不得散布含游戏内容的数据与构建 · 4 我们代码的许可证（GPL-3.0-or-later，只涵盖我们的代码）· 5 第三方组件 · 6 无担保 · 7 权利人联系（可立即下架）。
* `THIRD_PARTY_NOTICES.md`：链接进 app 的组件、开发 / 构建用的组件、提取器 exe 内含的组件、运行时下载的 ffmpeg（LGPL / GPL 声明）、系统字体、游戏内容。
* `PUBLISH.md`：发布 / 不发布清单与发布前检查。
* `.gitignore`（根目录白名单、`port\` 一份）。

---

## 12. 复现与发布检查

```
# 提取（源码；找不到 ffmpeg 时会下载，加 --no-video 跳过影片）
<workspace>\.venv\Scripts\python.exe -I <workspace>\extractor\rm8_extract.py "<游戏文件夹>" "<新的空输出文件夹>"
# 提取（exe）
<workspace>\extractor\build\dist\rm8_extract.exe "<游戏文件夹>" "<输出文件夹>"
# 检查
...\rm8_extract.exe --verify "<输出文件夹>" --full
# 运行桌面端
<workspace>\port\build-sdl2\rm8_app.exe "<输出>\game" "<输出>\scenes" --map 01 --size 640x360 --time 30 --shot out.png
# 构建发布树并审计（0 才算通过）
<workspace>\.venv\Scripts\python.exe -I <workspace>\extractor\tools\make_publish_tree.py --out <workspace>\out\publish
# 去原文版文档放到工作区（根 .gitignore 放行 docs_public\）
<workspace>\.venv\Scripts\python.exe -I <workspace>\extractor\tools\make_publish_tree.py --docs-only <workspace>\docs_public
```

仍待确认的小事：Steam 商店页面准确链接（README 里现在写的是“在 Steam 搜索 Richman 8”）、基础 APK 与 `rm8_extract.exe` 的正式签名。


---

## 13. 第三方依赖改为脚本下载（`port\extern\fetch_deps.py`）

用户决定：`port\extern\` 里的第三方源码不入库。`fetch_deps.py`（只用 Python 标准库）从各自的官方来源下载**固定版本 / 提交**，核对 SHA-256，放到 `port\extern\<名字>\`，并处理我们自己的补丁和文件：

| 依赖 | 来源与固定方式 | 备注 |
|---|---|---|
| SDL2 2.32.10 | libsdl-org 的 GitHub 发布文件 `SDL2-2.32.10.zip`，SHA-256 + 目录哈希 | 与现有 `extern\SDL2-2.32.10` 逐文件相同 |
| GLFW 3.5.1 | glfw 的发布文件 `glfw-3.5.1.zip`（SHA-256 与 GitHub 公布的 digest 一致） | 与现有目录逐文件相同 |
| librw | aap/librw 提交 `18532c2e13efbc1aa43f0b00b4459831f9414de0` 的源码包（GitHub 生成的包不是字节稳定的，所以按解包后的目录哈希固定）→ 自动运行 `patches\apply_librw_android.py` | 对全新下载的上游打补丁，结果与现有手工打过补丁的 `extern\librw` 逐文件相同（行尾除外：现有目录是 CRLF 检出） |
| CPython 2.4.6 | python.org 的 `Python-2.4.6.tgz`（SHA-256 固定；python.org 公布的 MD5 `7564b2b1…` 也一致），只解出 `port\deps_local\cpython24\files.txt` 列出的 208 个文件 | 再把 `port\deps_local\cpython24\` 里**我们自己写的** `rm8_config.c`、`rm8_getpath.c`、`pyconfig.h` 复制进去；结果与现有目录逐字节相同 |
| dr_mp3.h / pl_mpeg.h / stb_truetype.h | 各自仓库固定提交的 raw 文件（`cfdfbf6b…`、`c871f2be…`、`9f755b60…`），SHA-256 固定 | 与现有文件逐字节相同 |
| OpenCC 词典（TSCharacters、TSPhrases、LICENSE） | BYVoid/OpenCC 固定提交的 raw 文件，SHA-256 固定 | 与现有文件逐字节相同；提取器自带一份相同的副本 |

* 每个依赖最后还核对**整棵目录的哈希**；不符就删除下载并停止。解包逐个成员检查路径（没有绝对路径和 `..`）。下载缓存在 `extern\.cache`（`--offline` 只用缓存，`--verify` 只检查现有内容，`--force` 重新下载，`--record` 给维护者打印新版本的固定值）。
* `.gitignore`（`port\.gitignore`）：`/extern/*` 忽略，只放行 `fetch_deps.py` 和 `README.md`；`deps_local\` 入库。发布树（`make_publish_tree.py`）不再复制 `extern\` 的第三方源码，只带 `fetch_deps.py`、README 和 `deps_local\`。
* `port\README.md` 的构建步骤已加上“先运行 `python extern/fetch_deps.py`”，`CMakeLists.txt` 里的提示改了；`THIRD_PARTY_NOTICES.md` 记录了固定的版本 / 提交。

**验证**（没有碰现有的 `port\extern`）：把 `port` 复制到 `<workspace>\out\fetch_test\port`（不含 `extern`、`build-*`），在那里运行 `fetch_deps.py`（真实下载，约 5 分钟；再用缓存 `--offline`、`--verify`、`--force` 各跑一遍；篡改一个文件、损坏一个缓存文件，都会被发现），然后用 README 的步骤配置并构建桌面版（llvm-mingw + Ninja，483 个目标，约 1 分 40 秒）。同时用同一份源码快照 + 现有 `extern` 的副本构建对照版 `port_ctrl`。两个 `rm8_app.exe` 都用提取出的旧版数据运行 `--map 01 --size 640x360 --time 30 --shot`：**两张图逐像素相同**（`--time 0` 的图因为随机摆放每次运行本来就不同，同一个 exe 跑两次也不同）。

**把现有工作树切换过去的步骤**（等其他人的构建都停了再做）：
1. 在 `<workspace>\port` 里：`move extern extern_old`（留作备份），再 `mkdir extern`，把 `extern_old\fetch_deps.py`、`extern_old\README.md` 放回 `extern\`（或从 `<workspace>\out\fetch_test\port\extern\` 复制）；
2. 把下载缓存复制进来省去联网：`copy <workspace>\out\fetch_test\pinrun\extern\.cache` 到 `port\extern\.cache`；
3. `python extern/fetch_deps.py --offline`（或不加 `--offline` 联网下载），然后 `python extern/fetch_deps.py --verify` 应全部 ok；
4. 重新配置 / 构建各个构建目录（`build-sdl2`、`build-android` 等：路径没变，但文件时间变新，会整体重建；安卓的 `build_apk.py` 仍读 `extern\SDL2-2.32.10\android-project`）；
5. 确认无误后删除 `extern_old`。

（现状，第 16 节第 9 项：对现有 `port\extern` 运行 `fetch_deps.py --verify`，除 `librw` 外全部 ok；`librw` 的差异只是 CRLF 行尾。切换之后才会全部 ok。）


---

## 14. 一站式构建：提取器直接构建并打包移植版（Windows 文件夹 / 安卓个人 APK）

用户要求把提取器扩成“构建器”：`rm8_extract.exe <游戏> <输出> --target win|android|both`（不带参数时交互询问；已有输出时 `--build <输出> --target ...`）。实现在 `extractor\rm8x\toolchain.py`（工具安装）、`builder.py`（构建 + 打包）、`netdl.py`（下载）。

**源码从哪里来**：`--source` 指定源码文件夹（git 检出或发布树，里面有 `port\`）或发布页的源码包 `rm8-port-src-<tag>.zip`（`make_publish_tree.py --zip` 生成，1.3 MB）；不写就找 exe 旁边的 `port\`；还没有的话读 `extractor\data\source_release.json` 里的 URL + SHA-256 自动下载（发布第一个版本后填入；现在为空）。**建议的发布形态**：发布页放 `rm8_extract.exe` + `rm8-port-src-<tag>.zip` + `SHA256SUMS.txt`，用户下载两个文件放在一起即可。源码先复制进工作文件夹（不含 `extern\` 里的第三方树、构建产物、用户数据），不会改动用户的检出。

**工具**（缺什么装什么，装进私有缓存 `%LOCALAPPDATA%\RM8\toolchain`，`--toolchain-dir` 可改；不装进系统、不要管理员权限、不改 PATH；版本与 `port\README.md` 一致，固定大小 + SHA-256，Android 包另核对 Google 公布的 SHA-1）：

| 工具 | 版本（来源） | 下载 | 用于 |
|---|---|---|---|
| CMake、Ninja | 4.5.0、1.13.2（GitHub 发布文件） | 54 MB、0.3 MB | 全部 |
| llvm-mingw | 20261104 ucrt x86_64（GitHub） | 182 MB | Windows |
| Temurin JDK | 21.0.13+9（GitHub / adoptium） | 196 MB | 安卓 |
| Android NDK / build-tools / platform | r29（29.0.14206865）/ 36.1.0 / android-35（dl.google.com，与 sdkmanager 装的同一批包，直接下载省掉 cmdline-tools） | 795 + 57 + 61 MB | 安卓 |
| ffmpeg | 见 5.4（转影片，默认下载） | 115 MB | 影片 |

* 下载用系统自带的 `curl.exe`（Windows 10/11 都有；在本机上 urllib 只有 0.05–0.1 MB/s，curl 能到 1–10 MB/s），没有 curl 再用 Python；可续传；缺的工具**同时**下载；断网时可把文件按原名放进 `<缓存>\downloads\`。Python 不需要另装：构建脚本（`fetch_deps.py`、`build_apk.py`）由 exe 里自带的 Python 在进程内运行。
* **Android SDK 许可**：装 NDK / build-tools / platform 之前，程序取 Google 的 `repository2-3.xml` 里的 `android-sdk-license` 全文，**显示给用户并询问**（输入 y 才继续；文字的 SHA-256 固定为 `de5fa465…`，Google 改了文字会提示）；非交互运行必须显式加 `--accept-android-licenses`，否则停下。接受记录写在工具缓存里。不会静默接受。
* 空间检查：工具缓存（约 4–5 GB 解压后）、工作空间（4 GB）都会先检查，不够就报错并提示换磁盘。
* 可恢复：每一步（复制源码、装工具、下载第三方源码、Windows 编译、安卓编译）在 `<工作文件夹>\state.json` 里记签名（源码哈希 + 工具固定值），没变且产物还在就跳过；安装和下载先写 `.part` 再改名，中断不会留下半成品。日志（含完整编译输出）在输出文件夹的 `extract.log`；错误信息是中文，并带出编译输出的最后 25 行。

**构建与打包**：`fetch_deps.py`（第三方源码，缓存在工具缓存里）→ `cmake` 配置 + 编译 `rm8_app`（Windows：llvm-mingw + Ninja，485 个目标）/ 安卓（NDK toolchain，arm64-v8a，android-24）→ 打包：
* **Windows**：就地放进输出文件夹：`rm8_app.exe`、`rm8.bat`（启动器）、`README_windows.txt`；`--zip` 另外压成 zip（含用户数据，只限本人）。用 `llvm-objdump -p` 读 exe 的导入表：llvm-mingw 构建是**静态链接**，只依赖 Windows 自带的系统 DLL（24 个，含 `api-ms-win-crt-*`，Windows 10 自带），所以不需要随带 libc++ / libunwind / SDL2（以后若变成动态链接，程序会自动从 llvm-mingw 里复制缺的 DLL）。**`rm8.bat` 现在用 app 要求的两个参数**：`rm8_app.exe "%~dp0game" "%~dp0scenes" %*`；app 支持“单个输出文件夹 / manifest.json”后，只需把最后一行改成 `rm8_app.exe "%~dp0." %*`（`builder.py` 的 `build_windows` 里生成，注释里也写了）。
* **安卓**：编译 `libmain.so` / `libSDL2.so`，`build_apk.py`（不加 `--with-data`）打出基础 APK，再走 5.4 的流程装入用户数据并用用户自己的密钥签名 → `rm8_personal_*.apk`。已有基础 APK 时用 `--base-apk`，不再编译，也不装安卓工具。

**验证**（本机，干净环境模拟：空的工具缓存 `out\builder_test\toolchain`、空的工作文件夹、用 `make_publish_tree.py` 生成的源码包 `rm8-port-src.zip` 当源码，Steam 版当游戏；没有碰 `port\build-*`、`port\extern`、`<tools>`，没有用 adb）：
1. `rm8_extract.exe <Steam> <输出> --target both --source <源码包> --accept-android-licenses --zip` 从零开始：许可证全文显示 → 7 个工具全部真实下载并校验（CMake、Ninja、llvm-mingw、JDK、NDK、build-tools、platform 各一遍，ffmpeg 上一轮已证明）→ 下载第三方源码 → 编译 Windows 版（485 个目标）→ 打包 → 编译安卓版 → 基础 APK → 个人 APK。（本机到 GitHub 很慢，llvm-mingw 约 0.1 MB/s，整个过程主要耗在下载上。）
2. Windows 输出：`rm8_app.exe` 在 PATH 只含 `C:\Windows\System32` 的环境里用 `rm8.bat --map 01 --size 640x360 --time 30 --shot ...` 正常出图，与直接调用 exe 的图**逐像素相同**，也与之前用现有 `port\build-sdl2` 得到的图逐像素相同；日志 `map 01: ... articles=191 failed=0`。
3. 安卓输出：`zipalign -c -P 16 4` 通过；`apksigner verify --min-sdk-version 24` 通过（v2 = true）；`aapt2 dump badging` 读出 `com.rm8.port`；所有 CRC 通过；2704 个数据文件与输出文件夹逐个比对（SHA-256）一致；我们自己的 v2 验证器通过。（`verify_apk.py --build-tools <缓存>\android-sdk\build-tools\36.1.0 --jdk <缓存>\jdk`，用的是刚下载的工具，不是 `<tools>`。）
4. 再运行一遍同样的命令：提取“已是最新”，第三方源码、Windows 编译、安卓编译全部“跳过”，只重新装 APK（1 分 20 秒）。
5. 空工具缓存 + 不加 `--accept-android-licenses` 的非交互运行：显示许可证全文后停下，不下载任何东西；`--no-install-tools` 缺工具时直接报错。

**注意**：
* 新编译出的基础 APK 里 `libmain.so` 是 4.58 MB（现有 `rm8_viewer.apk` 的是 4.62 MB），因为源码在这期间有新提交；两者都不含游戏数据。
* 这套流程对源码的要求：`port\CMakeLists.txt`、`extern\fetch_deps.py`、`deps_local\`、`android\build_apk.py`（读 `extern\SDL2-2.32.10\android-project`）。`fetch_deps.py` 新增了环境变量 `RM8_DEPS_CACHE`（共享下载缓存）、curl 下载和“在打包后的 exe 里用 runpy 运行补丁脚本”的支持，不影响原有用法。
* 许可证：构建工具的许可证列在 `THIRD_PARTY_NOTICES.md`“一站式构建时下载的工具”一节（我们不分发它们）。


---

## 15. 迁移原版存档与进度（`--saves` / `--no-saves`）

用户要求：打包时可以把原版存档迁移到移植版。交互运行会问“是否打包存档（迁移原版存档到移植版）”；非交互运行默认关闭，必须加 `--saves`。实现：`extractor\rm8x\saves.py`，接入 `rm8_extract.py`（`prepare_saves`）、`rm8x\apk.py` / `apkcmd.py`（装进 APK）。用法见 `extractor\README.md`“迁移存档”一节。

**迁移什么、是否要转换**（对照 `port\app\save.cpp`、`collect.cpp`、`docs\save_port.md`、`docs\media_collect.md`）：

| 来源（只读） | 移植版位置 | 转换？ |
|---|---|---|
| `Save\record_NN.sav`（NN = 01..10，故事存档位）、`Save\record_mNN.sav`（自由模式存档位） | `<数据目录>\save\<同名>`（`save.cpp`：`record_%02d.sav` / `record_m%02d.sav`） | 不需要，逐字节复制；复制前按 `rm8SaveSlotInfo` 同样的规则检查（版本 > 6、`r8recrd`），坏文件跳过。`record_temp.sav`（自动存档）不迁移 |
| `Config\gamedate.bin`（181 个 int32 = 724 字节：剧情进度、解锁角色、拼图） | `<数据目录>\rm8_gamedate.bin` | 不需要：`collect.cpp` 的 `loadGameDate` / `saveGameDate` 与原版同格式、同魔数表（`GameDate_Load @0x4a99e0`）；提取器用同一套魔数校验并给出摘要 |
| 储藏箱 | `rm8_box.bin` | 原版没有独立文件（在故事存档里），移植版的 box 文件是移植版自己的，无可迁移内容 |

查找位置：游戏文件夹的 `Save`、`Config`（Steam 版的存档在安装目录里，`steam_autocloud.vdf` 说明 Steam 自动云同步的根就是它；本机 `E:\steam\userdata\<steam-account>\2185870` 里只有 `remotecache.vdf`，记录的是 `config/gamedate.bin`）；Steam 版还会只读地扫描 `<Steam>\userdata\<帐号>\2185870\remote\**`（Steam 目录取自注册表、`libraryfolder.vdf`）；同名取最新的有效文件。旧版（非 Steam）安装不扫 Steam 目录。

**电脑版**：放到 app 读的数据目录——`RM8_DATA_DIR`（设了就用）否则 `%APPDATA%\RM8`（`rm8UserDataDir`，`--saves-dir` 可改）。**绝不静默覆盖**：内容相同跳过；不同则交互询问（`y` 覆盖并先备份为 `.bak-时间`，`N` 保留，`a` 全部）、非交互保留原文件并写日志。游戏文件夹始终只读（自测里用文件大小 / 时间快照验证没有被改）。

**安卓个人 APK**：存档放进 `assets/rm8data/import_saves/save/record_*.sav`、`import_saves/rm8_gamedate.bin`，并在 `manifest.txt` 里按 `<大小>\timport_saves/<相对路径>` 登记（也计入数据版本号）。**app 的一处小改动（已做，仅在 `rm8_app.cpp` 的 `unpackBundledData` 里）**：登记路径以 `import_saves/` 开头的项，目标改为 `<数据目录>/<去掉前缀的路径>`，且**目标已存在就保留不覆盖**（记一行日志）；其余项照旧。安卓上数据目录就是 `scenes` 的上一级，也就是 app 读 `save\` 和 `rm8_gamedate.bin` 的地方。带存档的 APK 仍是个人 APK（`rm8_personal_DO_NOT_SHARE.apk`），完成提示和 README 都写明含个人数据、只限本人使用。

**验证**：
1. 旧版样本（`<old-edition-dir>\Save` 的 5 个存档 + `Config\gamedate.bin`，只读）→ `rm8_extract ... --saves --saves-dir <空文件夹>`：6 个文件复制，与来源 SHA-256 逐个相同（脚本 `out\saves_test\cmp.py`）。
2. 桌面 app 用迁移后的数据目录（`RM8_DATA_DIR`）：日志 `collect: profile .../rm8_gamedate.bin`；读档页（`RM8_MENU_AUTO="single,load"`）列出 01、02、03、08 四个故事存档（日期、地图、角色头像正常），自由模式读档页列出 m01（`out\saves_test\menu\`）；`--load .../record_03.sav` 载入棋盘：`save: loaded (original record), day 41, seat 0`，截图正常。（没有碰 `%APPDATA%\RM8`。）
3. 覆盖规则：重复迁移 → “已经是一样的”；改动目标后非交互 → 保留（0 个复制、2 个保留）；交互 `n` 保留 / `y` 覆盖并生成 `.bak-时间`。`tools\selftest.py` 新增存档用例（记录与进度迁移、自动存档不迁移、不同文件不被覆盖、默认关闭、游戏文件夹未改动），全部通过。
4. 安卓：用刚改过的源码经一站式构建流程（NDK 编译，56 个目标）生成基础 APK，再装入旧版数据和 6 个存档文件 → `<workspace>\out\saves_test\rm8_personal_DO_NOT_SHARE.apk`：`zipalign -c -P 16 4` 与 `apksigner verify --min-sdk-version 24` 通过，所有 CRC、2710 个条目逐个比对一致（`verify_apk.py` 现在也会比对 `import_saves/`），`libmain.so` 里确实含有新代码。unpack 逻辑没有在手机上跑（不用 adb、不安装），用与 C++ 循环逐行对应的 Python 镜像在临时目录验证：首次启动导入 6 个文件；之后换新 APK（版本变了）时已有的 `record_01.sav` 和 `rm8_gamedate.bin` 保持不变；缺的存档位会补回。

**注意**：旧版（Big5）存档配旧版提取数据、Steam 存档配 Steam 提取数据才对得上（`save_tables.txt` 按版本选编码）；`--saves` 默认读提取用的那个游戏文件夹，所以天然一致。


---

## 16. 发布就绪（release-ready）：双击流程、中文错误说明、全流程验证

本轮把提取器 / 构建器收尾到可以发布的状态。命令行选项**没有变**（只新增 `--version`）；`EXTRACTOR_VERSION` 升到 1.2.0（数据格式仍是 2，输出内容没变，只是旧输出会被判为“版本不同”而重新提取一次）。

### 16.1 改了什么

| 文件 | 改动 |
|---|---|
| `extractor\rm8x\detect.py`（新） | 自动找游戏：Steam 版读注册表（`SteamPath` / `InstallPath`）、`steamapps\libraryfolders.vdf` 的所有库文件夹、`appmanifest_2185870.acf`；旧版读卸载信息和常见位置（各固定磁盘根目录、`Games`、`Program Files`、桌面 / 下载 / 文档）。`suggest_fix()`：用户选了 `steamapps\common`、`Clump` 子文件夹、`RichMan8.exe` 本身时，找出真正的游戏文件夹。只列目录，不写任何东西 |
| `extractor\rm8x\wizard.py`（新） | 双击（不带参数）流程：游戏文件夹 → 输出文件夹 → 要什么 → 迁移存档 → 确认；联网前先检查网络；完成后的“你得到了什么、在哪里”汇总（同时写入输出文件夹的 `结果说明.txt`）；关闭控制台的 QuickEdit（点一下窗口不再让程序卡住）。最后一问“开始吗？”之前不创建任何文件 |
| `extractor\rm8x\friendly.py`（新） | 磁盘已满 / 没有权限 / 路径太长 / 磁盘出错 / 没有网络的中文说明 |
| `extractor\rm8x\netdl.py` | `unreachable_hosts()`：先探测主机能否连上（urllib 走系统代理，再用 curl 复核）；`offline_message()` 给出代理和手动放文件的办法；`looks_like_network_error()` |
| `extractor\rm8x\toolchain.py` / `builder.py` / `ffmpeg_tool.py` | 下载前先检查网络，断网时一条清楚的消息（不再是下载库的原始异常）；`fetch_deps.py` 在进程内运行时的网络 / 磁盘错误也被转成中文；ffmpeg 下载失败时说明“已跳过影片，联网后重新运行会补上”；`have_ffmpeg()`；**源码包自动发现**：exe 旁边的 `rm8-port-src*.zip`（发布页两个文件放一起即可）；Windows 版 `README_windows.txt` 里写了 SmartScreen 提示和显卡要求 |
| `extractor\rm8x\fingerprint.py` | `UnsupportedVersion`（版本不一致，交互运行时问用户是否仍要试试，等于 `--allow-unknown`）；“不是游戏文件夹 / 文件不全 / 版本不一致”的消息改成说人话，并建议 Steam 里“验证游戏文件的完整性” |
| `extractor\rm8_extract.py` | 统一的错误出口（`run_wrapped`）：所有失败都是中文 + 下一步怎么办；只有程序内部错误、构建失败、自检失败才提示“到 Issues 页面反馈并附 extract.log”，选错文件夹、断网、没空间、没接受许可协议等用户自己能解决的不提示；磁盘满 / 权限错误（`OSError`）单独说明，退出码 4；参数错误的提示改成中文；`--version`；管道输入时按 UTF-8 读 |
| `extractor\data\references\*.json` | **重新记录**（`refresh_references.py`）：之前的 `*_outputs.json` 记录的是旧版导出脚本（`exporters_digest` 与快照不符，输出统计校验被静默跳过），而且新的导出脚本读取 `Config\introduce.bin`，参考里没有它（缺这个文件时不会被报“游戏文件不全”）。现在 Steam 版 281 个、旧版 282 个文件，统计校验重新生效。备份在 `out\release_test\refs_backup\` |
| `extractor\tools\check_notices.py`（新） | 检查 `THIRD_PARTY_NOTICES.md`“所有会联网下载的东西”一节与代码里的 19 个下载地址（`toolchain.TOOLS`、`ffmpeg_tool.PINNED`、`fetch_deps.DEPS`）完全一致；`make_publish_tree.py` 会调用它 |
| `extractor\tools\make_publish_tree.py` | 另外检查：发布树里没有 `private`、`mod`、`out`、`ghidra_*`、`scripts`、私有文档、`__pycache__`、`*.pyc`；下载清单与代码一致 |
| `extractor\tools\selftest.py` | 新增：双击流程（选错文件夹 / 非空输出文件夹 / 回答“否”不创建任何东西 / 完整一遍 / 版本不一致回答“否”和“是” / 输入中途结束）、断网（没人监听的代理）、磁盘已满（模拟）、`ENOSPC` 的消息 |
| `THIRD_PARTY_NOTICES.md`（根和 `port\` 各一份） | exe 内含组件按实际清单重写（CPython 3.12 + OpenSSL / libffi / zlib、Tcl/Tk、numpy + OpenBLAS、Pillow + 随带库、VC++ 运行库、PyInstaller 引导程序）；新增“所有会联网下载的东西（完整地址）”一节 |
| `README.md`（根）、`port\README.md`、`extractor\README.md`、`PUBLISH.md` | 快速开始（Windows / 安卓）、FAQ（下载什么 / 为什么、许可证、不要分享个人 APK、存档位置、没有网络 / 磁盘 / 版本不符、SmartScreen / 杀毒、卸载）、Steam 商店链接、权利人联系 = 仓库 Issues 页面；`port\NOTICE.md` 与根目录的一致 |

### 16.2 验证（全部在本机；没有用 adb，没有往手机装任何东西，没有改动游戏文件夹，没有碰 `port\extern`、`port\app`、`private\`）

输出都在 `out\release_test\`；工具缓存复用 `out\builder_test\toolchain`（版本没变，没有重新下载）；Android 许可协议的接受记录沿用该缓存里的那份。

1. `sync_exporters.py --check`：up to date（但参考文件过期，已重新记录，见上）。
2. **Steam 版 `--target both --saves --zip`**（先用源码、再用新打的 exe 各跑一次；exe 那次用发布树生成的源码包 `rm8-port-src-1.zip` 当 `--source`，新工作文件夹从零编译，Windows 版 485 个目标、安卓版 474 个目标）：输出 `steam\` 和 `steam_exe\`，两次的 `scenes` 逐文件相同（2577 个文件，`compare_trees.py`）。`check_app_data.py`：2704 个文件与 `data_files.py` 一致，源码里的文件名全部找到（RESULT OK）。
3. **旧版（`<old-edition-dir>`）普通提取 + `--target win`**（`old\`）：成功；`check_app_data.py` OK。
4. **Windows 输出在只含 `C:\Windows\System32` 的 PATH 下运行**：Steam 版 `rm8_app.exe "<输出>\." --map 01 --size 640x360 --time 30 --shot` 出图正常（`map 01: ... articles=191 failed=0`），`--frames 120` 正常退出；旧版同样；`rm8_app.exe` 的导入表全是系统 DLL（24 个），不需要随带 DLL。
5. **APK**：`verify_apk.py --data-dir <输出> --build-tools <缓存> --jdk <缓存>`：CRC 全部通过、2709 个条目逐个比对一致（含 4 个 `import_saves`）、我们自己的 v2 验证通过、`zipalign -c -P 16 4` 通过、`apksigner verify` 通过（v2 = true）。用的是缓存里的 build-tools / JDK。
6. **`selftest.py`**：从源码跑一遍、用 exe 跑一遍（`--exe`），全部通过（各 29 项检查，ALL PASS）；“磁盘已满”一项是在进程内模拟的（把 `shutil.disk_usage` 换成只剩 1 MB），exe 那次跑的也是这一项源码版本。
7. **发布树 + 审计**（`make_publish_tree.py --out ... --zip ... --corpus <刚提取的 steam / old 的 scenes> ...`）：identical-to-game = 0、derived-data = 0、files-with-original-text = 0、forbidden file types = 0、private paths = 0、下载清单与代码一致（19 / 19）。包里没有 `private\`、`mod\`、`out\`、`ghidra_proj\`、反编译产物、私有文档（`code_map.md` 等 5 个）、`manifest.json`、`extract.log`、exe / dll / apk / dat / mpg / bik / pyc。把发布树压成的源码包解开后再用 `audit_repo.py` 审一遍，同样全为 0。
8. **exe 冒烟**：`--help`、`--version`、`--verify`、参数错误；双击流程用模拟输入逐一跑过：选错文件夹（`steamapps\common`，提示“你是不是想选这个”）、输出文件夹不是空的 / 不存在的游戏路径、回答“否”（什么都没创建）、断网（代理指向无人监听的端口：先检查网络，中文说明，并让你改选“只提取数据”）、完整的一遍（自动找到 Steam 版 → 迁移 3 个存档 + 进度 → 电脑版，源码包取自 exe 旁边，汇总里写明 `rm8.bat`）、选安卓后**拒绝** Android SDK 许可协议（显示许可全文，退出码 6，不下载任何东西）。
9. **`fetch_deps.py --verify`**（对现有 `port\extern`）：SDL2、GLFW、cpython24、dr_libs、pl_mpeg、stb、opencc_dict 全部 ok；`librw` 报 MISMATCH——**只是行尾**：现有目录是 CRLF 检出，274 个文件与新下载并打过补丁的 `librw` 逐字节比较，换行归一后全部相同，没有任何实质差异（`.fetched.json` 除外，那是脚本自己的标记）。第 13 节记录过这一点；切换到 `fetch_deps` 后（第 13 节的步骤）就是 ok。没有改动 `port\extern`。

### 16.3 给主 agent / 用户

* `port\app` 没有需要改的地方。Windows 版靠 `rm8_app.exe "<输出>\."`（单个输出文件夹）启动，已经可用；`rm8.bat` 也已是这个写法。
* **第一次发布后**要把源码包的 URL 和 SHA-256 填进 `extractor\data\source_release.json`（现在为空；不填也行，用户把 zip 放在 exe 旁边即可）。
* **签名**：Windows 的 `rm8_extract.exe` / `rm8_app.exe` 不签名（用户决定）；README、`README_windows.txt` 和完成汇总里都写了 SmartScreen 怎么点。PyInstaller 单文件程序偶尔被杀毒软件误报，FAQ 里写了用 `SHA256SUMS.txt` 核对。
* **切换 `port\extern`**：已完成（2026-11-15 23:15，主代理）：`extern` 已改为 `fetch_deps.py --offline`（缓存取自 `outetch_test\pinrun`）重新生成，`--verify` 全部 ok（含 librw）；`build-sdl2` / `build-android` 全量重建通过，地图 01 截图 failed=0；旧目录已删除。
* 许可协议：Android SDK 许可协议只会在交互时显示全文并询问，非交互必须自己加 `--accept-android-licenses`；这一点没有变。
* 我在 `%APPDATA%\RM8\apk_key` 误生成过一把测试用的签名密钥（第一次 `--target both` 没写 `--key-dir`），已经移到 `out\release_test\keys_steam\`，之后的测试都显式指定 `--key-dir`。`%APPDATA%\RM8` 里原有的存档和设置没有被碰过（测试的存档迁移都用 `--saves-dir` 指向 `out\release_test\appdata_*`）。
