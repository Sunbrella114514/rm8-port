# 事件特效审计与补全（effects）

标记：**[V]** 反编译核对（地址 `@0x…`），**[H]** 推测，**[A]** 移植的取舍。反编译来源 `out/final_v2/RichMan8_named.c`（名字只有一半可信，结论以代码 / 反汇编为准）。
代码：`app/boardfx.cpp`（地块特效任务、看穿、事件图标）、`app/present.cpp`（特效摆件播放器 `rm8FxPlay`）、`app/news.cpp`（UFO、新闻特效）、`app/npc_act.cpp`（载具秀、神、导弹）、`app/cards.cpp` / `events.cpp` / `event_magic.cpp`（各消息的入口）、`app/scene.cpp`（着色 / 透明度 / 脱离场景）。

## 0. 先更正一条规格

任务书（`rules_land.md` 6.2、`boardfx.md` 7）把“拆房动画 6002 + 样本 3”也套在新闻拆房、飞弹 / 核弹、怪兽上。反编译结论 [V]：

| 来源 | 实际行为 |
|---|---|
| **只有** 消息 0xD（拆屋卡、问答奖惩、魔法屋“拆一级”）走 6002 + 样本 3：`Role_StartLandLevelDown @0x4875e0` → `BuildLevel_StartDecrement @0x4eef80` → `NPC_InitDisplay @0x4f4f20`（类 0x5E66B8） | 效果动画结束时 `BuildLevel_StepDecrement @0x4ef860` 等级 −1 |
| 新闻拆房（台风 / 外星人 / 瓦斯 / 洪水 / 地震 / 〔string.bin#333〕） | 队列 `0x52(特效 art, 地块)` 先播特效（`Role_ShowEffectAnimAtTerra @0x48b320`，特效播完角色才继续），再 `0x4A(地块, 1)` = `u_Role_ResetTerraEffect @0x4876c0` → `BuildObj_ResetAll(1,1) @0x4ef8c0`：**瞬间**清楼，没有 6002 |
| 飞弹 / 核弹 | `TerraMgr_AnalyzeRoleStocks @0x4f22e0`（类 0x5E6A78，2.0 s）结束时 `Building_SuccessBuild @0x4f4800` = `BuildObj_ResetAll`，瞬间 |
| 怪兽 0x31 | 怪兽 art 6004 + 样本 6，**1.5 s** 时 `BuildObj_ResetAll`，动画结束任务结束（`@0x4f52e0 / @0x4f5330`） |
| 消息 0xF（`u_Role_ProcessUpMsg`，名字不准，实为“拆光”：6002 + `NPC_ResetBuildings`） | 反编译里没有任何发送者，移植不实现 |

## 1. 审计表

“角色反应”列 = 原版在同一处排的 0x5A（`thunk_FUN_00484930(位置, 类别)` = `rm8RoleSayAt`）等。状态：已做 / 近似 / 缺。

### 1.1 `Anim_CreateSprite @0x47b090` 的全部调用者

| 调用者 | 原版（art / 样本 / 时间） | 角色反应 | 移植 |
|---|---|---|---|
| `Role_ShowEffectAnimAtSelf @0x48b2b0`（0x2C） | 角色位置 +30、向相机拉 20、偏航 = 相机 − 180；`0x65(60)`，摆件 10 s（带动画回调）/ 4 s × 速度系数 | 无（发送者各自排） | 已做（present.cpp `rm8FxPlayAtRole`） |
| `Role_ShowEffectAnimAtTerra @0x48b320`（0x52） | 同上放在地块；样本表：0x177A→0x68、0x178A→0x65、0x178B→0x66、0x178C→0x67 | 无 | 已做 |
| `CPlayerTag__OnMovieIntro @0x492e40`（art 6000 Gwin，故事胜利） | 音乐 103、14 s | — | 已做（story.cpp） |
| `CPlayerTag__OnMovieDrama @0x492f30`（6006 Glost 破产） | 音乐 104、14 s | 破产前有 0x1A 表情（rules） | 已做（boardfx `rm8BoardFxBankruptcy`） |
| `Cls_5e33a8__OnClick @0x49bb40`（6122 = 粒子 23 + 样本 0x1E，神出场） | 4 s | 神的问候 / 附身话语（NPC） | 已做（npc_act `godClick`） |
| `Cls_5e2db0__OnRoleClick @0x49cd70`（6002 踩蛋 → 附身 100..103） | 4 s | 附身 | 已做（npc_act，`0x1772`） |

