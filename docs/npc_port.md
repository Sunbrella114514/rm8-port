# 板上 NPC 与附身系统（逆向 + 移植说明）

> 范围：npcinfo.bin 的 NPC（財神 窮神 衰神 福神 土地公 糊塗神 / 警察 小偷 野狗 / 幸運卡片 寶箱 / 路障 地雷 定時炸彈 機器娃娃 / 路霸 / 拼圖 / 踩蛋）、角色附身（`Role.Shape` / `Role.Become` = 消息 0x24）及其规则效果。
> 依据：`out/final_v2/RichMan8_named.c`（名字约一半不准，已按代码重新判断，下表“真实作用”列为准）。
> 记号：`[V]` 已读代码确认（附地址）；`[H]` 推测；`[A]` 移植里的近似。移植代码：`port/app/npc.h / npc.cpp / npc_act.cpp / npc_ui.cpp / npc_int.h`，数据 `port/scenes/npcs.txt`（`scripts/export_npcs.py`）。

## 1. 数据：npcinfo.bin

`NPC_LoadFromINI @0x49d520`（sscanf）[V]：`Id`→+0x204，`type`→+0x200，`date`→+0x228，`good`→+0x22C，`sound`→+0x230，`scale "%f,%f,%f"`→+0x21C，`artNum`→+0x208，`artId`（1..4 个，`%d,%d,..`）→+0x20C.. ，`ImgFName`→+0x100（不用），小节名→+0x00（NPC 名字，`%s` 格式的“%s離開”用它）。

| Id 名 | type | date | good | sound | artId | 模型 |
|---|---|---|---|---|---|---|
| 1 財神 | 0 | 7 | 1 | 55 | 501 | gods\D36（单模型，动作 0 走 1 / 2 站 3 登场） |
| 2 窮神 | 0 | 7 | 0 | 57 | 502 | D37 |
| 3 衰神 | 0 | 7 | 0 | 58 | 503 | D38 |
| 4 福神 | 0 | 7 | 1 | 56 | 504 | D39 |
| 5 〔string.bin#549〕 | 0 | 5 | 1 | 59 | 505 | D40 |
| 6 糊塗神 | 0 | 5 | 2 | 60 | 506 | D41 |
| 7 警察 | 1 | 7 | 2 | 61 | 509,510,508,507 | D42，4 朝向模型 fl fr br bl（同角色），动作 0..3 |
| 8 小偷 | 1 | 7 | 2 | 62 | 513,514,512,511 | D43 |
| 9 野狗 | 1 | 7 | 2 | 63 | 517,518,516,515 | D44 |
| 10 幸運卡片 | 1 | 0 | 2 | 65 | 1056×2 | NPC\N10 |
| 11 寶箱 | 1 | 0 | 2 | 66 | 1057×2 | NPC\N11（N11_1.tga 原版贴图包里缺失，同 m08s06） |
| 12 路障 | 2 | 0 | 2 | 12 | 1058 | N13（无动画） |
| 13 地雷 | 2 | 0 | 2 | 16 | 1059 | N14 |
| 14 定時炸彈 | 2 | 0 | 2 | 17 | 1060 | N15 |
| 15 機器娃娃 | 2 | 0 | 2 | -1 | 1063,1064,1062,1061 | N18m 4 朝向 |
| 16 路霸 | 3 | 5 | 2 | -1 | 15,16 | 占位（A02 的角色模型），**不是板上对象**，见 §6.4 |
| 17 拼圖 | 4 | 0 | 1 | -1 | 1065 | N16 |
| 23 踩蛋 | 3 | 0 | 2 | -1 | 15,16 | 占位 |
| 100..103 粉紅豬 青蛙 無敵超人 姚仙史萊姆 | 4 | 10/100/10/10 | 2 | -1 | 15,16 | 占位 |

`date` = 附身天数（神 7/5，路霸 5，青蛙 100 = 不限）[V]（+0x228 → 角色 +0x1DC，见 §5）。`good` = +0x22C：0 坏神（窮神 衰神）离开时角色说话 0x18 [V @0x483350]；`Npc.Goodness` 不读它，而是硬编码 id 1 4 5 10 11 [V @0x500520]。

## 2. 对象模型

NPC 是 Path/Mesh 基类（和角色同一个 `Cls_5e1b90` 家族）的子类，由 NPC 管理器 `*(0x632db4)`（`NpcMgr @0x4071c0`）持有一个数组（`[0]` 数据，`[3]` 个数）。`Npc_Factory @0x49e6c0` 按 id 建类 [V]：

| id | 类（vtable） | 大小 | 说明 |
|---|---|---|---|
| 1 2 3 4 | 0x5e3588 / 34e8 / 3448 / 33a8（基类 0x5e3628） | 0x118 | 神，+0x98 是各自的“附身后效果” |
| 5 6 | 0x5e3628 | 0x118 | 土地公 / 糊塗神：附身后没有额外效果 |
| 7 8 9 | 0x5e3310 / 3278 / 31e0 | 0x94 / 0x94 / 0xA4 | 警察 / 小偷 / 野狗 |
| 10 11 | 0x5e3148 / 30a8 | 0x94 | 幸運卡片 / 寶箱 |
| 12 13 14 | 0x5e3010 / 2f78 / 2ee0 | 0x94 | 路障 / 地雷 / 定時炸彈 |
| 15 | 0x5e2e48 | 0x94 | 機器娃娃（会走） |
| 23 | 0x5e2db0 | 0x94 | 踩蛋 |
| 17 | 0x5e2b50 | 0x98 | 拼圖（`Event_SpawnNpcInstance @0x49dd20` 建，+0x94 = 拼图号） |

字段（`NPC_InitBase @0x49f320`、`Cls_5e2b50__CardCheck @0x49b220`）[V]：+0x64 npcinfo 记录，+0x68 **状态**，+0x6C 数组下标（py_Npc_* 的 NPC 号），+0x70 创建者座位（`Npc.Create` 第 3 参），+0x7C/+0x80 待确认标志 / 角色位集，+0x84 所在道路点，+0x88 正在互动的角色，+0x8C 阶段，+0x90 计时器，+0x3C 剩余步数（娃娃）。

状态 +0x68：0 在地图上；1 互动中；2 等对话框（財神 / 窮神）；3 娃娃行走中；4 已离开（`Finalize` `@0x49b6c0`）；5 待删除（`Cls_5e2db0__Finalize @0x49cd20` = 4 之后再设 5：路障 地雷 炸弹 娃娃 踩蛋 拼圖；`PlayerTag_OnDeleteEvent @0x49e110` 把状态 5 的从数组删掉，**后面的下标前移**）；6 未放置（`CardCheck` 之后）。`Npc_PlayAction @0x49df20` → vf+0x6C `NextState @0x49b730`：4 → 6（神离开角色身上之后）。

虚表槽（NPC）：+0x10 绘制，+0x0C 每帧 `Update @0x49b350`（状态 < 4：+0x90 计时器减 dt，到 0 调 vf+0x88，神是 `OnSelect @0x49b8d0` → vf+0x94 `Animate`），+0x58 CardCheck(npcinfo, 数组下标)，+0x64 **OnLand**（放到道路点），+0x68 **Finalize**，+0x6C NextState，+0x74 **OnRoleClick**（角色停在这个点），+0x78 OnPlayerClick（只有青蛙 / 無敵超人用），+0x7C **经过检查**（只有路障非空），+0x80 存档，+0x8C CalcScale，+0x90 站立动作，+0x94 附身后效果 / 事件，+0x98 神的 BeginCollection 等。

