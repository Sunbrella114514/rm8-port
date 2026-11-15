# 16 个角色：隐藏角色、解锁、服装、专属规则（逆向清单 + 移植状态）

范围：大富翁8 的 16 个可玩角色（roleinfo.bin 的 `Id` 1..16）里**每个角色在代码中被单独对待的地方**：解锁条件与检查 / 写入位置、菜单可用状态、服装 / 泳装解锁与模型、剧情遭遇、专属技能与规则例外（自由模式和故事模式分开）、两个角色子类（伊格魯 / 莎拉）的行为，以及移植里做没做。
标记：`[V]` 已读反编译代码核实（附地址）；`[M]` 读了但语义是推断；`[H]` 假设；`[A]` 移植侧取舍。反编译来源 `out/final_v2/RichMan8_named.c`（函数名约一半不准，按代码行为改名并同时给地址）。关联文档：`story_mode.md`（流程 / 1.10 解锁 / 1.12 规则例外）、`menus.md`、`cards_port.md`、`presentation.md`、`rules_land.md`、`save_port.md`。
**2026-11-15 更正（故事事件审核，见 `story_mode.md` 第 5 节）**：下文所有「公园事件 / 公园宝箱 / 公园 drama」其实是 **怪兽袭击（消息 `0x31`，`CPlayerTag__OnParkEvent @0x48d310` 是它的处理函数，名字是错的）**：触发 = 怪兽打到一块地块（新闻 0x1e / 怪兽卡），不是走到公园格；`param_1+0x4c` = 被打地块的地主；舞美拉的「宝箱」= 怪兽真的出现过（`+0x37c`）。萊娜饭店折扣没有「前 5 次」限制，drama 3 / 4 在 exe 里无调用者。测试钩子 `storypark` 现在 = `storymonster`（`0x31` 打第一块住宅）。

---

## 0. 结论速览（回答「隐藏角色的出场 / 互动 / 解锁条件 / 专属技能做完了没有」）

1. **角色 Id 的全部判断点已穷举**（第 1 节表）：反编译里对 `*(roleinfo+0x300)`（= role+0xf8 → +0x300，名字里常写成 `+0x308`，**角色 Id 在 +0x300，+0x308 是 artId1**）的比较共 43 + 15 处，加上两个角色子类（Id 13 / 16 的 vtable）。除第 1 节列出的以外，**没有任何别的角色专属规则**：Python 脚本（AI / 卡片）里没有按角色号分支（只有 16 个 AI 各自的 3 个概率，`ai.md`），role.bin 是模型 / 动画库（1271 条，不含能力数据）。
2. **6 个隐藏角色（11..16）的解锁条件和写入位置全部找到并移植**（第 2 节）；本轮补了最后一个缺口——**詹姆斯（15）的连锁超市解锁**（地图里 art 0x45b 的旗标物件 + 点击事件，第 7.2 节）。
3. **服装 / 泳装**：服装 1..3 = 角色通关进度 stage ≥ 1..3；**泳装（服装 4）= stage ≥ 3 且 9 块拼图齐**，模型是 `artId + 0xC0`（`A%02dx` 系列）；原先移植只有颜色贴图，泳装模型没有，**本轮补上**（棋盘 / 角色菜单 / 结算 / 法院 / 登场演出，第 3 节）。
4. **菜单锁定状态**：角色菜单、拼图菜单对未解锁的 11..16 显示成灰色剪影（原版就是这样），**自由模式的地图菜单里未解锁的地图是「隐藏」**（第 3 张巴黎默认锁）；本轮按原版改了拼图菜单（原先「有拼图也点亮」是移植自加的）和自由模式地图菜单（原先全开）。测试覆盖：`RM8_UNLOCK_ALL=1`。
5. **两个角色子类已读**（第 6 节）：**伊格魯 = 带一只跟班怪物（A13b，小企鹅）的角色类；莎拉 = 带保镖（D46a）的角色类**。两个子类**没有任何规则作用**，只有表现：路径跟随的伴随对象。莎拉的保镖规则（免疫点选 / 飞弹 / 过路费）本来就由卡片模块实现，本轮补上了**两者的图形**。
6. **酷娃的飞弹没收**：已在 `npc_act.cpp` 实现（旧文档写「未做」是过时的），本轮核对了反编译并补了她被别人打时的「交接」消息（第 7.1 节）。
7. **第二轮（2026-11-15）补齐了第 9 节原先列的缺口**：泳装在 mg_21 小游戏里的模型、伴随对象的进出饭店 / 掉头 / 被抬走（救护车 / 警车 / UFO）分支、对白头像 `npc\p_18_01`、公园事件的 `0x52` 特效（GS-1 / GS-2 art）；故事乐透差异经核对**早已由 NPC 代理做完**（滚轮对话框，`npc_ui.cpp`，旧文档过时）。仍未做的见第 9 节。

---

## 1. 角色 Id 判断点全表（反编译里的 `role+0xf8 → +0x300`）

「作用域」：故事 = 先判 `setup+0 != 0`；座位 0 = `role+0x1a8 == 0`（人类主角）。移植列：已做 = 有对应实现。

