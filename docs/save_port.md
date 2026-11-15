# 存档 / 读档与游戏结束（移植版）

范围：`port/app/save.h` `save.cpp` `save_ui.cpp` 与各模块末尾的 `save_*.inc`。标记：**[V]** = 反编译已核对（地址）；**[H]** = 假设；**[A]** = 移植的适配。
解析器与原版格式见 `docs/save_format.md`（`scripts/savefile.py`）。本文件只写移植的做法、对应关系、验证和遗留。

## 1. 设计

* **文件 = 原版 record 原样（版本 9）+ 一个移植扩展块。** 头 0x64、setup、sflb（仅故事）、stocks、roles、terra、streets、npcs、点券中心、电力公司、乐透、奇迹表，逐字节按 `Game_WriteRecordToFile @0x4a7540` 的顺序。原版的读档 `Save_LoadRecord @0x4a78f0` 读完奇迹表就关文件，**不检查文件尾** [V]，所以尾部追加的扩展块被原版忽略：移植存档可被原版读（未实机验证），原版存档可被移植读。
* 扩展块 `"RM8X"` + 版本 + 总长，后面是 `tag(4) + 长度 + 数据` 的分块；每个模块自己注册自己的块（`RM8_SAVE_EXT`），读块时“块比模块知道的短就保持默认、比知道的长就忽略多余部分”，所以模块加字段只需写在块的**末尾**。块里放原版记录没有位置的状态：各个随机数流、闲置计时、位置补间、动画进度、奇迹份额等（第 5 节）。
* 保存位置 `<数据目录>/save/`（桌面：`rm8_settings.txt` 旁，`RM8_DATA_DIR` 覆盖；安卓：`.../files/save/`），**从不写游戏目录**。文件名 `record_NN.sav`（故事）/ `record_mNN.sav`（自由模式）/ `record_temp.sav`（自动存档）[V `CPlayerTag__SaveGameRecord @0x492850`；`story_mode.md` 已更正：`record_NN` 才是故事档]。
* 每个模块用两种小函数提供状态，声明都在 `save.h`：① 原版段（`rm8Save<Module>Write/Read`，字节布局由原版决定）；② 角色记录（`rm8Save<Module>RoleGet/Set`，每个模块填写/应用 `Rm8SaveRole` 里归它的字段）；③ 扩展块。模块文件只在**末尾加一行** `#include "save_xxx.inc"`，代码写在 `.inc` 里（能访问模块的私有状态，又不动模块本身）。
* 读档 = 先按档里的 setup 重建棋盘（地图、座位、选项、故事标志；和“新游戏”同一条路），再把各模块的状态覆盖上去，最后恢复回合流程。读档发生在主循环里（菜单 / gsys 的点击只排队 `rm8SaveRequestLoad`，不在 UI 回调里重建场景）。

## 2. 何时存档（静止点）

原版 gsys 只在人类等待掷骰（状态 6）时可打开；自动存档在每天交换回合时 [V `Game_TurnNextRole @0x4971a0` → `Game_SwitchRecordSlot @0x4a7380`]：先把上一次的 `record_temp` 拷到 slot 1，再写新的 `record_temp`（首次调用天数为 0，两个一起写）；`Game_Cleanup @0x4a1340 / Game_Shutdown @0x4a15d0` 删除 `record_temp`。移植照做：

| 点 | 钩子 | 写什么 |
|---|---|---|
| 一天结束（回合越过最后一座，新日之前，所有角色闲置，`RoleMgr+0x2C` = 座位数） | `rm8SaveBoundary(0)`（roles.cpp `turnNextRole`） | 自动存档：旧 temp → slot 1，新 temp；待写的手动存档；`RM8_SAVE_DUMP` |
| 人类等待掷骰（状态 6） | `rm8SaveBoundary(1)`（`beforeRollDice`） | 待写的手动存档 |
| 回合开始 / 掷骰前 | `rm8SaveBoundary(2/3)` | 只给测试用 `RM8_SAVE_DUMPT` |

* **手动存档**：gsys 存档按钮 → 槽位对话框 → `rm8SaveRequest(slot)`。人类正等掷骰则立刻写；否则**排队到下一个静止点**（人类下次等掷骰 / 天数变化）[A，原版只在状态 6 才给按钮]。读档到手动存档时当前角色保持原状态（6 = 继续等掷骰）；读到自动存档（当前序号 = 座位数）则 `Game_TurnNextRole`，从新的一天开始 [V `Game_LoadSavedGame @0x496a90` 的 `idx = n - 1; Game_TurnNextRole()`]。
* [A] 游戏开始那次（第 0 天）的自动存档不写：移植先建盘、再应用故事的起始值，那份档会缺起始物品；第一份在第 1 天结束时写。slot 1 因此永远是“上一份 temp”，和原版一样落后一天。
* `Game_BackupSaveFile @0x4a7220`（`save\back\record_bck%08d.sav`）反编译里**没有调用者**，没有移植。

