# 游戏内信息界面（底栏按钮、小地图、玩家资料、胜利条件、设置、点击信息框）

> 实现：`app/infoscreens.cpp` / `app/infoscreens.h`。依据：`out/final_v2/RichMan8_named.c`（`scripts/strfn.py`、`scripts/exe_dis.py` 读函数和汇编；字符串编号 = `StringTable_Get()` 偏移 / 4）、`docs/ui_system.md` 5.3。
> 置信度：**[V]** 已读代码或汇编核对；**[H]** 证据强、含义是推断；**[A]** 移植的适配。地址写作 `@0x…`。
> 原版全部在**状态 6**（等待人类掷骰）打开；打开后角色进入状态 9，关闭时回到 8，再由队列里的 100 回到掷骰。

## 1. 底栏按钮 → 动作 [V]

`gmaindlg.ui` 的按钮 6..10 只在当前角色状态 6 时显示（`Game_ToggleCharacterVisibility @0x4A6F90`，`hud.cpp` 早已实现）。`CUi_gmaindlg__vf10_Init @0x4E9550` 给每个按钮挂三个回调：松开（1）= `UI_OnDialogEvent @0x4E7700` → `g_GameState` vf12(id, 1)；鼠标移入（5）= `CText_SetContentFromStringTable @0x4E8EE0`；移出（6）= `Text_InitAndClear @0x4E8F20`。

场景 vf12 = `CGamePlayScene__vf12 @0x4A2E10`：有事件任务则转给任务；否则当前角色（`g_RoleMgr[0xB]`）状态为 6 时按 id 分发到 `Game_DispatchCardAction(任务号, 0)`：

| 按钮 ID | 图（NormalPic） | 任务号 | 界面 | 悬停文字（string 7355 + id） | 移植 |
|---|---|---|---|---|---|
| 6 | V（615） | 0x67 | `wincondition.ui` | 7361「關卡資訊」 | `infoscreens.cpp` |
| 7 | 走势图（617） | 0x61 | `StockBoard.ui` | 7362「股票」 | `stock.cpp`（已有） |
| 8 | 放大镜（619） | 0x66 | `playerinfo.ui` | 7363「個人資料」 | `infoscreens.cpp` |
| 9 | MAP（621） | 0x65 | `minimap.ui` | 7364「小地圖」 | `infoscreens.cpp` |
| 10 | 磁盘（623） | 0x68 | `gsys.ui` 系统菜单 | 7365「選項設定」 | `menu.cpp`（已有）；其中的“设置”按钮 4 = 任务 0x69 `setting.ui`，在 `infoscreens.cpp` |

`Game_DispatchCardAction @0x4A4420`（参数 2 = 0：要求状态 6；非 0：不要求）：`Game_TriggerWonderEvent @0x4A43E0`（只处理 0x6F）失败后 `Game_UseCard @0x4A4160` → `CreateGameObject(任务号) @0x4B1BC0`（任务挂在场景 +0x34，`Cursor_SetVisible(1)`），成功则 `CPlayerTag_SetStatusCode9`、`Role_ClearMsgQueues`、`Role_EnqueueMsg(100)`。任务结束（vf1 返回值最高位 = 1）→ `Game_EndTurn @0x4A48D0`：状态 9 → 8。
五个任务的 vtable（0x5E4500 / 0x5E4548 / 0x5E4590 / 0x5E4470 / 0x5E44B8）都没有跳过功能（vf13 = `Obj_Return0 @0x4F6230`，`docs/skip.md` 表里的 0x65~0x6B“不跳”）。

移植：`rm8InfoOpenFromHud(按钮, 座位)`（hud.cpp 的 onClick 调用）做同样的 3 件事（状态 9、清队列、入队 100）；关闭时 9 → 8（与 `stock.cpp` 的 `closeAll` 同一做法）。只对“人类 + 状态 6”开放（测试钩子 `RM8_INFO_FORCE=1` 放开）。

## 2. 胜利条件 `wincondition.ui`（任务 0x67）

