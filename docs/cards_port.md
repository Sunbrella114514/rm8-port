# 卡片系统移植说明（cards.cpp、python.cpp 的卡片部分）

> 范围：卡片库存、主界面卡片栏、卡片使用（人类 / 电脑）、CardPy 脚本所需的引擎函数。
> 卡片数据与逐张效果见 `docs/cards.md`（由 `scripts/gen_cards_md.py` 生成，本文第 7 节列出它的几处需要更正的地方）。
> 标记：**[V]** 已在反编译中核实（给出地址），**[H]** 推测，**[A]** 移植版的改动 / 简化。
> 代码：`app/cards.cpp`、`app/cards.h`、`app/python.cpp`（Role / Terra / Street / StPoint / Npc 部分）、`scripts/export_cards.py` → `scenes/cards.txt`。

## 0. 数据与接口

* `scripts/export_cards.py <游戏目录> port/scenes/cards.txt`：每张卡一行 `card id 名称 type level 点券 condition speak0 speak1 speak2 python 贴图 说明`，顺序 = card.bin 顺序 = 卡数组下标（下标 = Id − 1）[V Card_LoadCardBin @0x4adec0]；另有 `npc id date good`（npcinfo.bin，附身天数 = NPC +0x228）。说明文字取 string.bin 6500 + Id [V Card_ShowInfo_8f40 @0x4e8f40 读 StringTable + 26000 + Id×4]。
* `rm8CardLoadDefs` 在 `rm8_app.cpp` 里于 Python 初始化后调用（要早于第一局的 `rm8GameNewGame`）。
* `cards.h` 保持旧接口（`rm8CardAdd / At / Count / RemoveAt / RemoveId / ClearAll / Max / Price / Name`），新增 `Find / Ready / Restore / Type / Level / Desc / Supported ...`。events.cpp 的卡片店 / 卡片点只用旧接口。

## 1. 库存（Deck）

| 事实 | 依据 |
|---|---|
| 每个角色 8 个固定槽（`Role+0x2B4`，步长 0xC：{卡对象, 就绪标志}），卡数 `+0x6C`。加入卡 = **第一个空槽**，取走一张卡后**不压缩**，留空洞 | `Deck_AddCard @0x49a650`、`Deck_UpdateCount @0x49a9a0`、`Deck_GetCardAt @0x49a900` |
| 取走 = 卡对象移到暂存（Deck+0x60，原槽号 +0x68），`Deck_RestoreCard` 把它放回**原槽**（槽仍空才成功） | `Deck_RemoveAndStore @0x49a740`、`Deck_RestoreCard @0x49a7c0` |
| 就绪标志（+4）：选中使用的卡，卡片栏不显示 | `Deck_SetCardReady @0x49a830`、`Deck_ClearCardReady @0x49a880`、`Role_RefreshDeckUi @0x4812c0` |
| 开局每人 3 张：卡数组下标 8、10、11 = 遙控骰子(9)、轉向卡(11)、路障卡(12) | `Game_StartNewRound @0x4962a0`，调用点 0x496849 / 0x496893 / 0x4968dd（push 8 / 0xA / 0xB 后 `call Role_AddCardToDeck`）。闯关模式另给 pass_xx.bin 的 start_card（移植版没有闯关模式） |
| `Role_AddCardToDeck / Role.ReceiveCard / 消息 4` 的参数是**卡数组下标**（Id − 1），不是 Id。所以封印卡的 `34 + NPC ID` 得到的是 Id 35 + ID（封印财神得财神卡），**不是**原文档说的“差 1 的 bug” | `Role_ReceiveCardWithUi @0x488e80` 用 `CardArray_GetByIndex`；`py_Role_ReceiveCard @0x4ff480` 原样入队 |
| 手牌满（`+0x320 >= 8`）：电脑替换点券最低且低于新卡的那张；人类进入换牌（getcard 界面 + 点卡片栏上的卡替换：`Card_LoadAndShowCard @0x4ae790`、`Card_HandleSwap @0x4ae8a0`） | `@0x488e80`。消息 4 在 events.cpp 的 `receiveCard`：电脑自动替换，人类先问 `rm8CardsBeginSwap`（第 12 节）。**已验证**（`RM8_CARD_GIVE="0:2,3,4,5,6"` + `RM8_CARD_PTR="recv:19;swap:3;recv:20;dlg:1"`）：满手牌时 getcard 界面出现，点槽 3 的卡 = 换掉（新卡进空出的槽），点卡窗口 = 放弃新卡 **[A]** |
| 点券在 `+0x324`，上限 9999 | `Role_AddTicket @0x489ed0` |
| 卖牌：卡片店打开时（`DAT_00640958`）点卡 = 得 `ticket / 2` 点券 | `Role_SellCardForTickets @0x488cf0`；移植版 `rm8CardsSetSellMode(bool)` 供卡片店使用（目前没人调用） |

## 2. 卡片栏（gmaindlg）

