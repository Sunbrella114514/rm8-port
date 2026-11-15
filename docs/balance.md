# 月结算（Monthly Balance，事件任务 0x52）

代码：`app/balance.cpp / balance.h`（任务 + 离屏 3D + 测试钩子）。标记：[V] 反编译读到并给出地址，[H] 推测，[A] 移植适配。

## 1. 触发与时机 [V]

- `Game_OnTurnStart @0x4a3b50`（每个新的一天，第 6 天起 `+0x14 > 5`）：**每月 1 日** `Game_TriggerNormalEvent @0x4a6620` = `Game_UseCard(0x52, 0)`（`CreateGameObject(0x52)` → `Cls_5e49c8`，vtable 0x5E49C8），当前角色状态 9；15 日是乐透 `Game_TriggerHolidayEvent`（事件 0x62）。任务起来了（返回 1）就**跳过当天的日期新闻**（节日仍然检查，节日消息排在任务后面执行）。
- 利息不在回合开始时入账，而是任务第 2 步（`Balance_ShowDepositInfo @0x4afa40`）逐角色 `Role_AddMoney(利息, 1, 0)`（进存款，无表情）。
- 移植：`rm8GameOnNewRound`（game.cpp）→ `rm8EventDaily(mday)`（events.cpp）：`mday == 1` 起任务（当前角色 = 第一个未破产的座位）；没有任务（数据 / UI 缺失）时 game.cpp 退回到旧的静默入账（同样的利息公式）。**利息公式不变**：`存款 / 10`，金钱奇迹在时奇迹主人 `/ 5`、其它人 `/ 20`（`Wonder_CheckConstruction(0)`；`rm8EventInterestDivisor`），整数除。

## 2. 场景（加载 vf16 `@0x4af2e0`）[V]

隐藏主对话框（gmaindlg vf+0x1C(−1)），保存相机，`CDigitalAudio_StopMusic(0)` 暂停棋盘乐，`PlayBGMFaded(0x66 = 102)`（循环，月结算音乐），`+0x350 = 9`；3D 部分（`Cls_5e49c8__vf2 @0x4b0b00` 只在 `param == 1` = **UI 之后**画，先清 Z）：
- 主播：文章 0x41A = 1050（`article\NPC\N24B`，7 个动作），位置 `(120, −100, 600)`、缩放 1.5、绕 Y 转 180°，动作 0 循环；
- 每个未破产角色一个"柱子 + 小人"（`Cls_5e4384`，≤ 4，座位顺序）：`Sprite3D_InitResourceAndTransform @0x4aecc0`。**小人**（`this+8`，文章 = roleinfo `+0x308`，服装 4 加 0xC0）：转 180°、`Mesh_SetTranslation(pos)`、**不缩放（1.0，身高约 60）**、**动作 2（站）**，贴图 `A%02d_body_f_%02d`（`Asset_LoadCharacterTexture @0x4934a0`）。**柱子**（`this+0x3C`，`Sprite3D_LoadResource(0x529)` = 文章 1321 `element\Mys121`，一根很长的柱子，顶上一个圆盘 + 6 片半透明光片）：位置 `pos + (0, 61, 0)`、缩放 0.5、动作 0。`pos` = `(60 − 60 i, −100, 600)`。位置公式（[V] `Mesh_SetScale @0x47a9e0` / `Mesh_Rotate @0x47a790` 绕框架自身位置，`Mesh_SetTranslation` 相对"记录位置（初值 0）"移动）：框架位置 = 模型自带根位置（柱子是 (0, −197.7, 0)，几何以它为中心）+ 目标，**不随缩放变**——算出来圆盘顶面正好落在小人脚下（−98.7 ≈ −100），柱子往下伸出屏幕：一根"柱状图"。**画的顺序**：每个精灵先柱子后小人（`Cls_5e4384__vf4 @0x4aefb0`），再画主播。**旧版本的错误**：把 `Mesh_SetTexture(0, 0, 0)` 当成小人的最后一个动作（它其实作用在柱子上），所以小人播行走动画；缩放 0.5；没有柱子。
- 相机：`Camera_SaveViewState @0x41b2e0` 把**主相机**的框架置为单位（原点看向 +z），透视投影不变（**不是正交**，事件小人才用 OrthoCam）；视窗用端口主相机的 `0.225 W / H × 0.225`（4:3 时 0.3 × 0.225：每格 60 单位 = 170 像素，等于 balance.ui 列间距 170，布局自洽）。
- `balance.ui`（1024×768，**LoadScaled = 整屏布局 `UI_ScaleWidgetTree`**）：背景 4 块 563..566（≥ 500 宽的块随屏宽拉伸，宽屏时铺满整个屏幕）；标题标签 ID 9（左，82,209，165×129）；表头 ID 10 / 11；每个小人一列：存值标签 ID `2i+1`、第二行 `2i+2`；高亮光柱 ID 20..23（pic 1040，256×512，初始隐藏）。