`Cls_5e4500__vf16_ShowWinConditionInfo @0x4ACAB0`（vtable 0x5E4500：vf1 = `Container_GetParam @0x4B3ED0` 返回状态 +4，vf11 = 关闭 @0x4ACFA0，vf16 = 本函数 [V]）。文字由 `\n` 连接（分隔串 0x5E4130..0x5E4140 都是 `0A 00`，已读字节）：

* **自由模式**（开局设定 `&DAT_00632DE8` +0 == 0；反汇编 @0x4ACD5D..0x4ACF4C）：
  1. 时限（setup +0x18）== 2：string 7056「時間無限」；否则 string 7053「剩餘回合數：%d」，数值 = `Game_GetRandomValue @0x4A3E00`（旧名误导，实为 `Date_CalcDaysDiff @0x431680`：从今天到结束日期的天数）。
  2. string 7054「勝利條件：%s」，%s = string 7041 + setup +0x1C（0 地產最多 … 5 其他玩家破產）。
  3. string 7051「當前地圖：%s」，%s = `Container_FindByValue(setup+4) @0x4A8F80` 的名字（关卡名 / 下载地图文件名）。
* **故事模式**（setup +0 != 0；@0x4ACB13..0x4ACD58）：第一个角色的名字、「當前地圖：%s」、string 7052「比賽場次：%d」（setup +8 + 1）、string 7058「已得到的拼圖數目：%d」（`Event_CountOwnedItems`，拼图数）。故事分支只读了代码，没有在游戏里跑 [H]。

界面只有一个标签（ID 1，`Enabled=false`，居中），**没有按钮**：原版靠右键 / Esc（`CApp_HandleInputEvent` → 场景 vf11 → 任务 vf11 = 关闭）。[A] 触屏：点对话框或周围任意位置都关闭（`rm8UiAddWidget` 铺一个全屏透明按钮；标签设成 ignore，否则点在文字上被禁用的标签吃掉）。

移植新增存取：`rm8GameGetOptions()`（`game.cpp`，开局选项）、`rm8GameDaysLeft()`（`Date_CalcDaysDiff` 的月份天数逻辑，无时限 = 100，与原版 `0x4A3E00` 的初值一致）、`rm8MenuMapNameByDir()`（`menu.cpp`，用 `stages.txt` 的名字；不经菜单的 `--map` 测试里没有名字，退回文件夹名）。

## 3. 玩家资料 `playerinfo.ui`（任务 0x66）

vtable 0x5E4548：vf1 `Container_GetParam`、vf11 = 关闭 @0x4AAB40、vf12 = `Cls_5e4548__vf12_UpdateStats @0x4AAB80`（`UI_UpdatePlayerStats(id / 10 - 1)`）、vf16 = `Cls_5e4548__vf16_InitPlayerUI @0x4AA9E0` [V]。`CUi_playerinfo__vf10 @0x4AAC00`：按钮 1 → `u_Game_OnEvent_CallStateVf2c`（场景 vf11 = 关闭）；ID 10 / 20 / 30 / 40（4 个头像按钮）→ `UI_OnDialogEvent`（选列）；滑条 3 / 2 的回调 0x14（值变化）→ `UI_PlayerInfoOnRightClick @0x4AA980` / `OnLeftClick @0x4AA9B0`（换页）。

* **列**：`vf16` 按座位顺序把 `+0x1B4 != 7`（未破产）的角色依次放进第 1..4 列（`+8..` 存座位，`+0x18` 存个数），不足的列清空（头像按钮禁用、没有图，7 个标签文字空），然后选中第 1 列（`UI_UpdatePlayerStats(0)`）——不是当前行动的角色。
* **一列的内容**（`UI_FillPlayerInfoDisplay @0x4AACE0`，列 n 的基准 ID = n × 10）：n×10 头像按钮（图 `npc\<picName>_12`，鼠标移入图 `_13` [H]，移植只用 `_12`）；n×10+1..7 七个居中标签：

