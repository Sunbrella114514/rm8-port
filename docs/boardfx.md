# 棋盘反馈（boardfx）：事件图标、场景互动、地产规则的音效与特效

状态标记：`[V]` 已在反编译里核对（附地址）；`[H]` 推测；`[A]` 移植里的取舍。反编译来源 `out/final_v2/RichMan8_named.c`（函数名只有一半可信，以代码为准）。
移植代码：`port/app/boardfx.cpp` / `boardfx.h`；场景数据由 `scripts/export_scene.py` 导出（新增行见第 6 节）。

---

## 1. 事件图标（落脚点的小图标）

### 1.1 图标是什么 [V]

- 每个带事件的路格（`rnode` 的 event）在地图加载时放一个摆件：`BuildObj_SetupArticle @0x4ee230`——位置 = 路点中心，`vf34`（旋转）= −45°（`0xc2340000`），`vf23(动作 3, 播放次数 1, 槽 0)`，即**播一次并停在最后一帧**；对象字段 `+0x10`（位掩码）清零，`+4..+0xC` 记下位置。
- 图标 art 1001..（`event.bin` 的 `artId`），`role.bin` 第 2 个子表有 4 个动作：**0、2 = `eve_xxx_01.anm`，1、3 = `eve_xxx_02.anm`**（0 与 2、1 与 3 是同一个文件）。
- 两个文件都是 6 帧、0.167 秒，只动骨 1（图标本体）的 y：`_01` 从 5.47 降到 1.08（**按下去**），`_02` 从 1.08 升到 5.47（**弹起来**）。创建时播的是动作 3（`_02`），所以地图一开始图标是“弹起来”的。
- 图标 art 没有挂子 art 和 UV 动画（`role.bin` 第 1 个子表为空），但第 3、4 个子表有换贴图和粒子事件，见 1.4。

### 1.2 Approach / Depart 的精确行为 [V]

`Article_PlayApproachEffect(座位号, 角色位置, 参数) @0x4ee3c0`（this = 图标对象，`+0x10` = 站在上面的座位位掩码）：
1. 该座位的位已置位 → 什么都不做（返回 0）。
2. 距离 = `|图标位置 − 角色位置|`（三维，`RwV3dLength`）；位置指针为空时按 50 算。
3. 距离 **< 40.0**：
   - 掩码为 0（图标上还没有别人）：参数 == 0 → `vf23(动作 2, 1, 0)`（按下，无声音）；参数 != 0 → `vf23(动作 0, 1, 0)` **并** `CAudioMgr_PlaySample(0x7D, loops 1)`。
   - 然后置位该座位的位（即使图标上已经有别人，也只置位，不再播动作和声音）。

`Article_PlayDepartEffect(座位号, 角色位置) @0x4ee4f0`：
1. 该座位的位未置位 → 什么都不做。
2. 距离 = 同上（位置指针为空按 0 算）。
3. 距离 **> 10.0**：清除该位；清除后掩码为 0 → `vf23(动作 1, 1, 0)`（弹起）。

### 1.3 谁在什么时候调用 [V]

| 调用点 | 做什么 |
|---|---|
| `Role_StepAlongPath @0x485860`（走路每帧；先用 `vf+0x44` 移动角色） | ① 若走之前的节点有图标：`Depart(旧节点)`；② `Game_UpdateRoleDirection`；③ 步数 == 0：对当前节点 `Approach(…, 1)`，`Game_MoveRoleToPosition`；④ 否则：节点换了就对新当前节点 `Depart`；对**前方节点**（`this[0x11]`）`Approach(…, 步数 == 1)`；`Game_MoveRoleToPosition(前方节点, …, 步数 == 1)` |
| `CPlayerTag__OnCheckBankruptcy @0x48e920`（实为“把角色搬到另一个节点”：交换位置、传送卡） | `Depart(当前节点, (0,100,0))`（强制弹起）→ `vf15`（设路点）→ `Game_MovePlayerToPosition` → `Approach(新节点, 0)` |
| `CPlayerTag__SnapToNodePos @0x48cfd0`（`Cls_5e27d8_OnPropertyChange @0x47e090` 调用） | 重置路径，`Approach(当前节点, 0)` |
| `CPlayerTag__vf24_PlayEffectSetStatus8 @0x483110` | `Approach(当前节点, 0)`，状态 = 8 |
| `Role_ResnapToNode @0x48ced0`（`CPlayerTag__ClearFlag1C8AndResetPose @0x48ceb0`） | `Depart(当前节点, (0,100,0))` |
| `Game_LoadSavedGame @0x496a90` | 读档后对每个状态 < 3 的角色 `Approach(…, 0)`（移植没有读档） |

