# 大富翁8 (RichMan8) 逆向分析总览

> 分析对象：`<old-edition-dir>`（只读，未修改任何游戏文件）  
> 工作区：`<workspace>`　工具：`<tools>`（uv / Python 3.12 x64+x86 / JDK 21 / Ghidra 12.2）  
> 结论分级：**已验证** = 有代码/数据直接证据或经过往返测试；**推测** = 根据命名或上下文推断；**待验证** = 需要动态调试或游戏内测试确认。

## 0. 文档索引

| 文档 | 内容 |
|---|---|
| `docs/README.md` | 本文：整体架构、文件格式、逆向完整度、修改路线 |
| `docs/workflow.md` | **修改工作流**：mod/src 编辑 → build → deploy → restore，验证手段 |
| `docs/code_map.md` | **代码地图**：exe 各模块的地址范围与函数命名覆盖率、置信度分布 |
| `docs/gameplay_algorithms.md` | **核心玩法算法**：物价指数、过路费、股价涨跌（逐行核对代码） |
| `docs/exe_internals.md` | exe 内部：全局对象、结构体偏移、AI 回调时机、文件读写函数、RichMan8Fix 补丁位置 |
| `docs/cards.md` / `docs/ai.md` / `docs/python_api.md` | 49 张卡、16 个 AI、130 个脚本接口 |
| `docs/config_files.md` | Config 下各配置的字段说明 |
| `docs/save_format.md` | **存档格式**（解析器 `scripts/savefile.py`，5 个样本解析无剩余并通过自检） |
| `docs/assets_legal.md` | **素材分离与提取**：运行时读取的文件清单与权属分类、源码 / APK 审计、旧版 vs Steam 版差异、用户流程、安卓方案、提取器 `extractor/`（`rm8_extract.exe`）、app 需要的修改 |
| `docs/map_format.md` | **地图格式** .map/.ltm（解析器 `scripts/mapfmt.py`，30 个文件解析无剩余） |
| `docs/prt_format.md` / `docs/rolebin_format.md` | 粒子表、角色动画索引 role.bin（均解析无剩余） |
| `docs/autoplay.md` | **托管**（移植规格）：`+0x1B0 == 2`、开启 / 关闭路径（gsys→supply、F1..F4、任意键放回）、电脑代码路径、存档、触屏钩子、测试钩子与验证 |
| `docs/turn_flow.md` | **掷骰与回合流程**（移植规格）：骰子动画、回合状态机与消息队列、C++↔Python 交接点、回合箭头与镜头、同格错开、交通工具 |
| `docs/rules_land.md` | **地产层规则**（移植规格）：地块 / 街道 / 建筑的数据模型、地价 / 加盖费 / 过路费公式、落地消息链（0x12/0x11/0x1F/0x20/0x22）、人类界面与电脑 TBuy/TBuild、破产与拍卖、新的一天（物价指数、月息）、开局资金、表现层 id |
| `docs/audio.md` | **音频**（移植规格）：CAudioMgr_PlaySample 参数、音量、停止/暂停、棋盘音乐选曲规则、按事件列出的样本号、WAV/MP3 实际格式、移植实现与未做项 |
| `docs/particles.md` | **粒子特效系统**：payload 字段、RW prtstd/PTank 运行时算法、挂点 `[@n]`、26 个效果参数（模拟器 `scripts/prteff.py`） |
| `docs/minigames/lg_18~23.md` | 6 个小游戏的玩法、计分与主程序接口 |
| `port/README.md` | **安卓原生移植技术验证**：librw 解析全部 5523 个 RenderWare 资源零失败，并渲染出角色、建筑、剧情舞台 |
| `docs/characters.md` | **16 个角色**：隐藏角色解锁条件与检查位置、服装 / 泳装、菜单锁定、每个角色的专属规则、伊格魯 / 莎拉的角色子类（伴随对象）、移植状态 |
| `docs/re_context.md` | 已验证的编号表与结构偏移速查（也供子代理使用） |

## 1. 整体架构（已验证）