| ID | 内容 | 来源 |
|---|---|---|
| +1 | 总资产 | `Role_CalculateNetCash(1) @0x490140` = 存款 + 现金 − 贷款(+0x298) + 股票市值 + 名下地产 `Terra_CalcPrice(1)` 之和 |
| +2 | 现金 | +0x290 |
| +3 | 存款 | +0x294 |
| +4 | 股票 | `Stock_CalculateTotalValue(-1) @0x4994C0` |
| +5 | 点券 | +0x324（`Role_AddTicket @0x489ED0` 的字段） |
| +6 | 房地产 | `Role_SumOwnTerrasPrice(1) @0x48C5A0` |
| +7 | 贷款 | +0x298 |

  左侧七张图标（图 1067..1072、1097）按 y 对齐这七行：總資產 / 現金 / 存款 / 貸款 / 股票 / 點券 / 房地產（按图序：贷款在存款之后、股票之前；由 .ui 的 Y 坐标对出，渲染图核对过）。
* **选中列**（`UI_RefreshPlayerStockDisplay @0x4AB270`）：高亮底板 ID n+3（ID 4..7，.ui 里默认隐藏）只显示选中那列；卡片 ID 70..77（`UI_DisplayPlayerCards @0x4AB300`）按槽顺序前移显示图 `card\<名>`，空槽清图；股票列表（标签 50..57，`UI_DisplayStocks_Page1 @0x4AB480`，格式 `"%s %5d"` 股名、股数，只列股数 > 0）；地产列表（标签 60..67，`UI_DisplayStocks_Page2 @0x4AB6F0`）：种类 0..4 × 等级 0..5 依次，跳过（种类 1 且等级 ≥ 1）、（种类 ≥ 2 且等级 0），数量 > 0 的才列：种类 0 = 住宅（等级 0 = string 25「住宅建地」格式 `"%s   %4d"`，≥ 1 = string 26「%d級住宅」`"%s    %4d"`）、1 = string 14「商業建地」、2 = 16 研究所 `"%s  %4d"`、3 = 15 连锁超市 `"%s%4d"`、4 = 17 饭店 `"%s    %4d"`；数量 = `Role_CountOwnTerras(种类, 等级, 等级) @0x48C430`（种类 0 = 低字 1 的住宅地，1 = 低字 > 1 的商业地，2/3/4 = 低字 2 且高字 2/3/4 的商业建筑）。
* **滑条**（ID 3 = 股票列表，ID 2 = 地产列表，竖条，旋钮图 588，`MidSize=64`）：`Page1` 在 `param_3 != 0`（换列）时：行数 < 9 → 滑条禁用且值 0，否则启用，范围 0..行数−8，值 = 偏移。**原版的一个 bug**：`Page2` 数种类时在凑满 8 行就 `break`，`local_118` 最多 8，所以地产滑条永远 < 9 → 永远禁用，第 9 种以后的种类看不到。[A] 移植数全部种类，滑条按实际行数工作。
* 头像按钮点击 = 选列；ID 1（右上角叉）= 关闭。

移植（`infoscreens.cpp`）：滑条没有控件实现，用 `rm8UiAddWidget` 铺一个旋钮图（`ignore`），拖动靠 `rm8InfoTick` 每帧轮询“该滑条被按住（`rm8UiPressed`）+ 指针位置（`rm8UiPointerPos`）”，抬起时的点击也按指针位置设值；[A] 两条滑条上下端各放一个透明按钮（覆盖 .ui 里的箭头图 589 / 590），点一下偏移 ±1（触屏方便）。手指拖动要让 UI 收到移动：`rm8_app.cpp` 的 `SDL_FINGERMOTION` 对“手指按在 UI 上”调 `rm8UiPointerMove`。全屏的 1024×768 对话框打开时 `hud.cpp` 不画 HUD 的数字精灵（它们在 UI 之后画，会盖在上面）。

新增存取：`rm8StockHoldName / rm8StockHoldQty`（`stock.cpp`）、`rm8CardTex`（`cards.cpp`，卡图纹理键）；贷款用 `rm8EventLoan`，其余用 `turn.h` 现有接口。

状态、占有物（附身）、位置等**原版没有**在这个界面里显示（7 行数字 + 3 个列表 + 卡片），所以移植也没有。

## 4. 小地图 `minimap.ui`（任务 0x65）