## 3. 步骤机（`Cls_5e49c8__vf1 @0x4b0870`，`+0x20` = 下一步，`+0x2c` = 计时器）[V]

计时器每步 × `Anim_ApplyDifficultyScale`；`Balance_UpdateTimer @0x4b0a70`：计时器 > 0，**或标题语音（+8）还在播，或样本（+0x48）还在播**都继续等。点击 / 跳过 = vf13 `@0x4b0d90`：停语音和样本，计时器 0（下一步立刻执行；docs/skip.md）。

| 步 | 动作 | 标题 string（`@` = 语音） | 主播动作 | 时间 |
|---|---|---|---|---|
| 开场 | `Balance_AddSimpleRow(4701, 0, 1)`：清表（小人站在低柱子上） | 4701 各位客戶辛苦了，又到了每月銀行結算的日子～ | 0（一遍） | 3 s |
| 1 | AddSimpleRow(4702, 1, 2) | 4702 大富翁銀行將根據您的存款加發10%的儲金利息～ | 1 | 3 |
| 2 | **ShowDepositInfo**：表头 5501「存款：」/ 5502「利息：」；逐小人：存款（入账前）、利息 = 存款 / 除数，**立即入账**，标签 `%9d`；小人升高到 `存款 × 60 / 最大存款`（300 单位/秒） | — | — | 3 |
| 3 | `Balance_InitDisplay(4703, 2, 4, −1)`：清表，没在晃的小人开始上下晃（±3..5，25 单位/秒） | 4703 下面為您播報本月焦點人物！ | 2 | 3 |
| 4 | InitDisplay(4704, 3, 5, **样本 0x83**) | 4704 〔string.bin#4704〕…… | 3 | 3 |
| 5 | **ShowLossInfo**：表头 5503「損失金錢：」/ 5504「倒楣天數：」；每人 `+0x33C`（付过的租金累计）/ `+0x340`（倒楣天数）；分数 = `天数 × 2500 × 物价指数 + 损失`（≥ 0），小人升到 `分数 × 60 / 最大`；第一个最大者 = 悲情人物 | — | — | 2 |
| 6 | `ShowPlayerBalance(悲情, 0, 7)`：该小人动作 9（哭）、其余动作 4；标题 = `4704 + 角色 Id`（名字 + 名字语音）；样本 0x82；只显示该列的光柱 | 4705..4720 | — | 3 |
| 7 | 全部小人动作 2、光柱全隐；AddSimpleRow(4721, 4, 8) | 4721 別灰心，〔string.bin#4721〕！ | 4 | 3 |
| 8 | InitDisplay(4722, 5, 9, 样本 0x83) | 4722 本月冠軍是…… | 5 | 3 |
| 9 | **ShowTotalAssets**：表头 5505「總資產：」；`Role_CalculateNetCash(1)` = 现金 + 存款 − 贷款 + 股票市值 + 名下地块 `Terra_CalcPrice(1)`（含建筑）；第一个最大者 = 冠军；升高同上 | — | — | 2 |
| 10 | ShowPlayerBalance(冠军, 1, 11)：该小人动作 5（大笑）、其余 8；标题 `4722 + Id`；样本 0x82；光柱 | 4723..4738 | — | 3 |
| 11 | 重置显示；AddSimpleRow(4739, 5, 12) | 4739 其他人還要更努力哦！ | 5 | 3 |
| 12 | `Balance_OnClose @0x4aff10` → `Balance_Cleanup @0x4af650`：删小人 / 主播，隐藏 balance，**重新显示主对话框**，`Camera_RestoreViewState`，`StopBGMWithFade(0)` + `ManageBGMTransition(−5)` **恢复棋盘乐**；返回 0x80000000（`Game_EndTurn`：状态 9 → 8） | — | — | — |