### 1.2 其它放置特效摆件 / 任务的地方

| 效果 | 原版 [V] | 角色反应（原版） | 移植 |
|---|---|---|---|
| 加盖 | 6001 + 样本 2，1.5 s 后旧模型动作 1/3，写等级 + 动作 0/2 + 样本 0x7A（类 0x5E6618，`@0x4f4b50`） | 表情 1/2/0x10/0x12（`Role_BuildOnLand`，present） | 已做（boardfx 原有） |
| **拆一级（0xD）** | 6002（住宅缩放 0.5）+ 样本 3，建筑动作 1/3，**特效结束写 level−1**；无建筑则立即 −1 且无反应；点击跳过 = vf13 `@0x4f50e0` | 70 % 且地块是自己的：`SayAt(0, 5)`（`@0x4875e0`） | **本次已做**（`rm8BoardFxDemolish`，cards.cpp 0xD） |
| **天使卡 0x10**（街道 +1 级） | `Building_AnimateMultiple @0x4f3b20`（类 0x5E6988）：街道上有主且等级<5 的每块地：6121（粒子 22 光环）+ 旧模型动作 1/3，第一块 + 样本 0x1E；首个旧动画结束（`@0x4f3db0`）→ `Building_ProcessAnimation @0x4f3de0` 全部 +1 级（空商业地随机建筑 2..4）+ 新模型动作 0/2；首个新动画结束任务结束；无点击跳过 | 出牌台词（cards）；无额外 | **本次已做**（`rm8BoardFxAngelBuild`） |
| **地王卡 0x44** | `Street_ProcessTerraBuild @0x4f2190`（类 0x5E69D8）：整条街（≤63 块）闪烁 1.6 s（计数 2/3/4 交替，`BuildObj_SetAlpha`：地块垫 alpha 0x60 / 建筑灰 0x60），结束样本 1 + 全部 `Land_SetOwner`（`@0x4f4240`）；无跳过 | 无 | **本次已做**（`rm8BoardFxBlink`） |
| **交换卡 0xE** | `NPC_CreateFortuneTeller @0x4eee90`（类 0x5E6898，仅当两块地主人不同）：两块地一起闪 1.6 s，结束互换主人 + 样本 4（`@0x4f5690`；汇编核对 `@0x4f5710` 对 this+4 与 this+0x10 各调一次 SetAlpha） | 无 | **本次已做** |
| **0x2A 占无主 / 他人地**（问答奖励） | `Role_StartEffectOnOtherTerra @0x4879b0`：镜头对准地块，闪 1.6 s，`Land_SetOwner(自己)` + 样本 1；排 0x1E 到队首，`SayAt(1, 2 + rand%2)` | 类 2/3 | **本次已做** |
| **0x29 失去自己的空地**（问答惩罚） | `Role_StartEffectOnOwnTerra @0x487a70`：闪 1.6 s 后 `Land_ClearOwner`；`SayAt(0, 5 + rand%2)`。**原版怪癖 [V]**：`NPC_Angel_Init @0x4f5740` 判断 `[地块+0x4C] > 0`（汇编 `cmp …,0 / jle`），即地主座位号必须 ≥ 1；座位 0 的地块**立刻**清空，没有特效、没有状态 9、没有表情 | 类 5/6（座位 ≥ 1） | **本次已做**（含怪癖，events.cpp） |
| **巫师闪烁 0x4F**（新闻 26/34/29、破产拍卖前） | `Role_ProcessWizardMsg @0x487b60`：相机对准地块，类 0x5E6938 `@0x4f5d50` 闪烁（1.6 s：暗 0.4 / 亮 0.4 / 暗 0.4 / 亮 0.4，不是 2 s），不改数据 | 无 | **本次已做**（news.cpp 0x4F → `rm8BoardFxBlink`） |
| **魔法屋 / 故事事件 0x35**（随机 n 块有楼的自有地） | `Role_StartEffectOnRandomOwnTerras @0x487780` → 类 0x5E6A28 `@0x4f4380`：逐块：相机对准 → 6028 crash（住宅缩放 0.5）+ 样本 0x67 → 1.5 s 后 `BuildObj_ResetAll` → 再 1.0 s 下一块 | `SayAt(0, 5)` | **本次已做**（`rm8BoardFxCrash`，event_magic.cpp） |
| **怪兽 0x31**（怪兽卡、怪兽新闻） | 见 0 节；`CPlayerTag__OnParkEvent @0x48d310` 自由模式末尾 `u_NPC_Chest_Construct` | 无（原版也没有；旧移植误走 0x4A 让地主说话类 4，已去掉） | **本次已做**（`rm8BoardFxMonster`，任务到动画结束，不再是固定 90 帧） |
| 台风 / 外星人攻击 / 瓦斯 / 洪水 / 地震 / 拆迁 | 0x52(6027 tornato / 6026 ufo / 6028 crash) + 样本 0x66/0x65/0x67；特效播完后 0x4A 瞬间清楼；拆迁另 0x5C 补偿 | 地主 0x4A 时说话类 4；0x2E 交接 | 已做（news.cpp / present.cpp 原有）；本次核对：台风漏斗为半透明锥体，建筑在特效结束时消失 |
| 飞弹 / 核弹（0x36 / 0x37） | `CPlayerTag__OnHolidayCardUse @0x48de10`：6007 D18t / 6008 D19t + 样本 0x12 / 0x13，2.0 s 后清楼（飞弹留地主，核弹连主人），住院天数、交接 0x2E、0x81 | 被炸角色住院；击中保镖 / 泡泡免疫 | 已做（npc_act `blast`） |
| 地雷 / 定时炸弹 | 6005 粒子 1（地雷）+ 同函数 | 住院 | 已做（NPC 代理） |
| **UFO（新闻 20，消息 0x5F）** | `NPC_CreateRandom_Type3 @0x4a46d0` → `Stock_InitializeObject_Type3 @0x4a8580`（类 0x5E3BE0，0x5E3AF0 家族）：**网格对象**（`Mesh_LoadAndInit @0x47a2c0`，art 0x178A = 6026 ufo.dff，缩放 1，无样本）放在 `Mesh_SetTranslation(角色位置)`、动画 0 播一次（3.33 s，UFO 从左上飞来悬停，光柱向下）；`RoleAnimation_UpdateFrame @0x4b1370`：2.4 s `RoleAnimation_TriggerAction @0x4b1470` = 角色 vf+0x6C（隐藏，`Role_ClearFlag1C8` + `Role_ResnapToNode`），4.0 s `Cls_5e3be0__vf18 @0x4b16a0` = `Role_SetVanishStatus(天数) @0x48ae10`（状态 6，每回合 str359「消失還有%d天」，`Role_TickVanishStatus @0x48ae80` 到 0 → 状态 0 + 0x61 重新处理落点，**没有 UFO 归还演出**）；`vf2 @0x4b1420` 先 `RwCameraClear(z)` 再画网格（盖在一切之上）；点击跳过 `RoleAnimation_SkipToEnd @0x4b1320` | 无 | **本次已做**（news.cpp `UfoRun` + present.cpp 叠加层；位置 = 角色位置，不再 +30 / 拉近；跳过） |
| 警车 / 救护车（0x19 / 0x18） | 同 0x5E3AF0 家族（art 0x526 / 0x527，缩放 0.5，1.8 / 2.8 s 抬走，5.0 / 7.5 s） | 住院 / 坐牢后说话类 4 | 已做（npc_act）；**本次补**：点击跳过（`rm8NpcTransferSkip`，`@0x4b1320`） |
| 保镖（卡 46 其他角色）、狗咬 / 咬人烟 6024、夺命 0x177D | 0x2C 消息 6023 / 6024 / 6013 + 样本 0x3F | 住院 | 已做（NPC / 卡片代理） |
| 5 级房烟花 | 0x52(6010 eff_12) + 样本 0x68 | 表情 0x10 | 已做（present） |
| 神出场 6122 | 见 1.1 | 神的问候 | 已做 |
| 事件图标按下 / 亮 / 粒子 14 | `Article_PlayApproachEffect @0x4ee3c0` | — | 已做；**本次修**：亮版贴图和粒子 14 现在只在“停在图标上的角色把图标按到底之后”出现（见 3.3） |
| 摆件点击动画 / 声音 | `CArticleMgr vf29 @0x43e9c0` | — | 已做（boardfx 原有） |
| **遮挡半透明 40 %** | `RoleMgr_UpdateAll @0x495bf0` → vf26 / vf27 / `Art_UpdateGlowAlpha @0x441740` / `vf5 @0x43b210` 模式 3 | — | **本次已做**（见 3.4） |
| 新闻 TV 的 4 个 Im2D 格子 | `Cls_5e20a8__vf2 @0x4b4770` / `News_InitDialog @0x4b41b0`：2×2 个 240×280 屏幕矩形，顶点色 **0（ARGB 全 0）**，渲染状态 `0xC = 1`（VERTEXALPHAENABLE）、纹理栅格 0 | — | **结论：原版不可见**（alpha 0 的混合），不画是对的，从待办删除 |
| 街道查封（6003 D05t + land_close.tga）/ 涨价跌价箭头（6120 + land_up / land_down） | `Terra_LoadCloseIcon @0x42d3b0` / `Terra_SetHeight @0x42d4c0` / `Terra_UpdateHeightAll @0x42d650`（类 0x5E0910，`Terra_RebuildQuadMesh @0x4ef0f0` 给街道每块地盖一张贴图四边形，任务 1.2 s，之后随天数保留） | 无 | **缺**（数据已有，贴图 `texture\terra\*.tga` 在包里；未做） |
| 选目标标记 6009 eff_att_f（飞弹邻接点）、6114 箭头（选角色，`roles.cpp` 已做回合箭头）、6115 Eff_11、6119 eff_09（问答 NPC）、神灵 6011 / 6014 / 6015 | `Terra_RebuildFromTerra @0x49ff60` 等 | — | 近似（卡片选目标用黄色菱形；问答 / 精灵未做） |
| 魔法屋气泡（粒子 0） | 地图粒子 | — | 已做（particles） |