**经过与停下的区别**：
- 路过一个图标：进入 40 范围时按下（动作 2，无声音，因为 `步数 != 1`）；离开到 10 以外时弹起（动作 1）。一次完整的“踩下—弹起”。
- 停下（最后一步，`步数 == 1` 的前方节点）：距离 < 40 时按下（动作 0）**并播放 0x7D**；角色停着时图标一直按着；下一回合开始走、离开 10 以外时弹起。声音是在**到达前约 0.4 秒**（进入 40 范围的那一帧）播放的，不是到达那一刻。
- 同一个图标上有两个角色：第二个角色只置位，不重复播放；先走的离开时只清自己的位，最后一个离开才弹起。
- 破产的角色（`OnRoleBankruptcy @0x48f750`）只做 `Game_UpdateRoleDirection`，不做 Depart：图标位保持（原版如此，不影响显示）。

### 1.4 图标的“发光”和粒子（2026-11-15 补查，之前漏了 role.bin 的第 3、4 个子表）

按下图标时除了 0.167 秒的下沉动画，原版还有两样东西（`Article_Update @0x448540` 在动作播放期间按时间点触发 role.bin 的“动作事件”）：

1. **换贴图（发光）** [V]：`role.bin` 第 3 个子表（0x224 字节/项：`+4` 动作、`+8` 时间、`+0xC` 材质名、`+0x20` 贴图文件、`+0x120` 目录）→ `UI_LoadMaterial @0x44BDC0`（`RwTextureRead` 后 `UI_SetAttributeByName @0x44BCC0` 设到同名材质）。每个事件图标 art 4 条：动作 1（弹起）0 秒 `eve_xxx_d.tga`、0.05 秒 `eve_xxx.tga`；动作 0 / 2（按下）0.08 秒 `eve_xxx_d.tga`。`_d` 是**发亮版**（同一图案，亮黄 / 亮白光晕，贴图包 `article\event\eve_*_d.tga`）。即：踩上去 0.08 秒后图标变亮并一直亮着，离开时弹起并在 0.05 秒换回普通贴图。
2. **粒子特效** [V]：第 4 个子表（0x1C 字节/项：`+0` 动作、`+4` 效果号、`+0xC` 时间、`+0x10` 类型、`+0x14` 挂点）→ `Article_SpawnEffect`（PrtEffTable）+ `CopyMatrix` 跟随挂点帧。事件图标（除监狱 / 医院外）动作 0（**停下**，不含路过的动作 2）0.035 秒：效果 **14**（`prt05.tga`，球形发射 20 个，寿命 1 秒，速度 30，尺寸 20→0，加色混合 SRCALPHA/ONE，持续 1 秒）挂在 DFF 的 `[@1]Plane02` 帧上，即停下时图标上炸出一圈闪光。

之前文档写“没有粒子、没有 UV 动画”是只看了第 1 个子表，**作废**。`Art_UpdateGlowAlpha @0x441740` 等名字里带 Glow 的仍然是遮挡半透明，与此无关。移植：`scripts/export_scene.py art_action_events` 导出 `itex <动作> <时间> <材质> <贴图键>` 和 `ifx <动作> <时间> <效果> <挂点> <类型>`（跟在 `icon` 行后）；`scene.cpp` 解析到 `Rm8Icon::tex / fx`；`boardfx.cpp iconEventsUpdate` 每帧按动画时间触发（`setIconTexture` 把图标 clump 里所有 `eve_` 贴图的材质换掉，`rm8PrtAttachH(14, 图标, 1)`）。