| 地址 | 函数（按行为命名） | Id / 条件 | 行为 | 作用域 | 移植 |
|---|---|---|---|---|---|
| 0x4835e0 / 0x491a10 | `Role_CheckMissionsByCharacter` / 拼图条件检查 | 全部 16 个 | (角色 × 层) → 拼图条件表（`story_mode.md` 1.11） | 故事、座位 0 | 已做 `story.cpp` |
| 0x483f40 | `Role_RunStoryScriptEvents` | 1 / 6 / 5·10·8 / 7 | 大老千第 1 天贷款 + 第 10 天讨债；孫小美提示；解锁 14 的三步（阿土仔→烏咪→金貝貝，周三）；舞美拉公园宝箱计数（解锁 13） | 故事、座位 0 | 已做 |
| 0x48b410 | `Role_SpawnCharacterModel`（vf36）登场模型 | 全部：art = `0x180 + Id`，泳装 `0x190 + Id` | 开局登场动画（`A%02ddc`，role.bin 动作 0 一次） | 全部 | 已做 `present.cpp`（泳装模型本轮补） |
| 0x48bd00 / 0x49caf0 | 警察 / 小偷 / 野狗动作、`BeginAttach` | 10（烏咪）+ 座位 0 + 层 0 + 没有拼图 2 | 被野狗咬（住院）后队列追加 `0x58(2)`（拼图 2） | 故事 | **本轮补** `rm8StoryDogBite` |
| 0x48c950 / 0x48ca30 / 0x493770 | 同点相遇对白 | 3（錢夫人）遇 9（沙隆巴斯），map 5 | 第 1 次 drama 6，第 2 次 drama 7 + 过关；其余遇见走 `sp_XX` 对白 | 故事 | 已做 |
| 0x48d310 | `CPlayerTag__OnParkEvent` | 3 / 6 / 7 / 10 / 11 / 13 / 15 | 怪兽袭击（`0x31`）的剧情分支（drama 5 8 10 11 12 13+14+过关 15 17；**不是公园格**） | 故事 | 已做 |
| 0x48ee10 | `Role_CheckBankruptcy` | 11（金泡菜）/ 16（莎拉） | 金泡菜第一次破产被老爸救（现金 100000，drama 16）；莎拉破产 = 过关（drama 19） | 故事、座位 0 | 已做 |
| 0x48f8e0 / 0x48f9d0 / 0x48ff70 | 失败 / 通关奖励 / 通关收尾 | 11 / 任意 / 4 | 金泡菜层 0 首败给拼图 3；通关奖励按 (角色, 地图)；忍太郎层 1 额外剧情（drama 9） | 故事 | 已做 |
| 0x490df0 | `CPlayerTag__OnSendGodCard` | 12（酷娃） | **范围伤害卡（飞弹 / 核弹，半径 > 0）命中座位 0 的酷娃时整张卡作废**（emote 0x3f / 0x41，文字 str 570 / 571 `飛彈被酷娃沒收！`） | 故事、座位 0 | 已做 `npc_act.cpp`（本轮补交接） |
| 0x491270 | `CPlayerTag__OnDiceEvent`（由 `py_Npc_Create @0x500160` 调用） | 6（孫小美）+ 气候 1 的图 | 放置 NPC 12 / 13 / 14 / 15（路障 / 地雷 / 炸弹 / 机器娃娃）四种各一次 → 解锁 11 | 故事、座位 0 | 已做 |
| 0x491630 / 0x491710 / 0x4a37d0 / 0x4a4a10 | 连锁超市 | 3 / 4 / 5 / 10（地图旗标物件的 owner） | 玩家点击 art 0x45b 的旗标 → 置位 11 位之一，全齐解锁 15 | 故事、座位 0 | **本轮补** `rm8StoryActivateBuildings / ChainStoreClick` |
| 0x491830 / 0x4aa2e0.. | 拼图卡使用 `OnPuzzleCardUse` | 任意 | 给角色 Id 的第 k 块拼图（`Event_SetItemData`）并存盘 | 故事 | 已做 `collect.cpp` |
| 0x4962a0 | `Game_StartNewRound` | 任意 | 按 (座位 0 的角色, 地图) 取 pass 条目：起始卡 / 现金 / 点券 | 故事、座位 0 | 已做 |
| 0x495f60 | `Game_SpawnNpc`（真名 CreateRole） | **13 → 类 0x5e27d8；16 → 类 0x5e2918**；1 + 层 0 清钱 | 角色子类（第 6 节）；大老千层 0 现金 / 存款先清 0 | 创建角色 | **本轮补** 子类的伴随对象 |
| 0x49db20 | `Event_ProcessLanding`（`Npc_SpawnAll`） | 任意 | 放置 pass 的 `start_pic` 拼图 NPC（已拥有的不放） | 故事 | 已做 |
| 0x4a17b0 | `Game_OnUpdate` 状态 5 | 15 | **每个角色通关 4 层 → 解锁 16；用 15 通关 → 再解锁 12**；播结局影片 | 故事 | 已做 |
| 0x4a6780 | `Game_TriggerEventByDate` | (6,1) (7,3) (10,0) (11,1) (13,0) | 第 10 天后、座位 0 公园次数 < 1 → 新闻 0x1e 怪兽袭击 | 故事 | 已做 |
| 0x4b7910 | `News_EventMonsterAttack`（真名） | 10（烏咪） | **怪兽袭击不选烏咪的房子** | **全部模式** | 已做 `news.cpp` |
| 0x4f6c80 | `Hotel_ProcessVisitor` | 2（萊娜） | 座位 0 + 故事：饭店住宿费 ×7/10（str53，无次数限制；drama 3 / 4 无调用者） | 故事 | 已做 `event_build.cpp` |
| 0x48a570 / 0x48a9c0 | `SendToHospital / SendToPrison` | 16（莎拉） | 第一次住院 / 坐牢：drama 18 + 「是否需要直接進入下一關？」 | 故事 | 已做 |
| 0x4aa1e0 / 0x4aa200 | `Event_GetTerrainData / SetTerrainData` | 11..16 | 解锁标志（gamedate） | — | 已做 `collect.cpp` |
| 0x505940 / 0x505a80 / 0x5065f0 | 角色菜单 | 全部 | 未解锁灰；服装按 stage / 拼图；点击 = 找第一个没被用的服装 | — | 已做 `menu.cpp` |
| 0x50ad50 | 拼图菜单 vf16 | 11..16 | 未解锁灰（1..10 恒亮） | — | **本轮改** 与原版一致 |
| 0x506fc0 / 0x507100 | finalMenu（影片欣赏） | 全部 | `Event_GetLevel ≥ 4` 才亮；点击播 `end` 影片 | — | 已做 |
| 0x503510 | `Game_PopulateMapList` 自由分支 | 地图 1..11 | `Event_GetEventByType == 0` 的地图按钮**隐藏** | 自由 | **本轮改** |
| 0x491200 / 0x48a0c0 | `Role_CopyTrfToolValues / SetTrfTool` | 服装 4 | 4 个模型号各 `+0xC0` | — | **本轮补** |
| 0x4af2e0 / 0x4d7e20 / 0x505410 | 结算界面（`ShowBalanceUI`）、法院名单（`Court_LoadCharacterList`）、角色菜单模型（`Role_CreateMeshObject`） | 服装 4 | 同上：`artId + 0xC0`，且服装 4 不调 `Asset_LoadCharacterTexture` | — | **本轮补** |
| 0x4934a0 | `Asset_LoadCharacterTexture` | 2 / 15 → `other`；4 / 9 / 11 / 13 → `hair`；7 / 10 → `toujin` | 身体贴图 `A%02d_body_<f/b>_%02d`，额外部件按角色 | 全部 | 已做 |
| 0x4aa5b0.. / 0x500520 | `Event_GetQuestData`；`py_Npc_Goodness` | 1 4 5 10 11 | NPC「好神」判定写死了 Id（Npc.Goodness） | 全部 | 已做 `python.cpp` |
| 0x4b0670 | `Balance_ShowPlayerBalance` | 任意 | 月结算标题 str `4704 + Id` / `4722 + Id`（名字 + 语音） | 全部 | 已做 `balance.cpp` |
| 0x4ac2a0 等 | 存档 | 16 | 莎拉多存 4 字节（保镖计数，版本 ≥ 8） | 全部 | 已做 `save.cpp` |

角色表（`roleinfo.bin`，`scripts/export_roles.py` → `scenes/roles.txt`，`+0x38c` strId = 600 + 200·(Id−1)）里每个角色的**可玩数值差异**只有：步行 / 摩托 / 汽车速度（110..160 / 120..150 / 140..180，`+0x380`）、性别（`+0x304`，节日卡用）、绘制偏移、AI 脚本名；全部已读入移植。16 个 AI 脚本的差异只有 3 个概率（`ai.md`），阿土仔 / 星期三的 AI 不买股票，移植跑原版 `AI_xx.pyc`，所以已一致。

---

## 2. 隐藏角色解锁（11..16）

持久化：`config\gamedate.bin`（`GameDate_Load @0x4a99e0` / `GameDate_Save @0x4a9d60`，每个 setter 立即写）。**新档 1..10 号解锁**（`Event_InitializeData @0x4aa0b0`），11..16 的解锁标志 = 文件里该位置等于魔数（`scripts/gamedate.py`）。移植用 `port` 数据目录里的 `rm8_gamedate.bin`（同一格式，`collect.cpp`），**从不写游戏目录**；`RM8_IMPORT_GAMEDATE=1` 在没有档时一次性读 `<游戏>/Config/gamedate.bin`。