## 2. 角色反应汇总（新增 / 核对）

- 拆一级：`SayAt(0,5)`，条件 `param != 0`、`rand%100 < 70`、地块属于自己（`rm8PresentChance70`，cards.cpp）。
- 0x2A：先 0x1E 到队首，再 `SayAt(1, 2 + rand%2)`；0x29：`SayAt(0, 5 + rand%2)`（座位 ≥ 1）；0x35：`SayAt(0, 5)`；0x4A：地主说话类 4（news.cpp 原有）。
- 房屋被毁的地主、被抓 / 住院 / 消失的角色、金钱增减表情：见 `news.md` 第 7 节（原有，已核对）。
- **没有**的反应（原版也没有，已核对）：怪兽、地王、交换、天使、UFO、巫师没有额外表情；没有“角色转向事件”的代码（原版靠 `thunk_FUN_0041d850` 镜头聚焦，不转身）。
- 0x2E 交接：新闻拆房 / 消失 / 住院等都由新闻 / 卡片代理按 `Role_EnqueueMsgPairWithCurrent` 排好，本次没动。

## 3. 本次实现

### 3.1 地块特效任务（boardfx.cpp，`boardfx.h` 末尾）

| 函数 | 内容 |
|---|---|
| `rm8BoardFxDemolish(seat, plot)` | 6002 + 样本 3，建筑动作 1/3，效果结束写等级；点击跳过立刻结束（`rm8BoardFxBuildSkip` 扩展，`@0x4f50e0`） |
| `rm8BoardFxBlink(seat, plots, endSample, onEnd)` | 1.6 s 闪烁（地块垫 alpha 0x60 / 建筑 `rm8SceneTintArticle(0x60)`，0.4 s 一档），结束样本 + `onEnd`（改主人）；用于 0x4F / 0x29 / 0x2A / 0xE / 0x44 |
| `rm8BoardFxAngelBuild(seat, street)` | 6121 + 样本 0x1E，旧动画结束写 +1 级，新动画结束任务结束 |
| `rm8BoardFxCrash(seat, plots)` | 0x35 的逐块爆炸序列 |
| `rm8BoardFxMonster(seat, plot)` | 6004 + 样本 6，1.5 s 清楼，动画结束任务结束 |