[A] **旧 scene.txt 兜底**：如果设备上的 scene.txt 没有 `icon` 行（`deploy.py --no-data` 不推场景，或没推新文件），原来会**整个图标反馈静默消失**（既不按下、也没有 0x7D）。现在 `boardfx.cpp deriveIcons` 在绑定时用事件摆件自己（`article\event\eve_*.dff` 的位置 → 最近路点；动作 0/2 = `_01.anm`、1/3 = `_02.anm`；贴图 `_d`；效果 14）重建，并在日志里打 `boardfx: WARNING scene … has no icon lines`；正常情况日志每张图都有 `boardfx: map 01: 47 event icons, 47 of 126 road nodes carry one`。

### 1.5 移植实现

- `scripts/export_scene.py` 在每个事件图标的 `article … once` 行之后追加 `icon <tx> <ty> <动作>:<anm> …`，`scene.cpp` 解析到 `Rm8Scene::icons`（图标 clump、4 个动作的动画路径）。
- `boardfx.cpp`：`rm8BoardFxIconApproach / Depart`，逻辑逐行照上面的反编译（40 / 10 / 位掩码 / 最后一个离开才弹起 / 0x7D）；图标用 `rm8SceneArticleAnim(…, once)` 换动作。图标位置取同一路格的 `rnode` 坐标（导出时图标就在那里）。
- `roles.cpp` 的小插入：`stepAlongPath` 按原版顺序调用（见 1.3 第一行）；`rm8TurnPlaceAt`（传送 / 交换位置）按第二行调用。
- 验证：`RM8_BOARDFX_TRACE=1` 日志——路过：`icon (30,28) action 2 … action 1`；停下：`action 0` + `sound 0x7D`；`RM8_DEBUG_ANIM=eve_cardshop` 打印骨 1 的 y：创建时 1.1 → 5.5，按下 5.5 → 1.1，弹起 1.1 → 5.5。

---

## 2. 地产规则的音效与特效（真人与电脑走同一条代码路径）

| 场合 | 函数 | 表现 | 移植 |
|---|---|---|---|
| 买地（消息 9，也包括土地公 0xA、`BuyLandSimple` 0x50） | `Role_TakeLandOwnership @0x487250` | `Land_SetOwner` 之后播 **样本 1**（所有买地路径都经过这里；拍卖 0x41 `Role_PayAndTakeTerra @0x487c20` 没有声音） | 已做（`rm8BoardFxLandTaken`，game.cpp case 9）。**电脑买地没声音的原因**：移植里买地根本没有声音（不分人机），不是人机区别 |
| 付租（消息 0x20） | `Role_TransferRentToOwner @0x487f20` | 付款人 `Role_SubMoney` 之后播 **样本 0x7B**；没有嫁祸卡时才播（嫁祸卡路径只给地主加存款，不播；`OnLuckyGodEvent @0x490a40` 的取消分支 `b < 0` 也播 0x7B，移植还没有嫁祸卡） | 已做（`rm8BoardFxRentPaid`，game.cpp case 0x20）。表情由呈现模块做 |
| 加盖 / 升级（消息 0xB / 0xC） | `Role_BuildOnLand @0x487380` → `NPC_CreateOrLandEnter @0x4eeee0` → 任务 `NPC_CreateAndDisplay @0x4f4b50`（vtable 0x5e6618） | 角色状态 9；效果摆件 **art 6001（`eff\Eff_10`，三个戴安全帽的工人从右边走进来、敲打、走开，3.83 秒）+ 样本 2**；1.5 秒后旧模型（等级 1..4）播动作 1（住宅）/ 3（商业）一次；旧动画结束后 `Land_OnEnter` 才写入新等级并换模型，新模型播动作 0（住宅）/ 2（商业）一次 + **样本 0x7A**；再播待机动作（住宅 2 / 商业 0，循环）；效果摆件结束后任务完成，角色 9 → 8 | 已做（第 3 节） |
| 加盖到 5 级 | `Role_BuildOnLand @0x487380` 末尾 | 本人的地升到 5 级：表情 0x10，并在队首插入 `0x52(0x177A, 地块)`；`Role_ShowEffectAnimAtTerra @0x48b320`：效果 art 0x177A = 6010（`eff\eff_12`）+ 样本 **0x68**，再等 60 帧 | 呈现模块（`rm8PresentBuild` 已入队 0x52，`present.cpp` 处理） |
| 拆一级 / 拆光 | `NPC_InitDisplay @0x4f4f20`（`BuildLevel_StartDecrement @0x4eef80`） | 效果 art 6002（`eff\Eff_01`，住宅时缩放 0.5）+ **样本 3**；建筑播动作 1（住宅）/ 3；结束时等级 −1 或拆光 | **未做**：只有卡片（拆除、怪兽）会用，在 cards.cpp 里；见第 7 节 |
| 破产 | `Role_CheckBankruptcy @0x48ee10` | 清队列，提示 str91，表情 0x1A，消息 **0x7F**，然后 0x72 / 0x73 | 0x7F 已做（第 4 节） |
| 金钱增减 | `Role_AddMoney @0x489b80` / `Role_SubMoney @0x489ac0` / `Bank_*Money @0x47e6c0…` | **没有音效**，只有 70 % 的表情 | — |