```
RichMan8.exe (VC7.1, 32 位, 2008-01-11 编译, 未加壳)
 ├─ d3d9.dll        → 当前是 DXVK v3.1.1（Vulkan 转译，用户添加）
 ├─ dinput8.dll     → RichMan8Fix（用户添加的分辨率/字体修正代理 DLL，读 RichMan8Fix.ini）
 ├─ Mss32.dll + Mssd/   Miles 音频
 ├─ Binkw32.dll + Bink/*.bik   片头/片尾视频
 ├─ Python24.dll + Pysrc/*.pyc  内嵌 Python 2.4：AI、卡片效果、剧情动画全部在脚本里
 ├─ Gamelib/ltlgame_18~23.dll   小游戏插件（导出 LG_create_game / LG_delete_game）
 ├─ Config/*.bin, *.ui          加密 INI 配置
 ├─ Clump/ Texture/ Sound/ Music/  *.dat 资源包
 └─ Map/*.map + *.ltm           地图（rch8map v9）+ 光照/地形图
```

- 游戏逻辑核心（掷骰、移动、地产、银行、股票结算）在 exe 内；**决策和卡片效果在 Python 脚本里**，因此大部分玩法调整不需要改 exe。
- exe 启动时执行 `import sys,os` / `SRCDIR='pysrc'` / `sys.path.insert(0,SRCDIR)`，然后按名字导入脚本模块（如 roleinfo 的 `AI=AI_01`、card 的 `python=CardPy_01.py`）。
- `Richman.dll`（2006 年，64KB）不被 exe 导入，也没有被字符串引用，只导入 CreateFontW/CreateFontIndirectW，推测是早期的字体/繁简转换补丁残留，当前未使用。
- `乱码修正.exe` 不是可执行文件：它与 `完美存档备份/Config/gamedate.bin` 以及当前 `Config/gamedate.bin` 的 MD5 完全相同（724 字节），实际上是“全通关”进度文件的副本。
- `d3d9 - 副本.dll` 是 RichMan8Fix 的旧版本（以 d3d9 代理形式实现）。

## 2. 文件格式

### 2.1 Config 加密配置（已验证，可往返）
- 算法：逐字节加法 + 循环密钥：`密文 = (明文 + key[i % len]) & 0xFF`
- 密钥：`.bin` → `tiep8jdg`；`.ui` → `rich8ui`
- 明文是 **Big5(cp950)** 编码的 INI 文本。例外：`festival.bin`（春节日期表）、`gamedate.bin`（二进制进度）、`newOut/role.bin`（二进制）、`prt/*.tb`（粒子二进制）不加密。
- 工具：`python scripts/rm8crypt.py decutf8|encutf8 auto <输入> <输出>`（UTF-8 文本直接往返；`dec|enc` 处理原始 Big5 字节）。已验证 113 个加密配置全部可以逐字节还原。
- 已解密：`out/config_dec/`（Big5 原文），`out/config_utf8/`（UTF-8，便于阅读）

### 2.2 Python 脚本（已验证，可往返）
- `Pysrc/*.pyc`：Python 2.4（magic `6d f2 0d 0a`），共 121 个，其中 105 个为游戏脚本，16 个为标准库。
- 反编译：`out/pysrc/`（uncompyle6 原始输出），`out/pysrc_clean/`（去掉文件头、可直接重编译）
- 重编译：`py32 -I scripts/py24c.py <file.py>`。其中 `py32` 是 32 位 Python，路径为 `%USERPROFILE%\AppData\Roaming\uv\python\cpython-3.12.16-windows-x86-none\python.exe`。脚本通过 ctypes 加载游戏自带的 `Python24.dll`（副本位于 `tools/py24`），因此产物与游戏完全兼容。
- **严格往返验证**（`scripts/verify_pysrc.py`）：用游戏自带的 Python24.dll 把源码重新编译，再与原 .pyc 递归比较所有影响行为的字段（co_code、常量、名字表、变量表、参数个数、flags、栈深度、文件名）。
  - **105 个游戏脚本全部一致**，只有行号表不同（行号只影响报错时显示的行号）。
  - 反编译器原始输出中有 6 处错误，已修正（`scripts/fix_decompile.py`，修正后经字节码比对确认）：CardPy_12 有一处语法错误；CardPy_02/03/12/14 有 4 处把嵌套 `if` 合并成了 `and`（行为相同，只是结构不同）；**CardPy_04（交换卡 AI）有 1 处真正的语义错误**：`elif m < 2` 应该属于 `if s == sa`，反编译器把它挂到了外层的地主判断上，还引用了上一轮循环留下的 `sa`。
  - **规范源码以 `out/pysrc_clean/` 为准**；`out/pysrc/` 只是 uncompyle6 的原始输出，不要直接使用。