## 3. 原版段 ↔ 移植状态

| 段（`savefile.py`） | 移植写读（文件） | 说明 |
|---|---|---|
| header 0x34..0x63：版本 9、魔数、SYSTEMTIME、地图号、玩家数、4 个角色号 | save.cpp | 地图号：`scenes/stages.txt` 的 id，下载图 = 0x15 + 序号 [V `@0x4a9020`] |
| header 12 个 int（`g_GameState +4..+0x30`） | `game.cpp` `save_game.inc` | 年 月 日 星期 天数 \| 开始年 / 结束月（限时：开始月 +3 / +6）/ 日 / 星期 / 0 \| 物价基数 \| 物价指数。开始日期由当前日期减去已过天数得出，读入的档用档里的值 |
| setup（标志、地图、`+8` 关卡层、现金、点券、交通工具、时限、胜利条件、座位×3 int） | save.cpp | 座位：角色号、服装（`Role+0x1AC`）、0 人类 / 1 电脑 |
| sflb 0x54（仅故事） | save.cpp ↔ `rm8Box()`（collect.h） | 层、现金 {箱, 待带入}、点券 {箱, 待带入}、卡 8 + 8；读档恢复储藏箱并写回 `rm8_box.bin`（原版：只有读故事档才恢复箱子 [V]） |
| stocks | `stock.cpp` `save_stock.inc` | 上市 n + n×0x37C，备选 n + n×0x37C；移植的 `Stock` 与原版对象逐字段对应，名字转 Big5（`save_big5.inc`，由 `scripts/export_save_big5.py` 从 stocks.txt 生成）；读时按 Big5 还原名字、按名字找 `total` |
| roles：当前序号 + 每角色记录 | `save.cpp` 汇总，字段见下 | 记录 0x1EC + 变长项，角色 16 多 4 字节（版本 ≥ 8） |
| terra：T×16（+研究所 8）+ W×12 | `game.cpp` + `save_wonder.inc` + `save_build.inc` | **T 个地块的顺序 = 原版 Terra 数组顺序**：按各地块**最小道路格号**排序（`save_terra_order.inc`，`scripts/export_save_terra_order.py`；port 的地块表按锚点格序，两者在 map 1 不同）[H：map 1 / 3 / 10 的 5 份样本全部吻合，map 1 是唯一能区分两种排序的样本]。W 来自地图（01:3 02:3 03:1 04:2 05:1 06:3 07:2 08:2 09:1 10:1 11:1，下载图 0） |
| streets 136×16 | `game.cpp` | **记录号 ≠ street ID**：容器按 street.bin 的节顺序（第 0 节是空 ID 0），street.bin 没有 ID 48，所以第 47 条是街道 49 [V 样本：record_03 第 47 条价 60 = 街道 49 的价]。内容：封街天数、价格倍数 float、价格、倍数天数 |
| npcs | `npc.cpp` `save_npc.inc` | NPC 号、（拼图 0x11：`+0x94`）、`+0x68` 状态、道路点下标；读档销毁新局生成的对象再按档重建 |
| 点券中心 / 电力公司（4 个 float + 董事座位） | `event_power.cpp` | 点券中心是 `DAT_006403c8`，电力公司 `DAT_006403cc`（函数地址落在各自的 `TicketCenter_*` / `PowerCo_*` 区段内 [H]，档里点券中心在前） |
| 乐透 | `event_lottery.cpp` | 奖池 + 4 座各自的号码表；空号 = 1..32 去掉已持有 |
| 奇迹表 0x6c（3×0x24） | `save_wonder.inc` | 序号、激活、所有者、4 份额 float、成本 50000、比例 0.1f |

**角色记录**（`Rm8SaveRole`，按原版偏移）