查过但**没有音效**的：`Role_BuyLandConfirmed @0x487030`（买地的扣款与表情）、`Role_PayToll @0x487c80`（过路费提示）、`Role_CheckRentExempt @0x486580`、`Role_TransferRentRoadblock @0x488100`、`CPlayerTag__OnRoleSettlement @0x48f500`、`CPlayerTag__OnRoleBankruptcy @0x48f750`、`Land_SetOwner @0x4eec20`。字符串表里这些提示（str1..19、91、407…）没有 `@样本号` 后缀。

---

## 3. 加盖动画任务（boardfx.cpp `rm8BoardFxBuildStart`）

调用：`game.cpp` 的 case 0xB / 0xC 先扣款、算出新的等级和建筑种类（`rand()` 与原版一样在任务开始前取），再调用
`rm8BoardFxBuildStart(座位, 位置, 是否住宅, 旧等级, 旧模型, apply)`；`apply` 是 `Land_OnEnter`（写等级、换模型，返回新模型）。返回 false（缺效果 art）时 `apply` 立即执行。

[V] 阶段（`Cls_5e6618__vf1_UpdateTimer @0x4f4e40` / `NPC_PlayAnimOrComplete @0x4f4cc0` / `NPC_CompleteInteraction @0x4f4d70` / `Building_UpdateBuildingVisual @0x4f4a40`）：
1. 开始：效果 6001 放在“建筑位置向相机拉 20”（`Building_GetPosTowardCamera @0x4efad0`：位置 − 20 × 视线方向），旋转 = 相机偏航 − 180，动作 0 播一次；样本 2；角色状态 9。
2. 状态 0：计时 1.5 秒 → 旧模型存在且等级 1..4 → 动作 1 / 3 一次，等它播完；否则直接下一步。
3. 完成（状态 2）：`Land_OnEnter`；新模型动作 0 / 2 一次；样本 0x7A（只在新模型存在时）。
4. 新模型动作播完 → 待机动作（住宅 2 / 商业 0，循环）。
5. 效果摆件的动画播完（回调 `Cls_5e6618__vf4_DestroyArticle @0x4f4a00` 销毁它）且第 4 步完成 → 任务结束，角色 9 → 8（`Game_EndTurn @0x4a48d0`）。

[A] 时间用真实 dt（原版任务的 dt 来源没有查）；没有保存样本 2 的句柄去提前停止（原版只在 `vf13_Cleanup @0x4f4ec0` 被打断时停）。

---

## 4. 破产过场（消息 0x7F）

`CPlayerTag__OnMovieDrama @0x492f30` [V]：
- 镜头聚焦角色；`Anim_CreateSprite(0x1776 = 6006, 角色位置, 动作 0, 次数 1, 回调 Role_FindWithCondition, 样本 −1 = 无)`：art 6006 = `eff\Glost`（一幅“破産”字样的精灵从上方落下，动画 13.33 秒）；位置 y + 30、向相机拉 20，旋转 = 相机偏航 − 180；
- `CDigitalAudio_StopMusic(0)`（棋盘音乐暂停并记位置）+ `CDigitalAudio_PlayBGMFaded(0x68 = 104)`：播一遍曲目 104；
- 角色状态 0xE，等待计时 = `14 × (1 − min((speed − 1) / 10, 1))` 秒（`Anim_ApplyDifficultyScale @0x47b3b0`，speed = init.txt 的 `speed`；`Role_TickWaitTimer @0x4850b0`）；
- 计时到 → 状态 8；状态 0xE 的更新里（反编译第 64761 行附近）若场景标志 = 9：`CDigitalAudio_StopBGMWithFade(0)` + `ManageBGMTransition(−5)` 恢复棋盘音乐。