* 控件：图片按钮 11 + k（`card\<texName>` = 包内 `prt\card\dNN.tga`，128×128 画在 100×100 的控件里）、名称标签 19 + k、点击按钮 31 + k（98×128，无贴图，盖在图片上）[V `Card_SetImagePath @0x4e9ab0`、`CUi_gmaindlg_ResetEightSlots @0x4e9ca0`、`CUi_gmaindlg__vf10_Init @0x4e9550`]。
* **显示谁的牌**：当前行动角色的（包括电脑）[V `Role_RefreshDeckUi @0x4812c0`：只在该角色 = 当前角色时刷新]。就绪标志为 1 的卡不显示。
* **何时可点**：`Game_ToggleCharacterVisibility @0x4a6f90`（每帧）→ `Ui_ToggleCardDisplay @0x4e9900`：当前角色状态 6（等玩家掷骰）时按钮可点（槽内有卡才可点）；其它状态仅在换牌 / 卡片店卖牌时可点 [V]。
* **悬停（MouseOn 回调 5）** `Card_ShowInfo_8f40 @0x4e8f40`：图片按钮放大 1.25 倍（x − w/8，y − (h/4 + 20)，宽高 + 1/4；仅当宽 < 120 × 屏宽/1024，防止重复放大），名称标签显示卡名（`Card_ShowInfo @0x4e9780`），并设置全局鼠标提示文字 `"%s  %s  %d"` = 名称、说明（string 6500 + Id）、点券。移开（回调 6）`Card_SelectDisplay_91f0 @0x4e91f0`：还原位置 / 大小，清名称，清提示 [V]。
* 鼠标提示 CText：`CText_SetContent @0x40de60`（设文字）、`CText_Update @0x40dd10`（0.5 s 后显示）、`CText_CalculateLayout @0x40dfb0`（框在鼠标右上：x + 5，下边 y − 2，靠近屏幕右缘时移到鼠标左侧）、`CText_DrawBoxAndText @0x40dd90`（填充 0xffd7b6ff，黑色描边，黑字 16 px）、鼠标按下清除 [V]。移植版：`ui.cpp` 里 `rm8UiSetTooltip / drawTooltip`，字宽用移植版字体 **[A]**。
* 点击（回调 1）`Card_OnSelected @0x4e9340`：状态 6 → `Role_TryUseCardInSlot @0x488c80`（卡存在且 type ≠ 2 才行：入队 `2(槽)`，状态 = 8）；换牌 / 卖牌见上。

## 3. 使用流程（人类）[V]

```
点卡片槽 k ─ Role_TryUseCardInSlot ─ 入队 2(k)，状态 8
消息 2   Role_BeginCardUse @0x4897a0: 卡的检查 vf1(k)（失败: 入队 100）; Deck_SetCardReady(k); 镜头对准自己
         检查函数入队:  [0x5A(speak0) 插到队首]  [1(选择类型…) 0x1C]  3(k, 结果 | 0x80000000)
消息 1   Role_OpenModalUi @0x4883c0: kind = b & 0xFFFF; 状态 7，直到界面回答（Role_CloseModalUi @0x4889b0 把结果写进 b == 0x80000000 的那条消息）
消息 3   Role_FinishCardUse @0x4896e0: 结果 0 或 Execute(结果 − 1) 返回 0 → 清就绪、入队 100（回到掷骰前，可再用别的卡）
         成功 → this+0x4C0[卡Id] = 1（已用标志），Deck_RemoveAndStore(k)
```

* `Execute(param, sa, sb)` 的三个参数 = `(结果 − 1, speakNo[1], speakNo[2])` [V `Card_CallPythonCallbackWithParams @0x4ae650`、`Card_SetCustomValue @0x4ad7a0`]；`speakNo[0]` 是出牌时自己的台词（0x5A）。`Competence()` = `Card_CallPythonCallback @0x4ae580`，脚本缺失返回 −1。
* 卡脚本的 `Role.Wait(cur)` 入队 100 → `Role_BeforeRollDice`，所以一回合可以连用多张卡，最后点掷骰。
* **条件检查**（卡对象虚表 vf1，类由 `Card_CreateCardObject @0x4ad060` 按 Id 选）：
  * 默认 `Card_CheckConditionAndSelectTarget @0x4ad440`：type == 2 不能用；`condition` 最高字节检查脚下格（0x08 脚下不是自己的地（**无主也算**）、0x02 住宅地、0x04 自己的地、0x10 无主地）；再按低字节决定选择类型（见下）。
  * Id 7 / 8 `@0x4ad910`：已经是该交通工具（`+0x22C == 2 / 3`）则不能用；Id 9 `@0x4ad9d0`：乌龟中不能用，弹点数界面（kind 0x82）；Id 31 `@0x4adab0`：要有别处的其他角色，弹选角色（kind 0x85，文字 string 565）；Id 45 `@0x4adc60`：点券 ≥ 50，选角色（flags `(condition & 0xFFFF | 0x5000) >> 8`）；Id 46 `@0x4add60`：未被附身。
* **选择类型**（消息 1 的 b 低字节 = kind，a = flags）：1 角色、2 道路点、8 土地、0x20 NPC；没有选择（直接 `3(槽, 1)`）的情况：角色卡且“仅自己”（`condition & 0xF00 == 0x200`）、土地卡且范围“自身位置”（`& 0xF000 == 0x8000`）、街道卡且范围“所在街道”。选目标的 flags 含义见 `Game_FindClosestRole @0x498170`（`flags & 0xF == 4` 排除自己；隐身角色不可选；`flags & 0x20` 且有保镖不可选）、`TerraMgr_FindClosestTerra @0x4f23b0`（`flags & 0xF`：2 自己的、4 非自己的、8 无主；`flags & 0xF00`：0x200 要有建筑、0x400 等级 < 5）、`TerraMgr_FindClosestRolePosition @0x4f2900`（`flags & 1`：点上没有 NPC、没有角色）。
* 原版的选目标 = 鼠标射线点选（`Game_HandleCardClickInGame @0x4a2940` → `Game_FindBuildingByType @0x4a3f30`），光标换成卡片图案；结果 = 序号 + 1。**[A]** 移植版：屏幕上用黄色菱形标出合法候选，点（鼠标 / 触摸）离点击位置最近的候选（70 px × 屏高/768 内）；okcancel 对话框（隐藏确定钮）显示提示并提供“取消”（结果 0）。候选过滤同上。dice / rob / chsplayer 对话框走原版的 `.ui`，按钮 id 就是结果（点数 1..6、卡槽 1..8、角色序号 + 1）。

## 4. 电脑用卡 [V]

`Role_StayOrBeforeRoll @0x484610`（消息 0x25，a ≠ 0 的电脑）：