绘制：状态 < 4 且 +0x7C == 0 才画（`@0x49b910`）；影子（art 0x18A3 `mend\mnd_07`，`CardCheck` 里 `Mesh_AddMesh`；位置 = 节点 + (0,1,0)，缩放 0.9 [V @0x49b570]）只在状态 0 画。

### 2.1 创建与放置

- `Game_InitGame @0x4a1090` → `Npc_SpawnAll @0x49e2d0`：按 npcinfo 顺序，**type < 2** 的记录各建一个对象（即 id 1..11，数组下标 0..10，**Python 的 `for i in range(9)` 就是 id 1..9**），状态 6；然后 `Event_ProcessLanding @0x49db20`（故事模式的拼图，§6.9），然后 `Event_OnNpcEnable`。
- `Event_OnNpcEnable @0x49d9c0`：遍历数组，状态 4 或 6 的：type 0（神）各自 `TerraMgr_FindAvailableNPC` 取一个点 `OnLand`；type 1（警察 小偷 野狗 幸運卡片 寶箱）只收集，最后**随机选一个**放。游戏开始时调用一次，之后每个月 1 号：`Game_OnTurnStart @0x4a3b50`（先 `Game_AdvanceOneDay`、物价、股市，`日 == 1` 时 `Event_OnNpcEnable`；第 0 天的第一次调用只设标志）。注意状态 4 也算，所以**正附在某个角色身上的神（对象状态 4）下个月 1 号会被再放一次**，地图上和角色身上可以同时有同一个神。
- **选点过滤（2026-11 复核 `@0x4f2610` / `@0x49d9c0` / `Land_CreateObject_f1320 @0x4f1320`）[V]**：`+8 == 0`（`BuildObj_CreateEvent @0x4f19e0`：格子事件 > 0，或事件 ≤ 0 且地块字 100..103 → 事件 15/16/13/14，二者都会建事件图标对象；移植用 rnode.event 加 scene.txt 的 `icon` 行）、`+0x2c == 2`、不是 TileMgr `+0x40`（医院，事件 8 的格）/ `+0x44`（监狱，事件 7）、BFS 2 层内无 NPC / 活角色。反编译里**没有**“监狱 / 医院出口路”的专门判断，道路码 `road` 也只有“非 0”。出口路（`u_Path_FindEndpointElement @0x4f0280` 求出的死胡同支路，`Path_SelectRandomNext @0x47b8e0` 不会走进去）沿路全是事件格（7/8 加 10/11/12 拾取点）所以 15 张图里 14 张本来就选不到；`download201` 的监狱路有 1 个无事件 2 连接点 (26,45)，会被选中。**[H]** 移植 `buildNodeInfo` 额外把“从路口第一条死胡同链到尽头的所有点”排除（对其它 14 张图无影响）。检验：`RM8_NPC_SPAM=300` 每图重放 300 次 `Event_OnNpcEnable`，15 张图选点落在出口路 0 次（旧代码 download201 为 56/2800）。
- `TerraMgr_FindAvailableNPC @0x4f2610`：遍历道路点，取前 128 个满足：无事件图标（点 +8 == 0）、**恰好 2 条连接**（+0x2C == 2）、不是 TileMgr +0x40 / +0x44 两个特殊点、`Graph_BFS_FindNeighbors(点, 2)` 范围内（自己、邻居、邻居的邻居）没有 NPC（点 +0x10）也没有活着（状态 ≠ 7）的角色站着（`Find_NthOwned @0x4f0510`）。`rand() % 个数` 选一个。
- `OnLand @0x49b570`：点 +0x10 == 0 且状态 4/6；`vf+0x8C` CalcScale（神 / 警察 / 小偷 / 野狗 = **0.7 × npcinfo 缩放** `@0x49bc70 / 0x49c410 / 0x49c5b0 / 0x49c8d0`，其它不动）；`vf+0x90` 动作（神 警察 小偷 野狗 娃娃 = 动作 2 循环；幸運卡片 寶箱 路障 踩蛋 拼圖 = 动作 0；地雷 炸弹无）；朝向 `Path_SetPoints(点, 点的第 0 个邻居)`；状态 0，点 +0x10 = 自己。
- `Npc.Create(id, 点, 角色)` = `py_Npc_Create @0x500160`：`Npc_CreateInstance @0x49f0e0`（只允许 npcinfo type ≥ 2）建对象，点非 0 时 OnLand；放置音效 id 12 → 0x0C，13 → 0x10，14 → 0x11；+0x70 = 角色座位。**15 不建对象**，当前角色入队 `0x32, 0x65(0x32), 0x1E`。点上已有 NPC 时 OnLand 失败，对象留着不显示（移植：直接删掉）。
- `Npc.Disappear` = `py_Npc_Disappear @0x5003b0`：Finalize + 音效 0x23。`Npc.InScn(i)`：`u_Unk_IsReady @0x49f1f0`（已放置，状态 0）且点在视野内。`Npc.StPoint`：+0x84。`StPoint.Npc(点)`：点 +0x10 的 +0x6C。

## 3. 角色遇到 NPC

### 3.1 停下：消息 0x23 与 0x1D

`CPlayerTag__OnStopAtTerra @0x485c00`（vf19）[V]：站立动作 2、停走路音效、状态 8；**身上有定时炸弹**（+0x288）计数 −1，< 1 → `CPlayerTag__OnUseTargetCard @0x48e400`（爆炸）并结束，不再入队；`Role_RunPoliceThiefDogAction @0x48bd00` 成功 → 入队 0x1E，否则（故事模式）与同点角色对话 `@0x48ebb0 / 0x48c950`；最后入队 `0x23(点)`、`0x1D(点)`。

消息 0x23 `@0x481df0` → `NPC_HandleInteract @0x49dfb0(角色, 点)`：遍历数组，**第一个返回非 0 的**的返回值（点上只有一个 NPC）；特例：角色附身 0x65（青蛙）且 NPC 是神、或附身 0x66（無敵超人）→ 用 vf+0x78，匹配就 `return 0`（**不互动**）。返回 1 → 角色状态 10（NPC 演出中）并 `Role_ClearFlag1C8`；否则若点有事件图标，队首插 `0x65(30)`（`Role_GetWaitFrames(4)`）。状态 10 在 NPC 的 `Finalize` 里由 `Role_LeaveState10 @0x486110` 改回 8。

### 3.2 经过：vf18

`u_CPlayerTag_UpdateTurnCountdown @0x485a20`（走路时每到一个点、步数没走完）[V]，按顺序：

1. 身上有炸弹：计数 −1；< 1 → 站立、停音效、清队列、状态 8、`OnUseTargetCard`（爆炸），本帧结束；否则 `GetRandomBankruptRole(2, 0)`（同点另一个状态 ≤ 2、不隐形的角色）：对方没炸弹 → 把炸弹（计数）转给他、自己的清掉；对方有炸弹 → 两边计数互换。
2. `NpcMgr_CheckPassNode @0x49e090`（对每个 NPC 调 vf+0x7C）：只有**路障**非空（`Unk_ProxyVirtualCall_vf31 @0x49cec0`，取当前行动角色调 OnRoleClick）。其它 NPC（神 警察…地雷）**经过时不触发，只有停下才触发**。
3. 附身警察 / 小偷 / 野狗且与别人同点：`Role_RunPoliceThiefDogAction`（§5.4），成功 → 站立、停音效、入队 `0x26`（之后用剩余步数继续走）、状态 8。
4. 银行（事件 5，未附身糊塗神）：`Role_TriggerLandEvent(99)`（已在 `rm8EventPass`）。