- 标准库（`scripts/verify_stdlib.py`）：15 个模块与官方 **Python 2.4.1** 源码编译结果一致（游戏 DLL 的版本也是 2.4.1）。`random.pyc` 例外：它是开发者放进 pysrc 的 **Python 2.3 官方 random.py**，未经修改（与 2.3 正式版一致），所以随机数没有被动手脚，`randint` 是均匀分布。这 16 个模块 mod 时不需要动。

### 2.3 资源包 *.dat（已验证）
`scripts/rm8pak.py list|extract <dat> [outdir] [过滤串]`

| 包 | 格式 | 条目 | 内容 |
|---|---|---|---|
| Clump/clump0000.dat (40MB) | A | 5526 | RenderWare 模型 .dff 1180、动画 .anm 4312、.uva、1 个 .rws |
| Texture/tex0000.dat (698MB) | A | 3057 | .tga 2723、.bmp、.ani 光标、UI 贴图 |
| Texture/tex0001.dat | A | 2 | 补丁贴图（prt\ui\inside\j205_1_1.tga, j238.tga） |
| Sound/snd0000.dat (266MB) | B `snd%` | 2400 | WAV 音效/语音，按数字 ID 索引 |
| Music/mus0000.dat (62MB) | B `mus%` | 35 | MP3 背景音乐，按数字 ID 索引 |

- 格式 A：`u32 tag, u32 0, u32 文件总长, u32 条目数, u32 未知` + 条目数 ×`{u32 名字长, u32 偏移, u32 大小, u32 标志}`；在偏移处先存放无结尾 0 的路径名，紧接着是数据。
- 格式 B：`char[4] 'snd%'/'mus%', u32 未知, u32 条目数` + 条目数 ×`{u32 ID, u32 偏移, u32 大小, u32 标志}`，数据为原始 WAV/MP3。
- 模型是 RenderWare 3.x 格式（chunk 头中的 library ID 为 `0x1C02002D`，解码后是 RW 3.7.0.2），可以用 RW Analyze、Blender 的 DragonFF 插件等工具查看。
- 打包：`python -I scripts/rm8pak.py pack <原始.dat> <替换目录|-> <输出.dat>`，替换目录中同路径的文件会被替换。对 clump0000 / tex0001 / snd0000 / mus0000 重新打包（不替换任何文件）后，结果与原文件**逐字节一致**。
- 全量内容校验（`scripts/validate_assets.py`）：11013 个条目全部能被正确解释。1180 个模型、4312 个动画、31 个 UV 动画的 RenderWare chunk 树都恰好铺满（库版本 0x1C02002D），插件包括 Skin、HAnim、MatFX、BinMesh、UserData；另有 2952 个 TGA、68 个 BMP、2400 个 WAV、35 个 MP3、35 个动画光标，以及 1 个 DDS。只有两个开发者遗留的调试日志 memblock.txt 含乱码，无关紧要。

### 2.4 存档 Save/record_XX.sav（已验证，见 save_format.md）
- 写档 0x4A7540 / 读档 0x4A78F0。结构：`int 9` + `"r8recrd\0"` + 0x28 字节头（保存时间、模式、玩家数、4 个角色 ID）+ 12 个全局 int（日期、已进行天数、物价基数 100000、物价指数）+ 各子系统依次序列化（股票 22 支 × 892 字节、角色、地块、街道 137 条、NPC、点券中心、电力公司、乐透、奇迹表）。
- `python -I scripts/savefile.py <sav>` 输出 JSON。5 个样本全部解析到末尾，并通过 29~37 项自检（日期合法、各股剩余股数 + 玩家持股 = 10000 等）。约 93% 字节有代码依据。
- 注意：地块数取决于地图（读档时由 Map\NN.map 决定），所以解析器需要读取游戏目录中的地图文件。
- 其他存档文件：`save\record_m%02d.sav`（闯关模式）、`save\back\record_bck%08d.sav`（自动备份）、`record_temp.sav`。

### 2.5 gamedate.bin（已验证，可读写）
- 闯关 / 解锁进度。每个布尔值都存成 exe 里一张表中的特定魔数，不等于魔数即为假。工具：`python -I scripts/gamedate.py dump <file>`，`encode/decode` 往返与原文件逐字节一致。
- 内容：6 个隐藏角色（11~16 号）的解锁标志；16 个角色各自的闯关阶段（0~4）及 9 关通关标志；2 个额外标志和 13 个原样保存的 int。
- 当前目录中的 gamedate.bin（以及“完美存档备份”和 `乱码修正.exe`）= 全部角色解锁 + 全部关卡通关；`完美存档备份\原版` 中的是原始进度（只有萊娜打到阶段 1）。