1. `AI_PlanCardActions @0x4f9670`（**仅难度 > 2**，init.txt `difficuty`，本机文件 = 3）按固定规则排出 (卡 Id, 参数) 步骤；成功则不再看 2。
2. `Role_UseSpecialCard @0x4fbd10`：**第 3 天以后**（`g_GameState+0x14 > 2`；难度 < 2 时再掷 50%）；遍历 8 槽，对每张有脚本的卡调 Python `Competence()`，返回 ≥ 0 的进入候选，值 + 1 作为目标；均富卡(21) 立即选定，轉向卡(11) 优先，送神卡(26) 次之，否则随机一张。入队 `0x1B("%s使用%s")`（string 261）、`99(槽)`、`0x5A(speak0)`、`3(槽, Competence + 1)`，状态 8。
3. 都没有 → `Role_BeforeRollDice`：电脑先 `Role_UseCardBeforeRoll @0x4fbab0` 执行排好的步骤（同样入队 99 / 0x5A / 3），没有再 `AI_AskDiceNum` 掷骰。

移植版 `rm8CardsAiStart / rm8CardsAiUsePlanned`：`AI_PlanCardActions @0x4f9670` 的**所有块**都已移植，顺序同原版（`planCards` / `planLandGod`，`locPath` = `CPlayerTag__BuildLocationPath @0x492ff0`：从角色所在点按路网 BFS，模式 1 只沿前进方向、模式 ≠ 0 时连接数 ≥ 3 的点不再展开）：
1. **土地公附身块**（`+0x1bc == 5`、无乌龟、脚下是住宅地、街道已查封或手里有查封卡 5）：前方 3 点内他人的本街地块 ≥ 2 块且等级和 > 3、路上没有 NPC → 乌龟卡 10（对象 = 自己）；否则路障卡 12 放在前方 3 点内（没有角色、没有 NPC）等级最高（≥ 2）的他人本街地块点上（遇到路障 NPC 12 就停止搜索）；否则遥控骰子 9，点数 = 前方 4 点内等级最高（≥ 3）的点的步数。街道没查封时每个计划前面先排 (5, 1)。
2. 交换 + 購地、竹蜻蜓 + 購地 / 怪兽、停留 + 換位（早先已移植）。
3. **封印卡 35**：自己被窮神 / 衰神（2 / 3）附身 → 封印屏幕内的好神 / 棋盘角色（NPC id < 10 且不是 2 / 3 / 6）并对自己用它的卡（35 + id）；否则从 NPC 序号 8 往下找第一个屏幕内的坏神（2 / 3 / 6），把它的卡给屏幕内、状态 0、不隐形、无保镖、不被 2 / 3 / 6 附身的角色（已被附身必给，否则 50 %）；没有合适角色 → 搜索**不会**换下一个 NPC，直接进入下一块。
4. **搶奪卡 25**：除了拿搶奪卡和神明卡，**还会拿飞弹卡 18**（`Deck_GetCardAt` 循环里的 `id == 0x12` 分支）：对象周围 1 层的每个点，看它半径 1 的爆炸范围，范围里有自己的点或自己的地就放弃，第一个“干净”的点 = 开火点，步骤 = (25, 对象) + (18, 点 + 1)。
5. **轉向卡 11**（不是 1 / 5 附身、无乌龟）：前方 3 点内第一个路障 NPC（id 12）所在的他人地块，租金 > (3000..5999) × 物价指数 → 对自己用 11。
6. **地雷卡 16**：监狱点（`g_TerraMgr+0x44`，事件 7）在屏幕内，否则医院点（+0x40，事件 8），点上没有行动中的角色（状态 < 3）而有人类（状态 ≥ 3）→ 在这个点放地雷。

## 5. Python 接口（python.cpp）

* **入队语义** [V]：除下面“直接”的几个外，`Role.* / Terra.* / Street.* / Npc.Create` 都是 `Role_EnqueueMsg` 到**第一个参数那个角色**的队列（`py_Role_Pause @0x4fe660`、`py_Role_LostFocus @0x4fe710` 反汇编：`mov ecx, [role]` 后 `call 0x484890`）；消息在那个角色状态 8 时处理（对别人的 Pause / Become / Settle 在他自己的回合开始时才生效）。
* 直接（不入队）：`Role.Turn`（`Path_ComputeDistance @0x47b5f0`）、`Role.ChgTrfTool`（`Role_SetTrfTool @0x48a0c0`）、`Role.SetTrts`（`Role_SetTurtleDays @0x48b670`）、`Role.Average @0x4fe870`。
* 消息对照：Wait 100、Go 0x1A、Pause 0x65、Speak 0x5A、LostFocus 0x2E、Hypnosis 0x28、Become 0x24、Prison 0x19、Hospital 0x18、ChgPos 0x3D、Bubble 0x3E、Bodyguard 0x45、Latent 0x46、Seal 0x59、ReceiveCard 4、PlayPrt 0x2C、Terra.Buy 9、Build 0xC、Break 0xD、Exchng 0xE、Dragon 0x31、Bomb 0x36 / 0x37、Street.Close 0x2F、Flow 0x30、Build 0x10、Buy 0x44、Role.Grab 6 / 0x43 / 0x1B。
* **句柄** [A]：Terra = 序号 + 1（原版传对象地址）；StPoint（道路点）= 节点序号 + 1；NPC 号 = npc.h 句柄 − 1。`StPoint.Npc` 无 NPC 返回 −1，`Npc.ID` 返回 npcinfo Id。
* `Role.StPoints(角色, 范围, 方向) @0x4fdc80`：从角色所在点按路网连接逐层扩展（方向 0 全部、1 只向前（下一个点）、2 非向前；方向 ≠ 0 时连接数 ≥ 3 的点不再向外扩展），返回起点之后的点（没有则 `(0,)`）。
* `Role.InScn / Terra.InScn / StPoint.InScn`：`Camera_IsWorldPosInView @0x4fd5a0`（投影到屏幕，在 (20, 20) .. (宽 − 20, 高 − 100) 内）；无相机（离屏测试）视为可见。
* `Role.OnEvent(角色, 94 / 95 / 96, 参数)`：股票事件交给 `rm8StockEvent`（stock.cpp）；调用不成功且参数 ≠ 0 时入队 0x3B（把卡退回）**[H]**：反编译显示 `Role_TriggerLandEvent(ev)` 后无条件按参数入队 0x3B，原意应是失败时退卡。
* 模块：`CardPython` 不是引擎模块（是 `Pysrc\CardPython.pyc` 里的 `Reaction`）。旧版把它注册成空的内置模块，会让 `from CardPython import Reaction` 失败，已去掉。
* `Npc.*`、`Role.Become / Shape` 绑定到 `npc.h`（NPC 代理实现）。