vtable 0x5E4590：vf1 = `Cls_5e4590__vf1_GetState @0x429E90`（返回 +0x7C，0 运行 / 0x80000000 结束）、vf7 = `UpdateMinimap @0x429F60`（每次鼠标移动：+0x88 == 0 → 悬停，否则点击拖动）、vf9 = `HandleInput @0x429F10`（左键按下 → `Minimap_HandleClick`，成功置 +0x88 = 1；松开置 0）、vf11 = 关闭 @0x429F90、vf16 = `Cls_5e4590__vf16_SetupScreenshot @0x4298C0` [V]；`CUi_minimap__vf10 @0x42A390`：按钮 2 → 场景 vf11（关闭），控件 1 的回调 0x13（绘制）= `GameEvent_CheckFlag @0x4298A0` → `Im2D_DrawTexturedQuad @0x429EB0`（把 512×512 纹理画成两个三角形的四边形）。

* **截图**（vf16，反汇编 @0x4298C0..0x429C85 逐条读过）：`Camera_CreateWithProjection(512, 512, 半宽, 半宽)`（@0x41A6D0，`RwCameraSetProjection(2)` = 平行投影，近 1、远 2000（0x3F800000 / 0x44FA0000））；视窗半宽 = max(`(l0 − l3) + 200`, `(l2 − l5) + 200`) 取整后 / 2，其中 `l` = `g_TerraMgr + 0x48` 的包围盒（`RwBBox`：`l0..2` 最大、`l3..5` 最小）；盒子由 `TerraMgr_CreateAll @0x4F07D0` 对每个路格（`Land_CreateObject @0x4F1320`，`RwBBoxAddPoint(+0x14 位置)`）求并，再把 x、z 方向各向外扩 50；相机在盒心（x = (l3 + l0) / 2，z = (l2 + l5) / 2）上方 y = 1000（0x447A0000），框架先绕 Y 转 180°，再绕自己的 right 轴转 90°，即 right = (−1,0,0)、up = (0,0,−1)、at = (0,−1,0)（librw 相机的屏幕 x = 0.5 − dot(p, right) / 2vw：屏幕右 = 世界 +x，屏幕上 = 世界 −z）；清屏色 (0,0,0,0)；渲染 = `Game_OnShutdown @0x4A1C00`（名字不对，实为主场景渲染：地图 `Map_OnShutdown`、摆件、`Game_UpdateAllRoles` 角色、骰子），**只渲染一次**（任务创建时），光栅转成图像再建成纹理放进控件 1。
* **悬停**（`Minimap_HandleHover @0x42A080`）：鼠标在 0..512 内时取像素射线，依次：`Game_RoleUnderpoint @0x4982D0`（状态 < 3 的角色，半径 30 的球，`Rw_LineSphereIntersect`）→ 角色名；`Terra_FindBuildingAt @0x42A1A0`（路点，半径 25，`StPoint_TestLineSphereIntersect @0x4F01F0`）→ 事件名（event.bin 名字表），没有则街道名；`Terra_FindCharacterAt @0x42A2E0`（地块，`TerraMgr_FindClosestTerra`）→ 街道名。文字走 `CText_SetContent` = 鼠标提示（0.5 s 延迟）。
* **点击 / 拖动**（`Minimap_HandleClick @0x429FB0`）：鼠标在 0..512 内，射线打中地面（`QuadTree_RayCastTile_Wrapper`）→ `Camera_SetFocusPoint`（主相机移到那一点）；按住拖动时每次鼠标移动都重复。

移植：`rm8InfoPreRender(main)`（`rm8_app.cpp` 两处渲染循环里 `rm8EventPreRender` 之后）在打开后的第一帧用 `Camera::PARALLEL` 的离屏相机（512×512 `CAMERATEXTURE`）渲染 `rm8RenderScene + rm8RolesRender`，纹理用 `rm8UiSetTextureN` 放进控件 `Plane6`（ID 1）；盒子 = 路格（`Rm8Scene::rnodes`）位置的包围盒 ± 50，公式同上。相机每次只在这次渲染时加进世界（[A] 不留在世界里，以免世界销毁后悬空）。悬停 / 点击 / 拖动靠 `rm8InfoTick` 轮询指针（鼠标悬停、触屏长按都出提示；点按 / 按住拖动 = 相机跳到该点，`rm8TurnFocus`，移植瞬间到位，原版是平滑移动）。[A] 事件名表内嵌在代码里（event.bin 的 18 个名字；事件 13..16 在 event.bin 里没有名字，所以退回街道名，和原版 `Event_FindIndex < 0` 的回退一致）；地面高度不读（相机只用 x、z）；射线没打中地面时原版不动，移植只要在图内就跳。
截图核对：用 `RM8_INFO_MARK=1` 在每个角色的位置画小方块，四个方块都落在图里的路面上，说明图 ↔ 世界的映射方向正确。

