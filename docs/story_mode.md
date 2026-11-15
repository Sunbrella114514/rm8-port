# 闯关（故事）模式：逆向文档与移植设计

范围：大富翁8 的「故事模式」（主菜单 1「单人」，`setup+0 == 1`）：流程、关卡配置、储藏箱、胜负与结算、进度与解锁、剧情规则例外、过场动画引擎（Movie / CTime），以及移植方案。第 1~3 节是逆向与设计；**第 4 节是移植侧的实现状态、验证与待办**（P0~P5 做到哪一步、哪些是假设）。
标记：`[V]` 已读代码 / 反汇编核实（附地址）；`[M]` 读了代码但语义是推断；`[H]` 假设；`[A]` 移植侧的取舍。函数名以 `out/final_v2/functions.tsv` 为准，但其中不少名字是错的，本文按代码行为改名并同时给出地址。
反编译来源：`out/final_v2/RichMan8_named.c`。本轮新增的辅助脚本（只读分析，输出在 `out/story/`）：

| 脚本 | 输出 | 用途 |
|---|---|---|
| `scripts/story_users.py <串>` | 终端 | 列出函数体含某串的函数 |
| `scripts/story_setup_sites.py` | `out/story/setup_sites.txt` | 所有 `Singleton_GetPtrArray_00632de8()`（setup 块）调用点及后三行 |
| `scripts/story_callers.py <地址…>` | 终端 | 直接 `call` 的调用者（capstone 式扫描 E8 指令） |
| `scripts/story_vt.py <vtable> [n]` / `story_vtdiff.py` | 终端 | 带名字的 vtable 转储 / 三个角色类 vtable 对比 |
| `scripts/story_pass_table.py` | `pass_table.txt`、`pass_all.json` | 16 个 pass 文件整理成表 |
| `scripts/story_missions.py` | `missions.md/.json` | 把 `Role_CheckMissionsByCharacter` 解析成「角色 × 层 → 拼图 / 条件」表 |
| `scripts/story_movie_stats.py`、`story_art_check.py`、`story_check_snd.py` | `movie_stats.json`、`movie_arts.tsv` | 40 个过场脚本的调用统计；所用 art / 声音 / 音乐 / 字符串是否存在 |

---

## 0. 结论速览与对旧文档的修正

1. **故事模式 = 4 层关卡**：第 0 层固定一张图；第 1 层从 3~4 张里选 1 张；第 2 层从 3~4 张里选 1 张（忍太郎没有第 2 层）；第 3 层固定为魔都（巴黎，map 3）与莎拉公主决赛。打完第 3 层播结局影片，莎拉解锁。关卡表在 `pass_XX`（XX = 玩家所选角色号），`level` 字段 0..3 就是这个层。
2. **`setup+8` 不是 0/1 的剧情标志，而是「当前关卡层」0..3**（= 所选 pass 条目的 `level`）。`Cls_5e81e0__vf10_OnConfirm @0x503e90` 写入 [V]。`docs/save_format.md` 里 setup `+0x08`（样本 3/1）和 sflb 的 `f_00`（样本 3）因此都是关卡层，`menus.md` 里「+0x08 剧情标志（0/1）」要改。
3. **「9 个通关标志」其实是 9 块拼图的拥有标志**：`Event_GetItemData(role, 1..9) @0x4aa2e0`，由 `Event_SetItemData @0x4aa300`（唯一调用者 `CPlayerTag__OnPuzzleCardUse @0x491830`）置位；`Event_CountOwnedItems @0x4aa290` 数 9 个。角色的**闯关进度**只有 `stage` 0..4（`Event_GetLevel @0x4aa230`）。`scripts/gamedate.py` 的 `cleared` 字段应读作 `pieces`。
4. **`record_NN.sav` 才是故事存档，`record_mNN.sav` 是自由模式（多人）存档**：`setup+0 == 0` 时用 `record_m%02d`（`CPlayerTag__SaveGameRecord @0x492850`、`Game_DisplaySaveSlotList @0x4ac420`、`CSaveSlots_vf16 @0x50cf20`、`CSaveSlots_vf12 @0x50d490`、`Game_SwitchRecordSlot @0x4a7380` 均 [V]）。任务书里「record_mNN = 故事存档」是反的；`save/` 里现有的 `record_01~08` 都是故事档（setup 标志 1，带 sflb 段）。
5. **`Movie.SetCameraPos(x,y,z,a,b)` 的 a、b 是俯仰角和偏航角（度），不是移动 / 旋转速度**：`Camera_SetOrientationFromAngles @0x41b1f0`（经 thunk @0x443aa0）瞬时设置，没有补间 [V]（2.4）。
6. **`Movie.RoleCreate` 有 5 个参数**：`(roleIndex, x, y, z, type)`，格式串 `"(ifffi)"` [V @0x5125d0]；`roleIndex` 是 roleinfo 数组下标（**0 起，= 角色 Id − 1**，da17_2 里 0..15 全员），`type` 0 / 2 / 3 = 步行 / 机车 / 汽车模型组，1 = NPC 表（脚本里没用到）。
7. **`Movie.RoleSetAnim(h, 动作号, 次数)`**：第三个参数是 role.bin 动作的播放次数（0 = 循环，1 = 一次，n = n 次 [H]），不是「帧」。
8. **过场可以点击跳过，但只跳过当前这一个 `daXX_N` 脚本**：`CApp_OnMouseButton @0x4059f0` 在鼠标左键按下（code 0x110）时调用场景 vtable `+0x34` = `Cls_5dff48_vf13 @0x515b50` = `Movie_ReleaseAndRestoreView` [V 反汇编]；队列里的下一段随即开始（2.5）。
9. **`Event_SetEventData @0x4aa1b0` 在整个 exe 里没有调用者**：自由模式第 3 张图（巴黎）的解锁标志（gamedate 的 flagA，magic 0x2E1）只能来自 `gamedate.bin` 文件本身，游戏代码从不会写它。

完整度总览见第 1.16 / 2.7 节。

---

## 1. 整体流程

### 1.1 状态机总图

```
主菜单 单人(setup+0=1) → single 菜单(状态 8) → 1 新游戏
  → 角色菜单(状态 2)            选 1 个角色（只允许恰好 1 个座位）[0x506a30]
        └ 选角色时：Game_ClearBinaryData → 储藏箱和「关卡层」清零 [0x5065f0]
  → 地图菜单(状态 3, chsMapMenu 状态 1)    只显示当前层的候选图 [0x503510]
        ├ 0x13 按钮 → 储藏箱(boxmenu, 状态 0xB) → 关闭回地图菜单 [0x503d20]
        ├ 返回(0xC)  → 把「待带入」合并回箱子后回角色菜单；通关过一层后不可返回 [0x504310 / 0x5042a0]
        └ 确认(0x76) → OnConfirm [0x503e90]：setup+8 = 层，补对手座位
              层 0：主持人开场（状态 2→8）→ 0x65 → CApp_PlayMovieChapter(角色−1) 过场（start 段）→ Movie_Finish → CApp_StartNewGame
              层 >0：主持人介绍对手（状态 5→8）→ 0x65 → CApp_StartNewGame
  → 棋盘（Game_InitGame → Game_StartNewRound：套用 pass 的起始钱 / 卡 / 储藏箱带入物）
        ├ 通关 0x75(b≠0) → 0x7e 胜利特效 14s → 0x7d 奖励宝箱 → 0x7a Game_OnStageSelect → 游戏状态 4 或 5
        │      状态 4 → 地图菜单（状态 0xB）：主持人「恭喜闯关成功…请任选一关」，选下一层
        │      状态 5（刚打完第 3 层）→ 解锁莎拉 → CApp_PlayMovieRange(角色−1, 1) 结局过场 → Staff.bik 片尾 → single 菜单
        └ 失败 0x74 → 「是否接關？」okcancel → 0x76：确认 = 0x77 重开本关（状态 2）；取消 = 状态 3 回角色菜单
```

游戏场景状态 `g_GameState+0x48`（`CGamePlayScene__Game_OnUpdate @0x4a17b0` 的分派）[V]：1 进行中；2 重开本关（`Game_Cleanup; Game_UnloadAllResources; Game_LoadAllConfigTables; Game_InitGame`）；3 退出（自由模式回地图菜单 `SwitchToGameScene(3)`，故事回角色菜单 `(2)`）；4 本层通关（自由 → 3；故事 → `(4)`：地图菜单状态 0xB）；5 全部通关；6 / 7 存档 / 读档（`Game_WriteRecordToFile @0x4a7540` / `Save_LoadRecord @0x4a78f0`）。

### 1.2 数据结构

**setup 块** `&DAT_00632de8`（`Singleton_GetPtrArray_00632de8 @0x4083f0`），故事相关字段 [V]：

| 偏移 | 故事模式下 | 备注 |
|---|---|---|
| +0x00 | 1 | 主菜单 1 设置；测试点全表见 1.12 |
| +0x04 | 所选地图 stage id | 选图时写；`Game_InitGame` 载入 |
| +0x08 | **关卡层 0..3** | `OnConfirm @0x503e90` 写入 = 命中的 pass 条目的 `+4` |
| +0x0C/+0x10/+0x14/+0x18/+0x1C | 100000 / 50 / 0 / 2 / 5 | `Game_InitOptions @0x5031c0` 复位后不再改（故事隐藏选项行）；`Role_InitState @0x480d30` 仍用它们做初值，随后被 pass 覆盖（1.5） |
| +0x20 / +0x2C | 座位数组 / 个数 | 每项 12 字节 `{角色 Id, 服装, 0 人类 / 1 电脑}`；玩家在 0 号；对手由 OnConfirm 追加，服装 0、电脑 |

**故事运行对象** `&DAT_006405d0`（`Game_GetBinaryDataManager @0x494110`，名字不准，应叫 StoryRun / 储藏箱）[V]：

| 偏移 | 内容 | 读写 |
|---|---|---|
| +0x00 | `level`：本次运行已打到的层（只增，`Game_UpdateRecordStats @0x4abe20` 取 max） | 初值 0（选角色时清零） |
| +0x04 → 指向 `{value, pending}` | **储藏箱现金**：`value` 在箱里；`pending` = 本关要带入的现金 | `SFLB_IntMalloc(2,..)` 分配 8 字节（防改器的包装，语义就是两个 int） |
| +0x08 → 指向 `{value, pending}` | **储藏箱点券**，同上 | |
| +0x0C..+0x2B | 8 格储藏箱卡片（卡 Id，≤ 0 = 空） | `CardArray_FindSlotAndAdd @0x4abed0` / `u_CardArray_RemoveAt @0x4abf20` |
| +0x2C..+0x4B | 8 格「待带入」卡片 | `SlotArray2C_FindSlotAndAdd @0x4abf50` / `RemoveAt @0x4abfa0` |

存取：`Game_SaveBinaryData @0x4abd60` / `Game_LoadBinaryData @0x4abdc0` 写读 `level(4) + cash{value,pending}(8) + tickets{value,pending}(8) + 0x40 字节卡片区（+0x0C..+0x4B：箱 8 格 + 待带入 8 格）`，共 0x54 字节 = 存档的 sflb 段（样本 84 字节吻合）；清空 `Game_ClearBinaryData @0x4abcc0`（唯一调用者是角色菜单的选角色 `@0x5065f0`，所以每次新开一局故事都清箱；只有读故事存档才会恢复箱子）；`u_Obj_ApplyPendingChanges @0x4ac020`（把 pending 现金 / 点券并入箱子、待带入卡片放回空格并清空 pending，唯一调用者 `Cls_5e81e0_ShowGameMenu @0x504310` 即地图菜单「返回」）；`u_Obj_ResetSlotArrays @0x4abfd0`（只清 pending 和待带入卡片，调用者 `Game_StartNewRound @0x4962a0` 与上一个函数）。

**gamedate 对象** `&DAT_0061ae08`（`GameDate_Load @0x4a99e0` 启动时读一次，`GameDate_Save @0x4a9d60` 在每个 setter 里立即写；文件格式见 `docs/README.md 2.5` 与 `scripts/gamedate.py`）[V]：

| 内存偏移（对象） | 方法 | 含义 | 文件 int 序号 |
|---|---|---|---|
| `[r*0x40]` | `Event_GetTerrainData(r) @0x4aa1e0` / `Set @0x4aa200` | 角色 r 已解锁（1..10 初始化即为 1，11..16 由文件决定） | 0..5（角色 11..16） |
| `[r*0x40+4]` | `Event_GetLevel(r) @0x4aa230` / `Event_SetMaxLevel @0x4aa250`（取 max） | 角色 r 的 **stage 0..4**；≥ 4 = 通关过 | `6+(r−1)*10` |
| `[r*0x40+0xC+k*4]`，k=1..9 | `Event_GetItemData @0x4aa2e0` / `SetItemData @0x4aa300` | 角色 r 的第 k 块拼图 | `6+(r−1)*10+k` |
| `+0x800+t*4`，t=1..11 | `Event_GetEventByType @0x4aa190`（唯一读者 `Game_PopulateMapList @0x503510` 自由模式分支） | 自由模式地图 t 是否可选；初值 1，第 3 张 = 0；文件 flagA（int 166，magic 0x2E1）= 第 3 张；flagB（int 167，0x112）= `+0x830`（t = 12，无人读） | 166 / 167 |
| `+0x844..+0x864` | `Event_GetQuestData @0x4aa5b0` / `SetQuestData @0x4aa5d0`（取 max，上限 9999） | 9 个计数；唯一写者是小游戏宿主 `MiniGameHost_Cleanup @0x50e300` / `UnloadGame @0x511560`，下标 = 小游戏号 − 0x11 [H：每个小游戏的最高分] | 168..176 |
| `+0x880` | `Event_SetBitFlag @0x4aa380` / `CheckBitFlag @0x4aa350` | 11 位「连锁超市」位图，全 1 → 解锁 15 | 177 |
| `+0x884` | `Event_IncrementCounter @0x4aa420` | 舞美拉「怪兽出现」计数（每次怪兽真的出现过一回合），> 3 → 解锁 13 | 178 |
| `+0x888` | `Event_SetFlag2 @0x4aa470` | 4 位，全 1 → 解锁 11 | 179 |
| `+0x88C` | `Event_SetFlag3 @0x4aa510` | 3 位按序，第 3 位 → 解锁 14 | 180 |

文件 int 序号即 `scripts/gamedate.py` 的 181 个 int；布尔 / stage 是魔数表值（`gamedate.py tables`）。

**pass 条目**（`Config_LoadPassConfig @0x4950e0`，每个 roleinfo 一个数组 +0x3A0，个数 +0x3AC；条目 0x50 字节）[V]：

| 偏移 | 字段 | 用途 |
|---|---|---|
| +0x00 | `stage` | 地图 id；`Container_GetSubElement @0x495780(角色号−1, stage)` 按它查条目 |
| +0x04 | `level` | 层 0..3；`OnConfirm` 把它写进 `setup+8` |
| +0x08/+0x0C/+0x10 | `role` 三个对手 | 0 = 无；1..16 = 该角色；**负数 = 随机 `rand()%15+1`（只有 pass_16 的魔都，且可能与玩家或彼此重复，无去重）**；> 16 忽略 |
| +0x14/+0x18 | `start_card` | 开局送的卡 Id（0 = 无） |
| +0x1C | `start_cash` | 玩家现金 = 储藏箱带入现金 + 它 |
| +0x20 | `start_fund` | 玩家存款 |
| +0x24 | `start_ticket` | 玩家点券 = 带入点券 + 它 |
| +0x28/+0x2C | `start_pic` | 本层要在地图上放的拼图（由 NPC 17「拼图」承载），已拥有的不放：`Event_ProcessLanding @0x49db20`（`Npc_SpawnAll` 调用；移植已有 `rm8NpcStory`） |
| +0x30 | `reward_cash` | 通关奖励现金 → **加进储藏箱现金**（`@0x48f9d0`） |
| +0x34 | `reward_tichet` | 通关奖励点券 → 储藏箱点券 |
| +0x38..+0x44 | `reward_card` ×4 | 通关奖励卡 → 储藏箱空格（满则丢） |
| +0x48/+0x4C | `reward_pic` | 通关给的拼图，已拥有的不给：入队 `0x58(拼图号)` |