## 6. 角色状态与街道

* **睡眠(1) / 监狱(5)**：`Role_RunTurnStartStep @0x4842f0` 第 9 步按状态分派；`Role_TickSleepStatus @0x48aff0`、`Role_TickPrisonStay @0x48ac70`：天数 < 1 → 状态 0、入队 0x25(0)（监狱另提示 str 72“出獄”）；否则提示 str 562 / 71（“…還有N天”），天数 − 1，状态 8（本回合结束）。冬眠卡 = `Role_PutOthersToSleep @0x48c330`（别人状态 < 3 的都 `Role_SetSleepStatus @0x48afa0`：状态 1，天数 3）。陷害卡 = 0x19 → 送进监狱节点（事件 7）`CPlayerTag__SendToPrison @0x48a9c0`；持有嫁禍卡(24) 的人会先转嫁 `CPlayerTag__OnMissileCardUse @0x4901d0`（电脑 `rand() % 3 < 天数` 时转给随机别人，人类弹 chsplayer 0x84）。抓捕演出**已有**：0x19 / 0xC9 → `startTransfer` → `rm8NpcTransfer`（警车 art 0x526，npc_act.cpp，第 12 节后补记），演出结束时才调 `sendToPrison`；`SendToPrison @0x48a9c0` 本身不清队列，所以移植版也不再清（以前的“清队列 + 等 60 帧 + 结束回合”是没有演出时的权宜之计，已删）；队列空了以后由引擎自己等 60 帧并结束回合（`@0x481df0` 状态 8 无消息，roles.cpp `dispatch` 同样处理）。
* **乌龟**：`Role_StartRollDice @0x484e50`：乌龟天数 > 0 且不是指定点数时，天数 − 1，这次掷骰强制为 1。烏龜卡 `Role.SetTrts(目标, 3)`。
* **隐形**：`+0x1E4` = 10（消息 0x46），回合开始第 0 步 − 1（`Role_DecrementField1E4 @0x48ed70`）；> 0 时不付过路费，也不能被角色类卡选中。
* **泡泡**：消息 0x3E（`InitializeEventDisplay(10)`），第 4 步计数 − 1。**[A]** 只有标志，没有任何抵挡效果（需要攻击卡）。
* **停留**：`Role.Settle` 置标志（`CPlayerTag__SetDeferredAction @0x4911b0`）；下一次消息 0x25 时清标志、提示 str 563，再入队落点事件 0x1D（糊涂神附身 0x12）。
* **交通工具**：`Role_SetTrfTool`：骰子数 = 交通工具，按交通工具换模型组（roleinfo artId / artId2 / artId3）和行走速度。
* **换位卡**（消息 0x3D(0, 目标)）：两人互换 (from, to)（`CPlayerTag__OnPositionSwap @0x490c60`）；**竹蜻蜓**（0x3D(点, −1)）：飞到该点，`to` = 第一个 `rand()` 为奇数的连接，否则最后一个（`CPlayerTag__OnCheckBankruptcy @0x48e920`，名字有误）。**[A]** 没有飞行 / 旋转动画。
* **街道**：查封 `Terra_LoadCloseIcon @0x42d3b0`（街道 +0x108 = 天数，下标须 > 0；查封中免租，每天 − 1）、涨价 `Terra_SetHeight @0x42d4c0`（价格 = ftol(c / 旧倍率 × 价格)，9 天后 = ftol(价格 / 倍率)）；天使卡 0x10 `@0x488360`（该街所有有主且 < 5 级的地 + 1 级；没有可升的把卡退回）；地王卡 0x44 `@0x48d2c0` **[H]**（整条街归自己）；拆屋 0xD（`BuildLevel_StepDecrement @0x4ef860`：等级 − 1）、交换 0xE（`@0x487b00`：双方地主不同才交换）。**[A]** 没有拆 / 建 / 交换的演出动画，等级立即变化。
* **搶奪卡**：`py_Role_Grab @0x4fed70` kind 4 n = 1：人类入队 `1(目标, 0x220083)`（kind 0x83：rob.ui 列出目标的 8 张卡）、0x1C、`6(目标, 0x80000000)`；电脑 `6(目标, 9)`（自动：先拿搶奪卡 25、飛彈卡 18，其次神明卡 36..44，其次等级最高的，同级随机）。消息 6 = `Role_GiveCardToRole @0x489830`（参数 1 = **被抢的角色**，失败时退回搶奪卡）；成功后（反汇编 @0x489a35..0x489a74）：**被抢的人**的队列得到 `0x5A(0x4a)`（说话编号 0x4a，`thunk_FUN_00484890` = `Role_EnqueueMsg(0x5A, n, 0)`，**不是消息 0x4a**）和 `0x2E(抢的人 + 1)`，抢的人的队列最前插 `0x2E(被抢的人 + 1)`（控制权交给被抢的人说完话再还回来）。旧版把它写成“入队消息 0x4a”：新闻模块后来把 0x4a 做成“地块 a 失去建筑（和地主）”，于是每次抢卡都会把地块 0 的房子和地主清掉，已改正。老千卡 n = 0：消息 0x43 `Role_GiveTicketsAndAllCardsTo @0x4895b0`：自己付 50 点券、对方得 50，对方所有卡经消息 4 转给自己，对方牌清空。查稅卡 kind 1：目标付钱（现金优先），自己得现金，提示 str 532。

## 7. 对 `docs/cards.md` 的更正