### 3.3 各类 OnRoleClick（vf+0x74）

| NPC | 函数 | 行为 [V] |
|---|---|---|
| 神 1..6 | `Cls_5e33a8__OnClick @0x49bb40` | 状态 0 且点相同：特效 6122（`Anim_CreateSprite(0x17EA, 点, .., 音效 0x1E)`）、+0x88 = 角色、状态 1、阶段 1，返回 1。之后每帧：计时器到 0 → `Animate @0x49bce0`（§4） |
| 警察 7 | `Cls_5e2be8__OnRoleClick @0x49c390` + `Roll @0x49c480` | **先 Finalize**，+0x88 = 角色；清角色队列；`News_OnUpdate(rand() & 7, 角色)` → 新闻 0..7（§3.4），返回 2 |
| 小偷 8 | 同上 + `Roll @0x49c620` | 新闻 9（丢一半点券） |
| 幸運卡片 10 / 寶箱 11 | `@0x49c730 / 0x49c800` | Finalize；角色队列插 `0x53(0x41 / 0x42, 0)`（音效，不等）、位置 1 `0x3C(10 / 11, 座位+1)`（新闻 10 / 11，§3.4），返回 2 |
| 野狗 9 | `@0x49ca80` + `BeginAttach @0x49caf0` | 交通工具 +0x22C < 3（步行 / 机车）：Finalize、清队列、入队 `0x2C(0x1788, 0x3F)`（咬人特效 + 音效 63）、`0x18(2, 0)`（**住院 2 天**）。= 3（汽车）：角色状态 9、音效 0x40、狗播动作 5，沿“角色所在点 − 其第一个不是前进方向的邻居”方向飞 0.8 秒（XZ 300/秒，Y 先 +100/秒 0.5 秒再 −150/秒，`OnUpdate @0x49c940`），然后 Finalize、角色回状态 8 |
| 路障 12 | `@0x49ce50` | Finalize；队列位置 0 `0x53(0x46, 0x3C)`（音效 70 + 等 60 帧）、位置 1 `0x5A(0x34)`（说话）；`Role_CancelMoveSteps @0x48b6b0`（状态 1 时步数 = 0 并 vf19 停下），返回 2 |
| 地雷 13 | `@0x49cf50` | Finalize；角色无泡泡（+0x28C == 0）：清队列、入队 `0x60`（爆炸，§7）；有泡泡：队首 `0x2C(0x1775, 0x43)`、`0x3F`（泡泡消失）、`0x65(30)`；返回 2 |
| 定時炸彈 14 | `@0x49d000` | Finalize；队首 `0x53(0x11, 0x3C)`、位置 1 `0x3A(0x24 = 36, 0)`（给角色装炸弹，计数 36），返回 3 |
| 拼圖 17 | `@0x49d100` | 只有 0 号座位（故事模式主角）：队首 `0x53(0x41, 0)`、位置 1 `0x58(拼图号)`；Finalize；返回 2 |
| 踩蛋 23 | `@0x49cd70` | 特效 0x1772，状态 1，返回 1；特效结束 `OnUpdateInner @0x49cde0`：`Role_SetPossession(100 + (rand() % 10) / 3)`（100..103），Finalize |

**机器娃娃 15（`py_Npc_Create`）**：`0x32` 消息 = `CPlayerTag__OnQandaEvent @0x48da00`：建对象，`MiniGame_StartWithRole @0x49d250(角色.from, 角色.to, 角色)`：状态 3、剩余步数 **10**、从角色所在点朝 `to` 站好、动作 0、先 `vf+0x44(20.0)` 走 20 单位、音效 14；角色状态 10。每帧（`Cls_5e2e48__OnUpdate @0x49d180`）镜头对焦，`Path_AnimateAlongRoute(dt × 100)`（**100 单位 / 秒，不受游戏速度加成**，和 `Path_SelectRandomNext` 同样的随机转向）；到每个点：步数 −1，还有步数 → vf18 `OnLose @0x49d380` = 清掉**这个点上的 NPC**（`Unk_CallVirtualMethodIfValid @0x49d2f0` → 点 +0x10 → vf+0x68 Finalize，神 / 警察 / 路障 / 地雷 / 炸弹一视同仁）；步数 < 1 → vf19 `OnWin @0x49d320`：同样清最后一个点，娃娃自己 Finalize（状态 5，角色回 8）。AI 的“前方 6 格”只是判断，娃娃实际走 10 个点。

**封印 `Role.Seal(角色, NPC 下标)`** = 消息 `0x59(下标, 0)` `CPlayerTag__OnWonderConstruction @0x492930`（名字不对）：镜头对 NPC 所在点、特效 0x177C（= 6012 `eff\D35t`）+ 音效 0x23、等 4 秒 × 速度系数；队首插 `0x65(60)`，位置 1 `0x59(下标, 1)`；第二次：NPC `Finalize`，`0x65(30)`、`0x1E`。（CardPy_35：只有 id < 10 的能封印，同时给角色 id 34 + NPC id 的卡。）

### 3.4 新闻事件（警察 / 小偷 / 幸運卡片 / 寶箱）

消息 `0x3C(事件, 座位+1)` = `CPlayerTag__OnLotteryEvent @0x48e7e0` → `News_OnUpdate(事件, 座位) @0x4b4da0` → `News_OnSpecialEventUpdate @0x4b4e40`：事件 < 9 → `News_EventMinigame(3, 事件, 座位) @0x4b5ef0`；9 → `News_EventThief @0x4b7bc0`；10 `News_EventLuckyFind @0x4b7d10`；11 `News_EventTreasureChest @0x4b7e50`；0x2E..0x3A 是每日新闻（别的模块）。成功 → 任务挂 `g_GameState+0x34`、角色状态 9，持续 `3.8 × 速度系数` 秒（`Anim_ApplyDifficultyScale(0x40733333)`），到时 `News_OnClean`。每个事件：新闻 TV 里的 3D 插图（`News_CreateTextEffect @0x4b4f60`：art 6201 + 事件（警察 0..8 = N07m01..09）/ 6210 小偷 / 6211 / 6212，放在相机前 60 单位、缩放 0.05，TV 模型 art 1052 / `News_InitDialog @0x4b41b0`）+ 新闻文字（string 418 + 事件 = 警察 0..8，428 小偷，431 幸運，432 寶箱；`@061 / 062 / 064 / 066` = 音效号）+ 效果：

- 警察 0..8：对**座位**角色队首 `0x19(3, 0)`（坐牢 3 天，`CPlayerTag__SendToPrison`）；事件 8 = 附身警察遇到附身小偷（str426）。
- 小偷 9：座位角色队首 `0x08(点券/2)`（扣一半）。
- 10：座位角色队首 `0x04(-1, 1)` ×2（两张卡，按难度）。
- 11：座位角色队首 `0x07(250, 0)`（点券 250）。
- 之后 `Role_EnqueueMsgPairWithCurrent @0x4b8a00(座位, 位置)`：座位 ≠ 当前行动角色时，座位角色队首 `0x2E(当前+1, 1)`，当前角色队列 `位置` 处 `0x2E(座位+1, 0)`（`u_Role_StartInteractWithRole @0x48d040`：对方状态 0 → 8，自己 → 0）。