节名（城市名）不被读取。`config_files.md` 里 `reward_*` / `start_pic` 的「推测」可以改为 [V]。

### 1.3 菜单流程（逐步）

1. **主菜单**「单人」→ `setup+0 = 1`，进 single 菜单（`Cls_5e8190__vf12_OnMessage @0x508b50`，已在 `menus.md`）。single 的 1「新游戏」→ **角色菜单**（`Cls_5e8000_vf12 @0x50db90`）；2 读档；3 拼图图鉴；4 影片欣赏。
2. **角色菜单**（状态 2，`Cls_5e8140`）：
   - 打开时（`@0x505940`）：`Event_GetTerrainData(1..16)` 为 0 的角色按钮禁用（11..16 隐藏角色，未解锁灰掉）；`Stock_ClearAllData()`。
   - 点角色（`@0x5065f0`）：最多 `local_8 = 4` 座（小游戏 2；故事也可以多点，但开始按钮要求恰好 1 座），新座位 `{角色, 服装, 是否电脑 = 已有人类 ? 1 : 0}`；**服装 = 0..min(stage,3) 里第一个同角色没用的号**（stage 即 `Event_GetLevel(role)`）；随后 `Game_ClearBinaryData()`（清箱 + 层）。服装按钮 `Cls_5e8140_UpdateRolePanel @0x505a80`：服装 n（0..3）在 `stage ≥ n` 时可选，服装 4 在 `stage ≥ 3 且拼图 9 块齐` 时可选（按钮 0x17 / 0x21 由 roleinfo `+0x304` 决定）。这一块自由模式也用。
   - 开始（`@0x506a30`）：**故事必须恰好 1 个座位**（`iVar2 == 1`）→ `Ui_SetActiveMenuObject(3)`。返回（`@0x5069b0`）→ 状态 8。
3. **地图菜单**（状态 3，初始子状态 1）：
   - `OnShow @0x503100`：`setup+4 = 0`；`Game_InitOptions` 复位选项并**隐藏 6 行选项、显示储藏箱按钮 0x13**（`@0x5031c0`）；`Game_PopulateMapList` 取候选图；`u_RoleSel_SelectUniqueMatchEntry @0x504cd0`：若当前层恰好 1 张候选（层 0、层 3），自动选中。
   - **候选图算法**（`Game_PopulateMapList @0x503510` 故事分支）[V]：`L = min(run.level, 3)`；在玩家角色（座位 0）的 pass 数组里收集 `level == L` 的条目的 `stage`；一个都没有就 `L++` 并调 `Game_UpdateRecordStats(L)`（`run.level = max(run.level, L)`，**同时 `Event_SetMaxLevel(角色, L)` 把 gamedate 的 stage 也推到 L 并存盘**）再找（忍太郎的 pass_04 没有层 2，直接进魔都）。候选存进菜单对象 +0x10..+0x1C（最多 4 个），启用对应地图按钮。
   - 点地图 1..11：只记下 `setup+4`（`@0x503d20`）。0x13 → 储藏箱（`Ui_SetActiveMenuObject(0xB)`）。返回 0xC：**`run.level > 0` 时不可返回**（`OnBack @0x5042a0` 返回 0）；`level == 0` 时 `ShowGameMenu @0x504310` 先 `ApplyPendingChanges` 再回角色菜单。
   - 确认 0x76 → `OnConfirm @0x503e90`：要求 `setup+4 > 0`；把座位数组裁成只剩玩家；在玩家的 pass 里找 `stage == setup+4` 的条目，`setup+8 = level`；对 3 个对手：`role < 0` → `rand()%15+1`；`1..16` → 追加座位 `{role, 0, 1}`；0 / > 16 跳过。随后 `setup+8 == 0 ? 子状态 2 : 5`。
   - **主持人演出**（`Cls_5e81e0__vf1_OnUpdate @0x503830`）：主持人是 role.bin art 1048（`NPC\N23_act01.dff`，`Cls_5e81e0_LoadMesh @0x502d90`，位置 (100,−100,500)、转 180°），台词 = `string.bin [主持人資訊] 3801..3835`，语音 = 同号样本 `snd03801..03835.wav`（`Game_PlayMovie @0x504460`：先停上一句，文字进标签 0x34，播样本 `StringTable+32000+id*4`，句柄存 `this+0xC`，主持人动作号 = 第二参数）。**节奏**：`this+0xC != 0`（语音在播）时计时器不走；语音播完的回调 `Cls_5e81e0__vf14_ClearIfMatch @0x5043d0` 清零 `+0xC`，之后再等 `this+8` 秒（每步 `ScaledWait(0.2~1.0)`，见下表）进入下一步。**可以跳过**：左键点击经 `CApp_OnMouseButton` 调场景 vf13 = `Cls_5e81e0__vf13_OnSkipMovie @0x5041f0`：子状态 3..8 → 直接 0x65（开始游戏 / 播 start 过场）；0xc..0xf（通关后报候选图）→ 回子状态 1 并显示光标；停语音、计时清零 [V]。

     | 子状态 | 动作 | 内容 | 之后等待 |
     |---|---|---|---|
     | 2 | 0 | 玩家名（3800+角色 Id） | 0.5 s |
     | 3 | 1 | 3828「歡迎你參加大富翁比賽，你的第一關是」 | 0.2 s |
     | 4 | 1 | 地图名（3816+map） | 0.5 s |
     | 5（层 > 0 从这里开始） | 1 | 3829「接下來上場的人物是」 | 1.0 s |
     | 6、7、8 | 1 | 对手 1、2、3 的名字（座位 1..3，没有的跳过；3800+角色） | 0.5 s |
     | 0x65 | — | 层 0：`CApp_PlayMovieChapter(角色Id−1)`（过场，2.1）；否则 `CApp_StartNewGame()` | — |

     **host 的台词文字**：`Game_PlayMovie @0x504460` 把文字送给 `UI_DialogShowMessage @0x4e78b0(text, 0x34)`，该函数按 ID 找控件，找不到就什么也不做；shipped 的 `chsMapMenu.ui` 里**没有 ID 0x34 的控件**（IDs 只有 1..19、51、61..66、71..77、81..86、98..100、118），所以**原版并不显示 host 的文字，只有语音 + 动作**（移植为方便手机用户，把台词显示在说明标签 99 里 [A]）。

     通关后进地图菜单（初始子状态 0xB，`SwitchToGameScene(4)`）：`run.level < 3` → 动作 0xef6（3830「恭喜你闖關成功，接下來，請在」）→（0xc..0xf）逐个报候选图名（+0x10..+0x1C，3816+map，每次 `ShowMovieEx`）→ 0x10：3831「中任選一關地圖」→ 子状态 1（等玩家选）；`run.level ≥ 3` → 玩家是莎拉（16）则 3835「…恭喜我們的莎拉公主也進入決賽！」否則 3834「比賽終於進行到決賽─魔都…莎拉公主」→ 子状态 100 → 1。
   4. **开局**（`Game_InitGame @0x4a1090`）：载图 → 按座位数组建角色（`thunk_FUN_00495f60(角色−1, 座位+8 == 0 即人类, 座位+4 服装)`，角色 13 / 16 用特殊子类，见 1.12）→ `Game_ActivateCharacterBuilding @0x4a37d0` → NPC → 日期 → `Game_StartNewRound @0x4962a0`。故事不设结束日期（`setup+0x18 = 2`）。

### 1.4 关卡表（pass_XX 全表见 `out/story/pass_table.txt`）

结构规律 [V，来自 16 个文件]：
- 层 0：1 张图、2~3 个对手（第三个常为 0）。层 1：3 张图（阿拉伯 / 〔string.bin#3826〕 / 夏威夷）或 4 张图（上海 / 香港 / 臺北 / 東京，如萊娜、沙隆巴斯、烏咪、星期三、莎拉）。层 2：3 张图（拉斯維加斯 / 好萊塢 / 索伏特斯達）或 4 张图（上海 / 香港 / 臺北 / 東京，如大老千、舞美拉、金貝貝）；**pass_04（忍太郎）没有层 2**。候选里每张图的对手名单是同一批人换顺序。层 3：巴黎，对手 16 莎拉（3 个对手栏里只有第一个）；**pass_16（莎拉自己）的层 3 对手是 −1（随机 1..15）**。
- 起始钱 100000 / 存款 100000 / 点券 50 为主；例外：pass_09 层 0（300000 / 100000 / 200）、pass_11 层 0（200000）、pass_16 层 0（200000 / 200000 / 100）。
- `start_card`（层 0 的 1~2 张）：大老千 45、萊娜 无、錢夫人 47、忍太郎 35、阿土仔 7、孫小美 7、舞美拉 7、金貝貝 7、沙隆巴斯 8、烏咪 无、金泡菜 18、酷娃 18+33、伊格魯 35+48、星期三 7+9、詹姆斯 49、莎拉 8+46。
- `start_pic` / `reward_pic` 与拼图 1..9 的对应：层 0 拼图 1~3（`start_pic` 放地图，`reward_pic` 通关给），层 1 拼图 4~7，层 2 拼图 8~9，另有「条件拼图」（1.11）。
- 奖励：层 0 多为 1 张卡 + 拼图；层 1 多为点券 100 或现金 200000；层 2 多为卡（如 45 / 47 / 48 / 30 / 33）；层 3 没有奖励。

### 1.5 开局数值（棋盘建立时）

`Game_InitGame` 建角色时 `Role_InitState @0x480d30` 仍按 `setup+0xC/2` 给现金 = 存款 = 50000、点券 `min(50, 9999)`、骰子数 `clamp(vehicle+1,1,3)`；`Game_SpawnNpc @0x495f60` 里故事 + 层 0 + 大老千还把现金 / 存款清 0（`Role_SetMoney(0,0)/(1,0)`，随后被覆盖，无实际效果 [M]）。

随后 `Game_StartNewRound @0x4962a0` 的故事分支只作用于**座位 0**（反汇编核对 ECX = 角色数组首项）[V]：
1. 把「待带入」8 格里 > 0 的卡逐张 `Role_AddCardToDeck(Card_FindByIdReturnIndex(id))`；
2. 取 `carryCash = box.cash.pending`、`carryTickets = box.tickets.pending`；
3. 按 `(角色 Id − 1, setup+4)` 找 pass 条目；找到则：`start_card[0..1]`（≠ 0）加入卡组；`Role_SetMoney(0, carryCash + start_cash)`；`Role_SetMoney(1, start_fund)`；`Role_SetTicketAmount(carryTickets + start_ticket)`；
4. **无论找没找到**都 `u_Obj_ResetSlotArrays()`（清 pending 与待带入卡）。所以**重开本关（状态 2）时携带物不会再给**，只有 pass 起始值。
5. 所有座位照常 `AddCardToDeck(8 / 10 / 11)`（自由模式也有）。

### 1.6 储藏箱

- **三个入口** [V]：① 关卡中弃牌框 `throwcard.ui` 在故事模式多出按钮 2（自由模式 `+0x1c(2)` 隐藏，故事 `+0x18(2)` 显示）：`Card_ThrowPrepare @0x4aea80`；选「存入」时 `Cls_5e3ca8__Event_OnThrowCardSelection @0x4aeb20` 的 `param_1 == 2` → `Role_StoreCardToBox @0x488de0`：取卡 Id（卡对象 +0x204），`CardArray_FindSlotAndAdd`；箱满提示 str112「儲藏箱已滿」，卡不动。② 通关奖励（`@0x48f9d0`，见 1.8）。③ 地图菜单的 0x13 → boxmenu。
- **boxmenu**（状态 0xB，`Cls_5e7f10`，`.ui` 控件 [V `@0x501a70` / `@0x501f50`]）：标签 0x15 = str7201「現金」、0x18 = str7202「點券」，0x16 / 0x1b 显示现金的箱内值 / 待带入值（`CUi_boxmenu_UpdateDisplay @0x502050`），点券同理（`UpdateLand @0x5021b0`，名字不准）。按钮：0x17「现金全部移入待带入」（`RefreshDisplay @0x502580`：`pending = value; value = 0`）、0x1a 点券同（`RefreshLand @0x502640`）；卡片：箱内 8 格按钮 4..11（`RefreshCards @0x502700`：箱格 → 待带入空格），待带入 8 格按钮 12..19（`RefreshSlots @0x5027e0`：待带入 → 箱格）；按钮 1 / 2 / 3 为关闭 / 全部放回 / 全部带走。str111 说明「這裏儲藏著您在遊戲中保存的卡片和獎勵的點券與金錢…」。关闭 → `Unk_ShowGameMode(3)` 回地图菜单（`@0x502000`）。**移入待带入的现金是整笔搬运，不能分笔**（代码里只有 `pending = value` 一种操作 [V]）。
- 带入 / 存回：开局消耗（1.5）；地图菜单「返回」（level 0）合并回箱；其余路径（开局、重开）都会丢掉待带入。

### 1.7 关卡进行中的目标与胜负

故事不设时限和胜利条件（自由模式的 `Game_FindLeaderByWinCondition @0x4a6090` 只在 `setup+0 == 0` 时检查 [V @0x4a6090 开头]），**通关只有三种来源**，都汇到消息 `0x75(0, b≠0)`：

| 来源 | 地址 | 条件 |
|---|---|---|
| 所有对手破产 | `Role_CheckBankruptcy @0x48ee10`（调度器 `@0x481df0` 每次取消息前检查）→ 入队 `0x75(0,1)`（该函数 `h==1 && c==0` 分支） | 破产者是电脑，且「其他未破产者里 `h==1 && c==0`」即只剩人类一人（`docs/rules_land.md 3.3`） |
| 剧情事件胜利 | 见右栏各函数 | ① 角色 3（錢夫人）在 map 5 第二次与角色 9（沙隆巴斯）同点：`Role_StartNpcDialogForRole @0x48c950`（第 1 次 drama 6，第 2 次 drama 7 + `0x75(0,1)`）；② 角色 11（金泡菜）层 1 **第一次被怪兽袭击**（消息 `0x31`）走完 drama 13 / 14：`CPlayerTag__OnParkEvent @0x48d310`（怪兽袭击处理）末尾 `0x75(0,1)`；③ 莎拉的两条跳关路径（见下） |
| 莎拉特例 | `Role_CheckBankruptcy` 角色 16 分支；`SendToHospital @0x48a570` / `SendToPrison @0x48a9c0` | 莎拉破产 → drama 19 + `0x75(0,1)`（**破产 = 过关**）；莎拉第一次住院 / 坐牢（`+0x384 == 1`，`in_ECX[0xe1]`）→ drama 18 + 文本 str101「是否需要直接進入下一關？」okcancel → `0x75(0,0x80000000)`，确认结果 ≠ 0 则过关 |

失败：人类座位 0 破产 → `h == 0` → `0x74`（1.9）。电脑之间的破产按普通规则处理（0x72 清算 + 拍卖 + 0x73）。金泡菜（Id 11）**第一次**破产被救：drama 16「破产的金泡菜獲得老爸的金錢資助，再次進入比賽！」，现金设 100000（`Bank_SetMaxMoney_0047e800(100000,0)`），返回 0 不算破产，每局一次（`+0x380`）；`AskContinueNextRound` 里金泡菜 + 层 0 首次失败还会发拼图 3（`Event_GetItemData(0xb,3)==0 → 0x58(3)`）。

