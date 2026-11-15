# 托管（auto play）

范围：原版"托管"（计算机接管一个人类座位）的反编译事实、移植实现与验证。标记：**[V]** = 反编译已核对（地址）；**[A]** = 移植的适配 / 假设。
代码：`port/app/roles.cpp`（状态与逻辑）、`hud.cpp`（supply 对话框、座位头像触屏钩子、测试钩子）、`menu.cpp`（gsys 按钮 1）、`rm8_app.cpp`（F1..F4 键）、`game.cpp`（破产结算计数）、`save_roles.inc`（存档）。

## 1. 原版怎么做（[V]）

### 1.1 状态：角色 +0x1B0 有三个值

| +0x1B0 | 含义 | 写入处 |
|---|---|---|
| 0 | 人类 | `Role_InitState @0x480d30`（开局，setup 里 `+8 == 0`） |
| 1 | 电脑 | 同上 |
| 2 | **人类座位被电脑接管（托管）** | `Role_EnableAutoPlay @0x48A4D0` |

* `Role_EnableAutoPlay`：`+0x1B4 != 7`（没破产）且 `+0x1B0 == 0` 才生效：`+0x1B0 = 2`；若回合状态 `+0x1B8 == 6`（等玩家掷骰）立刻 `Role_StayOrBeforeRoll(1)`（走电脑路径：先股票 `AI_StockTrading @0x4F9310`，再卡片计划 `AI_PlanCardActions @0x4F9670`，最后 `Role_BeforeRollDice` 的电脑分支）。返回 1 / 0。
* `Role_DisableAutoPlay @0x48A520`：`+0x1B4 != 7`、`+0x1B0 == 2` 且 `+0x1B8 != 9`（不在事件任务里）才生效：`+0x1B0 = 0`。下一次 `Role_BeforeRollDice` 又会进状态 6 等玩家。
* 所有 AI 分支判断的是 `+0x1B0 != 0`（电脑与托管都进电脑代码）；人类界面判断 `== 0`。只有两处区分 `== 1`：`Role_CheckBankruptcy @0x48EE10` 数"其他人类 / 托管座位"（`!= 1`）与"其他电脑"（`== 1`）来决定 0x74 全败 / 0x75 唯一赢家 / 0x72 结算；`Game_TurnNextRole @0x4971A0` 只有存在 `!= 1` 的座位且总人数 > 1 时才进入新的一天。
* `+0x1B0` 在存档里**原样**存取（`CPlayerTag__SaveToStream @0x491FB0` 写、`LoadFromStream @0x4923B0` 读 this[0x6c]），所以托管状态随存档保存，读档后仍在托管。setup 段（座位表 `+8`）不变（仍为人类）。

### 1.2 开启：两条路

1. **界面（gsys → supply）**：底栏 gmaindlg 按钮 10（只在状态 6 显示，`CGamePlayScene__vf12 @0x4A2E10`）→ `Game_DispatchCardAction(0x68) @0x4A4420`（状态 9、清队列、入队 100）→ 系统菜单 gsys（`Game_UseCard @0x4A4160` 创建，按钮 1 托管 / 2 存档 / 3 读档 / 4 设置 / 5 关闭 / 6 退出）→ 按钮 1 → `NPC_SendActOnPlayer2 @0x4AA810` → `CUi_supply__ShowUI @0x4928E0`：在队首插入 `1(0, 0x87)`（`Role_OpenModalUi` 的 supply.ui 是/否框，问题文字是 .ui 自带图片 j231.tga）、`0x1C`（关框）、`0x79(0x80000000)`（等结果），并关掉 gsys。确认 → 调度器 case `0x79`：结果非 0 时 `Role_EnableAutoPlay`（此时角色状态为 8，只置标志），后面残留的 `100` = `Role_BeforeRollDice(0)` 接手，走电脑分支（本回合不做股票交易，因为没经过 `0x25(1)`）。取消（结果 0）→ `100` → 又回到状态 6。
2. **键盘 F1..F4**：`CGamePlayScene__OnEvent @0x4A1E80` 的键码 0x81..0x84（DirectInput 表 `0x5DF5F0`：DIK 0x3B..0x3E = F1..F4）= 座位 0..3，仅当 `DAT_00642780 == 0`（拍卖 / 奇迹界面没开）：先 `Game_GetPlayerByIndex @0x4A6CF0`（座位存在 → `Role_DisableAutoPlay`，成功即返回 = **关闭**）；否则 `Game_GetPlayerAtSeat @0x4A6C30`（当前角色的座位 == 该座位且状态 6 → `Role_EnableAutoPlay` = **开启**，同时直接 `Role_StayOrBeforeRoll(1)`）。所以：开启只能在"当前角色等掷骰"时、按自己座位的 F 键；关闭任何时候都行（状态 9 除外），对任何座位。
3. 秘籍词 `sup`（`CheatCode_Process @0x4A0530`）也调 `Role_EnableAutoPlay`，调试用，[A] 移植未做（移植没有秘籍词）。