### 2.6 其他
- `Config/init.txt`：明文 INI，`[param]` 节含分辨率、`character`、`speed`、`difficuty`（难度，推测对应脚本里的 `Role.GameDffty()`）、`sound/music`、`binkplay`。另有 `softstar=kdiEopWn`。
- `config\rich8.bin`：exe 会以 `r+b` 方式打开，**游戏目录中不存在此文件**。打开失败时会弹出“请将config目录及其下所有文件属性设为'存档'”。该函数读写文件末尾的一个计数字节，并写入一个由当天日期算出的值，推测是试用版的启动次数/日期检查残留。目前游戏能正常运行，说明这条路径未被走到或失败也不影响运行（待验证）。
- 小游戏：18 快手小厨师、19 记忆挖宝、20 打地鼠、21 插海盗、22 分裂球、23 踩蛋。均已逐一与游戏内官方说明（string.bin 6951~6956）核对，玩法和计分见 `docs/minigames/README.md`。

## 3. 关键结论速查（已验证）
- **租金** = RentTable[等级]（500/1000/2500/5000/8000/12500）× 街道系数% × 物价指数，同一地主的同街地产会累加。RentTable 位于 exe 文件偏移 0x1E65B0。
- **卡片 condition**：最高字节 = 使用前提（0x02 脚下是住宅地、0x04 脚下是自己的地、0x08 脚下是别人的地），最低字节 = 目标类型，中间字节 = 选择范围/归属。
- **AI 回调**：Access = 银行存（负数）/取（正数）；Borrow = 无贷款时借款金额；Count = 法院控告（0/1/2 三种罪名，3 不告）；DiceNum = 骰子数；Answer = 问答点。
- **难度**：init.txt 的 `difficuty` 就是脚本中的 `Role.GameDffty()`；≥3 时乌龟卡、路障卡、停留卡的 AI 会主动针对对手。
- **两个原版 bug**：CardPy_31 换位卡 AI 中的 `s == 1 and s == 5` 恒为假；CardPy_35 封印卡给出的卡 Id 为 `34+NPC编号`，比“封印谁得谁的卡”少 1。

## 3.1 玩法数据速查（已验证）
- 角色 16 人、地图 11 张（stage.bin：1 香港 2 台北 3 巴黎 4 东京 5 阿拉伯 6 上海 7 〔string.bin#3823〕 8 好莱坞 9 夏威夷 10 〔string.bin#3826〕 11 〔string.bin#3827〕；另有 download201~204 四张下载地图）
- 卡片 49 张、NPC 22 种（npcinfo.bin）、建筑 7 类（住宅/研究所/连锁超市/饭店/三种奇迹）、地点事件 18 种（event.bin）
- AI：16 个 AI 脚本里，14 个代码完全相同，只在 3 个概率参数上有差别；AI_05 与 AI_14 不买股票。

## 4. 逆向完整度（截至 2026-11-14）

| 对象 | 状态 | 验证方式 |
|---|---|---|
| Config 加密配置（113 个） | 完全还原，可编辑写回 | 逐字节往返 |
| Python 脚本（105 个游戏脚本 + 16 个标准库） | 完全还原，可修改重编译 | 字节码逐字段比对 + 运行时冒烟测试；标准库与官方源码比对 |
| 资源包（11013 个条目） | 完全解包，可重新打包 | 重打包逐字节一致；全部条目格式校验通过 |
| 存档 / 地图 / 粒子表 / role.bin / gamedate.bin | 格式完全解析 | 全部样本解析剩余 0 字节，存档另有 29~37 项语义自检 |
| exe 游戏逻辑（4462 个函数） | 100% 命名并写入中文说明 | 抽样盲审：正确 46%，方向对但过度推断 48%，错误 6.6%；低可信批次已用 sonnet 重做；关键结论用反汇编复核 24/24 |
| exe 自研中间层与 UI 库（569 个函数） | 99~100% 命名（多为低置信度） | 同上 |
| 第三方库（RenderWare / D3DX9 / libpng / zlib / VC 运行库） | 只命名被游戏调用的 API 入口；D3D9 调用按 ABI 确定性标注 | 内部实现不属于游戏逻辑，不逐一分析 |
| 核心玩法算法（物价、租金、股价、AI 回调、卡片条件） | 完全还原 | 主代理逐行阅读代码，并与存档数据核对 |
| 6 个小游戏 DLL（925 个独有函数 + 共享框架） | 100% 命名，玩法与计分文档化 | sonnet 审核重写，与官方说明文字核对 |