| 字段 | 模块 | 备注 |
|---|---|---|
| `+0x40/+0x44` 两个道路点 | roles | 下标 = `Rm8Scene::rnodes` 序 = 原版 TerraMgr+0 序 [V 5 份样本 22 个角色全部落在相邻节点] |
| `+0x1B0` AI、`+0x1B4` 状态（8 = 登场介绍未播）、`+0x1B8` 回合状态、`+0x5D0` 消息队列、`+0x22C` 交通工具、`+0x230` 骰子数 | roles | 移植的 `r.intro` ≙ 状态 8 |
| 点券 `+0x324`、现金 `+0x290`、存款 `+0x294`、附身 `+0x1BC`、隐形回合 `+0x1E4`、收租 / 付租 `+0x338/+0x33C` | game | |
| 贷款 `+0x298` 7 个 int：本金、年、月、日、星期（1..7，7 = 周日）、当天的天数、`(已付息次数 << 16) \| 3` | event_bank.cpp | 星期 / 天数由日期推出（样本 record_03 / 08 吻合） |
| 手牌 8 格 `+0x2B4`、乌龟 `+0x1CC`、停留 `+0x1D0`、状态天数 `+0x1D8`（睡 / 住院 / 坐牢 / 旅馆）、泡泡 `+0x28C`、莎拉的保镖 | cards | |
| 持股 `+0x328`：上市数组下标、股数、最近买价、成本 | stock | |
| 附身天数 `+0x1DC`、路霸受益人 `+0x1C0` / 天数 `+0x1E0`、定时炸弹 `+0x288` | npc | 读档对附身角色调 `rm8NpcBecome` 重建神明形象 |
| 故事旗标 `+0x374` 块、已用卡 `+0x4C0`×64、`+0x5C0`、`+0x5CC` | story | |

## 4. 读档流程（`rm8SaveParse` → 重建棋盘 → `rm8SaveApply`）

1. `rm8SaveParse`：读文件、核对版本 > 6 与魔数；按地图算出 T / W，走一遍结构（stocks 个数、队列长度、持股项、研究所额外 8 字节、拼图 NPC 的额外 int、乐透表长度……）得到各段偏移——文件被截断或不一致时在改动任何状态**之前**就失败。
2. app（`rm8_app.cpp` `loadSaved`）：用档里的 setup 设座位 / 服装 / 人类 / 选项，`rm8SaveStoryLoadSetup` + `rm8StoryBegin`，`loadMap`（新局：生成场景、角色、NPC、股票、HUD），放好相机。
3. `rm8SaveApply`：头 → 储藏箱 → 股票 → 每个角色（roles、game、cards、stock、story、events、npc）→ 地块（`rm8GameSetTerra` 更新模型和地块颜色）→ 街道 → NPC → 点券中心 / 电力公司 → 乐透 → 奇迹表 → 扩展块（按 order）→ `rm8SaveRolesFinish`：给在场角色做同格摆放（原版 `Game_MoveRoleToPosition(…, 1)`；本档有 ROLE 块时直接用档里的位置）、校正状态（只有当前角色可以在 6 / 8）、续回合。
4. 读到**一天结束档**时，存档是在角色循环的**中间**写的（由最后一个角色的 0x66 触发），ROLE 块记下当时的循环位置与帧时间，读档时补完那一帧的尾巴（该角色的动画、之后的角色、NPC / 卡 / 新闻更新、事件任务更新），否则闲置计时（抽随机数）会差一帧，几十天后分叉。
5. 原版存档没有扩展块：位置补间、闲置计时、各 RNG 都取新局的默认值。

## 5. 扩展块一览（`RM8_SAVE_EXT`，括号内为读档顺序）

| 块 | 模块 | 内容 |
|---|---|---|
| PYRN (5) | python.cpp | Python `random` 的状态（AI / 卡片脚本用；`random.getstate()`，Windows 的状态字是有符号 32 位）。`RM8_SEED=n` 每局开始给它播种（测试） |
| GAME (10) | game.cpp | 规则 RNG；排队消息文本池（队列里的文本号指向它，保存最近 256 条） |
| STCK (15) | stock.cpp | 股票 RNG、电脑冷却、封盘天数、市场事件 % / 天数 / 涨跌率 |
| ROLE (20) | roles.cpp | 角色 RNG、闲置计时（`idleT` / `idleGap`）、朝向、精确位置 + 同格补间 + 已登记节点、当前动画 / 一次 / 闲置动作 / 动画时间、旋转状态、等待计时、模型组（步行 / 机车 / 汽车，附身时不随交通工具变）、循环位置与帧时间 |
| PRES (22) | present.cpp | 表情计时 `exprT`（表情期间闲置计时冻结） |
| NPCS (25) | npc.cpp | 每个 NPC 的创建者座位 |
| DICE (30) | dice.cpp | 骰子 RNG、滚动 / 停留累加器 |
| CMPY (40) | event_power.cpp | 两家公司的消费等级 |
| WNDR (45) | save_wonder.inc | 3 个奇迹的 8 座份额等全部状态 |
| NEWS (50) | news.cpp | 周新闻最近 5 次选择（`DAT_00604520`） |