### 1.3 关闭：另有一条隐含路径

`Game_SelectNextPlayer @0x4A6D60` 在场景处理每个按键（vf5）时最先运行：当前角色在走路（状态 1）时，从下一个座位起找第一个 `+0x1B0 == 2` 的座位（当前角色自己除外）调 `Role_DisableAutoPlay`，成功则**吞掉这个按键**。即"别人的托管座位走路时随便按一个键，就把下一个托管座位还给人类"。

没有找到任何 HUD 上的"托管中"标记，也没有第二个 UI 按钮用来取消（取消只有 F 键和上面这条）。

## 2. 移植（`roles.cpp`）

* `Role::autoPlay`（+0x1B0 == 2）。托管时 **`r.human = false` 且 `r.autoPlay = true`**：所有已有的电脑分支（`rm8TurnHuman()` 为假的地方：骰子、买地 / 加盖、卡片 AI、股票、商店 / 事件 / 拍卖 / 小游戏 `aiMiniGame`、Python `AI_xx` 的 Terra.Build / Role.Grab 等）自动走，没有为托管另写一份逻辑。需要原版 `!= 1` 语义的地方用 `rm8TurnIsPlayer()`（人类或托管）：`game.cpp` 的破产结算计数。
* `autoEnable / autoDisable`（= `Role_EnableAutoPlay / DisableAutoPlay`）；`stayOrBeforeRoll`（= `Role_StayOrBeforeRoll`，从消息 `0x25` 的处理里提出来，供两处用）。
* `rm8AutoKey(seat)`（F 键）、`rm8AutoRelease()`（`Game_SelectNextPlayer`）、`rm8AutoSupplyOpen()`（gsys 按钮 1）、消息 `0x79`。
* [A] 状态 6 里托管，若 `AI_StockTrading` 做了交易，会入队文字和 `0x25(2)` 但状态仍是 6（原版不会调度，疑似原版遗留问题）；移植把状态改成 8 让队列继续。
* UI：`hud.cpp` 加载 `supply.ui`，`rm8HudOpenModal` 的 kind `0x87` 显示它，OK = 结果 1，取消 = 0（和 okcancel 一样回写 `0x80000000` 消息）。
* 键盘：`rm8_app.cpp` 的 `autoPlayKey`：先 `rm8AutoRelease()`，再 F1..F4 → `rm8AutoKey`。[A] 移植里有些对话框（股票、信息屏、卡片选目标、存读档、系统菜单）在状态 6 里开着（原版是场景任务，先吃键），这些开着时不允许开启托管；任何事件任务在跑时 F 键无效（原版是拍卖 / 奇迹界面开着）。
* 存档：`save_roles.inc` 写 `ai = 2`，读 `ai == 2` → `autoPlay`；setup 段座位表仍写"人类"。原版存档的 `+0x1B0 == 2` 读进来保持托管。

### 触屏（原版只有键盘）

* 开启：已有——HUD 按钮 10（gsys）→ 按钮 1 → supply，全是触屏可点的原版控件，不需要新增。
* 关闭：原版只有 F1..F4。**[A] 触屏钩子**（`hud.cpp` `rm8HudSeatClick`）：点一个被托管座位的头像六边形（gmaindlg 按钮 1..4）= 按该座位的 F 键（把它还给人类）；点其他座位头像仍是原来的"镜头看他 + 说话"。建议的映射，没有改动原版 UI。

## 3. 验证

测试钩子（`hud.cpp` `rm8HudAutoPlayTest`，`rm8_app.cpp` 主循环里调用；只在设置环境变量时生效）：