## 4. 神的附身过程（`Cls_5e33a8__Animate @0x49bce0`）

**2026-11 逐步复核后的时间线 [V]**：角色停到神的点上 → `NPC_HandleInteract` 返回 1 → 调度器 `@0x481df0` 置角色状态 10 并 `Role_ClearFlag1C8 @0x48a490`（**角色 +0x1C8 = 0，`CPlayerTag__Render @0x485d00` 的 `CanMoveNormally @0x4932e0` 为假：角色整个不画**，直到 `Role_LeaveState10 @0x486110` / `Role_SetPossession` 末尾的 `Role_SetFlag1C8`）→ `OnClick @0x49bb40`：特效 6122（粒子，`Anim_CreateSprite @0x47b090`，无动画回调所以寿命 4 秒 × 速度系数）+ 音效 0x1E → **特效存活期间神从 0.7 倍每秒 +0.2 长到满缩放（`Animate` 阶段 1 的 else 分支，`+0x74`）** → 特效结束（`+0x54 < 0`）：满缩放、动作 3 播一遍（`+0x5C` 忙）、npcinfo 音效 55..60 → 动作 3 播完：动作 2 循环，`Anim_PlaySound`（先停掉入场音效）播问候语音（string 400 + id 的 `@53xx`），屏幕中央出文字，计时 2.0 × 速度系数 → 计时到、动画不忙、**问候语音已播完（`+0x50 == 0`）** → `Role_SetPossession(id)`（角色形象换成神，角色重新可见）+ 神的附加效果。移植 `godClick / godAnimate(o, dt)` 按此实现；`Seat.hidden` 对应 +0x1C8，`rm8NpcRoleHidden` 给 `roles.cpp` 的绘制用。单模型的神附身后角色形象**不转身**（`Path_CalcAngle @0x47bfa0` 只对模型数 > 1 起作用，`rm8NpcRoleUpdate` 把 yaw 清 0）。问候文字：`@0x49b910` 在 800×600 屏上，字号 32，起点 y = H/2 + H/10，三层黑影偏移 (0,2)(2,0)(2,2)；移植在 1024×768 设计坐标里换算成 y = 460.8、字号 41、偏移 2.56（旧版 30 号字、y = 440 偏小偏高）。

| 阶段 | 动作 |
|---|---|
| 1（OnClick 的下一帧） | 缩放设为 npcinfo 全值（0.7 → 1.0）、`Mesh_SetTexture(3, 1)` 登场动作播一次、音效 npcinfo `sound`（55..60），阶段 2 |
| 2（动作 3 播完） | 动作 2 循环；播 string 400 + id 的语音（`@5301..5306`）、**屏幕中央**白字 + 3 个黑影（偏移 (0,2)(2,0)(2,2)，`Cls_5e33a8__OnUpdate @0x49b910`，y = h/2 + h/10，字体 32）显示这句话；计时器 = 2.0 × 速度系数；阶段 3 |
| 3（计时到、动画不忙） | `Role_SetPossession(id)`；然后 vf+0x98：**財神 / 窮神** `BeginCollection`（返回 1，状态 2，+0x7C = 1 隐藏神，弹滚轮对话框）；**衰神** `BeginAttach @0x49c280`；**福神** `BeginBless @0x49c310`；其它返回 0。返回 0 → `Finalize`（神从地图消失，角色回状态 8） |

- **滚轮对话框（2026-11 重做，`Lottery_InitScrollBar @0x49f810` / `Cls_5e2a80__OnUpdate @0x49f570` / `Lottery_OnStop @0x49fab0` / `Lottery_CalculateResult @0x49fb50` / `CUi_UpdateCharAnimation @0x410b30` / `CUi_CalcTrajectory @0x410f70`）[V]**：普通局用 `scrbar`（4 个数字轮），**故事模式（`*0x632de8`）用 `sscrbar`（6 个轮）**——倍数 “x2 / x3” 只存在于故事模式。每个轮是一个 CUi（步长 0x24c），把 `J218.tga`（512×256，52×40 的格）的字符以 40×32 画在对应 Label（id 3..8）的位置，向下滚动，每 32 像素一个字符，每过一个字符播 0x74（半音量）。数字轮：10 个字符，表 `DAT_005e36c8`（索引 0 = “0” 在 x=461，1..9 在 x=0,51,..,410），初始显示“1”，初速 `200 + (rand() % 10) × 10` 像素/秒（`@0x49f90f` 的 `fild; fadd [0x5e3788 = 200.0]`）；第 5 个轮（符号）2 个字符 `DAT_005e3768` = (51,214) “x” / (102,214) “÷”，第 6 个轮（倍数）3 个字符 `DAT_005e36d8` = “1”“2”“3”（倍数 = 索引 + 1），两者初始显示 “x” “1”。电脑 1.0 秒后自动停（`+0xde0`），人类按键 / 点击（`OnAbort → OnStop`）：标题文字清空，第 4 个数字轮（故事模式再加符号轮和倍数轮）减速度 200 像素/秒²；第 n < 3 个数字轮在右邻停下（速度 ≤ 0）且自己速度 ≥ 200 时才开始减速；速度 ≤ 50 后在下一个字符边界停；`+0xde4`（故事模式 `rand() % 5 == 0`）为真时减速的轮目标字符 = 右邻的字符（四个数字相同）。**符号轮和倍数轮只有在 4 个数字全相同且首位非 0 时才被更新（先符号后倍数，各自减速停下），否则一直停在 “x 1”**；金额 = 四位数字，故事模式再 × 倍数（符号 x）或 ÷ 倍数。结果停留 1.2 秒。移植：`npc_ui.cpp` 的 `Wheel / Reel` 按上面的状态机逐帧实现，精灵用 `ui.cpp` 新增的 `rm8UiSetPicFileUV`（图集格子）画在 UI 里；滚动方向、减速曲线细节 [A]。测试：`RM8_NPC_REEL_STORY=1`（强制 6 轮）、`RM8_NPC_REEL_CHEAT=1`（强制四数相同）、`RM8_NPC_REEL_HUMAN=1` + `RM8_NPC_REEL_PRESS=秒`。
- 財神（`@0x49bee0`，str408 “財神收取別人的現金給你，請按鍵確定金額。”）/ 窮神（`@0x49c0b0`，str410）：`Lottery_InitScrollBar @0x49f810` 的滚轮对话框（`scrbar.ui` 4 个数字，故事模式 `sscrbar.ui` 6 个）；电脑 1.0 秒后自动停（+0xDE0），人类按键；金额 = 4 位数字（`Lottery_CalculateResult @0x49fb50`，0..9999；6 位时 ×(+1) 或 ÷），结果显示 1.2 秒（`@0x49f570`）；确认 `OnConfirm @0x49bf50 / 0x49c120`（经 `Role_UpdateStatusTransition @0x4860d0` 状态 10 → `Collection_FindFirstByVf70`）：財神：**每个其它未破产角色**现金优先扣 `金额`（`Role_SubMoney(金额, 0, 0)`），附身者现金加 `金额 × 人数`（`Role_AddMoney(.., 0, 1)`，带表情）；窮神相反（其它每人加、附身者扣）。
- 衰神：角色卡片数 +0x320 / 2 > 0 → 入队 `0x1B(str411)`、`0x05(-1, 张数/2)`（随机丢这么多张）。
- 福神：入队 `0x1B(str413)`、`0x04(-1, 1)` ×2。
- 土地公 / 糊塗神：无。