`Balance_SetTitle @0x4b0cc0`：标题文字写进 ID 9 的标签并 `Anim_PlaySound(string 的 @ 号)`（新的语音替换旧的）。`Balance_InitDisplay` 的样本（0x83 / 0x82）由 `CAudioMgr_PlaySample(.., 1)` 播，替换旧样本。没有排名榜 / 明细表：全部是这个 4 列 × 2 行的表加两次"点名"。金钱奇迹的说明 string 4740 没有被这条代码引用。

## 4. 数据来源与计数器

- `+0x33C` = 付过的租金累计（`Role_TransferRentToOwner @0x487f20`、`Role_TransferRentRoadblock @0x488100`；反编译里还有一处 `+0x33C += 金额`）——移植 `GRole.rentOut`（已有，存档已存）。
- `+0x340` = 倒楣天数：`Role_RunTurnStartStep @0x4842f0` 第 9 步，状态 1（睡眠）/ 3（旅馆）/ 4（医院）/ 5（监狱），以及状态 0 且附身 6（糊塗神）且停留标志为 0（梦游）时 `+0x340++`；状态 6（消失）不计。**从不清零**（只见构造函数写 0）[H]，所以"本月"其实是整局累计。移植：`GRole.badDays`，`rm8GameNoteBadDay`（roles.cpp 第 9 步），`rm8GameBadDays`。
- `+0x338`（收到的租金）月结算不用。

## 5. 移植实现

- **渲染**：3D 部分画进一个**屏幕大小**的相机光栅（透明清屏、alpha 测试 ≥ 0xC4，屏幕尺寸变了就重建），作为纹理铺在 `balance.ui` 里一个运行时加的整屏标签上（Index 9，在高亮光柱之上，点击穿透；ui.cpp 新增 `rm8UiSetTextureExactN`：纹理 1:1 填满控件，不走事件小人用的方形留边）。原版也是在 UI 之后画 3D（`vf2` 的参数 1），所以小人盖在背景、标签和光柱上面。每帧在 `rm8EventPreRender`（event_npc.cpp 的钩子）里渲染。**列对齐**：宽屏时标签的 x 按 W/1024、宽按 H/768 缩放，所以小人的 x 取各自标签列的中心、主播取标题标签的中心（4:3 时恰好是 ±60 / 120 单位），光柱控件也移到各自列中心。
- **UI 模式**：`RM8UI_HUD`（整屏，同原版 LoadScaled）；任务开始 `rm8UiPushHidden()`（隐藏主对话框），结束 `rm8UiPopHidden()`；hud.cpp 的数字精灵在 `rm8BalanceActive()` 时不画。
- **跳过 / 触摸**：`skip()` = vf13（任何点击 / 触摸，包括落在 UI 上的——rm8SkipTap 先调跳过），每点一下推进一步；电脑回合照常继续（任务结束后 events 把状态 9 → 8，队列继续）。
- **音乐**：`rm8MusicPlay(102)`；结束 `rm8MusicStop()` + `rm8MusicResumeBoard()`。
- **改动的共享文件**（小）：events.cpp（`createTask` 的 `case 0x52`、`rm8EventDaily` 先判 1 日）、event_npc.cpp（`rm8EventPreRender` 多调一个函数）、game.cpp（静默利息退为兜底、`g_days > 5 || 强制`、新增 `rm8GameRentPaid / BadDays / NoteBadDay`）、turn.h、roles.cpp（第 9 步计数）、hud.cpp、rm8_app.cpp（载入数据）、CMake 两处。
- **测试钩子**：`RM8_BALANCE_EVERY=1`（每个新的一天都当作 1 日，不受第 6 天限制）、`RM8_DEBUG_BALANCE=1`（日志：每步、存款 / 利息、悲情 / 冠军）；配合 `RM8_SKIP_TEST=auto:0.4` 测跳过。
- **验证**：`--shot` 截图（背景 + 表头 + 数字 + 小人升高 + 光柱 + 主播）；日志：`存款 50000 利息 5000`（四人）、`存款 55000 利息 5500` 等；第二天 HUD 现金 50000 / 存款 55000 恢复；默认日历：第 17 天（12-01）、第 48 天（01-01，与元旦同日，节日消息在任务后执行）；`RM8_BALANCE_EVERY=1` 3 张图 4000 秒（64 天 / 63 次）无卡死；点击跳过：150 秒内完成 6 次对 2 次；Android 编译链接通过。