| 变量 | 作用 |
|---|---|
| `RM8_AUTO_PLAY=<秒>` | 游戏 `<秒>` 秒后，第一次人类等掷骰（状态 6）时，用**指针路径**依次点 HUD 按钮 10、gsys 按钮 1、supply 的确定（命中测试、按下、抬起） |
| `RM8_AUTO_PLAY_KEY=1` | 开启改成推一个 F1..F4 的 SDL 键事件（`Game_GetPlayerAtSeat` 路径） |
| `RM8_AUTO_PLAY_OFF=<秒>` | 开启后 `<秒>` 秒推该座位的 F 键事件（关闭，走事件循环） |
| `RM8_AUTO_PLAY_ROLL=1` | 关闭后人类再次等掷骰时点真实的掷骰按钮 |
| `RM8_AUTO_SEAT0=1` | 座位 0 开局即托管（给 `save_roundtrip.py` 用） |
| `RM8_FAST=1` | 开局推一个 `.` 键（加速，原版 0x2E） |

命令：`build-sdl2/rm8_app.exe "<steam-edition-dir>" scenes --map 01 --human 0 --frames N`（`RM8_AUTO_PLAY=3` 等放在环境里）。日志关键行 `autoplay test: ...`、`autoplay: seat 0 is played by the computer (...)`、`autoplay: seat 0 is played by the human again (key)`。

## 4. 验证记录（桌面，2026-11-15）

* **开启，真实指针路径**（`RM8_AUTO_PLAY=3`，地图 01 / 03，`--human 0`）：座位 0 第一次等掷骰时依次点 HUD 按钮 10 → gsys 按钮 1 → supply 确定；日志 `supply OK` → 角色状态 7（模态框）→ 8 → `autoplay: seat 0 is played by the computer (supply dialog confirmed)` → 状态 3（骰子）→ 1（走路）→ `rules: seat 0 buys plot ...`。F 键路径（`RM8_AUTO_PLAY_KEY=1`）同样成功，且状态 6 里开启时电脑先做股票交易（`stock: seat 0 buys ...`）再继续，队列没有卡住（上面的 [A] 修正）。
* **托管期间的几天**（地图 01 / 03 各 10 天、地图 07 另跑，座位 0 全程托管）：骰子（含卡片前置 `cards: AI seat 0 picks card 9 ... executed`、路障卡、均富卡、封印卡）、买地 / 加盖（`seat 0 buys plot`）、股票买入、事件格（答题 `computer answers right / wrong`、银行 `bank access`、奇迹投资 `wonder 1: seat 0 invests`、法院 `court: computer seat 0`、电力公司 `power company: computer seat 0 buys`、抽卡 `receives card`）、周新闻、收租。**小游戏**：`RM8_EVENT_REMAP=99:19` 把落在银行通过格的座位 0 改成落在小游戏事件 19，日志 `events: [58.78 s] seat 0 (computer) lands on event 19` → `events: minigame 19 seat 0: tickets +93`（电脑路径 `aiMiniGame`，没有弹出小游戏）。
* **关闭**：`RM8_AUTO_PLAY_OFF=8`，8 秒后通过 SDL 事件循环推 F1：`autoplay: seat 0 is played by the human again (key)`；座位 0 之后在下一次自己的回合进入状态 6 并一直等（`state 6 human 1 auto 0` 持续到运行结束），`RM8_AUTO_PLAY_ROLL=1` 再用真实的掷骰按钮投一次，状态 6 → 3 → 1 → 事件，回合照常结束。（一次是在自己回合走路途中被还回，也正常收尾。）
* **读档保持托管**：`--load` 一份座位 0 托管时写的存档（record 里 `+0x1B0 = 2`），不设任何钩子，座位 0 自己掷骰、买地（`autoplay test` 状态行 `human 0 auto 1`）。
* **save_roundtrip**（`scripts/save_roundtrip.py`，副本 `work_autoplay/rt_auto.py` 只让第 0 局的座位 0 用 `RM8_AUTO_SEAT0=1` 开局托管，A / B 不设）：对照（无托管）18 份记录全部相同；托管局 18 份里 16 份逐字节相同（含每天 / 每回合 / 每次掷骰前，座位 0 的 `ai = 2` 一路保持），另 2 份（`day0000.bin`、`t0001_s0.bin`）只差座位 0 的 `+0x1B0` 一个字节：那是 B 载入时棋盘建好、`rm8SaveApply` 之前写的测试转储（A 里被同名的应用后记录盖掉），不是真实存档，也不影响之后的任何记录。
* 安卓：`build-android` 编译通过（未部署、未上机）。

### 已知问题（与托管无关）

* `RM8_FAST=1`（'.' 加速）下，电脑座位遇到 福神（NPC id 4）会卡在状态 10，纯电脑局（不用托管）也复现；不加速一切正常。需要 npc / skip 模块的人看（本次长测因此没有用加速跑过 10 天以上）。
* 原版取消托管只有 F 键（以及走路时任意键放回），没有 HUD 标记；移植没有加标记。