移植 `rm8BoardFxBankruptcy`：聚焦、放 6006、`rm8MusicPlay(104, false)`、角色状态 9（移植没有状态 0xE，未知状态会被主循环复位成 8）、计时到后移除精灵、`rm8MusicPlayBoard(mapId)`（[A] 重新开始播放列表，不接着原来的位置；`Rm8Scene::mapId` 是新增字段）、状态 8。`game.cpp` 的 `rm8GameCheckBankruptcy` 在 0x1A 表情之后入队 0x7F。

后面的 0x72（清算）/ 0x73（宣布破产）移植原来就有。

---

## 5. 点击场景里的摆件（互动场景）

### 5.1 原版 [V]

流程（只在当前角色状态 6 = 等人类掷骰时）：
1. `CGamePlayScene__OnKeyInput @0x4a28b0`：按键标志 `0x10`：带 `0x100`（按下）→ `Game_HandleCardClickInGame @0x4a2940` → `Game_OnTileClickWithCard @0x4a4a10`；不带（松开）→ `Game_HidePlayerInfoPanel @0x4a2ad0`（隐藏信息面板，并调 `CArticleMgr vf29 @0x43e9c0`）。（0x100 = 按下 [H]）
2. `Game_OnTileClickWithCard`：`Camera_TestPixelRay @0x41a8c0` 取鼠标像素射线；依次找最近的角色、NPC、地块、股票、**摆件**（`Game_FindBuildingByType(射线, 0x80) → CArticleMgr vf28 PickByRay @0x43e0e0`），取距离最近的一种；摆件时：连锁店（art 0x45b）有特殊处理，否则 `vf32`（介绍文本，`introduce.txt`）→ `UI_ShowIntroduction`，没有则 `vf31` 的名字 → `UI_ShowPlayerInfo`。
3. `vf28 PickByRay`：对 `+0xc4 != 0` 且类型 4（DFF 摆件）的摆件（地图摆件创建时 `+0xc4 = 1`，`@0x43cb90`），先用包围球（世界球并集，**半径 ×1.5**，`@0x43cb90`；`vf16` 创建的是 ×1.2）和射线线段求交，按“射线起点到球心的距离”排序（最多 0x32 = 50 个），依次用 `Geom_RayIntersect`（几何三角形）精确求交，**第一个命中的**就是选中项（存到 `+0x60`）。
4. 松开时 `vf29 RandomizeVariant @0x43e9c0`：条件 `InitTxt_GetParam(5) > 4`（init.txt 的 `character`，本机 6；默认 3 时地图摆件根本不动画，见 `@0x43cb90`）、选中项存在、art 不是 0x45b、摆件动作数 > 1 且当前动作（`CArticle_GetTypeAtIndex(0)`）== 0：播动作 **`1 + rand() % (动作数 − 1)`**，次数 1，回调 `thunk_FUN_00445860` = `Article_SelectByTypeAndSetProperties(0, 0, 0)`（播完回到动作 0，循环）。选中项清为 −1。
5. 声音：`role.bin` 第 5 个子表（0x18 字节，全文件 32 条）是**动画声音事件**：`{art, 动作号, 0, 样本号 6401..6430, 标志 1, 时间(秒)}`。`CArticleMgr_CreateArticleFromRecord @0x43cb90` 在动作数 > 1 时给摆件装回调 `+0x2c8 = Vehicle_UpdatePos @0x43a280`（名字不准，它是“摆件动画声音回调”）；`Article_Update @0x448540` 每帧检查，动作时间走过事件时间时调用回调，回调播放 `CAudioMgr_PlaySample(样本号, loops 1)`（标志非 0 时无条件播放，为 0 时已在播就不重播）。事件只挂在动作 1、2 上（待机动作 0 没有声音）。
6. 冷却：点击动作播放期间（当前动作 ≠ 0）再点没有反应；播完回到动作 0 才能再触发。