| 角色 | 条件 | 检查 / 写入 | 提示 | 移植 |
|---|---|---|---|---|
| 11 金泡菜 | 用 6 孫小美玩故事，在**气候 = 1** 的地图（stage.bin +0x210）上把四种放置类 NPC（路障 12 → 位 0、地雷 13 → 位 2、定時炸彈 14 → 位 3、機器娃娃 15 → 位 1）各用一次；全齐解锁 | `CPlayerTag__OnDiceEvent @0x491270` → `Event_SetFlag2 @0x4aa470`（gamedate +0x888）→ `Event_SetTerrainData(0xb)` | drama 20..23（每放一种），全齐后 drama 24 | 已做 `story.cpp rm8StoryNpcPlaced`、`python.cpp` |
| 12 酷娃 | **用 15 詹姆斯通关全部 4 层** | `Game_OnUpdate @0x4a17b0` 状态 5：`Event_SetTerrainData(0xc, 1)`（座位 0 的角色 == 0xf） | 结局影片 | 已做 |
| 13 伊格魯 | 舞美拉层 0，公园宝箱（`OnParkEvent` 置 `+0x37c`）累计 **4 次**（跨局，gamedate +0x884） | `Role_RunStoryScriptEvents @0x483f40` 末段 `Event_IncrementCounter @0x4aa420`（> 3 → `Event_SetTerrainData(0xd)`） | drama 25..28 | 已做 |
| 14 星期三 | 层 0、**周三**、第 10 天以后，依次：阿土仔（5，位 0）→ 烏咪（10，位 1）→ 金貝貝（8，位 2），**每个角色各玩一局** | `Event_SetFlag3 @0x4aa510`（+0x88c，要按序）→ 位 2 → `Event_SetTerrainData(0xe)` | drama 29 / 30 / 31 | 已做 |
| 15 詹姆斯 | 11 个「连锁超市」位全亮（阿土仔层 0..3 → 位 0..3，烏咪层 0..2 → 位 4..6，錢夫人层 0..2 → 位 7..9，忍太郎层 0 → 位 10；**每一层要选对地图**，见 7.2） | 地图里 art 0x45b 的旗标物件被点击：`CPlayerTag__OnChainStoreEvent @0x491630` → `Event_SetBitFlag @0x4aa380`（+0x880，全 11 位 → `Event_SetTerrainData(0xf)`） | drama `0x20 + 位号`（32..42） | **本轮补**（7.2） |
| 16 莎拉公主 | **任一角色通关全部 4 层**（层 3 = 魔都对莎拉） | `Game_OnUpdate` 状态 5：`Event_SetTerrainData(0x10, 1)` | 结局影片 | 已做 |

`Event_SetTerrainData` 的全部调用者只有上面这 5 处 + `Event_InitializeData`（已用脚本核对）；`Event_SetEventData @0x4aa1b0` 全 exe 没有调用者 [V]，所以自由模式第 3 张地图（巴黎，gamedate 的 flagA）**只能由 gamedate.bin 文件本身提供**（Steam 目录的 gamedate.bin 里它是关的）。

**测试 / 全解锁**：`RM8_UNLOCK_ALL=1`（`collect.cpp` `unlockAll()`）使 11..16、所有服装（stage 4）、9 块拼图、所有地图都读成已解锁，**不写进档案**；`RM8_DATA_DIR=<目录>` 把档案放到别处。

---

## 3. 服装、泳装、拼图与菜单可用性

**stage（角色通关进度 0..4）**：`Event_GetLevel @0x4aa230`，只由 `Event_SetMaxLevel @0x4aa250`（取 max）写，调用链 = 通关结算 `0x7a` → `Game_OnStageSelect @0x4a1390` → `Game_UpdateRecordStats @0x4abe20`：每过一层 stage = 该层 level + 1（打完层 3 = 4）。

**拼图**：`Event_GetItemData / SetItemData @0x4aa2e0 / 0x4aa300`（每角色 9 块），来源：`start_pic`（地图上拾取）、`reward_pic`（通关）、条件拼图（`story_mode.md` 1.11 全表）。`Event_CountOwnedItems @0x4aa290` 数齐。

**角色菜单**（`Cls_5e8140`）：
* `vf16_Open @0x505940`：对 1..16 逐个 `Event_GetTerrainData`，为 0 则该六边形按钮灰（`vf+0x20(id, 0)`）。移植同（`menu.cpp enterRoles`）。截图：新档下 11..16 是 6 个灰色剪影，已解锁时全部有头像。
* `UpdateRolePanel @0x505a80`：服装按钮 19..22 = 服装 0..3，**stage ≥ k 才可点**；服装 4（泳装）按钮是 23（性别 0）或 33（性别 1，`+0x304`），**stage ≥ 3 且 `Event_CountOwnedItems == 9` 才可点**。
* 点角色 `@0x5065f0`：在 `0..min(stage,3)` 里找第一个该角色没被别的座位用的服装；服装按钮改服装。

**服装 → 模型 / 贴图**：服装 0..3 = 同一套模型 + `A%02d_body_<f|b>_%02d` 的颜色贴图（`Asset_LoadCharacterTexture @0x4934a0`）；**服装 4（泳装）= 4 个 artId（`fl fr br bl` 一组）各 +0xC0**（`u_Role_CopyTrfToolValues @0x491200`、`Role_SpawnCharacterModel @0x48b410`、`Cls_5e8140_LoadAndDisplayRoles @0x505fb0` / `Role_CreateMeshObject @0x505410`、结算 `@0x4af2e0`、法院 `Court_LoadCharacterList @0x4d7e20`），即 role.bin 的 art 193..384（`A01x_Act01_*`…；登场模型 `0x190 + Id` = `A%02dxdc`），**不再调 `Asset_LoadCharacterTexture`**（贴图在模型自带的材质里）。移植：`roles.cpp loadModel`（`color == 4` → `art + 192`）、`menu_anim.cpp`、`balance.cpp`、`event_npc.cpp / event_court.cpp`、`present.cpp`（登场）；`scripts/export_roles.py` 现在导出 `A%02dx` 的 200 个模型 / 1860 条动作 / 927 条换脸（`scenes/roles.txt` 已重导）。

**拼图菜单** `Cls_5e7f60__vf16 @0x50ad50`：16 个角色按钮，**1..10 恒亮，11..16 要 `Event_GetTerrainData`**（原先移植「有拼图就点亮」是自加的，已去掉）。**影片欣赏 finalMenu**：`Event_GetLevel ≥ 4` 才亮。

**自由模式地图菜单** `Game_PopulateMapList @0x503510` 自由分支：`Event_GetEventByType(1..11) == 0` 的按钮**隐藏**（不是灰）；默认只有第 3 张（巴黎）关。故事分支先隐藏全部，再只显示当前层的候选图。移植 `menu.cpp enterMap`（本轮改）。

---

## 4. 16 个角色逐个

「专属」= 该角色独有的规则 / 剧情；所有故事规则只对座位 0 的人类主角生效。起始卡 = pass 条目的 `start_card`（层 0）。