### 1.8 通关结算（`0x75` → `0x7e` → `0x7d` → `0x7a`）

1. `0x75(a, b)`（`Role_DispatchQueuedMsg @0x481df0` case 0x75）只在 `b != 0` 时执行 `CPlayerTag__OnParkCleanup @0x48ff70`（名字不准，实为「通关收尾」）：清所有队列；入队 `0x7e`、`0x7d`；若角色 4 且层 1 再入队 `0x7b`、`0x2c(0x8f6)`、drama 9（忍太郎的收尾剧情）。
2. `0x7e` = `CPlayerTag__OnMovieIntro @0x492e40`（名字不准）：在座位位置放特效 art 6000（`article\eff\Gwin.dff`），停菜单乐、播曲目 103（淡入）、`app+0x350 = 9`、角色状态 0xD、等待 14 秒（`Anim_ApplyDifficultyScale` = `ScaledWait`，见 2.2）。`0x7f` = `OnMovieDrama @0x492f30` 是破产版：art 6006（`Glost.dff`）+ 曲目 104（`Role_CheckBankruptcy` 的普通分支入队）。
3. `0x7d` = `CPlayerTag__OnTreasurechest @0x48f9d0`：按 `(座位 0 的角色, setup+4)` 找 pass 条目，依次：① `reward_pic` 两块里没拥有的 → `Role_EnqueueMsgAt(n, 0x58, 拼图号, 0)`；② `reward_cash > 0` → `box.cash.value += reward_cash`，提示 str7251「獲得%d元現金」；③ `reward_tichet > 0` → `box.tickets.value +=`，str7252；④ `reward_card` 4 张（> 0）→ `CardArray_FindSlotAndAdd`（箱满则丢，但提示照出）并显示卡图 `card\<texName>`，str7253「獲得%s」；最后入队 `0x7a`。（str115~118 的「通關獎勵」在别处用，本函数用 7251~7253。）
4. `0x7a` = `Game_OnStageSelect @0x4a1390`（名字不准，实为「关卡进度结算」）：游戏状态 = 4；若找到条目：`newLevel = 条目.level + 1`；`run.level < newLevel` 才 `Game_UpdateRecordStats(newLevel)`：`run.level = newLevel`，并 `Event_SetMaxLevel(角色, newLevel)` 写 gamedate 的 stage 并存盘；`newLevel > 3`（刚打完魔都）→ 游戏状态 = **5**。
5. 状态 4：切地图菜单（子状态 0xB）。状态 5：`Event_SetTerrainData(16, 1)`（解锁莎拉）；若玩家是 15 詹姆斯再 `Event_SetTerrainData(12, 1)`（解锁酷娃）；`CApp_PlayMovieRange(角色−1, 1)` 播结局过场（2.1）。

### 1.9 失败与「是否接關」

人类座位 0 破产 → `h == 0` → `0x74` = `CPlayerTag__AskContinueNextRound @0x48f8e0`：自由模式 `g_GameState+0x48 = 3`；故事：入队文本 str93「是否接關？」`0x1`（b = 0x80 okcancel）、`0x1C(okcancel)`、`0x76(0, 0x80000000)`。确认值写进 `0x76` 的第二参数：
- 非 0 → `CPlayerTag__OnTurnEnd @0x490030`：表情 0x1B + 入队 `0x77` → 状态 2 = **重开本关**（`Game_Cleanup / Game_LoadAllConfigTables / Game_InitGame` 同一份 setup，棋盘全部重建；**不会重播 start 过场，也不会再给储藏箱携带物**，1.5）；
- 为 0 → `Game__ExitMainLoop @0x490010`（状态 3）→ 角色菜单（箱子保留在内存，但再选角色会清空）。

### 1.10 进度与解锁

- **stage 写入**：只有 `Event_SetMaxLevel`（1.8 第 4 步），每过一层 +1 到 `条目.level + 1`，**取最大值**。stage 的消费者：服装解锁（角色菜单 1.3）、`finalMenu`（`Event_GetLevel(i) ≥ 4` 才能点，`@0x506fc0`）、候选图（`run.level` 来自本次运行，不来自 stage；**新开一局永远从层 0 开始**）。
- **拼图**：`reward_pic`（通关给）、`start_pic`（地图上拾取，`CPlayerTag__OnPuzzleCardUse @0x491830` 经消息 0x58）、条件拼图（1.11）；`Event_SetItemData` 置位并存盘。显示文字 str434「%s獲得%d號拼圖」，图 id = `(角色−1)*9 + 0x12d + 拼图号`。
- **隐藏角色解锁**（`Event_SetTerrainData(r,1)` 的全部调用者）[V]：

| 角色 | 条件 | 触发代码 |
|---|---|---|
| 11 金泡菜 | 玩家是 6 孫小美，地图气候 `clime == 1`（stage.bin，stage 记录 +0x210），在四种放置类 NPC（NPC 12 路障 / 13 地雷 / 14 定時炸彈 / 15 機器娃娃）里各用一次 | `CPlayerTag__OnDiceEvent @0x491270`（名字不准，由 `py_Npc_Create @0x500160` 调用，param = NPC Id，映射 12→位 0、13→位 2、14→位 3、15→位 1）→ `Event_SetFlag2` 四位全 1 → `@0x4aa470` 解锁；剧情 drama 20..23，全齐后 drama 24 |
| 12 酷娃 | 用 15 詹姆斯 通关全部 4 层 | `Game_OnUpdate @0x4a17b0` 状态 5 |
| 13 伊格魯 | 玩家 7 舞美拉，层 0，怪兽真的出现（`0x31` 处理末尾置 `+0x37c = 1`）后的回合开始，累计 4 次 | `Role_RunStoryScriptEvents @0x483f40` 末段：`Event_IncrementCounter()`（gamedate `+0x884` 跨局累计）；< 5 时 drama `0x18 + 计数`（25..28），> 3 解锁 |
| 14 星期三 | 层 0、周三（`g_GameState+0x10 == 3`）、第 10 天以后，依次：阿土仔（5）→ 烏咪（10）→ 金貝貝（8） | `RunStoryScriptEvents` 的 `Event_SetFlag3(0 / 1 / 2)`（按位序，第 3 位成立才解锁）；drama 0x1d / 0x1e / 0x1f |
| 15 詹姆斯 | 11 位「连锁超市」位全亮：錢夫人层 0/1/2 → 位 7/8/9，忍太郎层 0 → 位 10，阿土仔层 0..3 → 位 0..3，烏咪层 0..2 → 位 4/5/6 | `CPlayerTag__GetChainStoreIndex @0x491710`、`CPlayerTag__OnChainStoreEvent @0x491630`（玩家点击地图上类型 0x45b 的特殊建筑，`Game_OnTileClickWithCard @0x4a4a10`；该建筑由 `Game_ActivateCharacterBuilding @0x4a37d0` 在「建筑所属角色 == 玩家且该位未亮」时激活），drama 0x20 + 位号 = 32..42 |
| 16 莎拉 | 任一角色通关全部 4 层 | `Game_OnUpdate` 状态 5 |

- **地图解锁**：故事不涉及。自由模式第 3 张（巴黎）见 0 条第 9 点。

### 1.11 拼图条件表（`Role_CheckMissionsByCharacter @0x4835e0` → `CPlayerTag__OnStockMarketAccess @0x491a10`，真名 = 拼图条件检查）

调用点：`CPlayerTag__vf3_Main @0x481ce5`、`Role_RunTurnStartStep @0x484404`（step 2）、`Role_BeforeRollDice @0x484c8c`（非 0 则入队 `100` 稍后重试）。仅座位 0 且故事时检查；`Role_CheckPuzzleCondition(拼图号, 条件号)`：**还没拥有该拼图**且条件满足 → 入队 `0x58(拼图号)`，返回 1。

「角色 × 层 → (拼图, 条件)」全表（脚本 `story_missions.py`，已对照 pass 的 start / reward_pic：层 0 拼图 1~3、层 1 拼图 4~7、层 2 拼图 8~9）：

| 角色 | 层 0 | 层 1 | 层 2 |
|---|---|---|---|
| 1 大老千 | 2:1 | 6:2 | 8:6 |
| 2 萊娜 | 2:7 | 6:8 | 7:18 8:19 |
| 3 錢夫人 | 2:3 3:4 | - | 8:9 7:10 9:11 |
| 4 忍太郎 | 1:135 | 5:131 6:1 | - |
| 5 阿土仔 | 1:12 | 4:6 5:13 6:14 | 8:1 |
| 6 孫小美 | 2:11 | 4:1 5:2 | 7:119 8:6 |
| 7 舞美拉 | 2:10 | 4:1 5:5 6:133 | 8:13 |
| 8 金貝貝 | 1:6 2:119 3:1 | 5:4 | 8:8 |
| 9 沙隆巴斯 | 1:6 2:18 3:1 | - | 7:9 |
| 10 烏咪 | 1:15 | 5:6 6:18 | 8:121 |
| 11 金泡菜 | 2:5 | - | 6:1 7:8 8:119 |
| 12 酷娃 | 1:118 2:9 | 4:18 5:1 6:10 | 8:13 |
| 13 伊格魯 | 1:148 2:15 3:135 | 5:6 6:8 | 8:13 |
| 14 星期三 | 1:1 2:108 3:16 | 5:1 6:13 | 8:8 |
| 15 詹姆斯 | 1:1 2:13 | 4:16 5:8 6:4 | 8:13 |
| 16 莎拉 | 1:17 2:8 3:1 | 5:16 6:6 | 8:8 |

条件号含义（`@0x491a10` [V]；`Role_CountOwnTerras(a,b,c)` 的 a = 地产类型，见 rules_land）：1 总资产（`Role_CalculateNetCash(1)`）> 999999；2 点券 ≥ 500（`+0x324`）；3 股票总值 > 200000；4 存款（`+0x294`）> 500000；5 股票总值 > 500000；6 `CPlayerTag__CheckTownOwnerCondition`（整街 / 同街地主条件，[M]）；7 `CountOwnTerras(9,5,5) > 4`；8 地块表里类型 2 的地块**全部**属于自己；9 / 10 / 11 `CountOwnTerras(2 / 3 / 4,5,5) != 0`（研究所 / 连锁超市 / 饭店各有一块，5 级）；12 `+0x5c0 != 0`；13 `CountOwnTerras(0,5,5) > 4`；14 `CountOwnTerras(1,5,5) > 0`；15 公园访问次数 `+0x378 > 0`；16 奇迹表（`DAT_006403bc+8+i*0x24`，i < 3）里有自己的奇迹；17 `+0x5cc != 0`；18 / 19 `CountOwnTerras(0,5,5)` > 0 / > 1；**≥ 100：`role+0x330+cond*4 != 0`，即 `role+0x4c0[cond−100]`——使用过卡 Id = cond−100**（写入处 `Role_FinishCardUse @0x4896e0`：卡成功使用时 `this+0x4c0[卡Id] = 1`，不限故事模式）：108 卡 8、118 / 119 卡 18 / 19（飞弹类）、121 卡 21、131 卡 31、133 卡 33、135 卡 35、148 卡 48。这块 `0x100` 字节随存档 `PlayerTag_OnSave` 保存。
拼图收集本身（图鉴、持久化）归 collect 模块；故事只需在这些点调 `rm8PuzzleCollect(seat, 拼图号)`。

### 1.12 故事专属规则例外（测试 `setup+0` / `setup+8` 的全部位置）

`setup+0 != 0` 的判断共出现在下表函数里（清单来自 `story_setup_sites.py`，逐个读过）。**所有「角色 N / 层 L」的分支都只对座位 0（人类玩家）生效**，除非注明。