**仍未覆盖或未确认的内容**
- 所有结论都来自静态分析，**尚未在游戏中动态验证**（例如用调试器下断点）。
- 游戏逻辑中置信度为 L 的函数约占 12%（523 个；名称带 `u_`/`Unk_` 前缀，或保留 `Cls_xxxx__vfN` 一类的占位名），这些名称仅作线索。
- 存档中约 6~9% 的字节语义未知（布局已知）；地图装饰物有少量字段含义未知。
- RenderWare、D3DX 等第三方库的内部实现未逐一命名（不影响游戏逻辑分析）。

## 5. 修改路线（推荐由易到难）
1. **数值调整**（不动 exe）：解密 → 编辑 UTF-8 → 转回 Big5 → 加密覆盖。例如 card.bin 的价格、stock.bin 的股价和波动、pass_XX 的起始资金与对手、string.bin 的文本。
2. **行为调整**（不动 exe）：修改 `out/pysrc_clean/*.py` → 用 `py24c.py` 编译 → 覆盖 `Pysrc/*.pyc`。例如让 AI 更强/更弱、改卡片效果、修复原版 bug（CardPy_31 中的 `s == 1 and s == 5` 恒为假；封印卡发卡编号差 1）。脚本中可以调用 `python_api.md` 列出的全部接口，其中 6 个接口原版从未使用（如 `Role.Hospital`、`Role.Enemy`、`StPoint.Wonder`）。
3. **资源替换**：用 rm8pak 解包，替换 tga/wav/mp3 后重新打包（需要先编写打包器）。
4. **exe 层**：参见 `exe_internals.md`；RichMan8Fix 已在 0x4010b6、0x4011d3、0x405dd6、0x409d59、0x417950、0x418dfa、0x46e940、0x4eca70、0x5a8f00~0x5a9510 等处打了补丁（分辨率、字体），继续改 exe 时要避开或配合这些位置。

**所有修改前请先备份对应原文件。**

## 6. 工具清单（scripts/）
| 脚本 | 用途 |
|---|---|
| selfcheck_all.py | **一键全量自检**（10 项，见第 4 节） |
| savefile.py / mapfmt.py / prtfmt.py / rolebin.py / gamedate.py | 存档、地图、粒子表、role.bin、闯关进度的解析器 |
| validate_assets.py | 资源包全部条目的格式校验 |
| build_facts.py / auto_names.py / make_cards.py / merge_names.py / apply_review.py / finalize_exports.py | 函数命名流水线：提取事实 → 规则命名 → 生成卡片 → 合并 → 盲审校准 → 导出 |
| d3d9_annotate.py / simplify_c.py | D3D9 调用确定性标注；折叠越界检查样板代码，便于阅读 |
| lg_names_build.py / make_lg_cards.py | 小游戏 DLL 的命名与框架名同步 |
| rm8crypt.py | Config 加解密 |
| to_utf8.py | 批量把解密结果转成 UTF-8 文本 |
| rm8pak.py | 资源包列表/解包 |
| py24c.py | 用游戏自带 Python24.dll 编译 .py → .pyc（需 32 位 Python） |
| verify_pysrc.py | 严格往返验证 105 个游戏脚本（取代旧的 make_clean_src.py） |
| fix_decompile.py | 修正 uncompyle6 的结构错误（需在 verify_pysrc.py 重新生成源码后运行） |
| verify_stdlib.py | 把 16 个标准库 pyc 与官方 2.4.x 源码比对 |
| disdiff.py | 逐条指令对比原版与重编译的某个函数 |
| crosscheck_exe.py | 用 capstone 反汇编独立复核 Ghidra 得出的 24 条关键结论 |
| pymethods.py | 从 exe 中提取 Python C 扩展的方法表 |
| pe_info.py / strings.py / hx.py / xref.py | PE 信息、字符串、十六进制、立即数交叉引用与反汇编 |
| gen_cards_md.py | 生成 docs/cards.md |
| rm8mod.py | mod 工作流：init / status / build / selftest / deploy / restore |
| smoke24.py | 在游戏的 Python 2.4 中用模拟引擎执行全部脚本函数，捕获运行时错误 |
| fn.py | 从 Ghidra 导出的 decompiled.c 中按地址/名字提取函数，或用 `--callers` 查调用者 |
| keyfind.py | 猜测循环加法密钥 |
| ../ghidra_scripts/RM8Export.java | Ghidra 无头分析：命名 Python 接口函数，导出全量反编译 C 和字符串交叉引用 |