## 6. 覆盖与默认值

**移植有、档里也存的**：见第 3、5 节。**不存（瞬态）**：打开的对话框 / 事件任务 / 新闻演出 / 小游戏 / 胜利 14 s 场景 / 卡片选目标；卡槽的“已选中”标志与 `tempId`（上一张被移除的卡）；音频、粒子、相机；事件格按压状态；`g_over`。静止点上这些都是空的。

**原版有、移植没有（写默认 0 / 样本值，读原版档时丢掉）**：`+0x340` 起 `blk338[2..7]`（月结算展示用的计数，样本里有 1..5 和 ±100000）、`+0x374` 块 `[5..8]`、`+0x5C0` 的 `[1..2]`、`+0x288/+0x28C` 对象的内部（只存第一个 int）、SFLB 文本；奇迹表里的未初始化垃圾（写入 record_01 的默认）；header 的 `+0x28`；贷款块在无贷款时写 0（原版会写栈垃圾）。对 5 份原版样本做“读 → 立刻按原版格式写回”，与原档**只差**时间戳、未初始化垃圾和上面这些字段（`scripts/save_semdiff.py`）。

## 7. 存档 / 读档界面

* `save_ui.cpp`：`saveload.ui`（10 槽，按钮 1..10；平面 11 = 存档标题，12 = 读档标题）+ `cover.ui`（覆盖确认，确定 2 / 取消 3）；槽文字 string 7101「%s^^%d月%d日%d時%d分」（自由模式 = 地图名，故事 = 主角名），空槽 7102「空」[V `Game_DisplaySaveSlotList @0x4ac420`]。存档模式：空槽直接写，已用槽先问；读档模式：空槽禁用。
* [A] 原版 .ui 的 10 个按钮每个 64 高、却只隔 26，一个点会同时落在相邻三个按钮里；移植把每个按钮收成自己那条 26 高的带（图不变），文字用运行时标签画，另加一个“返回”按钮；Esc / 安卓返回键先关对话框。
* gsys：按钮 2 / 3 接到对话框（`menu.cpp` 一行）；主菜单读档页（`slmenu`）通过 `Rm8MenuHooks::saveSlotInfo / loadGame`（`rm8_app.cpp` 设置）。
* 命令行：`--load <文件>`（任意路径，含原版样本）；`RM8_IMPORT_SAVES=1` 启动时把 `<游戏目录>/save/record_*.sav` 拷进数据目录（只读访问游戏目录）。

## 8. 游戏结束

原版**没有**自由模式的“结算 / 排行榜”画面 [V]：`CGamePlayScene__Game_OnUpdate @0x4a17b0` 的状态 3（0x74 玩家全败、`AskContinueNextRound @0x48f8e0` 自由模式直接置 3）→ `SwitchToGameScene(3)` = 地图菜单；状态 4（0x75 胜利链 `0x48ff70` → 0x7E 胜利演出 14 s（特效 6000 + 曲 103）→ 0x7D 奖励宝箱（自由模式无奖励）→ 0x7A `Game_OnStageSelect`）→ 自由模式也回地图菜单；故事模式的“是否接關” 0x76 / 0x77、通关影片见 `story_mode.md`，`story.cpp` 已做。`finalMenu` 是故事通关影片欣赏，`graduate.ui` 是研究所，`balance.ui` 是月结算（不是结束画面）。

本次补的：**限时结束**。`Game_FindLeaderByWinCondition @0x4a6090`（只在自由模式、日期到结束日时）：按条件（地产 / 资产 / 存款 / 点券 / 股票，严格大于、先到先得）选**未破产**的领先者，**清空领先者的队列**、排 str 7057「遊戲時間到，%s獲勝！」和 `0x75(0, 1)`，并把回合交给他（`Game_TurnNextRole` 里 `idx = 领先者`）。移植原来只显示文字、置 `g_over`、游戏继续；现在 `game.cpp` 做上面三步，`roles.cpp` 的 `turnNextRole` 取 `rm8GameTakeLeader()`，后面是 `story.cpp` 已有的胜利链（演出 → 回地图菜单）。验证：把一份档改成 `timeLimit = 0`、结束日 = 3 天后，`--load` 跑电脑局，日志 `time is up, seat 0 wins` → `stage select -> map menu` → `action 3`。无菜单的演示运行（`rm8StoryOnBoard()` 为假）仍走 game.cpp 旧的 0x74 / 0x75 处理。