| 地址 | 函数 | 故事时的行为 |
|---|---|---|
| 0x4835e0 / 0x491a10 | `Role_CheckMissionsByCharacter` / `…OnStockMarketAccess` | 拼图任务（1.11） |
| 0x483f40 | `Role_RunStoryScriptEvents` | 回合开始 step 2：① 第 1 天 + 层 0：大老千（1）→ `Bank_TrySetLoan(150000, 今天, 期限 3)` 贷款 + `Bank_SetMaxMoney(50000, 现金/存款)` 把现金和存款各设 50000 + drama 1 + 消息 0x47；孫小美（6）→ 文字 str 0x6fb8/4（金泡菜提示 str7150「最近有個傢伙總出來破壞比賽選手的道具，你如果遇到可要小心嘍。」配头像 `npc\p_18_01`）；② 第 10 天以后：大老千（1）层 1 / 2 且 `+0x374 == 0`：现金 + 存款 ≥ 250000 / 300000 → drama 2 + `0x16(15000)`（讨债人收利息），`+0x374 = 1`；阿土仔（5）/ 金貝貝（8）/ 烏咪（10）层 0 周三 → 解锁 14 的 `Event_SetFlag3`（1.10）；③ 舞美拉（7）层 0 怪兽计数（1.10；`+0x37c` 由怪兽真的出现时置位） |
| 0x48a570 / 0x48a9c0 | `SendToHospital` / `SendToPrison` | 莎拉第一次住院 / 坐牢的跳关询问（1.7） |
| 0x48bd00 / 0x49caf0 | `Role_RunPoliceThiefDogAction` / `Cls_5e31e0__BeginAttach` | 烏咪（10）层 0 第一次被附身（野狗 / 警察 / 小偷）→ `0x58(2)`（拼图 2） |
| 0x48c950 / 0x48ca30 / 0x493770 | `Role_StartNpcDialogForRole` / `u_Role_QueueTagEntryMsgs` / `CPlayerTag_FindAndMarkEntry` | 同点相遇对白（`sp_XX`）和錢夫人遇沙隆巴斯（map 5）的特例，见 1.7；`docs/presentation.md 5` |
| 0x48d310 | `CPlayerTag__OnParkEvent`（**名字是错的：它是消息 `0x31` = 「怪兽袭击地块 `param_1`」的处理函数**——`0x31` 只由新闻 `News_EventMonsterAttack @0x4b7910`（入队到当前角色）和怪兽卡 `CardPy_06` 的 `Terra.Dragon` 入队；字符串 str573「怪獸拒絕出場，怪獸卡使用無效。」/ str574「怪獸逃跑，怪獸卡使用無效。」也说明了这一点。`param_1+0x4c` = 地块的地主座位号；`in_ECX` = 队列里有这条消息的角色，它的 `+0x378` 数「被怪兽打到几次」（先 +1，−1 = 已用）；剧情分支按**座位 0 的角色**分派）| 角色 3（錢夫人）且地块地主 == 座位 0：层 < 3 → `0x52(0x8df)` 特效 + drama 5，层 ≥ 3 且计数 ≥ 0 → drama 8 并置 −1；角色 6 计数 == 1 且层 1 → `0x52(0x9c5)` + drama 10；角色 7 计数 == 1 且层 3 → drama 11（怪兽照常出现）；角色 10 计数 == 1 且层 0 → drama 12（怪兽照常出现），否则地块地主 == 座位 0 → str573 并返回；角色 11 计数 == 1 且层 1 → drama 13 + `0x7b` + `0x2c(0x9c5)` + `0x7c` + drama 14 + `0x75(0,1)`（**通关**，不出怪兽）；角色 13 计数 == 1 且层 0 → drama 15，否则一律 str574 并返回（伊格魯主角时怪兽卡永远无效）；角色 15 且地块地主的角色 == 3 → `0x52(0x8df)` + drama 17。**凡是分支 `return` 的，怪兽都不出现、房子不拆**；没有 `return` 的才走 `u_NPC_Chest_Construct`（怪兽动画，成功则座位 0 的 `+0x37c = 1`，舞美拉的计数用）。**移植曾把它挂在「走到公园格（event 4）」上，已改为挂在 `0x31`（2026-11-15 审核，见第 5 节）** |
| 0x48ee10 / 0x48f8e0 | `Role_CheckBankruptcy` / `AskContinueNextRound` | 1.7 / 1.9 |
| 0x48f9d0 / 0x48ff70 | `OnTreasurechest` / `OnParkCleanup` | 1.8 |
| 0x490df0 | `CPlayerTag__OnSendGodCard`（`ECX` = 受害者，由范围伤害卡 `CPlayerTag__OnHolidayCardUse @0x48de10` 对范围内每个角色调用；`OnHolidayCardUse` 的调用者是调度器 `@0x482b10/4d/8a` 与 `OnUseTargetCard @0x48e42e`） | 受害者是座位 0 且角色 12（酷娃）时返回 1，整张卡作废：表情 `0x3f + 2*param_2`、str `570 + param_2`（570「飛彈被酷娃沒收！」/ 571「核子飛彈被酷娃沒收！」，param_2 = 卡型 0 / 1 [M]）、与使用者的 `0x2E` 交接。即**玩家用酷娃时，别人的飛彈 / 核子飛彈打不到她** |
| 0x491270 / 0x491630 / 0x491710 / 0x4a37d0 / 0x4a4a10 | 解锁 11 / 15 | 1.10 |
| 0x4a6780 | `Game_TriggerEventByDate`（`Game_OnTurnStart @0x4a3b50` 调） | 第 10 天以后，座位 0 的 `+0x378 < 1` 且 (角色, 层) ∈ {(6,1) (7,3) (10,0) (11,1) (13,0)} → `News_OnUpdate(0x1e)`「怪獸襲擊摧毀房屋一棟」（string 330，剧情怪兽，drama 9~15 的背景） |
| 0x4a1390 | `Game_OnStageSelect` | 1.8 |
| 0x4a17b0 | `Game_OnUpdate` | 状态 3 / 4 / 5 的故事分支（1.1） |
| 0x49db20 | `Event_ProcessLanding`（`Npc_SpawnAll`） | 放置 `start_pic` 拼图 NPC |
| 0x495f60 | `Game_SpawnNpc`（真名 CreateRole） | 角色 13 → 子类 vtable 0x5e27d8（带跟随演员 +0x5f0，建 `Cls_5e1b90` 动画体，`Cls_5e27d8_CreateMovieActor @0x47e1e0`）；角色 16 → 子类 vtable 0x5e2918（`CPlayerTag_3`，+0x5f0 带计数 +0x98，读写存档版本 ≥ 8；创建路径时播样本 0x2e `@0x4803e0`）。**这两个子类已读（`docs/characters.md` 第 6 节）**：`+0x5f0` 是一个路径跟随的伴随对象——伊格魯的怪物（A13b 小企鹅）和莎拉的保镖（D46a），只有表现、没有规则作用（莎拉的 vf25「保镖在」是免疫规则的判据）；存档里 `PlayerTag_OnSave` 为莎拉多存 4 字节（保镖计数） |
| 0x4962a0 | `Game_StartNewRound` | 1.5 |
| 0x49f810 | `Lottery_InitScrollBar` | 財神 / 窮神的滚轮对话框：故事 = `sscrbar` 6 个轮（4 数字 + 符号 + 倍数）+ `rand() % 5 == 0` 时四位全相同，自由 = `scrbar` 4 个轮。**[V] 已读已移植**（`npc_ui.cpp`，`npc_port.md`；`characters.md` 第 9 节）；乐透彩票本身没有故事分支 |
| 0x4a31f0 | `Game_LoadMapData` | 故事 + 地图 3 → 曲目 15（audio.md） |
| 0x4a6090 | `Game_FindLeaderByWinCondition` | 仅自由模式 |
| 0x4a7380 / 0x4a7540 / 0x4a78f0 / 0x492850 / 0x4ac2a0 / 0x4ac420 / 0x50cf20 / 0x50d490 | 存档 / 读档 / 槽位 | 故事用 `record_NN.sav`，并多写 / 读 sflb 段（`@0x4a7540` 内 `if (setup+0 != 0) Game_SaveBinaryData`） |
| 0x492e00 | `CPlayerTag__GetNameReference` | 文本里 `%s` 的角色名：自由模式用角色对象内嵌名 `+0x3c0`，故事用 roleinfo 名 [M] |
| 0x4acab0 | `Cls_5e4500__vf16_ShowWinConditionInfo` | 「勝利條件」信息框：故事显示 str7052「比賽場次：%d」（= `setup+8 + 1`）、「當前地圖」，自由模式显示时限 / 剩余回合 |
| 0x4aea80 | `Card_ThrowPrepare` | 弃牌框的储藏箱按钮（1.6） |
| 0x4f6c80 | `Hotel_ProcessVisitor` | 萊娜（Id 2，座位 0，无财神 / 穷神附身）在饭店：住宿费 ×7/10，提示 str53「萊娜消費享有7折VIP優惠。」（**没有次数限制**；drama 3 / 4（「超過5次必須正常付費」）在整个 exe 里没有任何调用者，只是数据——2026-11-15 核对 `Role_QueueNpcDialog` 的全部 10 个调用函数）|
| 0x503100 / 0x5031c0 / 0x503510 / 0x503830 / 0x503d20 / 0x503e90 / 0x5042a0 / 0x504310 / 0x504cd0 / 0x504540 / 0x5045f0 | 地图菜单 | 1.3 |
| 0x505940 / 0x505a80 / 0x5065f0 / 0x5069b0 / 0x506a30 / 0x50d9e0 / 0x50db90 | 角色菜单 / single 菜单 | 1.3 |
| 0x508b50 | 主菜单 | 写 `setup+0` |

### 1.13 剧情对白（drama）与新闻

- `drama.bin`（`speech\drama.bin`，42 段）经 `Role_QueueNpcDialog(段号, 插入位置) @0x48c700` 入队：每段的 `rlId`：1..16 角色、17 怪兽、18 玩家（莎拉线）、19 旁白（0x13，文本无说话人）、20 讨债人；规则：第一个说话者 = 左，第二个说话者 = 右（`0x54(左,右)` 先显示双人对话框，`0x55 / 0x56` 逐句，`0x1C` 收框；旁白段末尾 `0x65(Role_GetWaitFrames(3))` + 文本）。段号 ↔ 触发（1.12 / 1.10）：1~2 大老千讨债；3~4 萊娜 VIP 折扣；5~8、17 錢夫人 + 沙隆巴斯 / 詹姆斯；9 忍太郎通关收尾；10 孫小美；11 舞美拉；12 烏咪；13~14 金泡菜；15 伊格魯；16 金泡菜被救；18~19 莎拉；20~24 金泡菜 + 孫小美（解锁 11）；25~28 伊格魯 + 舞美拉（解锁 13）；29~31 星期三（解锁 14）；32~42 詹姆斯（连锁超市，解锁 15）。
- 同点相遇的 `sp_XX` 对白见 `docs/presentation.md 5`（移植已有 `rm8PresentSetStory`）。
- 新闻：自由模式周六新闻 + 故事的 0x1e 怪兽（1.12 `Game_TriggerEventByDate`）。

### 1.14 结局与影片

- `config\movie.bin`（`Movie_LoadConfig @0x5152a0`，按节 = roleinfo 顺序）：每角色 `start_num`（恒 2）、`start_1..` 、`end_num`（3~5）、`end_1..`。**每个角色的 start = `da17_1` + `da(角色)_1`，end = `da17_2` + `da17_3` + 角色专属 1~3 段**（金泡菜的 end 含 `da06_2`（借用孫小美的））。40 个脚本全部被引用，全部存在（`out/pysrc_clean/da*.py`）。
- 入口 [V]：`CApp_PlayMovieChapter(n) @0x4050a0`（`Container_PushChapterData @0x515b70` 把第 n 节的 `start_*` 压入队列，flag `+0x28 = 1`）；`CApp_PlayMovieRange(n, f) @0x405400`（`Container_PushRangeData @0x515c50` 把 `end_*` 压入队列，flag `+0x2c = f`）。n = 角色 Id − 1。
- 触发点：① `start`：地图菜单层 0 的 `0x65`（1.3）；② `end`：游戏状态 5（1.8），`f = 1`；③ `finalMenu`（影片欣赏，`@0x506fc0 / @0x507100`）：`Event_GetLevel(i) ≥ 4` 的角色按钮亮，点击 `CApp_PlayMovieRange(i−1, 0)`；返回按钮 0x12。
- 队列播完 `Movie_Finish @0x515d40`：`+0x28 != 0`（start）→ `CApp_StartNewGame()`；`+0x2c != 0`（结局）→ `CApp_SwitchToStartMovie(1)` 播 `Bink\Staff.bik`（`Movie_LoadStartAnimation @0x516750`，`"start.bik" + 参数*0x100`；`binkplay=0` 则跳过）→ 播完 `Movie_FinishBinkPlayback @0x516890` 回 single 菜单（状态 8）；`f == 0`（影片欣赏）→ 回 finalMenu（状态 7）。开场 `Start.bik` 参数 0，播完进主菜单。
- 通关特效 art 6000 / 6006 + 曲目 103 / 104（1.8）。

### 1.15 存档

故事存档 = 普通存档的段落全套 + **sflb 段**（`level + 箱现金{8} + 箱点券{8} + 0x40 字节卡片区`），`setup+8`（层）和座位的服装字段随 setup 段保存（`FUN_004a8740`）。样本 `record_01.sav`：setup `+0x08 = 3`、sflb `f_00 = 3`、玩家 4 忍太郎对 16 莎拉，箱内有卡 31 [V，`savefile.py` 输出]。`CPlayerTag_3`（莎拉）多存 4 字节（1.12）。读档后 `Game_ActivateCharacterBuilding` 重新激活连锁超市（`Save_LoadRecord @0x4a7ce7`）。存档里没有 gamedate（进度只在 `config\gamedate.bin`）。

### 1.16 反编译信息完整度（第 1 部分）

| 部分 | 状态 | 说明 |
|---|---|---|
| 菜单流程、候选图、确认、主持人状态机、开局数值、储藏箱、结算链、进度写入、解锁条件 | **够** | 全部读了函数体并给出地址；候选图 / 开局值 / 结算顺序是 [V] |
| pass 字段含义 | **够** | `Config_LoadPassConfig` 读了；`start_pic` / `reward_pic` 的用法 [V] |
| 拼图条件表 | **够** | 条件 1..19 与 ≥100 的语义 [V]，个别地产类型编号 [M] |
| 剧情专属例外（1.12） | **部分** | 每个入口的触发条件 [V]；个别消息码（0x47、0x52 的参数 0x8df / 0x9c5 / 0x8f6、0x7b / 0x7c 对 `+0x1c8` 的作用）含义 [M]；drama 的文字已读 |
| 角色 13 / 16 子类、乐透故事分支 | **够** | 子类行为已读并移植（`characters.md` 第 6 节，含进出饭店 / 掉头 / 被抬走）；乐透故事分支 = 滚轮对话框（6 轮 + 1/5 全相同），已移植 |
| 酷娃「没收」机制（`OnSendGodCard`） | **部分** | 触发入口 [V]，规则 [M] |
| 新闻 0x1e 的怪兽实现 | **部分** | 触发条件 [V]；怪兽的 NPC / 特效（`News_EventSwitch`）未读 |
| 动态验证 | **缺** | 全部是静态分析，没有实机验证（尤其「新开局清箱」「重开不再给携带物」这两条值得实机确认） |

---

## 2. 过场动画引擎

### 2.1 触发与生命周期

```
CApp_PlayMovieChapter / CApp_PlayMovieRange              [0x4050a0 / 0x405400]
  └ 删除当前场景 (+0x234)，new(0x30) Cls_5dff48（电影管理场景，vtable 0x5dff48）[0x407690]
  └ Movie_InitEngine [0x515780]：读 Config\movie.txt 即 movie.bin，CDigitalAudio_StopMusic(0)，app+0x350 = 9
  └ PushChapterData / PushRangeData：脚本名队列 +0x18（元素 0x100 字节字符串），Cursor_SetVisible(0)

每帧 Cls_5dff48_vf1 [0x5159d0]：
  无当前脚本(+0x14 == 0)：
      队列空 → Movie_Finish [0x515d40]（1.14）
      否则取队首名 → Cls_5dff48_vf1_5158a0：Movie_ReleaseAndRestoreView；new(0x170) CUi_SpeakAside [0x515da0]
            → vf16 = SpeakAside_InitPython [0x5141b0]；队首出队
  有脚本：CArticleMgr_UpdateEffects(dt)；脚本对象 vf1(dt) [0x514a60]；返回负值(0x80000000) → Movie_ReleaseAndRestoreView [0x515960]
每帧渲染 Cls_5dff48_vf2 [0x515a90] → CUi_SpeakAside_vf2 [0x514840]
```

- 一个脚本 = 一个 `CUi_SpeakAside` 对象（vtable 0x5e91c8，单例指针 `DAT_0064dba8`，所有 `Movie.*` 都作用于它）。**脚本之间场景对象整个销毁重建**：`Movie_Cleanup @0x5145b0` 先 `vf1(2.0)` 空转一帧，销毁 SpeakAside.ui、所有角色（列表 +0x10）、所有物件（列表 +0x20）、两张图片；`Movie_ReleaseAndRestoreView @0x515960` 再 `CAudioMgr_StopSample(0)`（停全部音效，**背景乐不停**）并 `Camera_RestoreViewState @0x41b360` 还原相机。`Py_Finalize` 只在 `+0x5b` 标志非 0 时调用，故事路径传 0，解释器常驻 [V]。全部队列播完才 `Movie_ShutdownEngine @0x515850`（停音效、`CDigitalAudio_StopAllBGM`、`app+0x350 = 0`）。
- `CUi_SpeakAside` 字段 [V 读代码]：+0x08 等待计时（秒）；+0x10 角色列表；+0x20 物件列表；+0x2C SpeakAside.ui 根；+0x30 / +0x34 标签 `asideWord` / `asideWord2`；+0x38 状态（`0x80000000` = 已结束）；+0x3C 恢复计数；+0x44 声音句柄（恒 0 [H]）；+0x48 `SetBackPic` 贴图；+0x4C `SetFrontPic` 贴图；+0x50 脚本模块；+0x58 生成器的 `next`；+0x5C `rolemove`；+0x60 生成器；+0x64 空参数元组；+0x6C 脚本名（0x100 字节）。

### 2.2 Python 入口与时间线

- `SpeakAside_InitPython @0x5141b0`：加载 `config\UI\SpeakAside.ui` 并按 `init.txt` 宽度 / 1024 缩放；找标签 `asideWord` / `asideWord2`；`Py_Initialize` 仅当 `+0x16c` 标志；`PyModule_Movie_Init @0x513700`、`PyModule_CTime_Init @0x516110` 注册模块；`PyImport_ImportModule(脚本名)`（如 `da01_1`）；取属性 **`rolemove`**（脚本里的生成器函数，每个 daXX_N 都叫它）；`rolemove()` 得到生成器；取它的 **`next`**；之后 `+8 = ScaledWait(1.6)`。
- 每帧 `CUi_SpeakAside_vf1 @0x514a60`：`+0x38 == 0x80000000` 则什么也不做；否则对每个角色调 vf3（`Cls_5e8cf8_vf3 @0x511c10`：向目标点移动 + 气泡跟随头顶）、对每个物件做 `SpeakAside_UpdateArticleAndUiPos @0x512050`（`Article_Update(dt)` 推进动画 + `SpeakAside_MoveArticleToTarget @0x512150` 移动 + 气泡位置）；`+8 -= dt`；`+8 ≤ 0` 且 `+0x44 == 0`：`+8 = ScaledWait(1.6)`，**调用 `next()`**，`+0x3C++`（恢复次数 ≥ 100 就强制结束，现有脚本最多 51 次 `yield`）。脚本异常或 `StopIteration` 不处理，只是 `+8` 保持 0，下一帧再调（结局靠 `Movie.QuitMovie`）。
- `ScaledWait(s) = s × (1 − min((speed−1)/10, 1))`（`@0x47b3b0`，函数名 `Anim_ApplyDifficultyScale` 是错的；`speed` = `init.txt` 第 6 项，默认 4 → ×0.7）。**`CTime.Wait(x) @0x5160b0` 直接写 `+8 = x`，不缩放**；所以只有「没调 `Wait` 就 `yield`」才会等 1.12 s，现有脚本每个 `yield` 前都有 `Wait`。`Movie.QuitMovie @0x5136d0 / Movie_StopMoviePlayback @0x514d30`：`+0x38 = 0x80000000`、`+8 = ScaledWait(0.1)`。
- 脚本全是 `Movie.*` / `CTime.Wait` 的顺序调用 + `yield None`（`out/story/movie_stats.json`：40 个脚本、共 2975 个调用（其中 660 个 `CTime.Wait`），等待总计 1895 s，最长 da16_1 = 162 s）。除了 `rolemove` 外没有任何别的 Python 构造（无分支、无循环、无 import random）。移植可以直接用内嵌 CPython 跑原 `.pyc`，也可以把脚本离线展开成时间线。