有声音事件的摆件（art）：801–804、806、807、809、810、815、816、851、878、883、890、896、1301、1302、1312（`Mys*` 系列地标），各 1–3 个事件。其余有多个动作的摆件（`role.bin` 里 541 个）点击只播动画，没有声音。

### 5.2 移植

- 导出：每个地图摆件之后，动作数 > 1 的追加 `aact <动作>:<anm> …`（动作 ≥ 1），有声音事件的追加 `asnd <动作> <样本> <标志> <时间>`；`scene.cpp` 解析到 `Rm8Scene::artActs`（动作 0 取摆件行里的待机动画）。各图 `aact` 行数：01 17、02 14、03 18、04 19、05 4、06 20、07 10、08 23、09 5、10 6、11 5、download201 10、download203 2、download204 9（202 为 0）。
- `bool rm8BoardFxTap(rw::Camera *cam, float sx, float sy)`：像素坐标是相机帧缓冲的像素（与 `rm8CardsOnTap` 相同）；返回 true = 有摆件反应。流程同 5.1 第 3、4、6 条：包围球 ×1.5 → 按球心距离排序 → 逐个做三角形求交 → 命中后随机动作 + 声音事件由 `rm8BoardFxUpdate` 每帧检查（`rm8UpdateScene` 末尾调用），动画播完自动回到待机。有人物上场时要求当前角色是等待掷骰的人类（状态 6），没有人物（纯地图查看）时不限制。
- **接入由监督者做**（rm8_app.cpp 的点击处理）：建议放在 `rm8CardsOnTap` 之后、“点击掷骰 / 双击换图”之前：`if(rm8BoardFxTap(cam, x, y)) return;`（原版按下 / 松开两步，移植合并成一次点击）。
- [A] 没做：按下时的信息面板（`UI_ShowIntroduction` / `UI_ShowPlayerInfo`，`introduce.txt` 文本）；与角色 / 地块 / 股票点击的“取最近”仲裁（移植里各自独立）；初始化时 `character ≤ 4` 不动画（移植总是动画）。
- 验证：`RM8_BOARDFX_TAP=2`（调试：2 秒后每 2.5 秒点一次下一个多动作摆件的球心，无视状态 6 的限制）+ `RM8_BOARDFX_TRACE=1`，日志例：`tap -> article action 1 (…mys15_1.anm)`、`sound 0x1901`（6401 = 0x1901 等）；有声音事件的摆件在事件时间点出声（Mys01 动作 1 的 6401 在 0.073 秒、Mys03 动作 2 的 6407 在 0.233 秒）。

### 5.3 另一个“摆件”机制：遮挡半透明（未做）

`RoleMgr_UpdateAll @0x495bf0` 每帧：`CArticleMgr vf26 ResetGlowEffect`（清所有摆件的 `+0xc8`）→ 对每个状态 < 3 的角色 `vf27 TestPixelRay`（角色位置 + 30 的 5 个点投到屏幕，从相机向这些点作线段，与摆件几何相交的摆件置 `+0xc8 = 1`）；`Art_UpdateGlowAlpha @0x441740` 把 `+0xcc` 向 0.4（遮挡中）或 1.0 渐变；`vf5_SetRenderMode @0x43b210` 模式 3 把 `+0xcc < 1` 的摆件按该透明度画。也就是**挡在角色前面的建筑 / 树变成 40 % 半透明**（仅 `character > 4`）。移植里没有这个半透明遍历，见第 7 节。

---

## 6. 数据与接口一览

`scene.txt` 新增行（只增不改，15 张图重新导出后用 diff 确认旧行一行没变）：
- `icon <tx> <ty> <动作>:<anm> …`：跟在事件图标的 `article` 行后；
- `aact <动作>:<anm> …` / `asnd <动作> <样本> <标志> <时间>`：跟在地图摆件的 `article` 行后；
- `bart 6001 / 6002 / 6006 …`：效果摆件的模型表（建筑特效、破产幽灵；与已有的 `bart` 行同格式）。