1. 封印卡“差 1 的 bug”不成立：`Role.ReceiveCard` 的参数是卡数组下标（Id − 1），`34 + NPC ID` 正好是 Id 35 + NPC ID。
2. condition 最高字节 0x08（購地卡等）：代码是“脚下不是自己的地”，**无主地也可以**（`@0x4ad440`：`terra == 0 || owner == self` 才拒绝）。
3. 附身神明卡 / 角色卡的选择 flags：`|0x4000`（神明卡 `|0x6000`）、范围位 0x2000 / 0x1000 本身不过滤距离，只有“屏幕内可见”（鼠标射线）起作用。
4. `AI_PlanCardActions` 是 **C++ 里的固定规则**（难度 > 2），不是 Python；电脑用卡的顺序见第 4 节。

## 8. 已实现与延后的卡片

| Id | 卡 | 状态 |
|---|---|---|
| 1 2 3 4 5 | 購地 建屋 拆屋 交換 查封 | 已实现（Python 原脚本 + 地产消息） |
| 7 8 9 | 機車 汽車 遙控骰子 | 已实现（换模型 / 速度 / 骰子数；点数界面） |
| 10 11 13 | 烏龜 轉向 停留 | 已实现 |
| 15 | 冬眠 | 已实现 |
| 20 21 | 均貧 均富 | 已实现（现金平分） |
| 25 27 45 | 搶奪 查稅 老千 | 已实现 |
| 26 | 送神 | 已实现（无神明时无效果） |
| 28 | 陷害 | 已实现（[A] 无抓捕动画） |
| 29 30 48 | 漲價 天使 地王 | 已实现 |
| 31 32 | 換位 竹蜻蜓 | 已实现（[A] 无动画） |
| 33 49 | 泡泡 隱形 | 隐形已实现；泡泡只有标志 |
| 22 23 47 | 黑 紅 鑽石卡 | 走 `rm8StockEvent`（stock.cpp）；事件无法开始时卡退回手牌 |
| 12 14 16 17 | 路障 機器娃娃 地雷 定時炸彈 | NPC 对象（npc.cpp，`RM8_NPC_READY 1`） |
| 34 35 36..44 | 路霸 封印 + 八位神明 | NPC / 附身（`rm8NpcBecome`）；封印卡的目标选择（kind 0x20）见第 12 节 |
| 6 | 怪獸卡 | 已实现：`Terra.Dragon` → 消息 0x31，由 news.cpp 的怪兽事件处理（拆一块地的房子）；故事模式下的抑制（str 573 / 574）= 同一个 0x31 处理函数 `CPlayerTag__OnParkEvent @0x48d310` 里的英雄分支，story.cpp 的 `monsterHit`（`rm8StoryMonsterHit`）已做，卡和新闻共用 |
| 18 19 | 飛彈 核子飛彈 | 已实现（第 12 节）：点选地图节点 → 消息 0x36 / 0x37 → `rm8NpcBlast`（npc_act.cpp） |
| 24 | 嫁禍 | 被动卡，不能主动用；被陷害时自动转嫁已做 |
| 46 | 保鏢 | 已实现（第 12 节）：只有莎拉公主有真保镖，其他角色“保镖打人”进医院 3 天 |

不能用的卡点击后提示“…的效果尚未移植”，卡留在手牌里，电脑也不会用（`supported()`）。

## 12. 第二轮：飞弹 / 怪兽 / 保镖 / 封印 / 换牌 / 丢卡 / 卖卡 / 魔法屋

**飞弹 / 核子飞弹（卡 18 / 19）[V]**
* 卡的 Python `Execute` 调 `Terra.Bomb(节点, type)`（type 0 / 1）→ 消息 0x36 / 0x37，调度函数 @0x481df0 调 `CPlayerTag__OnHolidayCardUse @0x48de10`（节点、特效 art 0x1777 / 0x1778、音效 0x12 / 0x13、半径 1 / 3、住院基数 3 / 5 天），再排 0x1e（镜头）和 0x67(9)。范围节点表 = `Terra_BuildNeighborList @0x49fd70`（沿道路连接一层层扩展，`neighbourhood()`）。
* 命中规则：范围内状态 < 3 的角色；泡泡（先 0x62 → `CPlayerTag__OnEventConditionCheck @0x48dc80` 弹破）和莎拉的保镖挡住；受害者手里的 16..19 号卡被没收（`Card__FilterSpecialCards @0x48dbe0`），每张多住院 1 天；故事模式主角酷娃（角色 12）在范围内会没收这颗导弹（`OnSendGodCard @0x490df0`，str 570 / 571）。每个受害者排 0x18（住院），转交用 0x2e；住院演出重用 `rm8NpcTransfer`（救护车）；失去交通工具（0x81）。
* 2.0 秒任务 `Cls_5e6a78 @0x4f4780`：结束时 `Building_SuccessBuild @0x4f4800`（type 0：范围内所有地块拆光建筑，**地主不变**）/ `Building_FinishWithoutSuccess @0x4f4890`（type 1 核弹：地块还失去地主）。已验证：飞弹重置 2 块地的建筑、地主保留；核弹 4 块地失去建筑和地主。
* 实现在 `npc_act.cpp` 的 `rm8NpcBlast`（`npc.h`）；cards.cpp 的消息 0x36 / 0x37 调它。目标选择走 kind 2（点选路上的点，`candidates()` 投影到屏幕）。

**保镖卡（46）[V]**：消息 0x45（`Role.Bodyguard`，vf+0x80）。只有莎拉（roleinfo 16，类 vtable 0x5e2918，`CPlayerTag_3__vf32 @0x4803e0`）有真的保镖：计数 7，回合开始第 3 步递减（`rm8CardsHasBodyguard`），期间免疫点选目标（flags 0x20）、飞弹、付过路费（game.cpp str 567）。其他角色（`CPlayerTag__vf15_ShowState @0x48ecf0`）：清队列、特效 0x1787、住院 3 天。**[H]** 原版 vf25 恒为 0（其他角色类）；保镖的跟随图形（`gods\D46a`，`Path_Create @0x480260`）已在 `roles.cpp` 的 `Follower` 里做了（`docs/characters.md` 第 6 节），原先写“没有画”（str 568 的“〔string.bin#567〕，〔string.bin#568〕”交给 NPC 模块用 `rm8CardsHasBodyguard`）。