### 2.3 渲染与坐标

`CUi_SpeakAside_vf2 @0x514840`（渲染）顺序 [V]：`u_Render_SetGlobalMode` → `CUi_SpeakAside_DrawBg1 @0x514ea0`（`SetFrontPic` 的图，先画）→ 渲染状态 0x1e = 0xc4、0x1d = 7（RW 状态号，疑为 Alpha 测试）→ 所有角色 vf4（`@0x480110`）→ 状态 0x1d = 8 → 所有物件 `Article_RenderAll`（部分物件 +0x58 落在 0x259 / 0x411 / 0x42b 范围内时另设一次 0x1e / 0x1d，`u_Obj_IsField58InRanges @0x514a10`）→ `CUi_SpeakAside_DrawBg0 @0x514d60`（`SetBackPic` 的图，**最后画，盖在 3D 上**）。两张图都是整屏四边形（`Im2D_DrawTexturedQuad @0x45de10`，顶点色白）、Alpha 混合（SRCALPHA / INVSRCALPHA，Alpha 取贴图）。**名字与层次相反**：`SetFrontPic` 的图先画（在场景后面），`SetBackPic` 的图最后画（盖在场景前面）。**但这些贴图是废的**：`texture\demo_tex\grass.tga`（32×32）和 `shine6.tga`（128×128）在 `tex0000.dat` 里是**全黑的 24 位 TGA（没有 Alpha 通道）**，`shine5` 根本不在包里（`Movie_SetBackgroundPicture` 读不到就不画）。脚本里 `SetFrontPic('shine6'/'shine5')` 共 36 次、`SetBackPic('grass')` 23 次。24 位图按 D3D 规则 Alpha = 1，理论上会盖一层不透明黑；但脚本一直这么用而画面可看，说明实际效果要么是没有画（读取失败 / 被格式转换成全透明），要么是被某个渲染状态抵消 [H]。**需要对原版同一帧截图确认**（`grass` 的 23 次集中在每个脚本开头）；移植第一版建议这两个调用留空操作。

相机：`Movie.SetCameraPos(x, y, z, a, b)` → `Camera_SetOrientationFromAngles(&{x,y,z}, a, b) @0x41b1f0`：游戏相机帧（`DAT_0061adfc`）`SetIdentity`；绕帧的 right 轴转 `a`（俯仰，度）；绕 (0,1,0) 转 `b`（偏航，度）；再平移到 (x,y,z)（RenderWare 后连乘，行向量约定）。首次调用时保存原相机角度 / 位置，脚本结束时 `Camera_RestoreViewState` 还原。投影（FOV / 近远裁剪）沿用游戏相机，没有改。

### 2.4 `Movie.*` / `CTime.*` 函数表

「句柄」 = 原版传回 Python 的对象指针（int）；移植用索引。`角色` 对象 = `MovieRole`（`MovieRole_Constructor @0x511aa0`，vtable 0x5e8cf8，Mesh 基类的 4 方向纸片人，内嵌 movieWord 气泡 +0x3C）；`物件` = `CUi_movieWord_ctor3 @0x511e30`（0x40 字节，vtable 0x5e9060，气泡 vf0 / vf1；+0x10 文章对象、+0x14..+0x1C 目标位置、+0x20..+0x28 当前位置、+0x2C..+0x34 累积缩放（初值 1）、+0x38 速度（初值 100）、+0x3C 角度（初值 0，创建时设成 180））。

| 函数 | 地址 | 行为 | 移植对应 |
|---|---|---|---|
| `Movie.RoleCreate(r,x,y,z,type)` | 0x5125d0 | `new(0x58) MovieRole`；`type` 0 / 2 / 3 → 取 roleinfo[r] 的 artId 第 0 / 1 / 2 组（4 个朝向模型，`+0x308 + 组*0x10`）、`type == 1` → NPC 表；`Anim_PlayMovieClip @0x47bc10`：载 4 个模型、`Mesh_AccumulateScale(roleinfo+0x338 缩放三元组 1.3,1.6,1)`、**按相机当前偏航 −180° 转纸片人**（`Mesh_Rotate @0x47a790`）、初始动作 2（站）、`+0x34 = roleinfo+0x380 = 步行速度`（120 之类，既是移动速度也是转向速度基数）；`Movie_SetRolePos @0x511bd0`；入列表 | `roles.cpp` 的纸片人（`rm8RolesPlace`）拆出独立「娃娃」对象（4 朝向模型、动作号、位置、缩放），不走回合流程 |
| `Movie.RoleDel(h)` | 0x512820 | 从列表移除并 `vf0(1)` 删除 | 娃娃销毁 |
| `Movie.RoleMove(h,x,y,z)` | 0x512990 | 只写目标位置 `+0x4c..+0x54`；每帧 `Mesh_MoveTowardTarget @0x511d20` 以 `+0x34` 单位 / 秒直线逼近（不转向、不换动作） | 每帧位移 |
| `Movie.RoleRotate(h,dir)` | 0x512a20 | vf10 `Path_SelectTarget @0x47bea0(dir, 0)`：`dir` 0..3 是 4 个朝向模型的下标；与当前相邻（同奇偶或同半区）时做**转身动画**（`Mesh_UpdateRotation @0x47bcd0`：转速 `540° × (+0x34/100)`/秒，转到 90° 时换模型下标并翻 180°），否则直接换 | 4 模型间转身 / 切换 |
| `Movie.RoleFace(h,dir)` | 0x512a90 | 同上但 mode = 1：立即换模型下标（`Mesh_SelectItem @0x47adf0`） | 立即切模型 |
| `Movie.RoleSetAnim(h,n,count)` | 0x512b00 | vf5 → `Mesh_SetTexture @0x47a570` → `Article_SelectByTypeAndSetProperties @0x445860(n, count, 0)`：role.bin 动作号 n（0 走 1 骰 2 站 3 待机 4..13 表情，`presentation.md 2.3`）、`count` 播放次数（0 循环 / 1 一次 / ≥ 0x80000000 取默认）；脸部贴图随动作换 | 已有 `rm8RoleExpression` / 动作播放；加 count |
| `Movie.RoleSpeak(h,id)` | 0x512b70 | 角色 +0x3C 气泡 vf0 `CUi_movieWord_vf0 @0x514030`：首次载 `movieWord.ui`（256×128，贴图 1087），文字 = `string.bin[id]`（`word` 白 + `word2` 阴影 +1 像素），每帧用头顶投影定位（`vf3 @0x511c10`）；**无语音、无口型** | `present.h` 的 `RM8SPEAK_BUBBLE`（现在一座一个，过场要「每句柄一个」） |
| `Movie.RoleStopSpeak(h)` | 0x512be0 | 气泡 vf1 `@0x514190` 隐藏 | `rm8RoleStopSpeak` |
| `Movie.RoleSetPos(h,x,y,z)` | 0x512c40 | `Movie_SetRolePos`：立即放置（同时写目标） | 位置 |
| `Movie.RoleSetSpeed(h,s)` | 0x512cd0 | `+0x34 = s`（移动速度；同时改变转身速度） | |
| `Movie.RoleSetScale(h,sx,sy,sz)` | 0x512d30 | `Mesh_SetScale @0x47a9e0`：目标缩放 ÷ 记录的当前缩放（+0x10..）得比例再累乘 = **绝对缩放，替换 roleinfo 的 1.3/1.6/1**（4 个朝向模型共用同一个值）。**已用用户的原版截图核对**（da17_2 舞台：前景英雄在底边、整个舞台可见；若误当作「乘上 roleinfo 缩放」英雄会大 3 倍遮住半屏） | |
| `Movie.ArtCreate(id,x,y,z)` | 0x512db0 | `new(0x40)`；`MovieArticle_Initialize @0x511f30`：`CArticleDFF_Load(id)`（role.bin 记录：dff + 贴图目录 + 动作表）、`CArticle_SetRotationAngle(180)`、`MovieObject_SetPosition @0x511f80`；入列表 | `rm8SceneAddArticle` / `rm8FxPlay` 的通用版（需要导出所有用到的 art，见 3.2） |
| `Movie.ArtSetAnim(h,n,count)` | 0x512ea0 | `Article_SelectByTypeAndSetProperties(n, count, 0)`：role.bin 动作号 n（按记录里的 id 二分查找）、次数同上 | `rm8SceneArticleAnim` + 次数 |
| `Movie.ArtDel(h)` | 0x512f10 | 移除并 `CUi_movieWord_dtor4 @0x511ed0` + `free` | `rm8SceneRemoveArticle` |
| `Movie.ArtMove(h,x,y,z)` | 0x513090 | 写目标 +0x14..+0x1C；每帧 `SpeakAside_MoveArticleToTarget` 以 `+0x38`（默认 100）单位 / 秒平移（仅「可平移」类物件：文章标志 +0x5c / +0x6c == 1；+0x68 == 1 的另一类走 `Article_SetupFrameForTransform`） | 位移 |
| `Movie.ArtRotate(h,a)` | 0x513120 | `CArticle_SetRotationAngle @0x512380(a)`：绕物件自身原点、轴 (0,1,0)、**转 (旧值 − a)°**；总旋转 = −a，创建时 a = 180（所以默认已翻 180°） | 偏航 = −a（符号 [H]） |
| `Movie.ArtSpeak(h,id)` / `ArtStopSpeak(h)` | 0x513190 / 0x5131f0 | 物件自带的气泡（vtable 0x5e9060 vf0 / vf1），位置 = 物件包围球顶（`2.2 × 半径 × 缩放 + y`）的投影 | 同 RoleSpeak |
| `Movie.ArtSetPos(h,x,y,z)` | 0x513250 | `MovieObject_SetPosition`：立即放置并同步目标 | |
| `Movie.ArtSetSpeed(h,s)` | 0x5132e0 | `+0x38 = s` | |
| `Movie.ArtSetScale(h,sx,sy,sz)` | 0x513340 | `CArticle_ApplyScale @0x512460`：围绕物件中心、按累积比例做相对缩放（记入 +0x2C..+0x34） | |
| `Movie.PlaySound(id,n)` | 0x5133c0 | `CAudioMgr_PlaySample(id, 0,0,0, 1000, 50, 1.0, n)`：n = Miles 循环次数；返回样本句柄（脚本不再使用） | `rm8SndPlay(id, n)` |
| `Movie.PlayMusic(id)` | 0x513430 | `CDigitalAudio_PlayBGMFaded(id, 0,0,0, 1.0, 0,0,0)`：曲目 41..50（`mus00041..50.mp3` 都存在） | `rm8MusicPlay(id, true)`；进入引擎时 `rm8MusicStop()`，退出时 `rm8SndStopAll() + rm8MusicStop()` |
| `Movie.SetBackPic(name,dir)` | 0x513490 | 先 `DelBackPic`，再 `Movie_SetBackgroundPicture @0x514fe0`：`RwImageSetPath(<游戏目录>\texture + dir)` 读 TGA 存 +0x48（2.3 的「后画」层；贴图全黑，效果存疑） | 第一版空操作（2.3） |
| `Movie.SetFrontPic(name,dir)` | 0x513500 | 先 `DelFrontPic`，`Movie_SetForegroundPicture @0x5150c0` 存 +0x4c（「先画」层，同样全黑；`shine5` 不在包里） | 同上 |
| `Movie.DelBackPic()` / `DelFrontPic()` | 0x513570 / 0x5135a0（实现 0x5151a0 / 0x5151d0） | `RwTextureDestroy`、清 +0x48 / +0x4c | |
| `Movie.SpeakAside(id)` | 0x5135d0 | `Movie_PlayDialogText @0x515200`：`string.bin[id]` 设给 `asideWord` 与 `asideWord2` 并显示整张 SpeakAside.ui（底部 1024×128 半透明条，白字 16 号居中，宽 945）；**无语音**（脚本另用 `PlaySound` 放旁白声） | `ui.h` 底部字幕条 |
| `Movie.StopSpeakAside(id)` | 0x513620 | 隐藏整条（参数被忽略） | |
| `Movie.SetCameraPos(x,y,z,pitch,yaw)` | 0x513650 | 见 2.3，瞬时 | 设相机帧 |
| `Movie.QuitMovie()` | 0x5136d0 | 结束本脚本（2.2） | 结束 |
| `CTime.Wait(sec)` | 0x5160b0 | `+8 = sec` | 引擎计时器 |

统计（`story_movie_stats.py`）：`RoleCreate` 56 次（55 次 type 0、1 次 type 2；用到的角色下标共 14 种：0..6、8..13、15，缺 7 和 14）、`ArtCreate` 318 次（213 个不同 art，**全部在 role.bin 里**，`movie_arts.tsv`：舞台 `anm\NN\dta*`、`NPC\N23_*`、`sign\a04`…）、`PlaySound` 368 次（126 个不同样本，循环次数 1..55；样本 221~6506，**样本 6506 不存在**，其余都在）、`PlayMusic` 67 次（曲目 41..50）、`SpeakAside` 84 次 / `RoleSpeak` 33 / `ArtSpeak` 60（字符串 6700..6869，全部存在，**都不带 `@` 语音**）。

### 2.5 输入

整个电影场景（vtable 0x5dff48）**没有键盘处理**，鼠标只有一个入口 [V 反汇编 `@0x4059f0`、`@0x403aa0`]：`CApp_OnMouseButton` 在**左键按下（code 0x110）**时调用场景 vtable `+0x34` = `Cls_5dff48_vf13 @0x515b50` → `Movie_ReleaseAndRestoreView`（销毁当前脚本对象，下一帧 `vf1` 取队列的下一段或 `Movie_Finish`）。键盘键码 0xa7 在 `CApp_OnKeyDown @0x405a90` 里被合成成鼠标左键按下 / 抬起（键盘模拟鼠标 [M]），也能跳过。右键（0x120）走 `CApp_HandleInputEvent`，对电影场景无效果。**所以：点击 = 跳过当前这一段（不是整部影片）；start 过场有 2 段（`da17_1` + 角色专属 `daXX_1`），点两下才进游戏。** 主持人演出（1.3）的左键跳过是另一个场景处理（`Cls_5e81e0__vf13 @0x5041f0`），同一个 CApp 入口。

### 2.6 Bink 影片

`Bink\Start.bik`（开场）与 `Bink\Staff.bik`（片尾，结局之后）经 `CApp_SwitchToStartMovie(0/1) @0x4052c0`（`binkplay=0` 跳过）。移植已有 `video.h`（离线转成 MPEG-1 + MP2，`rm8VideoOpen`）；`docs/media_collect.md`（collect 模块在写）负责。