## 5. 设置 `setting.ui`（任务 0x69）

vtable 0x5E4470：vf1 = `Cls_5e4470__vf1_UpdateTextByType @0x4AC8F0`（悬停文字）、vf11 = `Cls_5e4470__vf11_Reload @0x4B3EB0`、vf12 = `ReloadIfCmd5 @0x4ACA30`、vf16 = `Cls_5e4470__vf16_InitSettingUI @0x4AC840` [V]。

* 滑条 ID 1 音乐、2 音效（`Max=5`，0..5）、3 游戏速度（`Min=1, Max=5`）；初值：`InitTxt_GetParam(9)`、`(8)`、`(6) − 2`（init.txt 的 `music` / `sound` / `speed`）。`CUi_setting__vf10 @0x4AC7C0`：按钮 5 → `UI_OnDialogEvent`，滑条回调 0x14 = `Config_ApplySetting @0x50A710`：1 → `Music_SetVolumeLevel`、2 → `Sound_SetVolumeLevel`、3 → `InitConfig_SetSpeed(值 + 2)`，每次变动立即生效（和选项菜单同一函数，`docs/menus.md`）。
* 按钮 5（右上叉）→ 场景 vf12(5) → `ReloadIfCmd5` → `CUi_setting__vf12_reload @0x4AC980`：`InitConfig_SaveInitTxt`（保存 init.txt）、关闭界面。
* 悬停滑条 n：string 7300 + n（7301「音樂」、7302「音效」、7303「遊戲速度」）。
* 打开：`gsys` 按钮 4 → `NPC_SendMiniGameSignal @0x4AA890`：关闭 gsys（`NPC_ClearGameOptions @0x4AA6D0`）+ `Game_DispatchCardAction(0x69, 1)`（参数 1：不要求状态 6）。

移植：直接用 `menu.cpp` 选项菜单的值和 `applyOptions`（音量 / 速度 / 难度钩子都已存在），新增 3 个入口 `rm8MenuOptionGet / Set / Save`（`menu.cpp` / `menu.h`，`Save` = `saveOptions` 写 `rm8_settings.txt`）；`menu.cpp` 的 gsys 点击处理里按钮 4 → `rm8UiShow(gsys, false)` + `rm8InfoSettingOpen()`。旋钮图 1039（`BtnNormalPic`，`MidSize=41`），拖动 / 点轨道 / 悬停提示同上。关闭时保存。移植的系统菜单本身不占状态 9（原版 gsys 是任务 0x68，占 9），所以设置界面只在状态 6 时才把角色切到 9。

没做：gsys 按钮 1「托管」（`CUi_supply__ShowUI @0x4928E0` → 确认框 `supply.ui` → 消息 0x79 → `Role_EnableAutoPlay @0x48A4D0`：角色 +0x1B0 = 2 变成电脑接管，状态 6 时再 `Role_StayOrBeforeRoll(1)`）——需要 `roles.cpp` 提供“把人类座位改成电脑”的接口，不属于信息界面。

## 6. 点击棋盘的信息框（`ginfo.ui`）

`CGamePlayScene__OnKeyInput @0x4A28B0`：左键按下（0x10 + 0x100）→ `Game_HandleCardClickInGame @0x4A2940`：状态 6 → `Game_OnTileClickWithCard @0x4A4A10`；松开 → `Game_HidePlayerInfoPanel @0x4A2AD0`（状态 6 时 `UI_HidePlayerInfoDialog @0x4A4880` 隐藏 ginfo / intro，并调 `CArticleMgr` vf+0x74）。即：**状态 6 里按住左键显示鼠标下东西的信息，松开隐藏**。`Game_OnTileClickWithCard` 取像素射线，按 `Game_FindBuildingByType @0x4A3F30`（类型 1 角色 `Game_FindClosestRole @0x498170`、0x20 NPC `NPC_FindNearest`、8 地块 `TerraMgr_FindClosestTerra @0x4F23B0`、0x100 奇迹工地 `TerraMgr_FindClosestStock @0x4F2B80`、0x80 摆件 `CArticleMgr` vf+0x70）取最近的一种，都没有则取路点（类型 2）：