| Id 名 | 解锁 | 步行速度 / 性别 | 故事线与专属规则 | 自由模式差异 | 移植 |
|---|---|---|---|---|---|
| 1 大老千 | 初始 | 120 / 男 | 层 0 第 1 天贷款 150000（只记录，不加钱）+ 现金存款各设 50000 + drama 1；第 10 天后层 1 / 2 现金 + 存款 ≥ 25 / 30 万 → 讨债 drama 2（扣 15000）；起始卡 45；拼图条件见 1.11 | 无 | 已做 |
| 2 萊娜 | 初始 | 130 / 女 | 饭店住宿费 ×7/10（str53，无次数限制）；层 0 起始无卡 | 无 | 已做 |
| 3 錢夫人 | 初始 | 110 / 女 | 公园 drama 5 / 8（层 < 3 / ≥ 3 + 特效 0x8df）；**map 5 遇沙隆巴斯 drama 6 → 7 并过关**；连锁超市位 7..9（层 0..2，map 1 / 5 / 7）；起始卡 47 | 无 | 已做（特效 0x52 略） |
| 4 忍太郎 | 初始 | 150 / 男 | 层 1 通关额外剧情 drama 9（`0x7b 0x2c(0x8f6)`）；连锁超市位 10（层 0，map 4）；起始卡 35 | 无 | 已做（0x7b / 0x7c 只吞） |
| 5 阿土仔 | 初始 | 130 / 男 | 层 0 周三第一步解锁 14（drama 29）；连锁超市位 0..3（层 0..3，map 2 / 3 / 9 / 11 之中**对应层有旗标的那张**）；起始卡 7 | 无 | 已做 |
| 6 孫小美 | 初始 | 130 / 女 | 层 0 第 1 天提示（str 7150，配头像 `npc\p_18_01`，gtinfo 框，第二轮已显示）；气候 1 图放四种 NPC 解锁 11（drama 20..24）；层 1 公园 drama 10；起始卡 7 | 无 | 已做 |
| 7 舞美拉 | 初始 | 130 / 女 | 层 0 公园宝箱计数解锁 13（drama 25..28）；层 3 公园 drama 11；起始卡 7 | 无 | 已做 |
| 8 金貝貝 | 初始 | 100 / 女（最慢，摩托 120 / 汽车 140） | 周三解锁 14 的第三步（drama 31）；起始卡 7 | 无 | 已做 |
| 9 沙隆巴斯 | 初始 | 120 / 男 | 作为錢夫人的对手有专属相遇；起始卡 8 | 无 | 已做 |
| 10 烏咪 | 初始 | 120 / 女 | 层 0 被野狗咬 → 拼图 2（**本轮补**）；层 0 公园 drama 12；周三解锁 14 的第二步（drama 30）；连锁超市位 4..6；**怪兽袭击不选她的房子（全模式）**；起始无卡 | 怪兽袭击跳过她的房子 | 已做 |
| 11 金泡菜 | 孫小美放障碍物（第 2 节） | 120 / 女 | 层 1 公园 drama 13 / 14 并过关；**第一次破产被老爸救**（现金 100000，drama 16）；层 0 首败得拼图 3；起始卡 18 | 无 | 已做 |
| 12 酷娃 | 詹姆斯通关 | 140 / 女（偏移 0） | **别人的飞弹 / 核弹打到她时没收**（str 570 / 571）；起始卡 18 + 33 | 无（只在故事） | 已做（交接消息本轮补） |
| 13 伊格魯 | 舞美拉公园宝箱 4 次 | 130 / 男 | 层 0 公园 drama 15；**子类：跟班怪物（第 6 节）**；起始卡 35 + 48；层 ≠ 0 / 非首次公园出文字 str 574 | 跟班怪物也在自由模式 | **本轮补**（图形） |
| 14 星期三 | 周三三连 | 160 / 男（最快，摩托 140 / 汽车 150） | 无剧情特例（AI 不买股票）；起始卡 7 + 9 | 无 | 已做 |
| 15 詹姆斯 | 11 个连锁超市位 | 110 / 男 | 用他通关 → 解锁 12；起始卡 49；公园遇錢夫人 drama 17（`+0x4c` 的含义 [H]） | 无 | 已做 |
| 16 莎拉公主 | 任一角色通关 | 110 / 女（偏移 0） | **层 3 终极对手**；破产 = 过关（drama 19）；第一次住院 / 坐牢可直接过关（drama 18 + str 101）；**保镖卡真保镖**（免疫点选 / 飞弹 / 过路费，7 回合，其他角色用它只是进医院）；**子类：〔string.bin#567〕（第 6 节）**；起始卡 8 + 46；作主角时层 3 对手随机 1..15 | 保镖卡 | 已做（保镖图形本轮补） |

隐藏角色在故事里**只出现在两处**：莎拉作为层 3 的终极对手（`pass_XX` 第 3 层的对手栏）、以及主角（解锁后可选）。11..15 从不作为对手出现（已统计 133 条 pass 条目）。同点相遇对白 `sp_XX.bin`（每角色 15 节，节名 = 对方）整段被故事标志关住，自由模式没有。

---

## 5. 登场演出与开局

* 开局登场（`Role_LeaveStatus8 @0x483540` → 0x57 → `Role_SpawnCharacterModel @0x48b410`，**仅在 `status == 8` 即新局放置时**）：模型 art `0x180 + Id`（泳装 `0x190 + Id`）在角色位置 + roleinfo 偏移 / 缩放，朝向 = 相机偏航 − 180，动作 0 一次，`0x65(60)` 等它放完；**伊格魯**的 vf36 在此同时让跟班怪物从相机右侧 300 单位处跑进来（第 6 节）。
* 角色菜单选角色的语音：`roleinfo.strId + 0x1d` 的样本；故事开场 host 的介绍词（3801.. 台词 + 语音，莎拉主角时 3835 否则 3834）。均已移植。

---

## 6. 角色子类：伊格魯（Id 13）与莎拉（Id 16）——本轮逆向

`Game_SpawnNpc @0x495f60`：Id 13 → `new 0x5f4`、`PlayerTag_ctor @0x498910`（vtable **0x5e27d8**，类名 `Cls_5e27d8`）；Id 16 → `new 0x5f4`、`Cls_5e2878_ctor @0x498a60`（vtable **0x5e2918**，`CPlayerTag_3`）；其余 → `new 0x5f0` 的基类 `CPlayerTag`。两个子类比基类多 4 字节 `+0x5f0`：**一个伴随对象指针**（基类 `CPlayerTag` 是 `new 0x5f0`，没有这个字段）。vtable 与基类的差异（`scripts/story_vtdiff.py` 重跑核对）：

| vtable 槽 | 基类 | 伊格魯 0x5e27d8 | 莎拉 0x5e2918 |
|---|---|---|---|
| vf3 Update | `0x481500` | `0x47dc80`：基类 + 伴随对象 `vf3`（`Path_UpdatePosition @0x47ee20`）+ `Movie_SelectAnimByDistance @0x47fc30` | 同 |
| vf4 Render | `0x485d00` | `0x47dcf0`：基类 + 角色状态 < 3 且（`+0x1c8 != 0` 或伴随对象 `+0x80`）时画伴随对象 | 同 |
| vf5 命令 | `0x48a270` | `0x47e300`：基类 + 命令号 > 3（表情）转发给伴随对象 | 基类（不转发） |
| vf7 就绪 | `0x47af60` | `0x47e140` | 同 |
| vf15 放到路点 | `0x47b670` | `0x47dd60`：基类 + 伴随对象 `Path_LinkPoints @0x47faa0` | 同 |
| vf16 掉头 | `0x47b5f0` | `0x47de40`：基类 + 伴随对象「撤退节点」`+0x5c` | 同 |
| vf18 经过节点 | `0x485a20` | `0x47deb0`：基类返回 0（没停）→ 伴随对象路线 push 角色的下一个节点 | 同 |
| vf22 创建 | `0x480b30` | `0x47e1a0`：基类 + **`Cls_5e27d8_CreateMovieActor @0x47e1e0`**（造伴随对象） | 基类 |
| vf23 清理 | `0x480ce0` | `0x47dbf0` | 同 |
| vf25 | 恒 0 | 恒 0 | **`0x498ac0`：`+0x5f0 != 0` = 保镖在** |
| vf26 开始走 | `0x484f90` | `0x47de00`：基类 + push 目标节点 | 同 |
| vf27 隐身并复位 | `0x48ceb0` | `0x47dfa0`：基类 + 伴随对象朝相机右方向跑掉 | 同 |
| vf28 / vf29 住院 / 坐牢 | `0x48a570 / 0x48a9c0` | `0x47e010 / 0x47e050`：基类返回非 0 时伴随对象 `Path_ResetTrail`（隐藏） | 同 |
| vf30 / vf31 存 / 读 | `0x491fb0 / 0x4923b0` | 存同基类；读 `0x47e450`：状态 < 3 时伴随对象 `+0x90 = 0` | `0x480480 / 0x4804e0`：多存 `+0x5f0` 对象的计数 `+0x98`（版本 ≥ 8） |
| vf32 保镖卡（`Role.Bodyguard`） | `0x48ecf0`（别人：清队列 + 特效 0x1787 + 住院 3 天） | 同基类 | **`0x4803e0`：`Path_Create @0x480260` 造保镖，成功播样本 0x2e** |
| vf33 计数 | 空 | 空 | **`0x480420`：保镖计数 `+0x98` 减 1，< 1 时 vf+0x98 释放** |
| vf34 / vf35 进 / 出饭店 | `0x4852e0 / 0x4853a0` | `0x47df00 / 0x47df50`：基类 + `Path_SetNormalResult / SetReversedResult` | 同 |
| vf36 登场模型 | `0x48b410` | `0x47e3b0`：基类 + 伴随对象 `Path_SetWaypoint`（相机右 300 处） | 基类 |
| vf37 放回节点 | `0x48cfd0` | `0x47e090`：基类 + 伴随对象 `+0x90 = 0`（不隐藏）+ `Path_LinkPoints` | 同 |