全部任务期间角色是状态 9（`rm8TurnSetState`），结束回 8（`Game_EndTurn @0x4a48d0`）；数据（等级 / 主人）在原版的那一刻写入（`rm8GameSetTerra`）。存档规则：瞬态任务不存，存档点只在静止点，所以任务期间不会被存；`rm8BoardFxCancel / Clear` 清掉全部任务。

### 3.2 UFO、叠加层、跳过

- `present.cpp`：`rm8FxSetOverlay(handle)`：把特效摆件移出场景摆件表（`rm8SceneDetachArticle`），`rm8PresentRenderLate` 先 `cam->clear(Z)` 再画（`@0x4b1420`）。
- `news.cpp`：消息 0x5F → `UfoRun`（2.4 s 隐藏角色 `rm8NpcSetRoleHidden`、4.0 s `rm8CardsVanish`、状态 9→8）；`rm8NewsSkip()` 与 `rm8NpcTransferSkip()` 接进 `skip.cpp` 的 `rm8SkipRequest`（场景任务 vf13，`@0x4b1320`）。

### 3.3 事件图标的亮 / 粒子（用户反馈修复）

role.bin 的时间事件：动作 0（停下按压）`0.08 s` 换亮版贴图、`0.035 s` 粒子 14；动作 2（路过）`0.08 s` 亮；动作 1（弹起）`0.0 s` 亮、`0.05 s` 暗。按压动画只有 0.167 s，而按压从“前方节点距离 < 40”就开始，所以旧实现在角色还在走最后一小段时就亮了。现在：路过（动作 2）不亮；弹起的“亮”事件只在图标已亮时有效；动作 0 播完后只“待命”，直到该座位真的停在节点（状态不再是走路且距离 < 12）才一次性换亮版贴图并放粒子 14；角色在此之前离开则取消。日志 `RM8_BOARDFX_TRACE=1`：`action 0 … sound 0x7D t=3.30` → `texture …_d.tga (seat 0 stopped, press complete) t=3.50`；路过只有 `action 2` / `action 1` 和暗贴图。