`boardfx.h`：`rm8BoardFxUpdate / Clear`（scene.cpp 每帧 / 销毁时调用）、`rm8BoardFxIconApproach / Depart`（roles.cpp）、`rm8BoardFxTap`、`rm8BoardFxLandTaken / RentPaid`（game.cpp）、`rm8BoardFxBuildStart / Cancel`、`rm8BoardFxBankruptcy`、`rm8BoardFxSetCamera`。

调试环境变量（默认关）：`RM8_BOARDFX_TRACE=1`（日志）、`RM8_BOARDFX_TAP=<秒>`（自动点击）、`RM8_BOARDFX_TESTFX=<art>:<秒>`（在离屏幕中心最近的角色身上放一个效果摆件，例如 `6001:1`）。离屏截图（`--shot`）用固定相机，当前角色可能不在画面里。

---

## 7. 未做 / 待确认

> 2026-11-15：第 1 条（拆房、天使、地王 / 交换闪烁）、第 3 条（遮挡半透明）已做，见 `effects.md`（注意：新闻拆房 / 飞弹 / 怪兽在原版里没有 6002 动画，只有拆屋卡 0xD 有）；事件图标的亮版贴图 / 粒子 14 改为“停在图标上并按到底之后”才出现。

1. 拆房动画（`NPC_InitDisplay @0x4f4f20`：art 6002 + 样本 3 + 建筑动作 1 / 3）和天使卡整街加盖（`Building_AnimateMultiple @0x4f3b20`：每块地 art 6121 + 样本 0x1E；地王 / 交换卡闪烁后样本 1 / 4 `@0x4f5690`、`@0x4f4240`）：都是卡片效果，在 cards.cpp 里；`boardfx.cpp` 已有 `spawnEffect` 和任务框架，卡片模块可以照 `rm8BoardFxBuildStart` 的写法接。
2. 超市 / 饭店 / 研究所的进入任务（`Role_OnArriveCommercialBuilding @0x486260` → `Game_SelectCard(建筑)`，单骰交接、饭店住宿）及其音效：需要事件任务框架（events 模块）。
3. 遮挡半透明（5.3）。
4. 点击摆件时的名称 / 介绍面板；点击与其他点击目标的仲裁。
5. 读档时对所有角色的图标 Approach（`Game_LoadSavedGame @0x496a90`）：移植没有读档。
6. 加盖任务里样本 2 的句柄在被打断时的停止；任务 dt 的时间缩放。
7. `Role_BuildOnLand` 的 `local_8 < 6` 判断（等级超过 5 时不建）：移植在 `arriveLand` 里已拦住（5 级不再问）。
8. 破产过场结束后棋盘音乐是重新开始播放列表，不是从暂停处继续。

## 8. 验证记录

- 离线（`build-boardfx\rm8_app.exe … --map 01 --time N --shot`，`RM8_BOARDFX_TRACE=1`，电脑自动玩）：图标按下 / 弹起 / 0x7D 的日志与反编译的距离规则一致；买地出现 `sound 0x1`、付租 `sound 0x7B`、加盖 `sound 0x2 → 0x7A`；`RM8_START_CASH=1,0` 触发破产过场（截图里“破産”字样的幽灵精灵）；`RM8_BOARDFX_TESTFX=6001:0.1` 截图里三个戴安全帽的工人走进来。
- 窗口模式（`SDL_AUDIODRIVER=dummy RM8_AUDIO_TRACE=1`）：`audio: play 1`（买地）、`play 125`（0x7D 停在图标上）、`play 6417 / 6407 / 6401`（点击摆件的声音事件）。
- 15 张地图重新导出：旧行一行没有变化，只多了 `icon` / `aact` / `asnd` / `bart` 行。
- 真机“没反馈”调查（2026-11-15）：窗口模式 / `--shot` + `RM8_AUTO_ROLL=1` / `--human 0/-1` / 菜单起局（`RM8_MENU_AUTO`，普通主循环）日志里按下、弹起、`sound 0x7D`、`audio: play 125` 都正常；`RM8_SHOT_DT=0.033 / 0.1`（模拟慢帧）次数一致；15 张 scene.txt 都含 `icon` 行。已补：发光贴图 + 效果 14（1.4）、旧 scene.txt 兜底、`rm8_app.cpp` 主循环里的 `RM8_AUTO_ROLL`（窗口模式也能自动掷骰）、`RM8_SHOT_DT`