**只有走到神身上才有这些额外效果**；卡片（財神卡…糊塗卡 36..41）只发 `0x24`，角色直接获得附身，什么额外都没有。

## 5. 附身（`Role_SetPossession @0x48b6e0`，消息 0x24）

### 5.1 设置 / 清除

- 参数 0（送走）：`Npc_PlayAction(旧编号)`（对象 4 → 6，可以再被放出来）、清网格、+0x1BC = 0、按**当前交通工具**重载网格（`u_Role_CopyTrfToolValues`），速度 +0x34 = roleinfo `speed[交通工具 − 1]`。
- 参数 0x10（路霸）：不改形象；`+0x1C0` = **当前行动角色座位**，`+0x1E0` = NPC 16 的 date（5）；`return`（见 §6.4）。
- 其它（NPC_FindByID 找得到）：清网格，**用 NPC 的 art 列表重建网格**（`vf1 CreateMeshList(+0x20C, +0x208)`：神 1 个模型，警察 / 小偷 / 野狗 4 个朝向模型）= 角色**整个变成 NPC 的样子**（走路 / 站立 / 掷骰 / 表情动作的映射表 `0x5E1D94`：逻辑动作 0 1 2 → 0 1 2，3..13 → 2，14 → 3，`CPlayerTag__vf5 @0x48a270`）；+0x1BC = 编号；+0x1DC = NPC +0x228（date）；**速度 = roleinfo `speed[0]`**（不论交通工具）；`Role_FinalizeMovement @0x481af0` 重新摆放：缩放 = `CPlayerTag__GetStatusShape @0x490bf0`（npcinfo 缩放，**不是** roleinfo 缩放）× 同点补间比例；走路音效下标 0（`vf20 @0x48b1f0`）。
- 之后 `Role_SetFlag1C8`；`Game_SetPrice_0047e510(编号)`（只写一个 HUD 价格字段，无关）。
- **HUD 没有附身标志**（`+0x1BC` 只在上面这些函数、存档 `0x491fb0`、卡片条件里用到）。
- 单模型的神（1..6）没有朝向模型，所以角色脚下画一个**朝向箭头**：`CPlayerTag__InitializeNpcModels @0x4913c0` 建 8 个网格（art 6309 6308 6306 6314 6313 6312 6311 6310 = `mend\med_06_01..08`，红色箭头平面），`Path_CalcAngle` 里 vf14 `@0x4915e0` 按 8 个扇区选一个，`CPlayerTag_UpdatePossessedMeshTransform @0x491540`（位置 = 角色 + (0,3,0)，缩放 = (比例, sx·比例/sy, sx·比例/sz)），`CPlayerTag__RenderNpcModel @0x4915a0` 只在附身 1..6 时画。
- 100..103（踩蛋）：npcinfo art 是 15,16（A02 角色模型的占位），**没有真正的形象** [A：移植里保留角色自己的模型]。

### 5.2 持续时间与离开

回合开始步骤 6（`Role_RunTurnStartStep @0x4842f0` case 6）= `Role_TickPossessionExpire @0x483350` [V]：
1. 路霸：`+0x1C0 >= 0` 时 `+0x1E0` −1，< 1 → `+0x1C0 = -1`、`+0x1E0 = 0`。
2. 有附身：`Role_DecPossessionDays @0x48bcb0`（`+0x1DC < 100` 时 −1，< 1 则置 0 并返回 1；≥ 100 不动），到期且状态 ≠ 7：状态 8；入队 `0x1B(str433 "%s離開～@073"，%s = NPC 名)`；状态 < 3 入队 `0x2C(0x17EA, 0x1E)`（特效 6122 + 音效 30）；入队 `0x24(0)`（送走）；NPC 的 good（+0x22C）== 0 → 入队 `0x5A(0x18)`（说话）。步骤 6 之后照常入队 `0x67(9)`。

即：被附身的这一回合（获得时已在回合内）不算，之后第 1..date 个自己的回合开始各减 1，**第 date 个回合开始时离开**（财神等 7 个回合）。

`CPlayerTag__ClearPossessedGod @0x48edc0`（新闻“驱邪”事件用）：+0x1D8 = 0；附身 2 / 3 / 6 时 +0x1DC = 0（下回合开始即离开）；清炸弹。

### 5.3 对规则的影响（逐个函数核对，详见 `rules_land.md`）

| 编号 | 效果 | 函数 |
|---|---|---|
| 1 財神 | 付过路费 = 0（str407）；超市 / 饭店费用 = 0（str407） | `Role_PayToll @0x487c80`、`Hotel_ProcessVisitor @0x4f6c80` 状态 0xB |
| 2 窮神 | 过路费 ×2（str409）；超市 / 饭店费用 ×2 | 同上 |
| 3 衰神 | 买地：照扣价钱但**不得地**（str412，70% 说话 4 / 5）；加盖：照扣但不盖（str412 + 等 30 帧）；AI 的 TBuy / TBuild 返回 0 | `Role_BuyLandConfirmed @0x487030`、`Role_BuildOnLand @0x487380` |
| 4 福神 | 买地免费（str414）；停在**自己**的 < 5 级地上：`0x22` → 免费加盖（str415，`0xC(地块, b)`，空商业地 b = rand()%3+2）。停在无主 / 他人地上不入队 0x22 | 同上、`Role_ApplyGodOnLand @0x48bb50`、`@0x4868e0` 末尾 |
| 5 土地公 | 停在**不是自己的**地上（含无主）：`0x22` → str416 + `0xA(地块, 1)` 免费占地（房屋保留，70% 与原主互动）；无主地不问买（AI TBuy 返回 0） | `@0x48bb50`、`Role_TakeLandOwnership @0x487250` |
| 6 糊塗神 | 不问买 / 不问加盖；落点有事件图标也不触发（`Role_OnArriveNode @0x4861a0`）；掷骰自动（`Role_BeforeRollDice @0x484c80` → `Role_ShowSleepwalkAndRoll @0x48b140`：str564 “%s夢遊中\還有%d天～” + `0x1A(0)`）；回合开始步骤 9：状态 0、无“停留一回合”标志时直接夢遊；停留一回合时入队 0x12 而不是 0x1D；**他的地租别人不用付**（`Role_CheckRentExempt @0x486580`：地主状态 0 但附身 6 → 同〔string.bin#9〕，str9）；过银行不触发 | 各处 |
| 7 8 9 | 遇到同点角色时的动作（§5.4）；其它规则同未附身 | `@0x48bd00` |
| 路霸（16） | 地主的 +0x1C0 ≥ 0 且受益人未破产：别人付给**地主**的过路费改付给**受益人**（现金优先扣付款人、受益人存款加，str566 “〔string.bin#566〕，將地租送給%s”，60% 且地主状态 0 时 `0x2E` + 地主说话 0x57） | `Role_PayToll`、`Role_TransferRentRoadblock @0x488100` |
| 100 / 101 / 102 | 只有 `NPC_HandleInteract`（青蛙无视神，無敵超人无视所有 NPC）和卡片目标过滤（`Game_FindClosestRole @0x498170`）读它们 | |

### 5.4 附身警察 / 小偷 / 野狗（`Role_RunPoliceThiefDogAction @0x48bd00`）