| 命中 | 文字（`UI_ShowPlayerInfo @0x4A4800` → ginfo） |
|---|---|
| 角色 | 角色名 |
| NPC | NPC 名（+0x64） |
| 地块 | 街道名 + 等级：空地 string 11「%s^^空地」（无主）/ 12「%s^^住宅建地」/ 14「商業建地」，已盖 13「%s^^%d級住宅」/ 16「%d級研究所」/ 15「%d級連鎖超市」/ 17「%d級飯店」；有地主再接 string 18「\地主：%s」，住宅且租金 > 0 再接 19「\租金：%d元」（地产奇迹 `Wonder_CheckConstruction(1)` 的拥有者租金 ×1.2） |
| 奇迹工地 | 完成度 < 100：奇迹名（string 201 + 序号）+ `\n\n` + string 212「投資完成比例：%d%%」；≥ 100：`UI_ShowIntroduction`（intro.ui） |
| 路点 | 事件名，没有则街道名 |
| 摆件（0x80） | 有介绍 → `UI_ShowIntroduction`（intro.ui，`introduce.bin` 景点介绍）；否则摆件名 → ginfo。映射见 6.1 |

移植（`rm8InfoBoardTap(cam, x, y)`，`skip.cpp` 的 `rm8SkipTap` 在“UI 没吃 / 不是卡片目标 / 没有可跳过的等待”之后调用，紧接着才是 `rm8BoardFxTap` 的摆件动画——和原版“按下显示信息、松开播动画”是同一次点击的两件事）：只在“当前角色是人类且状态 6”时；射线 = `boardfx.cpp` 同一公式；角色用半径 30 的球（头顶 +30）、NPC 半径 30、地块用射线与该地块地面的交点距中心 < 35 [H]、路点半径 25（[V] 路点半径），按沿射线的距离取最近，路点最后兜底。[A] 触屏没有“按住 / 松开”：一次点按显示，下一次点按、6 秒、或回合离开状态 6 时隐藏；右键 / Esc 同样关闭。未做：奇迹建成后的介绍框（`u_BuildObj_GetArticleStatus2`，不属摆件表）。摆件见 6.1。

### 6.1 摆件的名字 / 介绍（BUGS #10）[V]