**伴随对象**（基类 `Cls_5e1b90`，构造 `Path_CtorInitialize @0x47ed30`，大小 0x98；保镖是子类 `Cls_5e1ca8`，0x9c）：
* **伊格魯的跟班 = 怪物 A13b**（art `DAT_006044ec` = 499 500 498 497 = `A13b_Act01_fl fr br bl`，贴图 `A13_Q_f_act01…`，role.bin 15 个动作 0..14，缩放 = roleinfo 缩放，`Anim_PlayMovieClip @0x47bc10` 初始动作 2）。游戏里是一只**小企鹅**（截图见验证记录）。贯穿整局，**没有任何规则作用**。
* **莎拉的保镖 = `gods\D46a`**（art `DAT_006044fc` = 521 522 520 519，只有 2 个动作，穿西装戴墨镜讲耳机的男人）；`Path_Create` 里 `+0x98 = 7`（= 保镖卡的 7 回合），由 vf33 在回合开始第 3 步递减（移植的 `rm8CardsHasBodyguard` 已经在做同一件事）；`Cls_5e1ca8` 的 vf5 把动作号钳到 0 / 1（0..2 → 0 / 1，其余忽略）。
* **行为（[V] 逐分支读过，字段含义 [H]）**：`route`（`+0x4c`，`+0x58` 个）= 角色**还要经过的节点**，在 vf26（开始走，push 角色目标节点）和 vf18（每到一个还有剩余步数的节点，push 角色的新目标节点）里追加，并置 `+0x84 = 1`、`+0x80 = 0`；`Path_AnimateAlongPath @0x47f0b0`：每帧按 `速度 × dt × ((游戏速度 − 1)/4 + 1)` 朝下一个节点走，路线空了就停在**最后一个节点前 20 单位**（`L − 20 < 步长`）并 `Path_ResetAnimation @0x47f9d0`（动作 2，`+0x94 = 0.3 s` 的起步延迟）；速度默认 100（`Path_CheckInitializedOnce @0x47f970` 置动作 0 + 速度 100），`Movie_SelectAnimByDistance`：与角色距离 > 80 且速度 < 200 → 动作 14（跑）+ 速度 200，< 40 且速度 ≥ 200 → 回走。`vf15 / vf37`：`Path_LinkPoints(角色出发节点的第一个「不是角色目标」的邻居, 角色出发节点)`：伴随对象站在出发节点朝邻居方向 20 单位处，面朝节点。`vf36`（伊格魯开局登场）：`Path_SetWaypoint`：先把伴随对象放到**相机右方向 300 单位**，目标点 = 它原来的位置（`+0x7c` 路点模式，`+0x80 = 1` 角色还隐藏时也画），跑进来。`vf28 / vf29`：住院 / 坐牢隐藏，`vf37` 放回时显示。
* **移植（`roles.cpp` 的 `Follower`，本轮新做）**：上述分支（路线、停 20、延迟 0.3 s、跑 / 走切换、LinkPoints、开局右侧 300 单位跑入、住院 / 坐牢隐藏并在出院时重新链接、表情转发（伊格魯，半数概率只在静止时）、莎拉的保镖由 `rm8CardsHasBodyguard` 驱动创建 / 释放）；贴图 / 模型来自 `roles.txt`（`export_roles.py` 现在导出 497..500 / 519..522）；伴随对象用**自己的随机流**（不改变存档状态，往返测试照过）。**第二轮补的分支 [V]（`roles.cpp`，下面三条 + 角色自己的饭店步行）**：
  * **进出饭店**（消息 `0x13 / 0x14` = `CPlayerTag__EnterHotelStatus @0x4852e0` / `LeaveHotelStatus @0x4853a0` 的参数 0 版，伊格魯 / 莎拉的重写 `Cls_5e27d8_OnStandby @0x47df00` / `OnMove @0x47df50` 在角色自己的处理之后，当节点有建筑对象（`*(node+4)`，位置 `+0x88`）时调 `Path_SetNormalResult @0x47fd40` / `Path_SetReversedResult @0x47fd80`：`+0x60` = 建筑，`+0x64` = 1 / 0，延迟 `+0x94` = 0.3，反向版还清 `+0x90`（显示））。`Path_UpdatePosition @0x47ee20` 在 `+0x84`（走路线）为 0 且 `+0x60 != 0` 时：进 = `Path_AnimateBetweenPoints @0x47f380`（首次 `Path_CheckInitializedOnce` 朝建筑转向；目标 = **伴随对象自己的第一个节点 `+0x40`**（不是角色的节点）与建筑的中点，到了 `Path_ResetAnimation` 并 `+0x90 = 1` 隐身）；出 = `Path_AnimateAlongEdge @0x47f510`（目标 = 第二节点 `+0x44` 朝第一节点 20 单位的站位，首次以 `vf+0x2c(目标, 当前位置)` 转向——反编译里的参数顺序如此，即背朝目标；到了 `ResetAnimation` + 朝 `(A, B)` 转向）。**角色自己的步行**原先也没做（"直接隐藏"），现在按 `Role_StepTowardBuilding @0x4854b0` / `Role_StepTowardTerraNode @0x4856d0`（状态 `0xb / 0xc`，步长 `dt × 速度 / 2`，**不乘游戏速度**——只有状态 1 才乘）做：进 = 向「节点与建筑的中点」走，体型随距离缩到一半（`min((L − 步长)/50 + 0.5, 1)`），到达时 `vf34(1)` = 状态 3 + 1 天（`rm8CardsEnterHotel`）；出 = 状态置 0 立刻（`LeaveHotelStatus(0)`），走回节点，体型 `max(1 − (L − 步长)/50, 0.5)` 回到 1，到达 `vf35(1)` = 放回节点（`vf15`：伴随对象 `Path_LinkPoints`）、站立、状态 8。步行音（`vf20` / `vf21`）同角色普通步行。`Role_TickHotelStay @0x4851a0` 住满后入队的 `0x14, 0x61` 里 `0x61` 仍按旧办法换成 `0x25(0)`（`NPC_HandleInteract` 未做 [A]）。节点没有建筑（测试钩子里的假入住）时 [A] 立刻入住 / 立刻出来。
  * **掉头**（`vf16` = `Path_ComputeDistance @0x47b5f0`，Python `Role.Turn`，卡 CardPy_11；基类还播样本 `0xb`，原先漏了，现补）：伊格魯 / 莎拉重写 `Cls_5e27d8_OnAngleChange @0x47de40`：掉头前后角色的目标节点 `+0x44` 变了，且新目标 == 伴随对象的第一节点 `+0x40`（它正站在角色要去的那一侧）→ 伴随对象的撤退节点 `+0x5c` = 旧目标。`Path_UpdatePosition` 看到 `+0x5c != 0` 就先走 `Path_MoveTowardWaypoint @0x47f6b0`（不等 0.3 s 延迟）：第一节点 == 撤退节点时朝第一节点方向走，直到离第二节点（角色的节点）20 单位（`20 <= 步长 + 距离` 时步长改成 `20 − 距离`、`ResetAnimation`、朝 `(A, B)` 转向）——即**倒退着退到角色背后**；否则先走向第二节点，到了把第一节点换成撤退节点，下一帧进入前一种。
  * **被抬走**（`vf27` = `Role_ClearFlag1C8` + `Role_ResnapToNode @0x48ceb0`，由 `RoleAnimation_UpdateFrame @0x4b1370` 的第一个时间点经 `RoleAnimation_TriggerAction @0x4b1470` 调：**救护车 2.8 s、警车 1.8 s、UFO 2.4 s**）：伊格魯 / 莎拉重写 `Cls_5e27d8_OnSpeedBoost @0x47dfa0` 在基类之后 `Path_SetSpeedVector @0x47fdd0`（速度向量 = 相机右方向，`+0x6c = 0`，`+0x94 = 2.0`）：2 s 后 `Path_TranslateByVelocity @0x47f8b0` 以 100 单位 / 秒向相机右方走，累计 60 单位后改跑（动作 14、速度 200）。**可见效果**：角色此时 `+0x1c8 == 0` 不画，`Cls_5e27d8__vf4 @0x47dcf0` 要 `+0x1c8 != 0` 才画伴随对象，所以它在这段时间也看不见；救护车 / 警车流程结束时 `vf28 / vf29` 的 `Path_ResetTrail @0x47fe30`（`ResetAnimation` 释放速度向量 + 隐藏）收尾。**UFO 例外**：UFO 的结局是 `Role_SetVanishStatus`（状态 6），不走 `ResetTrail`；若干天后 `Role_TickVanishStatus @0x48ae80` 调 `vf37 @0x47e090`（`+0x90 = 0` + `Path_LinkPoints`，而 `LinkPoints` **只清 `+0x84`，不清 `+0x68` 速度向量**）——按反编译，**伴随对象会在角色回来后继续往相机右边跑，直到角色下一次开始走路（`+0x84 = 1` 的路线分支优先于速度分支，路线走完 `ResetAnimation` 才释放向量）**。移植照此实现（`rm8TurnFollowerRunOff`，`followerSettle` 不动速度向量）；这个可见后果 [H]（没有在原版里看到过）。
  * 其余（读档、`Cls_5e27d8__vf5` 表情转发等）不变。**移植里仍简化的**：`vf15 / vf37` 之后调的 `followerSettle`（站立、清走路、0.3 s 延迟）是移植原有的 [A]，原版的 `LinkPoints` 不改动作；伴随对象的 `vf+0x2c` 转向用角色同一套 `calcAngle`。存档里不存伴随对象位置（原版同：读档按 `vf22 / vf32` 重建）。