### 2.7 完整度（第 2 部分）

| 部分 | 状态 | 说明 |
|---|---|---|
| 生命周期、调度、计时、跳过 | **够** | 全部读了函数体 / 反汇编 |
| 所有 31 个 `Movie.*` + `CTime.Wait` 的参数与基本语义 | **够** | `PyArg_Parse` 格式串与调用的 C++ 函数都读了 |
| 文章（物件）动画机制细节（`count` 的 n > 1 语义、`ArtRotate` 符号、按类型的平移 / 旋转支持 +0x5c / +0x68 / +0x6c） | **部分** | 需要在移植里对 213 个 art 逐类验证（先不支持的类型会不动 / 不转） |
| 渲染细节：背景清屏色、灯光（是否用场景灯、舞台 dff 是否预烘焙）、相机裁剪 | **缺** | `u_Render_SetGlobalMode` 与 `Article_RenderAll` 未展开；`docs/README.md` 提到 `dta01_02_front.png` 渲染正常，说明用 librw 默认灯即可呈现，但与原版的亮度差异未对比 |
| 气泡的精确位置（包围球系数） | **部分** | 角色 `2 × 半径 × 缩放 + y`（`@0x511c10`），物件 `2.2 × 半径 × 缩放 + y`（`@0x512150`），半径来自文章包围球（`Article_CalcClumpBoundingSphere`） |
| 脚本语义 | **够** | 40 个脚本只有 `Movie.*` / `CTime.Wait` / `yield`，`story_movie_stats.py` 已统计 |

---

## 3. 移植实施方案

### 3.1 现状（`port/app`）

- **有**：回合 / 规则（`roles.cpp` 925 行、`game.cpp` 466 行）、卡片、NPC（`rm8NpcStory(puzzles)` 已能放拼图）、表现层（`present.h`：气泡、表情、特效播放器 `rm8FxPlay`、`rm8PresentSetStory`）、音频（`rm8SndPlay / rm8MusicPlay`）、菜单（`menu.cpp`：故事和自由共用地图 + 角色菜单，boxmenu / finalMenu / picmenu 未做）、`video.h`（Bink 转 MPEG-1）、内嵌 CPython 2.4（`python.cpp`：`Movie` / `CTime` 模块目前是占位）、`collect.h`（只有 `rm8PuzzleCollect`，在写）。
- **没有**：故事状态（运行对象、pass、箱子）、过场引擎、主持人演出、结算链（`game.cpp` 里 `0x74 / 0x75` 只打日志，`0x76 / 0x77 / 0x7a / 0x7d / 0x7e / 0x7f` 无处理）、gamedate 档案。

### 3.2 模块与文件