* `CArticleMgr vf0 @0x43AC90` 加载时读 `Config\introduce.txt`（`INI_LoadIntroduceTexts @0x4436E0`，游戏目录里是加密的 `introduce.bin`）：每个键的值里 `\` = 换行，`#` 后的数字存进记录 +0x100，**= role.bin 的摆件号**（键 1..67 对应摆件 641..707，即各地图的地标建筑 `build\Mxx\mxxsNN.dff`；值形如 `<空格><名称（奇迹类型）>\<介绍>`，前面的空格是给标题居中的）。
* 点击时 `Game_OnTileClickWithCard @0x4A4A10` case 5：`CArticleMgr vf32 @0x43ECC0`（`Registry_FindByInt(摆件号)` 在上述记录里按 +0x100 找）有文字 → `UI_ShowIntroduction @0x4A4840` → `intro.ui`（标签 341×94、16 pt）；否则 `vf31 @0x43EBB0` = role.bin 记录（0x650 字节本体）**+0x128 的字符串 = 摆件名**（遇 `[` 截断）→ `UI_ShowPlayerInfo` → `ginfo.ui`。选中摆件靠 `vf28 PickByRay @0x43E0E0`（球 ×1.5 → 三角形求交）。
* 导出：新脚本 `scripts/export_introduce.py` → `scenes/introduce.txt`（每行 `article\<dff>` TAB 名 TAB 介绍；496 行，其中 66 条有介绍，其余是有真名的摆件，如「樹樁」「西裝男」；纯 ASCII 的内部名如 `eve_cardshop` 不导出，事件图标仍由路点答复）。键用 scene.txt / `Rm8Scene::clumpNames` 同样的模型路径，比较时不分大小写。旧版（Big5）和 Steam 版（GBK）都能导出（简体 / 繁体自动，移植另有 OpenCC 转换）。
* 提取器同步：`extractor/tools/sync_exporters.py` 的 ROOTS 加 `export_introduce`（`--check` 通过），`rm8x/pipeline.py` 加一步，`rm8x/validate.py`（`SHARED_TEXT`、模型键扫描排除表）、`tools/audit_repo.py`、`tools/scan_source_content.py` 的原文清单加 `introduce.txt`；`port/android/data_files.py` 的 `SHARED` 加 `introduce.txt`（deploy.py 和带数据 APK 共用该表）。
* 移植：`rm8InfoLoadData(scenes)`（rm8_app.cpp 启动时），`rm8InfoBoardTap` 里候选 5 = 射线穿过的最近摆件（`rayClump`：球 ×1.5 + 三角形，距离与角色 / NPC / 地块 / 路点一起比）；介绍用 `intro.ui`（标题行 + 正文，手动按 335 px 折行），名字用 `ginfo`。表里没有的摆件（地块上的房子、事件图标、水、特效）不答复，由地块 / 路点回答。
* **触屏长按**：`rm8_app.cpp`：单指不动按住 ≥ 0.5 s（不在 UI 上、不在菜单里）= 一次 `rm8InfoBoardTap`，抬起时不再算点按；之前只有 < 0.25 s 的点按才触发，所以“长按”实际上什么都没有。鼠标沿用原版的按下 / 点击（不做悬停）。
* 测试：`RM8_INFO_AUTO` 新增 `art:<模型名片段>[:n]`（点该摆件球心的屏幕位置）、`fdown:x:y` / `fup:x:y`（真实 SDL 手指事件，窗口模式走长按路径）、`box`（记录信息框是否在显示及其文字）。

## 7. 改动清单（新文件之外的小改）

* `app/hud.cpp`：onClick 里 6 / 8 / 9 → `rm8InfoOpenFromHud`；每帧 `rm8InfoTick(cam)`；全屏资料界面打开时不画数字精灵；`rm8HudInit` 开头 `rm8InfoReset()`。
* `app/menu.cpp` / `menu.h`：gsys 按钮 4；`rm8MenuOptionGet / Set / Save`、`rm8MenuMapNameByDir`。
* `app/rm8_app.cpp`：`rm8InfoSetScene(&sc)`；两处 `rm8InfoPreRender(cam)`；Esc / 返回键和右键先 `rm8InfoClose()`；手指拖动时 UI 按住则 `rm8UiPointerMove`。
* `app/skip.cpp`：`rm8SkipTap` 里加 `rm8InfoBoardTap`。
* `app/ui.cpp` / `ui.h`：`rm8UiPointerPos`、`rm8UiPressed`（只读存取）。
* `app/game.cpp` / `turn.h`：`rm8GameGetOptions`、`rm8GameDaysLeft`。
* `app/stock.cpp` / `stock.h`：`rm8StockHoldName / Qty`；`app/cards.cpp` / `cards.h`：`rm8CardTex`；`app/npc.cpp` / `npc.h`：`rm8NpcName`。
* `app/rm8_app.cpp`（补）：`rm8InfoLoadData`；单指长按。`scripts/export_introduce.py` + 提取器 / `data_files.py` 同步（见 6.1）。
* `CMakeLists.txt`：`app/infoscreens.cpp` 加进 `rm8_app` 和 `rm8_scene` 两份源码表（Android 用同一份表）。

**新增需要存档的状态：无**（界面是临时的；设置值已由 `rm8_settings.txt` 保存；`rm8GameDaysLeft` 用的结束日期已在 game.cpp 里，存档模块早就处理）。

## 8. 测试钩子和验证

环境变量（只用于测试）：