---

## 7. 本轮补的两条规则

### 7.1 酷娃的飞弹没收（`CPlayerTag__OnSendGodCard @0x490df0`）
调用点 `CPlayerTag__OnHolidayCardUse @0x48de10`：范围（飞弹半径 1、核弹半径 3，`0 < param_4`）内的每个角色（状态 < 3）按序调用；**受害者是座位 0 的酷娃（故事）→ 整张卡作废**（直接 return，不建爆炸任务）：受害者队列 `thunk_FUN_00484930(0, 0x3f + 2·type)`（emote）、`0x65(Role_GetWaitFrames(3))`、`Role_QueueTextMsgAt(2, str 570 + type)`；**使用者 ≠ 酷娃时**再把控制权交接：酷娃队列第 3 位 `0x2e(使用者 + 1)`，使用者队列第 0 位 `0x2e(酷娃 + 1)`。移植 `npc_act.cpp blast()`：此前只做了受害者队列，**本轮补交接**；测试钩子 `RM8_NPC_MISSILE=<座位>[n]` + `RM8_STORY=12,1`（日志 `npc [t]: the missile of seat N is confiscated …`）。

### 7.2 詹姆斯的连锁超市（`Game_ActivateCharacterBuilding @0x4a37d0`、`CPlayerTag__OnChainStoreEvent @0x491630`、`CPlayerTag__GetChainStoreIndex @0x491710`）
* 11 张地图 01..11 **各有一个 art 0x45b（= `sign\a015`，詹姆斯的地主旗，`1100 + 15`）的摆件**，地图文件记录 `[17]`（`+0x44`）= **所属角色**（map 01 / 05 / 07 = 3 錢夫人，04 = 4 忍太郎，02 / 03 / 09 / 11 = 5 阿土仔，06 / 08 / 10 = 10 烏咪）。导出器 `export_scene.py` 之前没读这个字段（旗标在所有地图、所有模式里都画出来），现在写 `aown <角色>` 行。
* `Game_ActivateCharacterBuilding`（`Game_InitGame` 放完角色后、读档后各调一次）：**只对第一个** art 0x45b 的摆件：开着的条件 = 故事 + 座位 0 的角色 == 摆件的所属角色 + `GetChainStoreIndex()` ∈ 0..10 + `Event_CheckBitFlag(idx)`（该位还没亮）；否则 `CArticleMgr vf38`（`+0xC4 = 0`）关掉——**自由模式里不画**。`GetChainStoreIndex`：3 錢夫人 层 < 3 → 层 + 7；4 忍太郎 层 0 → 10；5 阿土仔 → 层；10 烏咪 层 < 3 → 层 + 4。
* 点击（`Game_OnTileClickWithCard @0x4a4a10` 的 kind 5，人类、座位 0）：`OnChainStoreEvent`：故事、`+0x1a8 == 0`、状态 6（等骰子）、索引 0..10、`Event_SetBitFlag` 新置位、`Role_QueueNpcDialog(0x20 + idx)` 成功 → 阿土仔（索引 0..3）再 `Role_EnqueueMsg(4, -1, 1)` 两次（**送两张卡**），然后 `100`（重新掷骰）、状态 8；摆件随后关掉。**每一层要选对地图**才碰得到自己的旗（例：錢夫人层 1 的候选 10 / 9 / 5 里只有 map 5 有她的旗）。
* 移植：`story.cpp` `rm8StoryActivateBuildings / rm8StoryChainStoreClick`，`scene.cpp / scene.h`（`artOwner`、`rm8SceneSetArticleActive`），`boardfx.cpp tapImpl`（命中 `artOwner` 摆件不播点击动作，走故事事件），`roles.cpp`（`rm8RolesSpawn` 末尾调 Activate）；`collect.cpp` 的 `rm8ProfileSetBit` 本来就有（全 11 位 → 解锁 15）。测试钩子 `RM8_CHAIN_TAP=<秒>`（对旗标发点击，屏幕外时直接走事件）。

---

## 8. 验证记录（本轮）