| 新文件 / 改动 | 内容 | 规模 |
|---|---|---|
| `app/profile.h/.cpp`（或并入 `collect.cpp`） | gamedate 的读 / 写（181 个 int + 魔数表，格式见 `scripts/gamedate.py`，保持与原文件互通）：`rm8ProfileStage(role)`、`rm8ProfileSetMaxStage`、`rm8ProfileRoleUnlocked(r)` / `Unlock`、`rm8ProfilePiece(role,k)` / `SetPiece`、`rm8ProfileBits/Counter/Flag2/Flag3` | ~250 行 |
| `app/story.h/.cpp` | `Rm8StoryRun`（level、箱 {cash, tickets, 8 卡}、待带入 {cash, tickets, 8 卡}）、pass 表加载（`scenes/story.txt`）、候选图算法（1.3）、`rm8StoryConfirm(map)`（补对手、定 `setup+8`）、`rm8StoryApplyStart(seat 0)`（1.5）、结算：`rm8StoryOnWin / OnLose / OnContinue`（1.7~1.9）、`rm8StoryReward()`（1.8 第 3 步）、进度写入与解锁（1.8 第 4~5 步、1.10）、剧情脚本事件（`RunStoryScriptEvents`、`OnParkEvent`、`OnDiceEvent`、`OnChainStoreEvent`、新闻 0x1e、拼图条件表）、存档段（sflb） | ~1100 行 |
| `app/movie.h/.cpp` | 过场引擎：脚本队列（读 `movie.bin` 导出表）、生成器驱动（`rm8PyMovieStart(module)` / `Step()`，用内嵌 CPython 跑原 `.pyc`）、对象表（句柄 → 娃娃 / 物件）、计时（`Wait` 不缩放，默认 1.6×0.7）、相机（俯仰 / 偏航 / 位置）、字幕条与气泡、背景 / 前景图、音频、点击跳过、结束回调（start → 开棋盘，end → Staff 片尾 → single 菜单，viewer → finalMenu） | ~900 行 |
| `app/python.cpp` | 把 `Movie.*` / `CTime.Wait` 从占位换成 `movie.cpp` 的实现（31 个小包装，第 1~2 参数解析照 2.4 表） | ~200 行 |
| `app/roles.cpp`（拆分） | 纸片人娃娃独立 API：`Rm8Doll`（创建 / 方向 / 动作 + 次数 / 位置 / 目标 + 速度 / 缩放 / 气泡）；4 模型转身动画（540°/s × 速度 / 100） | ~250 行改动 |
| `app/present.cpp` | 气泡改为「每对象一个」的通用版（`rm8BubbleCreate(anchorFn)`），过场与棋盘共用；字幕条 | ~120 行 |
| `app/menu.cpp`（或 `menu_story.cpp`） | 故事流程：角色菜单规则（1 座位、服装、解锁灰显、选角色清箱）、地图菜单（候选图、自动选中、返回限制、0x13 箱子按钮、确认）、**主持人演出**（art 1048 + 台词 3801..3835 + 样本 + 状态机 1.3）、boxmenu 全屏（现金 / 点券 / 8 + 8 格）、finalMenu 影片欣赏、读故事档 | ~900 行 |
| `app/game.cpp` / `roles.cpp` / `events.cpp` / `npc.cpp` | 钩子：0x74 / 0x75 / 0x76 / 0x77 / 0x7a / 0x7d / 0x7e / 0x7f 处理；弃牌框「存入储藏箱」按钮；破产特例（金泡菜 / 莎拉）；莎拉住院 / 坐牢询问；通关 / 失败调用 story；`start_pic` 拼图 NPC；酷娃 / 萊娜特例 | ~500 行 |
| `scripts/export_story.py`（新） | 导出：`scenes/story.txt`（16 个 pass：stage、level、对手、start / reward 全字段；`movie.bin` 的 start / end 脚本名；drama.bin；主持人 string / 样本）、`scenes/movie_arts.txt`（213 个 art：dff、贴图目录、动作表 `id:anm`，格式同 `fxart` 行，用 `export_scene.art_table`）、`scenes/movie_strings.txt`（6700..6869）、`scenes/story_missions.txt`（1.11 表） | ~300 行 Python |
| `pysrc` | 原版 `da*.pyc` 在游戏目录 `Pysrc\` 里，内嵌解释器已经从那里加载 AI / 卡片脚本，**无需再导出** | 0 |

**总规模**：C++ 约 4200 行（改动 + 新增），Python 约 300 行，数据导出若干 txt。

### 3.3 与现有系统的依赖

- **规则**：`rm8GameNewGame` 要接受「按座位覆盖起始现金 / 存款 / 点券 / 起始卡」（现在只有统一的 `Rm8GameOptions`）；结算需要 `rm8TurnClearQueue + rm8TurnEnqueue` 的现成接口；`rm8GameCheckBankruptcy` 里 `0x74 / 0x75` 的分支要改为调用 story。
- **回合流程**：`Role_RunTurnStartStep` step 2 / `Role_BeforeRollDice` 的拼图条件检查要在 `roles.cpp` 里加两个钩子点（`rm8StoryTurnStart(seat)`、`rm8StoryBeforeRoll(seat)`）。
- **collect 模块**：必须先有 profile 读写（stage、拼图 1..9、角色解锁位、三个位图 / 计数）。建议由 collect 提供 `profile` 接口，story 只调用；拼图收集 UI（0x58 消息的图）也归 collect。
- **菜单**：`Rm8Setup` 加 `level` 字段；`Rm8MenuHooks` 增 `loadGame` 的故事路径。
- **音频**：曲目 41..50、103 / 104 已在 `mus0000.dat`；样本 3801..3835 与过场 5xxx / 6xxx 样本已在 `snd0000.dat`。
- **场景**：过场需要一个无地图的 `Rm8Scene`（空 world + 默认灯）；`rm8LoadClump`、`rm8LoadAnim`、`rm8SceneAddArticle` 可直接用。
- **视频**：开场 / 片尾走 `video.h`。

### 3.4 实施顺序

1. **P0 数据与档案**（~1 天）：`export_story.py`；profile（gamedate）读写；确认与原 `gamedate.bin` 互通（用 `scripts/gamedate.py` 往返核对）。
2. **P1 故事骨架，不带过场**（~3 天）：Setup.level、角色菜单规则、地图菜单（候选图 / 自动选中 / 确认 / 补对手）、开局数值（`ApplyStart`）、结算链（`0x75 → 0x7e → 0x7d → 0x7a → 状态 4/5`）、失败与接关（重开本关）、进度与解锁写入、箱子数据（先不做 boxmenu 界面，用日志 / 固定策略「全带入」）。验收：从大老千一路打到魔都，gamedate.bin 的 stage / 解锁位与原版一致。
3. **P2 过场引擎**（~4 天）：`movie.cpp` + `python.cpp` 的 Movie/CTime；先在 `--movie da01_1` 的独立入口里逐个脚本播放对照原版截图（相机、物件动画、气泡、字幕、声音）；再接 start / end 触发、点击跳过、Staff 片尾。
4. **P3 主持人演出与 boxmenu / finalMenu**（~2 天）。
5. **P4 剧情规则例外**（~3 天）：1.12 的钩子逐个接（RunStoryScriptEvents、公园事件、drama 对白、新闻 0x1e、连锁超市建筑、拼图条件表、莎拉 / 金泡菜特例、角色 13 / 16 子类）。其中角色 13 / 16 子类需要先补读（1.16）。
6. **P5 存档 / 读档**（已做，见 `docs/save_port.md`）：sflb 段与 setup+8 随记录读写（`save.cpp`），读档走 `rm8SaveStoryLoadSetup`；读后重新激活连锁超市（`Game_ActivateCharacterBuilding`）：特殊建筑后来已移植（`characters.md` 7.2），读档重建场景走同一个 `rm8RolesSpawn` → `rm8StoryActivateBuildings` [A，未单独测读档后的旗标]。

### 3.5 风险与未知

1. **过场渲染保真度**：舞台 dff 的光照 / 清屏色 / 裁剪面未逆向（2.7），可能出现整体偏暗或穿帮；对策：先按 librw 默认灯渲染，再按项目已有的「原版对照方法」截原版窗口同一帧对照（只截图，不读内存）。另有一条已知疑点：`SetBackPic` / `SetFrontPic` 的两张贴图全黑（2.3），先当空操作。
2. **文章动画机制**：`count` 语义、`ArtRotate` 的符号、不同文章类型（+0x5c / +0x68 / +0x6c）对「移动 / 旋转」的支持，需要在 213 个 art 上逐个试；脚本里 `ArtMove` 只有 54 次、`ArtRotate` 2 次，范围可控。
3. **角色 13 / 16 子类**与剧情事件的未读细节（0x47 / 0x52 / 0x7b / 0x7c 消息）：先按 [M] 实现，用 drama 文本验证触发时机。
4. **莎拉跳关、金泡菜复活等特例**依赖的队列消息顺序（`0x75` 的 b 参数 / `0x76` 的 okcancel 回填）在移植的队列里要等价：`rm8TurnModalResult` 已有，注意 `0x80000000` 占位。
5. **「新开局清箱」「重开不再给携带物」**只是静态结论，实机未验证；建议第一版照代码实现，同时留开关。
6. **存档兼容**：若要读原版 `record_NN.sav`（带 sflb），解析器在 `scripts/savefile.py`，但现有移植存档模块（别人在做）的格式未知，需对齐。
7. **输入**：安卓无鼠标，点击跳过要对应触摸（点一下 = 跳过一段），避免误触：建议点击需间隔 ≥ 0.5 s。
8. **脚本里一个失效样本（6506）**：原版同样无声，移植不要报错。
9. **UI**：地图菜单主持人用 3D 纸片人（art 1048）+ 文字，移植可用现有 art 渲染 + `ui.h` 的标签。

### 3.6 开放问题（需要后续确认）

- 角色 13（伊格魯）/ 16（莎拉）的子类具体做什么（跟随演员、莎拉的计数 `+0x98`）。
- `Movie_InitEngine` 把 `app+0x350` 设为 9 的作用（推测：事件状态，屏蔽输入 / 光标）。
- 乐透在故事里的差异（`Lottery_InitScrollBar @0x49f810`，`+0xdcc` 取值）。
- ~~每个 `drama` 的 `0x52` 特效参数（0x8df / 0x9c5 / 0x8f6）是 art 号还是字符串号~~：**art 号**（`article\eff\GS-1 / GS-2 / GS-3.dff`），已移植（`characters.md` 第 9 节）。
- 酷娃「没收卡」是否对所有卡生效，还是只对飞弹类（string 570 / 571）。
- 拼图图鉴（picmenu）的显示规则由 collect 模块负责，本文只给出拼图的来源（reward / start / 条件）。

---

## 4. 移植实现状态（story / movie / story_host，对照第 1~3 节）

标记同前：`[V]` 已对反编译核实（给地址）、`[M]` 推断语义、`[H]` 假设、`[A]` 移植侧取舍。验证方式：桌面版 `rm8_app`（`build-story`），菜单走 `RM8_MENU_AUTO` 的 `tap:<id>`（真实指针路径），长跑用下表的环境变量；Android 版 `build-story-android` 已通过编译（`libmain.so` 链接成功），没有出 APK。

### 4.1 文件与阶段完成度

| 阶段 | 内容 | 状态 | 文件 |
|---|---|---|---|
| P0 数据 | pass 表（16 个英雄 133 条）、movie.bin（start / end 脚本名）、拼图条件表（77 条）、drama.bin（42 段）、过场用 art 表（211 条，含 host art 1048，`mart` 行带 UV 动画字典） | 完成 | `scripts/export_story.py` → `port/scenes/story.txt`、`movie_arts.txt`；`android/deploy.py` 的共享文件元组已加两项 |
| P1 运行时 | 候选图、确认（补对手、定层）、开局数值、储藏箱携带、通关链 0x75→0x7e→0x7d→0x7a、失败链 0x74→0x76→0x77 / 状态 3、进度写入与解锁、结局（解锁莎拉 / 酷娃 + 结局影片 + Staff 片尾） | 完成 | `app/story.h/.cpp`；`game.cpp`（消息 / 破产 / 公园 / 新闻钩子）；`rm8_app.cpp`（宿主：重开 / 角色菜单 / 地图菜单 / 结局） |
| P2 过场引擎 | `Movie.*` 31 个函数 + `CTime.Wait`，脚本生成器驱动（内嵌 CPython 跑原 `da*.pyc`），纸片人（4 朝向、转身、动作 + 次数、移动、缩放、气泡）、物件、相机、字幕条、声音 / 音乐、点按跳过当前段、40 个脚本全部跑通 | 完成（见 4.4 的未决项） | `app/movie.h/.cpp`；`python.cpp` 的 `kImplMovie` / `rm8PyMovie*` |
| P3 host 与菜单 | host 演出（art 1048 + 3801..3835 台词 + 语音 + 状态机 + 点击跳过）、角色菜单故事规则、地图菜单故事分支、boxmenu 接入、finalMenu | 完成 | `app/story_host.h/.cpp`；`menu.cpp`（`enterRoles` / `enterMap` / `enterFinal` / `rm8MenuOpenPage`）；`menu.h` |
| P4 规则例外 | 拼图条件、开局事件（大老千贷款 / 讨债，孫小美提示）、公园对白与通关、錢夫人遇沙隆巴斯、莎拉住院 / 坐牢跳关、莎拉 / 金泡菜破产特例、金泡菜解锁（孫小美放障碍物）、星期三 / 伊格魯解锁、怪兽新闻 0x1e | **大部分**（缺项见 4.4） | `story.cpp` 的 rules 段；钩子在 `roles.cpp`（步骤 2 / 空队列 / 掷骰前 / 步数 18）、`game.cpp`（公园 0x1D、新一天）、`present.cpp`（相遇）、`cards.cpp`（卡片使用标志、住院 / 坐牢）、`python.cpp`（`Npc.Create`）、`collect.h/.cpp`（gamedate 的三个计数 / 位图）、`events.h` + `event_bank.cpp`（`rm8EventSetLoan`） |
| P5 存档 | 按任务书跳过（没有存档模块），箱子自带 `rm8_box.bin`，进度在 `rm8_gamedate.bin` | 未做 | — |

### 4.2 核实过的行为（带地址，比文档第 1~3 节新增 / 修正的）

1. **host 文字**：见 1.3（chsMapMenu.ui 没有 0x34 控件，原版只有语音 + 动作）。语音按 `Game_PlayMovie @0x504460`：停上一句、播 `snd<id>`（字符串 `@` 后的数字，3801.. 的号与字符串号相同）、host 动作 `anim` 播一次后回 3（`Ui_InitTexture @0x5030e0`）；语音在播时计时器不走（`+0xC`，`@0x5043d0` 清零）。状态机 `@0x503830`、跳过 `@0x5041f0` 如 1.3 表（点击：3..8→0x65，0xc..0xf→1）。host 渲染：恒等相机、窗口 0.3×0.225（宽屏按 4:3 区域保持）、`RwCameraClear(z)`、alpha 测试 ≥196，位置 (100,−100,500) 转 180° [V `*(float*)0x5e76b0`]。
2. **不可选的地图是「隐藏」不是「禁用」**（`Game_PopulateMapList @0x503510`：先 `vf+0x1c` 隐藏 1..11，再对候选 `vf+0x18` 显示 + `vf+0x20(id,1)` 启用）；故事版 `Game_InitOptions @0x5031c0` 隐藏的是 13..18、61..66、71..76、81..86 四组控件并显示 0x13。
3. **角色菜单**（`@0x5065f0` / `@0x505940` / `@0x505a80`）：点角色 = 在 `0..min(stage,3)` 里找第一个该角色没用过的服装，加座位；故事模式再 `Game_ClearBinaryData`（原版**无条件**清，移植只在故事时清 [A]）；未解锁（`Event_GetTerrainData == 0`）的角色灰掉；服装按钮 19..22 对应服装 0..3，`stage ≥ n` 才可点，服装 4 在 `stage ≥ 3 且 9 块拼图齐` 时可点，按钮是 23 还是 33 由 roleinfo `+0x304` 决定（取 sex，**[H]**）；开始：故事必须恰好 1 个座位（`@0x506a30`）；返回：故事 → single 菜单（`@0x5069b0`）。座位的服装已传给棋盘（`rm8RolesSetOutfits`，body 贴图 `A%02d_body_f_%02d`；服装 4 的贴图不一定存在，缺了就用默认）。
4. **选角色的语音**（用户反馈 4，`Cls_5e8140_PlayRoleSample @0x506b20`）：语音 = 字符串 `roleinfo.strId + 0x1d` 的 `@` 样本（`StringTable + 32000 + 索引×4`），先停上一条；移除选中座位时停（`@0x5065f0`）；离开角色菜单时停（`@0x505a10`）。自由模式同一份代码，已一并修。
5. **通关链**：`0x75(a,b≠0)` 在**最后一个存活的人类**的队列上排队（`Role_CheckBankruptcy @0x48f245..0x48f26c`：先给破产者 `0x73`，再对存活者 `Role_ClearMsgQueues + 0x75(0,1)`，`ECX` 不是 `this`）；移植的 `rm8GameCheckBankruptcy` 已改。`0x7e` 的 14 s 通关特效可被点击跳过（`Role_StopMoveAndIdle @0x484790` 对状态 0xD / 0xE 同样处理），结束时恢复棋盘音乐（角色状态机 case 0xd / 0xe，`RichMan8_named.c` 64756 行附近：`StopBGMWithFade` + `ManageBGMTransition(-5)`）。`0x7d` 的奖励卡进储藏箱（满则丢，文字照出）。
6. **大老千第 1 天**（`Role_RunStoryScriptEvents @0x483f40`）：`Bank_TrySetLoan @0x47ea00` **只是记录**贷款字段（本金 150000、今天、3 个月），不加钱；随后 `Bank_SetMaxMoney @0x47e800` 把现金 / 存款各设成 50000。移植新增 `rm8EventSetLoan`（只记录）。
7. **拼图条件**（`OnStockMarketAccess @0x491a10`）：条件 1 = `Role_CalculateNetCash(1)`（存款 + 现金 − 贷款 + 股票 + 所有地产 `Terra_CalcPrice(1)`）；6 = 整条街的地产都是自己的（`@0x491930`，**[V]**，文档 1.11 原写 [M]）；7/9/10/11/13/14/18/19 用 `Role_CountOwnTerras @0x48c430`（kind 0 住宅、1 任意商业、2/3/4 研究所 / 连锁 / 饭店、9 全部）；12 = 住过院（`+0x5c0`，由 `SendToHospital` 置位 [H]，移植在检查时轮询状态 4）；15 = 公园次数 `+0x378`；17 = 掷过 18 步（`BeginMoveSteps @0x484f90` 里 `+0x5cc`）；≥100 = 用过卡 `cond−100`（`Role_FinishCardUse` 写 `+0x4c0[id]`，移植在 `cards.cpp` 的 `finishUse` 里记）；16 = 有奇迹（奇迹未移植，恒假 [A]）。检查点：回合开始步骤 2、状态 8 队列空时、`Role_BeforeRollDice`（非 0 则入队 100 重试）。
8. **怪兽袭击的剧情分支**（`OnParkEvent @0x48d310`，消息 `0x31`；移植 `rm8StoryMonsterHit`，在 `news.cpp` 的 `0x31` 处理里先调用）：见 1.12 的该行。**2026-11-15 审核更正**：旧版把这些分支挂在「走到公园格（event 4）」上，并把 `param_1+0x4c == 0` 猜成「到访者是座位 0」[H]——实为 `0x31` 的参数是地块、`+0x4c` 是地块地主；结果是 3 / 6 / 7 / 10 / 11 / 13 / 15 号英雄的剧情（含金泡菜层 1 通关、舞美拉解锁伊格魯）原先只会在走到公园格时误触发，怪兽新闻来了却什么都不演、还每天重复来（`Game_TriggerEventByDate` 看座位 0 的计数 < 1）。`0x52` 特效的对象是地块（`b` = 地块号）。
9. **失败**：`0x74` → str93 okcancel → `0x76`：确认 = `0x77` 重开（同一份 setup，不再给携带物、不重播过场，**已验证**）；取消 = 状态 3 回角色菜单。[A] 回角色菜单时把座位裁成英雄 1 个（原版保留 3 个对手座位，开始按钮要求恰好 1 个座位，所以原版得手动删；移植裁掉避免卡死）。
10. **进度**：`Game_UpdateRecordStats` 写 `run.level`（箱子对象）+ `Event_SetMaxLevel`（gamedate stage），结局 `Event_SetTerrainData(16)`，詹姆斯再解锁 12；已用「RM8_STORY=15 连打」实测写回 `rm8_gamedate.bin`（stage 4、解锁 12 / 16、拼图 3 / 7 / 9）和 `rm8_box.bin`。

### 4.3 过场引擎的实现要点（`movie.cpp`，对照 2.1~2.5）

- 调度如 2.1~2.2：每个 `daXX_N` 一个「段」，段间销毁重建场景对象；首个 `next()` 在 `ScaledWait(1.6)` 之后；`CTime.Wait` 直接设计时器、不缩放；`QuitMovie` / `StopIteration` / 错误 / 100 次恢复结束本段；全部播完关背景乐、停声音（`Movie_ShutdownEngine @0x515850`）。
- 渲染：纸片人（alpha 测试 ≥196）→ 物件（`rm8RenderArticles`，两遍）→ 粒子 → UI（气泡、字幕条）；`SetBackPic` / `SetFrontPic` 是空操作（2.3：两张贴图是废的）；灯光是 librw 默认的环境光 0.75 + 平行光 0.5 **[A]**，相机近 / 远裁剪 58 / 5800（`scripts/game_camera.py` 读到的游戏相机），视窗 0.225·w/h × 0.225（竖直视角不变，宽屏横向加宽）。
- 相机 `SetCameraPos(x,y,z,pitch,yaw)`：恒等 → 绕右轴转 pitch → 绕 (0,1,0) 转 yaw → 平移（`Camera_SetOrientationFromAngles @0x41b1f0`），经用户确认的原版 / 移植截图对比无需改动（桌面窗口里整个舞台可见、英雄在底边）。
- **`Movie.RoleSetScale` 是绝对缩放**（`Mesh_SetScale @0x47a9e0`，见 2.4 表）；早期版本把它当成「乘上 roleinfo 缩放」，英雄会大 3 倍遮住半屏（用户在手机上看到的就是那个版本，已修）。`ArtSetScale` 同样是绝对（`CArticle_ApplyScale @0x512460` 先除以累积值）。
- 动作次数（`RoleSetAnim` / `ArtSetAnim` 第 3 参数）：1 = 播一次停在末帧，n > 1 = 播 n 次后停在末帧 **[H]**（原版语义没有细读），0 = 循环。
- 点击 / 点触 / 任意键 = 跳过当前段（`Cls_5dff48_vf13 @0x515b50`），0.5 s 内不重复；Esc / Android 返回 = 跳过整部。
- 粒子 art（6120..6124 的雪 / 烟）走 `rm8FxPlay`，需要 `particles.txt` 已载入（`rm8MovieInit` 里载）。
- 已知小问题：`da05_2` 有一个 `article\anm\05\.tga` 空名贴图找不到（原版数据；移植只是不贴）。

### 4.4 未做 / 未决 / 假设

- **未做**（本条为 4.4 初稿，**已过时**；2026-11-15 复核：存档、乐透故事差异、公园 `0x52` 特效、`0x7b / 0x7c`、对白头像 `npc\p_18_01` 都已做，见 `characters.md` 第 9 节）：P5 存档（`record_NN.sav`、sflb）；乐透故事差异；（原先列在这里的 `OnSendGodCard` 酷娃飞弹豁免、詹姆斯连锁超市、角色 13 / 16 子类、奇迹条件 16 已在后来补上，见 `docs/characters.md`）；公园事件里的 `0x52` 特效；`0x7b` / `0x7c`（`+0x1c8` 标志）只吞掉；原版故事对白的头像 `npc\p_18_01` 不显示。
- **用户反馈 2（开场「英雄逐个入场、带动作和语音」）仍待确认**：翻遍 40 个脚本，**故事开场的两段 `da17_1`（旁白 + 舞台）和 `daXX_1` 里没有「英雄逐个入场」**；`da17_1` 只有舞台背景 + 旁白（`PlaySound(6203)` + `SpeakAside`），`PlaySound` 的样本在移植里确实播放（`RM8_AUDIO_TRACE` 可见）。最像的有三处：① `Bink\Start.bik` 开场影片（媒体模块，不是 Python 过场）；② host 开场介绍对手（3829 + 3 个名字，每句带语音，已实现，但**只有 host、没有对手模型**，原版有没有对手模型出场不确定）；③ 结局 `da17_2` 里 14 个英雄一起「原地走路」（`RoleSetAnim(r,0,15)` + `PlaySound(249,3)`）。需要用户 / 主管指明是哪一段、选了哪个英雄。
- **假设 [H]**：roleinfo `+0x304` = sex；`param_1+0x4c` 的含义；`Movie` 动作次数 n > 1；`SendToHospital` 置 `+0x5c0`；`ArtRotate` 符号 `−a`；怪兽新闻在座位 0 上触发。
- 没有对照原版同机位截图：灯光亮度、清屏色（过场用黑色）、字幕条宽度（原版宽屏拉伸，移植是居中 4:3 条）。

### 4.5 测试钩子（环境变量，供复现）

| 变量 | 作用 |
|---|---|
| `RM8_STORY=<角色>[,<地图>]` | 不走菜单直接开一局故事（`story.cpp` 的 `rm8StoryDebugStart`） |
| `RM8_STORY_WIN=<秒>` / `RM8_STORY_LOSE=<秒>` | 〔string.bin#7057〕后强制通关（座位 0 队列放 `0x75(0,1)`）/ 座位 0 钱清零 |
| `RM8_STORY_ANSWER=0\|1` | 状态 7 的对话框自动回答（接关 / 不接关） |
| `RM8_STORY_CHAIN=1` | 无菜单时自动选下一层的第一张候选图，一路打到结局 |
| `RM8_NOMOVIE=1` | 故事开局 / 结局跳过过场 |
| `RM8_MOVIE=<脚本,…>` | 只播过场脚本然后退出；`RM8_MOVIE_SHOTS=t,t` 在影片时钟到 t 秒截图（目录 `RM8_MENU_SHOTDIR`）；`RM8_MOVIE_SKIP=t` 在 t 秒跳过当前段；`RM8_MOVIE_SPEED=n` 加速时钟；`RM8_MOVIE_NOSKIP=1` 忽略输入跳过；`RM8_MOVIE_TRACE=1` |
| `RM8_DATA_DIR=<目录>` | 档案（`rm8_gamedate.bin` / `rm8_box.bin`）放别处，目录必须已存在（测试不污染 `port/`） |
| `RM8_MENU_AUTO` 新记号 `skip` | 等同点击 host（`Cls_5e81e0__vf13_OnSkipMovie`） |

---

## 5. 故事事件总表与移植审核（2026-11-15）

范围：反编译里**所有**读故事标志（`Singleton_GetPtrArray_00632de8()[0] != 0`）的函数（`out/story/setup_sites.txt` 的 52 个函数，另有 30 个只读 `+4` / `+8` 的非故事函数，逐个看过），每个英雄 × 层的触发、移植的实现位置和验证。标记：`OK` = 与反编译一致；`已修` = 本轮发现错并改了；`差` = 与原版有差别（说明）。**天数 / 周几**：`g_GameState+0x14`（天数；开局的登场轮 = 第 0 天，第一轮真正的回合 = 第 1 天）、`+0x10`（周几，1 = 周一 … 7 = 周日，第一天用系统的 0..6）；**层** = `setup+8` = pass 条目的 `level`（0..3）；**座位 0** = `role+0x1a8 == 0`。故事规则都只对座位 0 的主角生效（`0x31` 怪兽处理按「座位 0 的角色」分派，见 E10..E18）。

### 5.1 事件表

| # | 英雄 / 层 | 触发条件 | 发生什么 | 地址 | 移植位置与状态 |
|---|---|---|---|---|---|
| E01 | 1 大老千 / 层 0 | 第 1 天，回合开始 step 2 | 贷款 150000 只记录（3 个月）+ 现金存款各设 50000 + drama 1（位置 1）+ 0x47 | 0x483f40 | `story.cpp rm8StoryRunScriptEvents`，OK（用户反馈的 bug：登场轮曾被算成第 1 天，已修为第 0 天；菜单路径与 `RM8_STORY=1` 都在第 1 天的回合触发） |
| E02 | 6 孫小美 / 层 0 | 第 1 天 step 2 | 文字 str7150（金泡菜提示）+ 头像 `npc\p_18_01` | 0x483f40 | 同上，OK（日志 `孫小美 day-1 hint`） |
| E03 | 1 大老千 / 层 1 | 第 > 9 天，`+0x374 == 0`，step 2 | 现金 + 存款 ≥ 250000 → drama 2 + `0x16(15000)`；够不够都置 `+0x374 = 1` | 0x483f40 | 同上，OK（`RM8_START_DAY=10 RM8_STORY_CASH=300000,0` 触发；层 2 阈值 300000，290000 不触发） |
| E04 | 1 大老千 / 层 2 | 同上 | 阈值 300000 | 0x483f40 | OK |
| E05 | 5 阿土仔 / 层 0 | 第 > 9 天且周三（`+0x10 == 3`） | `Event_SetFlag3(0)` 首次成功 → drama 29 | 0x483f40 / 0x4aa510 | OK |
| E06 | 10 烏咪 / 层 0 | 同上 | `SetFlag3(1)`（要先有位 0）→ drama 30 | 同上 | OK |
| E07 | 8 金貝貝 / 层 0 | 同上 | `SetFlag3(2)`（要先有位 0、1）→ drama 31，位 2 → 解锁 14 | 同上 | OK（顺序实测：先玩 8、10 不触发；5 → 10 → 8 依次 drama 29 / 30 / 31，解锁 14，gamedate `+0x88c` = 7） |
| E08 | 7 舞美拉 / 层 0 | 回合开始 step 2 且 `+0x37c != 0` | `Event_IncrementCounter`（`+0x884`，跨局）；< 5 → `+0x37c = 0` + drama `0x18 + 计数`（25..28）；> 3 → 解锁 13 | 0x483f40 / 0x4aa420 | OK（`RM8_STORY_HIT=1` 连续 4 天：drama 25..28，解锁 13）；`+0x37c` 的置位源 E18 **已修** |
| E09 | 全部英雄 / 层 0..2 | 回合开始 step 2、状态 8 队列空、掷骰前 | 拼图条件表：44 个（英雄 × 层）共 77 条 `(拼图, 条件)`；条件 1..19、≥ 100（用过卡 Id = 条件 − 100） | 0x4835e0 / 0x491a10 | `story.cpp rm8StoryMissionCheck`；44 个 key / 77 条与反编译**逐条机器比对 0 差**；条件 8（地图上全部类型 2 地块都是自己的）原先在「没有类型 2 地块」时为假，反编译的循环空转时为真，**已修** |
| E10 | 3 錢夫人 / 层 < 3 | 消息 `0x31`（怪兽袭击地块 T），T 的地主 == 座位 0 | `0x52(0x8df, T)` 金光 + drama 5（位置 1）+ `0x1e`；**不出怪兽、不拆房** | 0x48d310 | `story.cpp rm8StoryMonsterHit`，**已修**（原挂在公园格） |
| E11 | 3 錢夫人 / 层 3 | 同上，计数 ≥ 0 | `0x52(0x8df, T)` + drama 8 + `0x1e`，计数置 −1（只一次） | 同上 | **已修**（实测：第 1 次 story branch，第 2 次怪兽出现） |
| E12 | 6 孫小美 / 层 1 | `0x31`，本角色第 1 次 | `0x52(0x9c5, T)` + drama 10 + `0x1e`，不出怪兽 | 同上 | **已修**（hero 6 map 5 `RM8_START_DAY=10`：第 11 天怪兽新闻 → story branch） |
| E13 | 7 舞美拉 / 层 3 | 第 1 次 | drama 11 + `0x1e`，**怪兽照常出现** | 同上 | **已修** |
| E14 | 10 烏咪 / 层 0 | 第 1 次 → drama 12（怪兽照常）；否则 T 的地主 == 座位 0 → str573，不出怪兽 | 同上 | 同上 | **已修**（两种都实测） |
| E15 | 11 金泡菜 / 层 1 | 第 1 次 | drama 13 → `0x7b` → `0x2c(0x9c5)` → `0x7c` → drama 14 → `0x75(0,1)` 通关，不出怪兽 | 同上 | **已修**（map 5 第 11 天怪兽 → 通关 → 奖励 → 地图菜单；改前怪兽每天重复来、永不通关） |
| E16 | 13 伊格魯 / 层 0 | 第 1 次 → drama 15 + `0x1e`；**之后任何怪兽（含怪兽卡）一律 str574 且不出现**（任何层） | 同上 | 同上 | **已修** |
| E17 | 15 詹姆斯 | `0x31` 的地块地主的角色 == 3 | `0x52(0x8df, T)` + drama 17 + `0x1e` | 同上 | **已修**（`RM8_MONSTER_OWNER=1`） |
| E18 | 全部英雄（故事） | `0x31` 没有被分支吞掉、怪兽建成 | 座位 0 的 `+0x37c = 1`（E08 用） | 0x48d310 末尾 / 0x4ef000 | `rm8StoryMonsterBuilt`，**已修** |
| E19 | 6/1、7/3、10/0、11/1、13/0 | 第 > 9 天、座位 0 的 `+0x378 < 1`，周六新闻 / 节日没占用当天 | 每天起「怪兽袭击」新闻 0x1e，`+0x378` 被 E10..E17 加到 ≥ 1 后停止 | 0x4a6780 | `rm8StoryMonsterDue` + `game.cpp rm8GameOnNewRound`，条件 OK；改前计数永远不增 → 怪兽每天来（**已修**，随 E10..E17） |
| E20 | 3 錢夫人 / map 5 | 座位 0 停下时同点有角色 9：第 1 次 | drama 6；第 2 次 drama 7 + `0x75(0,1)` 通关（`+0x374` 计数） | 0x48c950 | `rm8StoryMeetSpecial`，OK（`storymeet,storymeet`：drama 6 → 7 → 奖励） |
| E21 | 全部英雄 | 停下时同点有别的角色，`sp_XX` 里没用过的第一条（每局每角色一次） | `0x54` + 对白 + `0x1c` | 0x48ca30 / 0x493770 | `present.cpp rm8PresentOnStop`（表现模块，本轮没复核对白内容） |
| E22 | 16 莎拉 | 第 1 次住院 / 坐牢（`+0x384`，座位 0） | drama 18 + str101 okcancel → 确认 `0x75` 通关 | 0x48a570 / 0x48a9c0 | `rm8StoryHospitalPrison`，OK（`RM8_STORY_HOSP`：第 2 次不再问；`RM8_STORY_ANSWER=1` 通关） |
| E23 | 10 烏咪 / 层 0 | 座位 0 被野狗咬（自己撞狗 / 附身野狗的角色咬它），还没有拼图 2 | 住院队列里 `0x18(2)` 之后 `0x58(2)` | 0x48bd00 / 0x49caf0 | `rm8StoryDogBite`，**已修**：移植的住院流程会清队列，排在 `0x18` 后面的 `0x58` 被清掉，**从来没拿到过拼图 2**；改为咬人时记标志、住院搬运完后再排 `0x58`（`rm8StoryDogBiteDone`），实测拿到 |
| E24 | 11 金泡菜 | 座位 0 现金 + 存款 ≤ 0，第 1 次（第 ≥ 1 天） | str91 + drama 16（位置 1）+ 现金设 100000 + 0x47，**不算破产**；以后按普通破产 | 0x48ee10 | `rm8StoryBankruptSpecial`，OK（`RM8_STORY_LOSE=8,30`：先被救，第 2 次进入接关询问） |
| E25 | 16 莎拉 | 座位 0 破产 | 清队列 + str91 + drama 19（位置 1）+ `0x75(0,1)`：**破产 = 通关** | 0x48ee10 | OK（`RM8_STORY_LOSE=8`：通关奖励） |
| E26 | 全部英雄 | 人类（座位 0）破产且没有别的人类 | `0x74` → str93 okcancel → `0x76`：确认 = `0x77` 重开本关；取消 = 角色菜单；11 / 层 0 先得拼图 3 | 0x48f8e0 | OK |
| E27 | 全部英雄 | 所有对手破产（剩一个人类） | `0x75(0,1)` | 0x48ee10 | OK |
| E28 | 全部英雄 | `0x75`（b ≠ 0） | 清队列 → `0x7e`（特效 6000 + 曲目 103，14 s）→ `0x7d`（奖励拼图 / 现金 / 点券 / 卡进储藏箱，str7251..7253）→ `0x7a`（stage 写入；打完层 3 → 状态 5） | 0x48ff70 / 0x48f9d0 / 0x4a1390 | OK（1 / 4 / 15 / 16 号 4 层连打：gamedate stage 4、拼图、解锁都写入） |
| E29 | 4 忍太郎 / 层 1 | `0x75` | 额外 `0x7b`、`0x2c(0x8f6)`、drama 9（位置 4） | 0x48ff70 | OK（实测 drama 9 位置 4） |
| E30 | 6 孫小美 / 气候 1 的图 | 座位 0 用路障 / 地雷 / 定時炸彈 / 機器娃娃（NPC 12 / 13 / 14 / 15）各一次 | drama 20 + 位（路障 0、娃娃 1、地雷 2、炸弹 3）；四种齐 → 解锁 11 + `0x65(30)` + drama 24；**该 NPC 没有被放出来**（`OnDiceEvent` 返回 1，`py_Npc_Create` 直接结束） | 0x491270 / 0x500160 | `rm8StoryNpcPlaced`；剧情 / 解锁 OK；**已修**：原先 NPC 照放、之后才演剧情（`python.cpp Npc_Create` 现在先问剧情，且用当前角色而不是 owner 参数） |
| E31 | 3 / 4 / 5 / 10 → 詹姆斯 | 地图旗标（art 0x45b，所属角色 == 座位 0 的角色）+ 本层对应的位未亮，点击，状态 6 | 置位（全 11 位 → 解锁 15）+ drama 32..42；阿土仔的 4 位再给 2 张卡 + `100` | 0x4a37d0 / 0x4a4a10 / 0x491630 / 0x491710 | OK（`RM8_STORY=5,2 RM8_CHAIN_TAP`：bit 0，drama 32） |
| E32 | 全部英雄 | 打完层 3（状态 5） | 解锁莎拉（16）；用詹姆斯再解锁酷娃（12）；`CApp_PlayMovieRange(角色−1, 1)` + Staff | 0x4a17b0 | OK（chain 实测：解锁 [16]，詹姆斯 [12, 16]，stage 4） |
| E33 | 12 酷娃 | 别人的飞弹 / 核弹（半径 > 0）打到座位 0 | 整张卡作废：表情 0x3f + 2t、str570 / 571、与使用者互相 `0x2e` | 0x490df0 | `npc_act.cpp blast()`，OK（`RM8_NPC_MISSILE`） |
| E34 | 2 萊娜 | 在饭店住宿，座位 0，没有财神 / 穷神附身 | 住宿费 ×7/10 + str53；**无次数限制，drama 3 / 4 无调用者** | 0x4f6c80 | `event_build.cpp`，OK（旧文档「前 5 次」是错的，已更正） |
| E35 | 1..16 | 层的 `start_pic`（未拥有的拼图）放在地图上，座位 0 点击得到 | NPC 17 拼图，`0x58` | 0x49db20 / 0x49d100 | `rm8NpcStory`，OK |
| E36 | 全部英雄 | 弃牌框的储藏箱按钮 | 卡入箱，满则 str112 | 0x4aea80 / 0x488de0 | `cards.cpp`，OK |
| E37 | 全部 | 故事 + map 3 | 棋盘曲目 15 | 0x4a31f0 | `audio.cpp rm8MusicPlayBoard(…, story)`，OK |
| E38 | 全部 | 胜利条件信息框 | str7052「比賽場次：setup+8 + 1」 | 0x4acab0 | `infoscreens.cpp`，OK |

### 5.2 数据 / 流程项

| 项 | 内容 | 移植状态 |
|---|---|---|
| 起始值 | 16 个英雄各层：起始现金 / 存款 / 点券 / 卡 + 储藏箱携带物（`Game_StartNewRound @0x4962a0`，只作用于座位 0） | OK（日志 `story: start values`；133 条 pass 与独立解析的 `out/story/pass_all.json` **逐字段 0 差**） |
| 候选图 / 确认 / 补对手 | `Game_PopulateMapList @0x503510`、`OnConfirm @0x503e90`（负数对手 = `rand()%15+1`，随后退出循环） | OK（代码对照；`RM8_STORY_CHAIN` 4 个英雄 4 层，忍太郎跳过层 2） |
| 开场 / 结局影片 | `movie.bin` 16 组（start = da17_1 + daXX_1，end = da17_2 + da17_3 + 专属） | `story.txt` 的 movie 行与 `movie.bin.txt` 一致；播放见 4.3 |
| drama.bin | 42 段 | `story.txt` 文字与 `drama.bin.txt` 一致（7 / 15 / 31 号在 `num` 之外多一行，原版也只读 `num` 行）；**drama 3 / 4 在 exe 里没有调用者** |

### 5.3 本轮改动和验证

- **改动（带 `[V]` 地址，注释不引原文）**：`story.cpp`（`rm8StoryMonsterHit` / `rm8StoryMonsterBuilt` 取代 `rm8StoryParkEvent`；条件 8；`rm8StoryDogBite` + `rm8StoryDogBiteDone`；日志与测试钩子）、`news.cpp`（`0x31` 先调 `rm8StoryMonsterHit`）、`game.cpp`（去掉公园格钩子；`RM8_START_DAY`）、`python.cpp`（`Npc.Create` 先问剧情）、`cards.cpp`（`sendToHospital` 末尾 `rm8StoryDogBiteDone`）、`events.cpp`（测试钩子 `storymonster` / `storymeet`）。没有新增持久状态（存档往返不受影响：`g_dogPiece` 只在咬人到住院搬运之间存在）。
- **新测试钩子**：`RM8_START_DAY=<天>[,<周几 1..7>]`（第一轮真正的回合就是那一天，登场轮为前一天）；`RM8_STORY_CASH=<现金>[,<存款>]`；`RM8_STORY_HIT=1`（每个回合开始，怪兽按新闻的方式打地块 0）；`RM8_STORY_NPCPLACE=12,13,14,15`；`RM8_STORY_HOSP=<秒>,…`（`0x18(2)`）；`RM8_STORY_LOSE=<秒>,<秒>`（可多个时刻）；`RM8_EVENT_TEST=storymonster`（怪兽打第一块住宅，地主 `RM8_MONSTER_OWNER`，默认座位 0；旧名 `storypark` 同义）/ `storymeet`（`RM8_MEET_SEAT`）。**`RM8_STORY=<英雄>[,<地图号>]` 的第二个参数是地图号，不是层**。
- **验证方法**：桌面版 `rm8_app`，`RM8_FAST=1 RM8_AUTO_SEAT0=1`（座位 0 由电脑接管，跑得动长局；人类座位会停在事件 UI 上，要用 `RM8_EVENT_TEST` 钩子）；16 个英雄层 0 各跑 6000 帧并逐个看 `story:` 日志；其余场景见表中的「实测」；菜单路径（`--menu` + `RM8_MENU_AUTO`）也跑了大老千。**没有对照原版实机**。
- **未决 / 假设**：① `Role_EnqueueMsg` 在反编译里丢失了隐含的 ECX：野狗分支的 `0x18` / `0x58` 按「入被咬者的队列」处理（条件里判断的就是被咬者是烏咪座位 0）；② `+0x37c` 在 `0x4ef000` 失败时不置位，移植的怪兽动画没有失败路径，一律当成功；③ 住院搬运清队列是移植的 [A]，原版不清（E23 的修法绕开它）；④ E21 的 `sp_XX` 内容与「每局一次」标志是表现模块的事，本轮没复核；⑤ 周三事件依赖系统日期（原版同）；⑥ 怪兽新闻的触发顺序 / 概率（随机新闻池里的怪兽）归 news 模块，本轮只核对了故事相关的 `0x1e`。