走路经过点（§3.2 第 3 项）和停下（§3.1）都检查：附身 7..9 且 `CPlayerTag__GetRandomBankruptRole @0x48ebb0(2, 1)`（名字不对：随机选一个**同点（from 相同）、状态 ≤ 2、+0x1E4 隐形 < 1、不是自己**的角色）。命中则：清队列（附身者），并——
- 警察：目标附身 8（小偷）→ 入队 `0x3C(8, 目标+1)`（str426）；否则 `0x3C(rand()%8, 目标+1)`（str418..425，目标坐牢 3 天）。
- 小偷：自己点券 += 目标点券 / 2（立即），入队 `0x3C(9, 目标+1)`（目标丢一半）、`0x47`。
- 野狗：自己队列 `0x42(0xE)`（动作 14 = 咬）、`0x2C(0x177D, 0x3F)`、`0x42(2)`、`0x2E(目标+1, 1)`；**目标队列** `0x18(2, 0)`（住院 2 天）、`0x2E(自己+1, 0)`。
- 命中后若自己还有步数（+0x3C > 0）：`Role_ClearFlag1C8` + 入队 `0x7C`。

## 6. 其它 NPC 细节

### 6.1 定时炸彈（角色身上，+0x288）

`CPlayerTag__InitializeTarget(计数, 1) @0x48e2d0`（消息 0x3A）建 `Bomb_InitDraw @0x499b40` 对象：+0 = 计数；位置 = 角色 + 离开方向 × (−10)（`u_CPlayerTag_UpdateAttachedPosition @0x48e4b0`）；每帧 `Billboard_RenderWithNumber @0x499ef0`（`TimeBomb.tga` / `TimeBomb_01.tga` 两张 32×32 相机朝向方块，`%2d` 两位数字，字体 16 黑字）。踩地雷卡的 AI：`Role.TimeBomb(角色)` = `CPlayerTag__GetTargetData`（计数，0 = 无），`Role.TimeBombDis` = 消息 0x51 = `CleanupTarget @0x48e440`（送神卡 26 用）。计数每经过一个点 −1，最后一个点也减（§3.1 / §3.2），至 0 爆炸：`OnUseTargetCard @0x48e400` = `CleanupTarget`、`CleanupEventDisplay`（泡泡也炸掉）、`OnHolidayCardUse(所在点, 0x1775, 0x45, 0, 0, 7)`。

### 6.2 爆炸（地雷 / 炸弹 / 导弹卡共用 `CPlayerTag__OnHolidayCardUse @0x48de10(点, 特效 art, 音效, 半径, 参数, 基础天数)`）

受害者 = 半径内（`Terra_BuildNeighborList @0x49fd70`，半径 0 = 只有这个点）所有状态 < 3、**无泡泡**（+0x28C == 0）、无保镖的角色；行动者排第 0 个，其余按座位。每个受害者住院天数 = `Card__FilterSpecialCards @0x48dbe0`（该角色的卡里 id 16..19 的全部丢掉，**每张 +1 天**）+ 基础天数（地雷 3，炸弹 7）。入队（行动者）：若行动者是受害者：清队列 + `0x18(天数, 0)`；其他受害者各自队列 `0x18(天数, 0)` + `0x2E` 接力。队首依次插：`0x81(1, 座位+1)`（**交通工具回到步行**，`Role_SetTrfTool(1)`）、`0x62(点, 半径)`（`CPlayerTag__OnEventConditionCheck @0x48dc80`：清范围内泡泡），特效任务（状态 9）：`TerraMgr_AnalyzeRoleStocks @0x4f22e0`（特效 + 爆炸点范围内地块的破坏：第 5 个参数 = 0 的拆光建筑（地主不变，飞弹 / 地雷 / 炸弹），= 1 连地主一起清（核弹）；**与半径无关**，旧版写“半径 > 0 才破坏”是误读，2026-11 已改：地雷 / 炸弹爆炸会拆掉所在点的地块建筑，见 cards_port.md 第 11 节）。地雷在 `0x60` 里还会入队 `0x5A(0x3B)`（说话）。

### 6.3 泡泡 / 保镖

`Role.Bubble` = 消息 0x3E → `CPlayerTag__InitializeEventDisplay(10) @0x48e5f0`（+0x28C，音效 0x21）；回合开始步骤 4 倒计时（`Counter_DecrementAndCheckZero`）；`0x3F` = `CleanupEventDisplay`。**保镖**：角色 vf+0x64（vf25 `HasBodyguard`）在所有角色类里都是 `Obj_Return0_4f6230`（恒 0；特殊角色 13 / 16 的类 `Cls_5e27d8 / Cls_5e2878` 没有覆盖这一槽），`Role.HasBdygrd` 也恒 0，所以所有 NPC / 过路费 / 爆炸里的“有保镖 → 神离开 / 免付 / 免疫”分支都是**死代码**。`Role.Bodyguard`（保镖卡 46，CardPy_46）= 消息 0x45 → vf+0x80 `CPlayerTag__vf15_ShowState @0x48ecf0`：清队列、`0x2C(0x1787, 0)`（特效 6023 `eff\D46tB`）、`0x18(3, 0)`（**住院 3 天**）——即原版保镖卡的实际效果就是把使用者送进医院 3 天（[H] 设计残留 / 原版 bug）。

### 6.4 路霸（NPC 16）

不是板上对象。路霸卡（CardPy_34）：`Role.PlayPrt(目标, 6122, 30)`、`Role.Become(目标, 16)`（§5.1 参数 0x10）、`Role.Pause(目标, 30)`。= 目标（地主）+0x1C0 = 出牌者座位，5 个目标的回合；期间别人付给目标的过路费转给出牌者（§5.3 末）。

### 6.5 音效 / 字符串速查

| 用途 | 编号 |
|---|---|
| 神出现（OnClick 特效）/ 附身时 | 0x1E / npcinfo sound 55..60；问候语音 5301..5306；离开 073 |
| 放路障 / 地雷 / 炸弹 | 0x0C / 0x10 / 0x11 |
| 路障拦住 / 炸弹拾起 / 幸運卡片 / 寶箱 / 拼圖 | 0x46 / 0x11 / 0x41 / 0x42 / 0x41 |
| 地雷爆炸 / 炸弹爆炸 / 野狗 | 0x43 / 0x45 / 0x3F（咬）、0x40（汽车撞开） |
| 娃娃启动 / 封印 / Npc.Disappear | 0x0E / 0x23 / 0x23 |
| 滚轮 | 0x74（半音量） |
| 新闻语音 | 警察 061，小偷 062，野狗 063，幸運 064，寶箱 066 |

| string | 内容 |
|---|---|
| 401..406 | 神的问候（@5301..5306） |
| 407 / 409 | 財神免租 / 窮神加倍 |
| 408 / 410 | 滚轮标题（財神收钱 / 窮神破财） |
| 411 / 413 | 衰神丢卡 / 福神得卡 |
| 412 / 414 / 415 / 416 | 衰神〔string.bin#412〕 / 福神全免 / 福神免费加盖 / 土地公占地 |
| 417 | 夢遊（`%s夢遊中` 是 564，417 无直接引用） |
| 418..426 | 警察新闻 0..8（8 = 警察抓小偷） |
| 428 / 431 / 432 | 小偷 / 幸運卡片 / 寶箱 |
| 430 | “〔string.bin#430〕，%s被咬傷住院2天”（无直接引用，[H] 医院场景用） |
| 433 | `%s離開～` |
| 434 | `%s獲得%d號拼圖` |
| 564 / 566 | 夢遊 / 路霸 |

### 6.10 野狗咬人与住院 / 入狱演出（2026-11 复核）[V]