### 3.4 遮挡半透明（2026-11-15 修订：渐隐摆件延后到角色之后画，见 BUGS 19）

`boardfx.cpp glowUpdate`（15 Hz 检测 + 逐帧渐变）：对每个状态 < 3、可见的角色取 `位置 + (0,30,0)` 及左右 ±20 / 上下 ±30 的 5 点，窗口内（30 < x < W−30，30 < y < H−100）的点从相机向它作线段，与地图摆件（`Rm8Scene::mapArticles`，原版 +0xC4 != 0）的包围球 ×1.5 和三角形求交，命中者透明度向 0.4 渐变，否则回 1.0（速度 1 / 秒 [H]）；`scene.cpp rm8RenderArticles` 把透明度 < 1 的摆件放到后一遍（alpha 混合、不写 Z）。`RM8_NO_SEETHROUGH=1` 关闭，`RM8_BOARDFX_TRACE` 打印命中数。仅当 init.txt `character > 4` 时原版才做（本机 6）；移植恒开。

### 3.5 烟雾 alpha（用户反馈修复）

原因：`prt\prtNN.tga` 多为 24 位 RGB（无 alpha），effect 6 / 7 / 10 / 18（SRCALPHA/INVSRCALPHA）和 11 / 15 / 16 / 25（SRCCOLOR/INVSRCALPHA）的目的因子用 alpha，整张方块 alpha=1 → 烟团带着硬边的方块。修复 [A]：目的因子是 INVSRCALPHA 且贴图没有 alpha 时，alpha 取三个颜色通道的最大值（黑 = 透明，`rm8PackTextureLumaAlpha`，particles.cpp 在创建发射器时用）。加色类（SRCALPHA/ONE，神烟 6122 等）本来就不受影响。**[H]** 原版如何得到这个 alpha 没找到（`RwTexture_ReadCallback @0x409800` 的 raster 格式 0 / 0x8004，TGA 24 位 alpha 应为 255；没有 mask 文件），所以这是视觉上的修正，不是逐行还原。

## 4. 测试钩子

`RM8_FX_TEST="<秒>:focus:<x>:<z>;<秒>:set:<地块>:<主人>:<等级>[:<建筑>];<秒>:msg:<座位|-1>:<命令>:<a>:<b>;…"`（boardfx.cpp）：按顺序在场景时间到达时设地块 / 把消息放到座位队首（-1 = 当前角色，等它在状态 6 / 8），`focus` 让 `RM8_SHOT_FOLLOW` 的相机一直对准某点。`RM8_BOARDFX_TRACE=1` 打印任务开始 / 结束 / 样本。例：
`RM8_FX_TEST="0.5:focus:-225:-625;1:set:1:2:3;2:msg:0:0xD:1:1"`（拆地块 1）、`…msg:0:0x10:10:0`（天使，街 10）、`…msg:0:0x44:10:0`、`…msg:0:0xE:0:1`、`…msg:0:0x35:0:2`、`…msg:0:0x31:1:0`、`…msg:0:0x5F:3:0`（UFO）、`…msg:0:0x4F:1:0`（巫师）、`…msg:0:0x2C:6024:0`（咬人烟）。