**封印卡目标（kind 0x20）[V]** `NPC_FindNearest @0x49f000`：flags != 0 时只列 NPC id < 10（神明和棋盘角色）且在地图上的（`rm8NpcCount / OnMap / Id / Node`），结果 = NPC 序号 + 1。

**手牌满换牌 [V/A]** `Card_LoadAndShowCard @0x4ae790` + `Card_HandleSwap` / `Cls_5e24f8`：events.cpp 的 `receiveCard` 先问 `rm8CardsBeginSwap`；人类手牌满时显示 getcard.ui（卡名 + 图 + 提示条），角色进状态 9，卡片栏全部可点，点哪张就换哪张（音效 0x7c，必要时表情）。**[A]** 点击卡窗口本身 = 放弃新卡（原版如何放弃未查）。

**丢卡 / 存卡菜单 [V]** `Card_ThrowPrepare @0x4aea80`、`Cls_5e3ca8__Event_OnThrowCardSelection @0x4aeb20`、`Card_ThrowClear @0x4aebd0`：状态 6 时对卡点右键（`CGamePlayScene vf11 @0x4a2cf0`，右键 = 模态结果 0）出 throwcard.ui：按钮 1 丢弃（`Role_RemoveCardFromDeck`），按钮 2 存入故事储藏箱（`Role_StoreCardToBox @0x488de0` → `rm8BoxStoreCard`，只在故事模式显示，满了 str 112）。菜单出现时清队列、排 100、状态 9，关闭后回状态 8。**[A]** 触摸：手指在卡上按住 1.4 s 打开（`touchHold`）。右键同时用于取消点选 / 点数 / 卡列表 / 角色列表对话框（`rm8CardsOnRightClick`）。

**卖卡 [V]** `Role_SellCardForTickets @0x488cf0`：卡商店任务存活时卡片栏变成卖卡模式（`rm8CardsSetSellMode`，event_shop.cpp 接线），点卡 = 删除、加 价格 / 2 点券（上限 9999，`Role_AddTicket @0x489ed0`）。商店界面里点数变化时重新着色。

**魔法屋 4 / 5 / 16 / 17 [V]** `MagicHouse_ApplyMagicEffect @0x4cdb10`：4 / 5 对持股设 ±10% 涨跌停 3 天（`Util_Apply2DTransform @0x4d3900` → `rm8StockForceHoldings`）；17 持股减半（`Util_ApplyScoreChange @0x4d3720` → `rm8StockHalveHoldings`，成本不变）；16 / 消息 0x27 kind 4 两人互换全部持股（`Role_SwapAssetsWithRole @0x48bf70` → `rm8StockSwapHoldings`）。API 在 stock.h / stock.cpp，event_magic.cpp 调用。

**触摸 / 指针验证**：`RM8_CARD_PTR` 驱动全部走 `rm8UiPointerMove/Down/Up` 和 `rm8CardsOnTap`，和真实触摸同一条路径。

**卡片范围（屏幕可见性）与公平性 [V/A]**
* 原版：范围位只靠 `Camera_IsWorldPosInView @0x4fd5a0`（投影后在 (20,20)..(宽−20, 高−100) 内，用的是**当前相机**）。电脑和人类是同一个测试：`AI_PlanCardActions @0x4f9670` 里的 8 处调用、`py_Role/Terra/StPoint/Npc_InScn`（Python 的 Competence）都调它；没有按节点距离的规则。用卡时相机已对准用卡人（`Camera_SetFocusPoint(role)`），选目标期间不能平移（沿用上一轮分析）。人类点选还靠鼠标射线，只能点到屏幕上的东西。
* 移植 **[A]**：只有一个范围函数 `rm8CardsPosInView(p)`（cards.cpp），人类的候选（`candidates()` 四种 kind）、AI 计划（`roleVisible`）和全部 `*.InScn` 都走它。它**不读实时相机**（玩家可能已经平移 / 缩放），而用“默认棋盘视角（yaw / pitch / 距离）对准当前行动角色”的虚拟相机，窗口取真实的宽高和视窗（竖直视窗不变，宽屏左右看到更多，对人和电脑一样）；边距 20 / 100 像素按高度 / 768 缩放。默认视角由 rm8_app.cpp 的 `cardRangeView` 传入（`rm8CardsSetDefaultView`）。
* 选目标期间相机锁定在这个默认视角（`rm8CardsPicking()` 时 rm8_app 忽略平移 / 旋转 / 缩放输入并用默认视角摆相机），所以人类能点到的恰好就是范围内的目标。
* 验证（`RM8_RANGE_LOG=1`，座位 1 第一回合，人类与电脑的行数逐字相同）：1024x768 → 28/126 节点、12/48 地块；1920x1080 → 36 / 16；2520x1080 → 42 / 19。座位 0 开局：13 / 22 / 24 个节点可选（人类的飞弹点选候选数 = 范围内节点数）。


## 9. 测试钩子（都是环境变量，未设置时无影响）