野狗（走路 / 机车，`BeginAttach @0x49caf0`）：狗 `Finalize`（立即消失，**没有狗自己的动作**），清队列，入队 `0x2C(0x1788, 0x3F)`、`0x18(2, 0)`。`0x2C` = `Role_ShowEffectAnimAtSelf @0x48b2b0`：特效艺术 0x1788 = 6024 = `article\eff\N09m.dff`（一团打架的烟尘，里面有狗，动画 0.667 秒）+ 音效 63，之后等 60 帧。**`0x18` 不是立刻住院**：`@0x481df0` case 0x18 → `NPC_CreateRandom_Type2 @0x4a45f0`（`Stock_InitializeObject_Type2 @0x4a84f0`）建任务对象 `Cls_5e3b90`（存在 `g_GameState + 0x34`，角色状态 9）：**救护车**（art 0x527 = 1319 `element\Mys119`，缩放 0.5，动画 7.8 秒，警笛音效 0x48 循环，时间 2.8 / 7.5 秒）。`CGamePlayScene__Game_OnUpdate @0x4a17b0` 每帧调 `RoleAnimation_UpdateFrame @0x4b1370`：车开到角色旁；**2.8 秒**调 `RoleAnimation_TriggerAction @0x4b1470` = 角色 vf+0x6c `@0x48ceb0`（`Role_ClearFlag1C8` + `Role_ResnapToNode`：角色被抬走、不画）；**7.5 秒**调 `Cls_5e3b90__vf18 @0x4b15d0`：角色 vf+0x70 = `CPlayerTag__SendToHospital @0x48a570`（挪到医院点、状态 4、`Role_SetFlag1C8` 重新可见、末尾说话 4）、相机对焦、停警笛，返回 0x80000000 → `Game_EndTurn @0x4a48d0`（所有角色状态 9 → 8）。**坐牢（0x19 / 0xC9）同构**：`Type1 @0x4a4510`（`@0x4a83c0`）= **警车**（art 0x526 = 1318 `Mys118`，缩放 0.5，动画 4.33 秒，音效 0x47，时间 1.8 / 5.0 秒）→ `Cls_5e3af0__vf18 @0x4b1500` → `SendToPrison @0x48a9c0`。别人 / 地雷 / 炸弹 / 附身警察 / 附身野狗造成的住院入狱走同一条消息，非当前角色靠 0x2E 接力（受害者队列 `0x18` 在前、`0x2E(攻击者+1, 0)` 在后；受害者演出时攻击者状态 0 等待）。移植：`cards.cpp` 的 0x18 / 0x19 / 0xC9 → `startTransfer` → `rm8NpcTransfer`（`npc_act.cpp` 的 `Transfer`：加载模型、`placeMdl` 缩放 0.5、动画 0 播一遍、音效循环、1.8 / 2.8 秒隐藏角色、5.0 / 7.5 秒调用 `sendToPrison / sendToHospital` 回调并恢复显示）；`npcs.txt` 增加 1318 / 1319 的模型和动画（`export_npcs.py`）。验证：`RM8_NPC_PLACE="9@1" RM8_NPC_GO=1 RM8_AUTO_ROLL=1 RM8_SHOT_FOLLOW=1 RM8_DEBUG_NPC=1` 日志 “transfer 2 … starts / picked up / arrives”；全电脑 400 秒模拟（附身野狗咬非当前角色，3 张图）无卡死，受害者住院流程完整。`RM8_NPC_NO_TRANSFER=1` 关闭演出（对照用）。

### 6.6 拼圖（NPC 17）

只在**故事模式**：`Event_ProcessLanding @0x49db20`（`Npc_SpawnAll` 末尾）：故事模式标志（`*(0x632de8)`）、座位 0 角色 id（+0x300）→ 故事对象 `DAT_00632db0[id-1]`，找 +0x3A0 数组里第一项 == 当前关卡编号的，取它的 +0x28 / +0x2C 两个拼图号（= `pass_XX.bin` 的 `start_pic`，如 `4,5`），号 > 0 且 `Event_GetItemData(角色 id, 号) == 0`（这个存档还没拿到）→ `Event_SpawnNpcInstance @0x49dd20(号)` 在 `TerraMgr_FindAvailableNPC` 点放一个。座位 0 停上去：音效 0x41，`0x58(号)` = `CPlayerTag__OnPuzzleCardUse @0x491830`：`Event_SetItemData(角色 id, 号)`，文字 str434（带拼图图 `(id−1)×9 + 0x12D + 号`），拼图对象状态 5。通关奖励 `reward_pic` 是故事模块的事。自由模式没有拼图。

## 7. 消息一览（NPC 相关）

| 消息 | 处理函数 | 含义 |
|---|---|---|
| 0x18(天数, 0) | `NPC_CreateRandom_Type2` | 住院（cards 模块） |
| 0x19(天数, 0) | `CPlayerTag__OnMissileCardUse` / Type1 | 坐牢（cards 模块） |
| 0x1E | `Camera_SetFocusPoint(角色)` | 镜头 |
| 0x23(点) | `NPC_HandleInteract` | §3.1 |
| 0x24(编号) | `Role_SetPossession` | §5 |
| 0x2C(art, 音效) / 0x2E / 0x53 / 0x5A | present.cpp | 特效 / 接力 / 音效 + 等待 / 说话 |
| 0x32 | `OnQandaEvent` | 机器娃娃启动 |
| 0x3A(计数) / 0x51 | `InitializeTarget` / `CleanupTarget` | 炸弹装 / 拆 |
| 0x3C(事件, 座位+1) | `OnLotteryEvent` | §3.4 |
| 0x3E / 0x3F | 泡泡 | cards 模块 |
| 0x42(动作, 次数) | vf5 | 角色动作 |
| 0x58(号) | `OnPuzzleCardUse` | 拼图 |
| 0x59(下标, 0/1) | `OnWonderConstruction` | 封印 |
| 0x60 | 地雷爆炸 | §6.2 |
| 0x62(点, 半径) / 0x81(交通工具, 座位+1) | `OnEventConditionCheck` / `Role_SetTrfTool` | 爆炸后处理 |

## 8. 移植实现

**文件**：`npc.cpp`（数据、对象、放置、每月刷新、动画、绘制、附身形象、API）、`npc_act.cpp`（各类互动、消息、爆炸、新闻、娃娃、炸弹、测试钩子）、`npc_ui.cpp`（问候文字、滚轮对话框、炸弹计数）。`scripts/export_npcs.py` → `scenes/npcs.txt`（`android/deploy.py` 已加入推送列表）。

**公开接口**（`npc.h`）：`rm8NpcCreate / Remove / At / Id / Shape / Become`（cards.cpp / python.cpp 已在用，句柄 = 数组下标 + 1）；`rm8NpcCount / OnMap / Node`（封印选目标用）；`rm8NpcLoadDefs / SetCamera / NewGame / Clear / Update / Render / RenderLate / MonthStart / Story`；回合钩子 `rm8NpcInteract / Pass / OnStop / TurnStep / Sleepwalk / Message / Press`；规则 `rm8NpcBully / TimeBomb`；形象 `rm8NpcRoleShape / RoleUpdate / RoleRender`。

**共享文件改动**：见 §9。**2026-11 修复新增钩子**：`RM8_NPC_SPAM=n`（n 次重放 `Event_OnNpcEnable` 并记录选点）、`RM8_NPC_DOLL=1`（0 号座位开局放娃娃）、`RM8_DEBUG_NPC_ARROW=1`（打印 8 个地面箭头模型的实际指向与扇区方向）、`RM8_NPC_NO_TRANSFER=1`、`RM8_NPC_REEL_STORY / CHEAT / HUMAN / PRESS`。