## 6. 存档（给存档 agent）

- 任务本身是瞬态（对话框 / 事件任务不存）；利息在第 2 步入账，所以存档应在静止点。
- 需要存的**角色状态**：`GRole.rentOut / rentIn`（`+0x33C / +0x338`，save_game.inc 已存 `rent[0..1]`）和**新的 `GRole.badDays` = `+0x340`**（原档的 `blk338[2]`，save_port.md 第 6 节写"移植没有"：现在有了，`Rm8SaveRole` 加一个字段，`rm8SaveGameRoleGet / Set` 读写 `rm8GameBadDays` / `rm8GameNoteBadDay` 对应的 `g_r[seat].badDays`）。`blk338[3..7]` 月结算不用。

## 7. 未做 / [A]

- 3D 层是屏幕大小的光栅，画质同屏幕分辨率。
- 服装 4 的 `+0xC0` 文章偏移没做（小人固定服装 0）；`+0x1AC`（角色服装）在移植里没有。
- 倒楣天数从不清零是 [H]（没找到清零写入）。
- `event_npc.cpp:160/163` 的 `"article\role_tex\A%02d..."` 用了单反斜杠（`\r` 是回车符），法院 / 魔法屋的小人身体贴图键可能因此没找到——balance.cpp 用 `\\` 写对了，事件 agent 请核对。
截图（`port/results/`）：`balance_open.png`（开场，小人站在柱子上）、`balance_deposit.png`（存款 / 利息，柱子升高）、`balance_loss.png`、`balance_tragic.png`（悲情人物哭，其它人动作 4，光柱）、`balance_champion.png`（冠军笑，其它人动作 8）、`balance_last.png`。逐步截图用 `RM8_BALANCE_HOLD=n`（停在第 n 步的画面，0 = 开场）+ `RM8_START_CASH=1,300000`（让数值有差别）。

**主播的表情**（BUGS #26）：主播（文章 1050 = N24B）的脸来自 role.bin 子表 3（`face` 记录，34 条：part `face`，按动作号 + 时间换 `N24B_head_01a01 / 01a02 / 01b01 / 01b02`，即眨眼和张嘴说话），和角色脸同一机制（`Mesh_SetTexture` 播动作时 `CArticle` 按动画时间选脸贴图）。balance.cpp 之前只读 `event_arts.txt` 的 `bart` 行，漏了同文件里的 `face` 行，现在读入；`RM8_DEBUG_BALANCE=1` 打印每次换脸。截图 `port/results/balance_anchor_faces.png`（开场 / 第 3、4、7、11 步，主播嘴型不同）。