| 变量 | 作用 |
|---|---|
| `RM8_INFO_FORCE=1` | 任何座位 / 任何状态都可打开（全电脑的模拟），且不改状态 |
| `RM8_INFO_AUTO="…"` | 逗号分隔的步骤，HUD 存在后在下一次 `rm8InfoTick` 执行到第一个 `wait:帧` 为止；`RM8_INFO_DELAY=n` 先等 n 帧（经菜单进入的窗口模式）：`open:win|player|map|setting`、`hud:N`（**真实指针点击** gmaindlg 按钮 N 的中心）、`hover:N`、`tap:ID`（当前界面控件中心的真实点击）、`tapui:名字:ID`（已加载的 UI，如 `gsys:4`）、`slider:ID:0..1`（真实按下 + 拖动 + 抬起）、`mm:u:v`（小地图上的真实点击，记录悬停文字和相机焦点）、`board:x:y` / `plot:N` / `role:座位` / `node:N`（点棋盘，记录信息框文字）、`state`、`close`、`wait:帧` |
| `RM8_INFO_PADROWS=n` | 两个列表各补 n 行假数据（测滚动条） |
| `RM8_INFO_MARK=1` | 小地图上给每个角色画一个小方块（验证映射） |

验证（桌面，`build-info`；截图在 `out/info/final/`）：

* 全电脑模拟 700 s（21 天，4 人，有股票 / 地产 / 贷款）后 `open:player`：七行数字与手算一致（如 24516 + 35150 + 36960 + 19000 = 115626；贷款 100000 的角色净值 = 存款 + 现金 − 贷款 + 股票 + 地产），列表、卡片、选中列高亮正确（`player.png`、`player_cards.png`）；`slider:3:1.0`、`slider:2:0.5` 真实拖动后两个列表滚到底 / 中间（`player_scroll.png`，用 `RM8_INFO_PADROWS=14`）。
* `--human 0` 的真实指针流程：`hud:9`（真实点击小地图按钮）→ 屏幕 3 + 状态 9 → `tap:2`（关闭）→ 状态 8；`hud:8` → 屏幕 2、状态 9。
* 经菜单进入的真实对局（窗口模式，`RM8_MENU_AUTO` + `RM8_INFO_AUTO`）：依次 `hud:8` / `tap:1` / `hud:9` / `tap:2` / `hud:6` / `tap:1` / `hud:10` + `tapui:gsys:4`（进设置）/ `tap:5`，每一步日志的屏幕号、状态号都对，最后状态回到 6（可以掷骰）。胜利条件显示「〔string.bin#7053〕：92」（11-15 起三个月到 02-15 = 15 + 31 + 31 + 15）、「胜利条件：〔string.bin#7042〕」、「當前地圖：香港」（`win_menugame.png`）。
* 小地图 `open:map`：整个可玩区域俯视图（`map_tap.png`），`mm:0.46:0.228` 悬停得到街道名「旺角路」，点击后 `rm8RolesFocus` 的序号变化、焦点 = 点击的世界坐标；`RM8_INFO_MARK=1` 的四个角色方块都落在路面上。
* 设置：`open:setting,slider:1:1.0,slider:3:0.0`，旋钮位置与 `rm8_settings.txt` 的值一致（music=1、sound=4、speed=5 → 滑条 3 = 3）；关闭才保存。
* 棋盘信息框：格点 + `plot:N` 点按，得到「銅鑼灣^^1級住宅」「淺水灣^^住宅建地\地主：阿土仔\租金：500元」「商業建地\地主：阿土仔」「九龍城」「公園」「福神」「衰神」「錢夫人」等（`board_info.png`）。
* 悬停 HUD 按钮：`hover:8` 0.5 s 后出现「個人資料」。
* Android：`build-info-android`（arm64-v8a，android-24）编译链接通过（`libmain.so`）。

## 9. 遗留 / 需要实机或对照原版确认

* 玩家资料的头像鼠标移入图（`_13`）、角色头像按钮在非人类座位上的表现没做；
* 小地图原版是平滑移动相机，移植瞬间到位；图内但射线打不中地面时原版不动；
* 故事模式的胜利条件文字没在游戏里跑过（只读了代码）；
* 摆件（景点）的名字 / 介绍框（`UI_ShowIntroduction`，intro.ui）和奇迹建成后的介绍没做，原因见第 6 节；
* 托管（gsys 按钮 1）没做，见第 5 节；
* 触屏的右键（取消）等价物只有“再点一下”和 Android 返回键。