**与原版的差异 [A]**：
- ~~新闻 TV 没做~~：已由 `news.cpp` 实现（docs/news.md）：`newsEvent` 现在调用 `rm8NewsStart`（电视机 + 主播 + 3D 图 + 标题 + 效果 + 3.8 × 速度系数 秒）。
- 财神 / 窮神的滚轮：数字用 0.07 秒换一次的随机数字代替“滚动字符动画”（`CUi_UpdateCharAnimation @0x410b30`），电脑 1.0 秒自动停，人类点对话框或空格 / 回车停，每个轮 0.25 秒停一个；金额分布 0..9999 均匀。
- 爆炸的 `0x62`（清范围内泡泡）、对地产的破坏（半径 > 0）、住院本身（0x18）不在这里（见 cards）。
- 100..103 踩蛋形象；故事模式拼图只提供 `rm8NpcStory(号[2])` 入口；炸弹计数用 2D 方块（原版 Im3D 方块）。
- 机器娃娃转弯时的转身动画（`Path_CalcAngle` param_3 = 1，540 度/秒，90 度处换模型）已按原版移植；原版娃娃经过有角色的点时还会像角色一样让站在点上的角色缩小让开（`Game_RoleInteract / Game_RoleInteractEnd`），移植没做。“NPC 倒着走”：复核 `MiniGame_StartWithRole @0x49d250` / `Path_AnimateAlongRoute @0x47b730` / `Path_SelectRandomNext @0x47b8e0` 后娃娃的方向规则（从角色 from 朝 to，到点随机选非来路、非死胡同链、连接数 ≥ 2 的邻点，无路可选才折返）与移植一致，没有找到反向的代码路径；确实不一致的有三处已修：单模型形象会继承角色的转身角、`Path_CalcAngle` 缺 `模型数 < 4` 条件、娃娃没有转身动画。[H] 用户看到的“倒着走”可能来自这些（神形象在转弯时整个转一圈）。
- 神互动时“相机忙”（`DAT_0061ae00 +0x20 / +0x2C / +0x38`）检查没有（移植相机瞬时对焦）。
- 野狗动作 5（飞走）在模型里不存在（动画只有 0..3），保持站立姿势移动。

**测试钩子**（环境变量，见 `npc_act.cpp` `actNewGame`）：`RM8_NPC_PLACE="id@k,.."`（在 0 号座位前方第 k 个点放 NPC，k = 1 是它第一个走到的点）、`RM8_NPC_MOVE="座位@k"`、`RM8_NPC_BECOME="座位:编号[:天数]"`、`RM8_NPC_VEHICLE="座位:交通工具"`、`RM8_NPC_BOMB="座位:计数"`、`RM8_NPC_SEAL=id`、`RM8_NPC_GO=n`（0 号座位第一次必掷 n）、`RM8_DEBUG_NPC=1`（日志，带游戏内时钟）。截图：`RM8_SHOT_FOLLOW=1`（`--shot` 时镜头看回合流程的焦点）。例：`RM8_NPC_PLACE=1@1 RM8_NPC_GO=1 RM8_SHOT_FOLLOW=1 rm8_app.exe <游戏目录> scenes --map 01 --size 1280x720 --time 9 --cam 225 40 0.06 --shot out.png`。

## 9. 共享文件改动（均为小段插入）

- `turn.h`：`rm8TurnSelectNext`、`rm8TurnPossessSync` 两个声明。
- `roles.cpp`：`#include "npc.h"`；`onStop`（炸弹 / 附身警察小偷野狗）、`animateAlongRoute`（经过检查：炸弹 / 路障 / 附身警察小偷野狗，在银行检查之前）、`placeModel`（附身时用 NPC 缩放）、`beforeRollDice`（糊塗神夢遊）、`dispatch` 的 0x67 步骤 6（`rm8NpcTurnStep`）和步骤 9（糊塗神夢遊）、`mainUpdate` 的状态 10（NPC 演出中不取消息）、`rm8RolesLoadDefs`（读 `npcs.txt`）、`rm8RolesClear` / `rm8RolesSpawn`（`rm8NpcClear` / `rm8NpcNewGame`）、`rm8RolesSetCamera`、`rm8RolesUpdate`（每个角色 `rm8NpcRoleUpdate` + `rm8NpcUpdate`）、`rm8RolesRender`（NPC 先画；附身角色画 NPC 形象）、`rm8RolesRenderLate`、`rm8RolesHumanRoll`（滚轮按键）；文件末尾 `rm8TurnSelectNext` / `rm8TurnPossessSync`。
- `game.cpp`：`#include "npc.h"`；`rm8GameMessage` 里 cards 之后 `rm8NpcMessage`；`0x1D` 糊塗神不触发事件图标；`arriveLand` 的 A / B 分支改成 `if / else if` 链（原来的提前 `return` 会跳过末尾的 `0x22`，福神 / 土地公就没有效果）；`arriveCommercial` 超市 / 饭店费（財神 0、窮神 ×2）；`0x9` 买地（衰神 / 福神）、`0xB` 加盖（衰神）、`0x1F` 过路费（財神 / 窮神 / 路霸 / 免付表情）、新增 `0xA`（占地）和 `0x22`（福神 / 土地公）；`rm8GameOnNewRound` 月初 `rm8NpcMonthStart`。
- `rm8_app.cpp`：`--shot` 时 `RM8_SHOT_FOLLOW=1` 让镜头看回合流程的焦点（测试用）。
- `CMakeLists.txt`：两个源文件列表加 `app/npc_act.cpp app/npc_ui.cpp`；`android/deploy.py`：共享文件元组加 `npcs.txt`。
- 2026-11 追加：`ui.cpp / ui.h`：`Widget.uv[4]` + `rm8UiSetPicFileUV`（图集格子，滚轮对话框用）；`roles.cpp`：`rm8RolesRender` 里 `rm8NpcRoleHidden(seat)` 时不画角色（+0x1C8）；`cards.cpp`：0x18 / 0x19 / 0xC9 经 `startTransfer`（救护车 / 警车演出，`rm8NpcTransfer`）；`npc.h`：`rm8NpcTransfer / rm8NpcRoleHidden`；`scripts/export_npcs.py`：加 1318 / 1319。
- 2026-11 追加（地雷火花 / 警车救护车灯光）：NPC 模型不再用 `clump->render()`，改走文章渲染路径 `rm8RenderArticleClump`（scene.cpp，`Atomic_ParseUserDataFlags @0x45c370` 的 `$ * ( ) ...` 标志、两遍 `0x45c6a0`）。地雷 art 1059（N14）有 `[@1]` 钩子帧，role.bin 粒子表 hook 1 → 特效 20（引信火花），`export_npcs.py` 输出 `hook <art> <hook> <eff>`，`loadMdl` 用 `rm8PrtAttachH` 挂发射器，`freeMdl` 停掉。警车 1318 的原子 6 为 `[$*)]`（不剔除、第二遍、SRCALPHA/ONE 的灯光面片），救护车 1319 的原子 13 / 14 为 `[)*]`；转运演出的车用文章路径并按 [A] 以场景的低 alpha 参考（10）绘制（角色通道的 >= 196 会吃掉半透明灯光）。调试：`RM8_DEBUG_NPC_FLAGS=1` 打印每个原子的用户数据与贴图。