| 变量 | 作用 |
|---|---|
| `RM8_CARD_GIVE="座位:id,id;座位:id"` | 开局后额外发卡 |
| `RM8_CARD_USE="座位:槽,槽;座位:槽"` | 该座位每回合（消息 0x25）先用一个槽的卡；`RM8_CARD_USE_DAY=n` 从第 n 天起 |
| `RM8_CARD_AUTO=n` | 人类的选择自动回答：点选取第 n 个候选；点数界面取 n（默认 6）；卡列表 / 角色列表取第一项 |
| `RM8_CARD_HOVER=k` | 卡片栏第 k 槽显示成悬停状态（单张截图用，鼠标提示另需 `RM8_TIP_DELAY=0`） |
| `RM8_CARD_ALL=1` | 允许使用 NPC / 神明卡（现在默认已允许） |
| `RM8_CARD_PTR="步;步;..."` | 真实指针路径的测试驱动（每 0.4 s 一步，条件满足才执行）：`slot:N` 点卡槽、`cand:N` / `candrole:S` / `candnear:S` 点选候选、`dlg:N` 点对话框控件、`swap:N` 换牌点卡槽、`rclick:N` 右键卡槽、`cancel` 右键取消、`hold:N` 手指按住 1.7 s、`menu:N` 点丢卡菜单按钮、`sell:N` 卖卡、`recv:ID` 给人类发卡（走消息 4，手牌满开换牌）、`goto:节点` 把人类放到节点、`ownall:S` 所有地块归 S、`report` 打印地块数、`wait:秒` |
| `RM8_CARD_PTR` 新步骤 | `near:S` 把人类放到座位 S 旁边的路点（进入卡片范围）；`ownall:N`：N < 10 = 座位 N 等级 2，N ≥ 10 = 座位 N % 10、等级 N / 10（`ownall:51` = 座位 1、5 级）；`goto:节点` 现在选一个真实的下一点 |
| `RM8_DEBUG_CARDS=1` 新输出 | 每次用卡前后各一行 `snapshot before / after`（每个角色的现金 / 存款 / 点券 / 点 / 交通工具 / 附身 / 状态 / 乌龟 / 泡泡 / 隐形 / 手牌，地块数 / 等级和，地图上的 NPC），用来核对效果；`RM8_DEBUG_COMP=1` 打印电脑看每张卡时 `Competence()` 的值 |
| `RM8_RANGE_LOG=1` | 每回合开始打印行动角色的卡片范围（范围内的角色 / 节点 / 地块数）和点选候选数 |
| `RM8_ROLES="16,6,1,5"` | 开局四个角色的 ID（测莎拉等） |
| `RM8_MAGIC_EFFECT="4,5,16,17"` | 魔法屋的数字依次取这个表（至少 4 个互不相同的数，否则抽数循环不会结束） |
| `RM8_DEBUG_CARDS=1` | 日志：用卡、电脑选卡 / 计划、卡片模块处理的每条消息、监狱 / 睡眠 |

截图：`rm8_app.exe <游戏目录> scenes --map 01 --human 0 --size 1600x900 --time 20 --shot out.png`（座位 0 为人类，停在状态 6）。全电脑自测：`--human -1 --time 600`。

## 10. 共享文件改动（均为小插入）

* `hud.cpp`：`rm8HudInit` 里用 `rm8CardsHudInit` + `rm8UiOnClick(g_main, …)` 取代隐藏卡槽的一行；`rm8HudRender` 里 `rm8CardsHudUpdate` / `rm8CardsHudRender`。
* `game.cpp`：`rm8GameMessage` 开头调 `rm8CardMessage`、消息 1 先问 `rm8CardModal`；街道查封 / 涨价状态（`g_stFx`，每天倒计时，查封免租）；末尾一组小接口（`rm8GameSetTerra / SetShape / Invisible / StreetClose / StreetFlow / TerraPos / TerraPrice / NodeEvent`…）。
* `roles.cpp`：回合开始第 0 / 4 / 9 步、消息 0x25 / 100、掷骰（乌龟）、`beforeRollDice`（电脑计划）的钩子；末尾一组接口（`rm8TurnNodeTo / Links / NodePos / TurnAround / PlaceAt / SwapPlaces / SetVehicle`）。
* `turn.h`：上述接口的声明。`rm8_app.cpp`：加载 `cards.txt`；点击棋盘空白处交给 `rm8CardsOnTap`（触摸轻点和鼠标点击）。
* `ui.cpp / ui.h`：`rm8UiGetRect / SetRect / SetTooltip` 与鼠标提示框绘制（`RM8_TIP_DELAY` 测试开关）。
* 第二轮：`npc_act.cpp / npc.h`（`rm8NpcBlast`，任务带地块列表和类型）、`stock.h / stock.cpp`（3 个持股接口）、`event_magic.cpp`（效果 4 / 5 / 16 / 17）、`events.cpp`（`receiveCard` 先问换牌）、`event_shop.cpp`（卖卡模式）、`roles.cpp`（`rm8CardsSetCamera / Update`、第 3 步）、`game.cpp`（付过路费的保镖分支）、`ui.cpp / ui.h`（`rm8UiPressIsTouch`、`rm8UiSetRootPos`）、`rm8_app.cpp`（右键交给 `rm8CardsOnRightClick`；截图模式 `rm8UiSetScreen`）。
* `python.cpp`：本模块的函数表 `kImpl2`（实现过的名字不再注册成存根）；`Stock` 模块的存根未动。

## 11. 开放问题

* 手牌满换牌时“放弃新卡”的原版操作未查（现在点卡窗口放弃 **[A]**）。
* ~~地雷 / 定时炸彈爆炸是否也摧毁地块~~：已定。反编译明确：地雷（消息 0x60 @0x481df0）和定时炸弹（`OnUseTargetCard @0x48e400`）都调 `CPlayerTag__OnHolidayCardUse(点, 0x1775, 音效, 半径 0, 参数 0, 天数)`，第 5 个参数（任务 +0x28）= 0 → `Building_SuccessBuild`，所以和飞弹一样：爆炸点所属的地块拆光建筑、地主不变（半径 0 = 只有这个点的地块）。npc_act.cpp 已改成 type 0；电脑的地雷脚本 `CardPy_16` 不把雷放在自己的地块点上（`owner == curRole: continue`）也印证了这一点。
* ~~怪兽卡故事模式抑制~~：story.cpp `monsterHit` 已做（第 8 节）。~~飞弹演出里的等待按 `waitScale` 缩放~~：反编译没有这样的等待——飞弹 / 核弹 / 地雷的任务 `Cls_5e6a78__vf1 @0x4f4780` 是常数 2.0 秒（`Game_OnUpdate @0x4a17b0` 把原始 dt 交给任务的 vf1，不乘速度系数；`Anim_ApplyDifficultyScale` 只在显式的等待里用），npc_act.cpp 的 `type >= 0 ? 2.0f` 就是这个值；演出里别的等待（酷娃没收的 `0x65(30)`、`0x2C` 之后的 `0x65(60)`、被抓后的 `0x65`）都是消息 0x65，roles.cpp 的 `case 0x65` 已经乘 `waitScale()`。（莎拉保镖的图形已补，酷娃的飞弹没收含交接已补，见 `docs/characters.md`。）
* 卡片栏名称标签的文字颜色未核实（移植版用 .ui 的默认黑色）。
* `Role.OnEvent` 失败时退卡是推测 **[H]**。