* 构建：`port/build-chars`（桌面，通过）、`port/build-chars-android`（`libmain.so` 链接成功，无 APK）。
* **菜单锁定**（`RM8_MENU_AUTO="multi,new,map:1,start,role:2,shot:…"`，`RM8_DATA_DIR` 指临时目录，**不碰真档**）：新档下角色菜单 11..16 为灰色剪影、服装按钮只有 1 亮、自由模式地图菜单 10 个地图（巴黎隐藏）；`RM8_UNLOCK_ALL=1` 下 16 个头像全亮、选角色 16 + 服装 4 后座位上是**泳装**的莎拉（比基尼）、大老千（泳裤），进棋盘后棋盘模型也是泳装。
* **伊格魯**：`RM8_ROLES=13,1,2,3 --human -1`，`RM8_DEBUG_FOLLOW=1` 每秒一行：开局企鹅从 154 单位外跑进来、停在 20 单位处；掷骰后 `walk 1 route 1`，角色走后企鹅跟到节点前 20 单位停下；截图见 `tmp_chars/shots/f13_t5.png`（企鹅站在伊格魯旁）。
* **莎拉保镖**：`RM8_ROLES=16,1,2,3` + `RM8_CARD_GIVE=0:46` + `RM8_CARD_USE=0:3`：用卡后创建保镖并跟随；`RM8_GUARD_TEST=1` 让它从开局就在，截图 `g16f_t7.png`（西装墨镜保镖站在公主旁）。
* **詹姆斯**：`RM8_STORY=15 RM8_STORY_CHAIN=1 RM8_STORY_WIN=4`（四层连打）→ 解锁 12、16，stage 4；阿土仔 `RM8_STORY=5,2`、忍太郎 `RM8_STORY=4,4`：日志 `chain-store sign (owner N) on the board` → `RM8_CHAIN_TAP` 点击 → `sign of bit N used`、drama 32 对话画面（詹姆斯 + 阿土仔，截图 `shots/story/menu_e.png`）；预置 bit 0..9 的档点击忍太郎的旗 → **解锁 15**（`rm8_gamedate.bin` 的 +0x880 = 2047，解锁列表 [15]）。
* **酷娃**：`RM8_STORY=12,1 RM8_NPC_MISSILE=0` → 日志 `the missile of seat 0 is confiscated by the story hero 酷娃`。
* **存档往返**：`scripts/save_roundtrip.py`，`RM8_ROLES=16,13,6,1`，`01 31 14 1500`（213 份记录）与 `02 3 17 1600`（222 份）**逐字节相同**；没有新增存档状态（解锁在 gamedate，保镖计数本来就存，伴随对象是纯表现）。
* **别的代理的回归**：`RM8_STORY=15 RM8_STORY_CHAIN=1` 连打时在第二张地图崩溃，定位到 `boardfx.cpp` 的 `glowClear()`（透视功能新加的）声明了但 `rm8BoardFxClear` 没调用，旧地图的 `g_glow` 悬空；**已在 `rm8BoardFxClear` 末尾加了一句 `glowClear()`**（仅此一处，EFFECTS 代理请知悉）。

* **第二轮验证（2026-11-15）**：
  * mg_21 泳装：`RM8_MINI_OUTFIT=4 RM8_MG21_AUTOSTART=1 RM8_MG21_PIRATE=1 rm8_app <游戏> scenes --minigame 21 --shot … --time 6`：四个玩家全是泳装模型（比基尼 / 泳裤，贴图自带），`RM8_MINI_OUTFIT=0` 对照是普通服装（截图 `out/_mg21/shots/swim_o4.png / swim_o0.png`）。
  * 对白头像：`RM8_STORY=6,1 RM8_SHOT_ON_INFO_N=1 RM8_GAME_SHOT=<png> RM8_NOMOVIE=1 RM8_SKIP_INTRO=1 rm8_app … --frames 3000`（新钩子 `RM8_GAME_SHOT`：交互循环里在信息框触发后 30 帧截图并退出）：第 1 天提示框带角色 18 的头像（`out/_mg21/shots/story_p18.png`）。
  * 饭店：新钩子 `RM8_EVENT_TEST=hotelwalk`（座位 0 放到饭店地块的节点上入队 `0x13`，`RM8_HOTEL_QUICK=1` 再排 `0x65(60), 0x14`）+ `RM8_ROLES=13,1,2,3`，0.2 s 一格的截图序列：伊格魯转身、走向饭店、体型缩小、**企鹅走到「它的第一节点与饭店的中点」后消失**，1 秒后角色在中点重新出现、体型长回、走回节点，**企鹅同时重新出现并走到角色旁 20 单位**（`out/_mg21/shots/hq_sheet.png`）；完整一轮（`RM8_DEBUG_FOLLOW=1`）：入住后两个回合的 `status 3`，出店后 `role (375,-575) status 0`、企鹅 `(395,-575)` 距离 20。
  * 掉头：`RM8_TURN_TEST=0.5`（新钩子，座位 0 在该时刻 `Role.Turn`）：日志 `turn around: … target 59 -> 67, companion comes from 67 (retreat)`，企鹅从 `(489,-61)` 穿过角色的节点走到 `(461,-89)`（另一侧，距离 20），截图序列企鹅从角色身前绕到背后。
  * 被抬走：`RM8_NPC_PLACE="9@1" RM8_NPC_GO=1 RM8_AUTO_ROLL=1 RM8_DEBUG_NPC=1 RM8_DEBUG_FOLLOW=1`（野狗咬人 → 救护车）：`transfer 2: seat 0 is picked up` 后企鹅 `vel 1`、2 秒延迟后往相机右方跑（距离 67 → 417 → 767，速度 200、动作 14），救护车到医院时 `ResetTrail`：`vel 0`、`hidden 1`、被重新放到医院点旁 20 单位。UFO 同一函数 `rm8TurnFollowerRunOff`，未单独跑（UFO 新闻只能随机抽到状态 0 的角色）。
  * 公园特效：`RM8_STORY=3,1 RM8_EVENT_TEST=storypark RM8_GAME_SHOT_FRAME=70 …`（新钩子 `storypark`：座位 0 放到公园格并入队 `0x1D`）：金光特效（GS-1）盖在公园格上，之后錢夫人与沙隆巴斯的对白框（drama 5）。
  * 存档往返：`RM8_ROLES=16,13,6,1 scripts/save_roundtrip.py … 01 31 14 1500`（215 份）与 `02 3 17 1600`（224 份）逐字节相同（本轮没有新增存档状态：饭店步行状态 `0xb / 0xc` 只在回合中间，存档点不在那里；伴随对象是纯表现）。

---

## 9. 未做 / 假设