## 9. 验证

* **往返相同**：`scripts/save_roundtrip.py`（或 `work_save/rt.sh` 同法）。流程：新局 → 取第 0 天的档 → A：读它并连续玩 T 秒，每天结束 / 每回合开始 / 每次掷骰前的档（时间戳置 0）都落盘；B：读 A 第 K 天的档，同样玩；B 的所有落盘档必须与 A 对应的档逐字节相同。结果见第 10 节。差异的定位：`scripts/save_diff.py`（按段 / 扩展块列出差异字节）、`save_semdiff.py`（按字段）。
* **原版样本**（只读 `<old-edition-dir>\save`，Steam 目录的 save 里只有 steam_autocloud.vdf）：`record_01 / 02 / 03 / 08 / m01` 全部读入，建出场景（建筑、地主颜色、NPC、奇迹、角色），电脑接管后各连续跑 22–47 天不卡。
* **界面**（`RM8_MENU_AUTO`，真实指针路径）：`sys`、`sysbtn:2`（gsys 存档按钮）、`slot:N`、`cover:ok`、`sysbtn:3`、`tap:N`（主菜单读档页），截图 `menu_savedlg / cover / loaddlg / slmenu`。
* **故事**：`RM8_STORY=4` 存 slot 2（setup 标志 1、sflb 段），`--load` 后 `story: board start, hero 4 map 4 layer 0`，强制胜利走通链。
* 安卓：`build-save-android` 编译通过（未打包、未上机）。

## 10. 结果

* **往返**：15 个配置（地图 01–11、download201 / 203，种子与读档日各不相同，每局最长 78 天，其中两个带附身神明、一个有座位开局即破产），每个配置比较 150–410 份记录（每天 / 每回合 / 每次掷骰前），**全部逐字节相同**。调试过程中靠这个测试逐个揪出的“隐藏状态”都已进了扩展块：Python random、闲置计时（含表情期间冻结）、动画进度、同格补间位置、旋转状态、模型组、帧尾（日结束记录写在角色循环中间）、奇迹（事件代理新加）。
* **人类等待掷骰的手动存档**（`RM8_SAVE_AT=5@2` 走真实的请求 → 静止点 → 写盘路径）：读回后立刻再写，与存档那一刻的记录只差 8 字节（ROLE 块里的“循环位置 / 帧时间”）。
* **原版样本**：5 份全部读入；读入后立刻按原版格式写回，与原档只差时间戳、未初始化垃圾、`blk338[2..3]`、`+0x5C4`（第 6 节）；街道段、股票段（含 Big5 名字）、地块段、NPC 段、贷款、乐透、公司段逐字节一致。
* 界面、故事、限时结束、安卓编译：第 7–9 节。

## 11. 给模块作者：加状态

* 随机数流 / 计时 / 瞬态以外的游戏状态：在自己的 `save_xxx.inc`（或已有的）里写一对函数并 `RM8_SAVE_EXT(TAG, order, write, read)`；新字段**只加在块末尾**，读时用 `r.opt(x)`（块短就保持默认）。
* 属于原版记录的字段：在 `Rm8SaveRole` / `Rm8SaveHooks`（奇迹、研究所）里找位置，没有就写扩展块。
* 状态要影响后续**随机数消耗**（计时、动画长度…）时必须一起存，否则读档后几十天会分叉——用 `save_roundtrip.py` 回归，先看 `save_diff.py` 指出的段。
* 读档后需要重建的可见物（模型、HUD）：在 `afterLoad` 或 Set 函数里做（奇迹 `ev::wonderSet` 重建场地模型）。

## 12. 遗留 / 假设

* [H] 地块顺序 = 最小道路格序（3 张图的样本吻合）；奇迹对象顺序按锚点格序（record_m01 一例吻合）；点券中心 / 电力公司两个对象的身份；研究所 2 个 int 的含义（卡下标、剩余天数，样本里没有研究所）。
* 手动存档只在静止点写（人类等掷骰 / 天数变化）。
* 版本 7 的记录没有测过（读档函数按原版 `> 6` 接受，角色 16 的 4 字节仅 ≥ 8）。
* 移植存档能否被原版读还没在原版里试过（格式逐段对照 + `savefile.py` 解析无剩余 + 25 项自检通过，扩展块在 `Save_LoadRecord` 读完之后）。
* 读原版档时 `blk338`、`+0x5C4` 等未知字段丢失；写出的是 0。
* 安卓只编译，未真机。