## 13. 收尾轮（2026-11-15）：逐卡验收与修正

**测试方法**：人类 = `RM8_CARD_GIVE="0:id" RM8_CARD_PTR="slot:3" RM8_CARD_AUTO=0`（需要的卡先 `goto` / `near` / `ownall`），看 `snapshot` 的前后差；电脑 = 四个座位各发两张该卡，`--human -1 --time 1500`，数 `picks / plans`、`uses`、`executed` 和错误。脚本在 `tmp_cardtest/`（临时）。

**修正**
1. **搶奪卡的交接消息**（真 bug）：`giveCard` 以前入队消息 0x4a，其实是**说话编号** 0x4a（`0x5A(0x4a)`），而 0x4a 已被新闻模块做成“地块清空”，每次抢卡会清掉地块 0。已按反汇编改成：被抢的人 `0x5A(0x4a)` + `0x2E(抢的人 + 1)`，抢的人最前 `0x2E(被抢的人 + 1)`。
2. `cards.cpp` 的 `0x2c / 0x2e / 0x4a / 0x53` 吞消息的 case 已删：present.cpp（0x2C / 0x2E / 0x53）和 news.cpp（0x4A）在 `rm8GameMessage` 里先于卡片模块处理，那个 case 实际上只会吞掉漏网的消息。
3. `AI_PlanCardActions` 剩下的块全部移植（第 4 节）：土地公计划、封印、路障 + 转向（路障由土地公计划和转向计划使用）、地雷、搶奪时拿飞弹。
4. 地雷 / 定时炸弹爆炸会拆地块建筑（第 11 节）。
5. 抓捕：`sendToPrison / sendToHospital` 不再清队列（演出由 `rm8NpcTransfer` 提供，已存在）。
6. 手牌满换牌：已验证（第 1 节）。
7. 怪兽卡的故事模式抑制：已在 story.cpp（`monsterHit`），无需再做。飞弹等待：无此等待（第 11 节）。

**逐卡结果**（人类：效果在 snapshot / 日志里可见；电脑：四座位、自动对局，无错误日志）

| 卡 | 人类 | 电脑 |
|---|---|---|
| 1 購地 / 2 建屋 / 3 拆屋 / 4 交換 / 5 查封 | OK（脚下无主地买下 −3000；建 / 拆；换地主；街道 0x2F） | OK（1 只在脚下是别人的地时才选，必须有人先买地，自动局里出现 2 次 → 计划块也出现） |
| 6 怪獸 | OK（拆房，故事模式抑制在 story.cpp） | OK |
| 7 機車 / 8 汽车 / 9 遥控骰子 | OK（交通工具 1→2 / 1→3；点数 6） | OK |
| 10 乌龟 / 11 转向 / 13 停留 / 15 冬眠 | OK（乌龟 0→3；冬眠别人状态 1） | OK（11 只在 Competence ≥ 0 时用，自动局里见到） |
| 12 路障 / 14 機器娃娃 / 16 地雷 / 17 定時炸彈 | OK（NPC 12 / 13 / 14 在选中点出现；娃娃走 10 步） | OK |
| 18 飛彈 / 19 核彈 | OK（爆炸、住院、地块拆光 / 连地主清） | OK（19 需要 Competence 条件，造地块后出现） |
| 20 均貧 / 21 均富 | OK（现金平分） | OK |
| 22 黑 / 23 紅 / 47 鑽石 | OK（股票事件，电脑自选股涨跌停 str 527 / 528 / 558） | OK |
| 24 嫁禍 | 被动卡，点击无效（原版 type 2） | 被陷害时自动转嫁 |
| 25 搶奪 / 45 老千 / 27 查稅 / 28 陷害 | OK（对象在范围内才有候选；卡转移、现金 ±10000、警车入狱） | OK |
| 26 送神 | OK（附身解除 / 炸弹清除） | OK |
| 29 漲價 / 30 天使 / 48 地王 | OK（0x30 / 0x10 / 0x44） | 48 未触发（需要整条街 ≥ 4 块、别人占 ≥ 2..5 块且等级和 ≥ 3..6；1500 秒自动局 + 全地块归座位 1 都没出现 Competence ≥ 0），只验证了人类路径 |
| 31 換位 / 32 竹蜻蜓 | OK（〔string.bin#538〕 / 飞到点） | 31 / 32 未触发（Competence 要求前方 6 格内大量自己 / 无主地且 1/3、1/18 的随机；199 / 96 次查询里 0 / 1 次 ≥ 0，计划未走到执行），只验证了人类路径 |
| 33 泡泡 / 49 隱形 / 46 保鏢 | OK（泡泡 10；保镖：莎拉有，其他角色进医院） | OK |
| 34 路霸 / 35 封印 / 36..44 神明卡 | OK（附身 7 天，路霸 16；封印把 NPC 收回并得到神卡） | OK（封印 / 神明计划见第 4 节） |

没有发现与原版不一致且反编译 / Python 脚本明确的卡；没有卡“留在手牌里没效果”（除原版本来就拒绝的情形：条件不符时 `checkCard` 拒绝并写日志 `the check of the card refuses it`）。

**仍未做 / 不确定**：手牌满时放弃新卡的原版操作；电脑计划块里 `Camera_IsWorldPosInView` 用的是移植版的默认视角虚拟相机（第 12 节）；封印 / 土地公 / 转向 / 地雷计划只在难度 ≥ 3 且条件凑巧时才出现，自动局里只实测了封印（两种分支）、土地公（查封 + 乌龟）、搶奪 + 飞弹；转向和地雷计划只做了代码对照。