## 5. 验证记录（build-fx，`--shot`，地图 01，seed 7）

- 拆一级：`demolition of plot 1 starts t=3.40 … done (level 3 -> 2) t=4.77`（效果 1.33 s + 建筑动作）；截图序列（hammer + saw 6002、房子缩小）；跳过（`RM8_SKIP_TEST=3.8`）→ `done t=3.83`；紧接着 `present: seat 0 class 5`。
- 天使：`angel build … lead 0 t=3.50`、`levels written t=3.83`（旧动画 0.33 s）、`done t=4.83`；截图：光环粒子 + 3 栋房子升级。
- 地王：`3 plot(s) blink t=3.50 … blink ends t=5.10`，截图：垫子闪、结束变红（座位 0 颜色）。
- 交换：`2 plot(s) blink … blink ends t=5.10 / sound 0x4`，垫子颜色互换。
- 0x2A：闪烁、`sound 0x1 t=5.10`、`present: seat 0 class 2`；0x29：座位 0 立即清、座位 1 闪 1.6 s。
- 魔法屋炸楼：两块地逐块爆炸、楼消失、留地主旗；怪兽：恐龙出场、1.5 s 楼消失。
- UFO：截图序列（UFO 飞入、光柱、角色在 2.4 s 后消失、4 s 后回状态 8）。
- 台风：电视机新闻 → 漏斗 → 楼消失 → 地主说话类 4。
- 看穿：`see-through: N scenery articles between the camera and a role`（地图 05 t=0.07 命中 4 个，截图对比：塔楼半透明、角色可见）。
- 烟雾：咬人烟 / 爆炸烟 / 神烟 的放大截图软边。
- 图标：`final approach … action 0 … sound 0x7D t=3.30`，`texture …_d.tga (seat 0 stopped, press complete) t=3.50`。
- 长模拟（全电脑，`RM8_NEWS_EVERY=1 RM8_START_CASH=500000,0`，3000 s，地图 01 / 05）和 `RM8_CARD_GIVE` 发满拆屋 / 交换 / 怪兽 / 天使 / 地王 / 飞弹的 3000 s 模拟（地图 01 / 03）：0xD、0xE、0x10、0x44 多次触发，无卡死、无 stuck 日志。
- 电脑 / 安卓：`build-fx` 与 `build-fx-android` 编译链接通过。

## 6. 未做 / 待确认

1. 街道查封 / 涨价跌价的地块贴图叠加（1.2 表末）：数据在包里，需要 `Terra_RebuildQuadMesh` 的四边形规格。
2. 选目标时的 6009 / 6114 / 6115 标记、问答 NPC 6119、精灵 6011 / 6014 / 6015。
3. 烟雾 alpha 的原版来源 [H]；看穿的渐变速度 [H]；`0xF` 无发送者。
4. 破产过场的 0xE 状态（用 9 代替）、故事模式的怪兽新闻调用（原有）。
5. 地王 / 交换 / 巫师等任务原版没有点击跳过（vf13 返回 0），移植同样不可跳过（拆房和加盖可以）。

## 7. 追加（2026-11-15 晚）：加盖运镜 / 烟雾、骑车动作表

- 加盖 / 拆房 / 怪兽 / 导弹等特效摆件的**定时粒子事件**（role.bin 子表 4）现在导出为 `fxev` 行并由 `present.cpp` 的特效播放器按片段时间触发：6001 加盖工人 0.9 s 粒子 10（烟尘，[@1]）、6002 拆房 0 s 粒子 10 [@2]、6004 怪兽 0.192 s 粒子 10、6008 飞弹 1.3 s 粒子 12、6027 龙卷 0 s 粒子 11。`rm8BoardFxBuildStart` 改走 `rm8FxPlay`，并像原版（构造时 vf+0x44 = `Obj_CallHandlerOnField4 @0x4f4ad0`）把相机移到地块。
- 骑车 / 开车：`roles.cpp vehicleAnim`（表 0x5E1DD0，见 `turn_flow.md` 5）。