**第二轮（2026-11-15）已补**（这些在第一轮的第 9 节是「未做」）：
* **泳装在 mg_21 小游戏里**：`Rm8MiniPlayer` 新增 `outfit`（= 描述符 `+0x50` = `role+0x1ac`，`CMiniGameHost_vf16 @0x50ddd0` 填 `auStack_37c[k*0x15+3]`），`event_mini.cpp` 用 `rm8TurnOutfit(seat)` 填；DLL `LG21_Player_ctor @10005cf0`：`+0x50 == 4` 时模型 = `图标 − 1 + 0xC0`（`@10005d37`），**不**调 `Asset_LoadCharacterTexture`（泳装模型自带贴图）；`SetIcon @10005c30` 同样跳过贴图；海盗跳出时 `icon + 1` 的 `0x153 / 0x15f / 0x117 …` 位置修正表〔sp_NN.bin#spl_16〕为 `icon + 0xC0` 写的（`mg_21.cpp pirateEmerge`）。`roles.txt` 里没有泳装模型的旧数据 [A] 退回普通模型。测试 `RM8_MINI_OUTFIT=4`（`--minigame 21`）。
* **故事乐透差异**：就是 `Lottery_InitScrollBar @0x49f810` 的滚轮对话框（財神 / 窮神附身的「〔string.bin#408〕」界面）：故事 = `sscrbar`（6 个轮：4 个数字 + 符号 + 倍数）+ 1/5 概率的「四位全相同」，自由 = `scrbar`（4 轮）；**NPC 代理早已完整实现**（`npc_ui.cpp`，`docs/npc_port.md` 的滚轮对话框一条），旧文档写「未做」是过时的。这轮核对了 `Cls_5e2a80__OnUpdate @0x49f570` / `Lottery_OnStop @0x49fab0` / `Lottery_CalculateResult @0x49fb50` 与 `npc_ui.cpp` 逐分支一致，并截图验证（`RM8_NPC_REEL_STORY=1` 在自由局里强制 6 轮：`2 5 5 5 x 1`）。**乐透彩票本身（letou / letouErnie 事件 `0x4c7020 … 0x4ca1b0`）没有任何故事分支**：整段里没有一处读 `*0x632de8`（反编译全文 86 个读故事标志的函数，乐透 / 开奖里 0 个）。
* **对白头像 `npc\p_18_01`**：`Role_QueueTextMsg(str 7150, 图片名 npc\p_18_01, -1)` 在 `Role_RunStoryScriptEvents @0x483f40`（第 1 天、层 0、孫小美（6））。条目 `Msg_CreateTextEntry @0x49a3a0` 的 `+0x100` = 图片名，`Role_ShowInfoDialog @0x488ae0` 看到 `+0x100 != 0` 用 gtinfo（`vf+0xc(名字, 2)` 按文件名取图，`prt` + 反斜杠 + `npc` + 反斜杠 + `p_18_01.tga` = 编号 18 的头像（不是可玩角色，drama.bin 的 rlId 18 = 玩家 / 旁白线 [M]））。移植：`game.cpp rm8GamePushTextPicFile` + `hud.cpp rm8HudInfoPicFile`（与按 uvlist 号取图的 `rm8GamePushTextPic` 并列），`story.cpp` 用它。
* **公园事件的 `0x52` 特效**：`OnParkEvent @0x48d310` 的角色 3（两条）/ 6 / 15 分支在对白前 `Role_EnqueueMsgAt(0, 0x52, art, 公园对象)`；**art 0x8df = `GS-1`、0x9c5 = `GS-2`、0x8f6 = `GS-3`（`article\eff\GS-n.dff`，金光），是 art 号**（回答 story_mode.md 的开放问题）。`export_roles.py`（`scripts/` 和 `extractor/exporters/` 两份）新增导出这三个 `fxart`，`scenes/roles.txt` 重新生成（只多 3 行）；`present.cpp` 的 `0x52` 新增 `b <= -2` = 路网节点 `−2 − b` 的位置（`rm8FxPlayAtPoint`，镜头跟过去，等 60 帧）；`story.cpp rm8StoryParkEvent` 先排 `0x52` 再排对白（原版顺序）。同一批 art 让原先被吞的 `0x2C(0x9c5)`（金泡菜通关）/ `0x2C(0x8f6)`（忍太郎层 1）也有了模型。`0x7b / 0x7c`（`Role_ClearFlag1C8 / SetFlag1C8`）早由 `present.cpp` 的 `g_hidden` 实现（旧文档过时）。
* **伴随对象**的进出饭店、掉头、被抬走（第 6 节）。

**仍未做 / 假设**
* `Role_TickHotelStay` 住满后的 `0x14, 0x61` 里 `0x61`（`NPC_HandleInteract` 等）仍用 `0x25(0)` 代替 [A]；饭店里被送医院时原版 `0x18` 分支的 `vf35(1)`（把住店的角色立即放回节点）没做（移植直接进医院）。
* UFO 之后伴随对象继续往相机右边跑（第 6 节）：按反编译实现，**未在原版里确认**。
* **假设 [H]**：`param_1+0x4c`（公园事件 / 錢夫人相遇的「到访者是谁」）；`+0x5c0` 住院标志；伴随对象字段命名；怪物 A13b 在伊格魯层之外的「Q」贴图（只作用于表情，未单独核对）；公园事件特效的位置取路网节点位置（原版是公园对象 `+0x88`，应是同一点）。
* 动态验证：全部是移植侧的测试，**没有与实机 / 原版画面逐帧对照**（伴随对象的位置 / 速度、泳装模型的朝向、饭店步行的缩放都是按反编译常数做的，没有原版截图对照）。

---

## 10. 测试钩子（环境变量）

| 变量 | 作用 |
|---|---|
| `RM8_UNLOCK_ALL=1` | 角色 11..16、服装（stage 4）、9 块拼图、自由模式地图全部读作已解锁；不写档案 |
| `RM8_DATA_DIR=<目录>` | 档案（`rm8_gamedate.bin` / `rm8_box.bin` / `save/`）放别处；目录必须已存在；**测试不要碰真档** |
| `RM8_IMPORT_GAMEDATE=1` | 没有移植档案时一次性导入 `<游戏>/Config/gamedate.bin`（只读） |
| `RM8_CHAIN_TAP=<秒>` | 詹姆斯的连锁超市：该时刻起每 2.5 s 对旗标物件发一次点击（在屏幕内走真实的拾取路径 + 状态 6 门，屏幕外直接走故事事件），日志 `boardfx: chain-store tap …` |
| `RM8_DEBUG_FOLLOW=1` | 伊格魯 / 莎拉的伴随对象每秒打印一行（位置、路线数、速度、动作、距离） |
| `RM8_GUARD_TEST=1` | 莎拉的保镖从开局就在（不用保镖卡） |
| `RM8_NPC_MISSILE=<座位>[n]` | 该座位在开局向座位 0 所在节点发射飞弹（`n` = 核弹）；配合 `RM8_STORY=12,1` 看酷娃没收 |
| `RM8_ROLES=13,16,6,1` / `RM8_STORY=<角色>[,<地图>]` / `RM8_STORY_CHAIN=1` / `RM8_STORY_WIN=<秒>` / `RM8_CARD_GIVE` / `RM8_CARD_USE` | 既有钩子（`story_mode.md` 4.5、`cards_port.md`） |
| `RM8_MINI_OUTFIT=<0..4>` | `--minigame` 里所有玩家的服装（4 = 泳装，mg_21 用 `图标 + 0xC0` 模型） |
| `RM8_EVENT_TEST=hotelwalk` / `storypark`（+ `RM8_HOTEL_QUICK=1`） | 座位 0 放到饭店地块的节点上走进饭店（`0x13`；`QUICK` 再排 1 秒后 `0x14` 走出）/ 放到第一个公园格并入队 `0x1D`（故事的公园对白，要 `RM8_STORY`） |
| `RM8_TURN_TEST=<秒>` | 座位 0 在该游戏时刻 `Role.Turn`（伴随对象的撤退分支，配合 `RM8_DEBUG_FOLLOW=1` 的日志） |
| `RM8_GAME_SHOT=<png>` + `RM8_SHOT_ON_INFO_N=<n>` 或 `RM8_GAME_SHOT_FRAME=<帧>` | 交互循环（`--frames`，故事测试）里在触发后 30 帧用 `glReadPixels` 截图并退出（`--shot` 模式不跑故事开局） |
| `RM8_MENU_AUTO="multi,new,map:1,start,role:16,tap:23,shot:x"`（`--menu`） | 菜单截图：新档 vs 解锁档的角色菜单；`tap:23` 点服装 4（性别 1 的角色是 33） |
