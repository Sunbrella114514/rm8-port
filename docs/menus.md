# 前端菜单：主菜单 → 游戏设置 → 开局（移植说明）

标记：`[V]` 已在反编译里核对（附地址）；`[H]` 推测；`[A]` 移植里的取舍。代码：`port/app/menu.h` / `menu.cpp`，应用接入见 `rm8_app.cpp`。
反编译来源 `out/final_v2/RichMan8_named.c`（函数名只有一半可信，以代码为准）。辅助脚本：`scripts/menu_fn.py <地址>`（打印 final_v2 函数）、`scripts/menu_ui_dump.py <ui名>`（紧凑列出 .ui 控件）、`scripts/export_stages.py`（生成 `port/scenes/stages.txt`）。

## 1. 流程

```
启动 ─ logo(CLogoScene) ─ [Bink 开场影片：跳过 A] ─ 主菜单(状态1)
主菜单 1 单人 / 2 多人 → single / mult 菜单(状态8) → 1 新游戏 / 2 读档(状态9) / 3 拼图图鉴(状态10) / 4 影片欣赏(状态7) / 5 返回
主菜单 3 选项(状态5)   4 小游戏(状态6)   5 退出
新游戏（自由模式 mult）：地图菜单(状态3) → 角色菜单(状态2) → 开始 → 棋盘
新游戏（故事模式 single）：角色菜单(状态2) → 地图菜单(状态3) → 过关影片 → 棋盘（A：故事模式未做，两个菜单都走自由模式流程）
游戏中：gmaindlg 按钮 10 → gsys 系统菜单 → 退出 → exit.ui 确认 → 回到地图菜单
```

### 1.1 菜单状态机（`@0x405650` 起）

* [V] `u_CApp_SwitchToGameScene @0x405650`（旧名 SwitchToMenuScene）新建菜单场景（vtable 0x5E0048）后调 `Ui_SetActiveMenuObject(n) @0x509fe0` → `Obj_CreateByTypeAndInit @0x509910` 按编号建状态对象：1 mainmenu(0x5E8190)、2 chsRoleMenu(0x5E8140)、3 / 4 chsMapMenu(0x5E81E0，参数 1 / 0xB)、5 optionMenu(0x5E80F0)、6 ltgameMenu(0x5E80A0)、7 finalMenu(0x5E8050)、8 single/mult(0x5E8000)、9 slmenu(0x5E7FB0)、10 picmenu(0x5E7F60)、0xB boxmenu(0x5E7F10)；建好后调 vf16（显示菜单）。
* [V] `Ui_CloseAllMenuDialogs @0x508c60` 销毁全部菜单单例的对话框。
* [V] 菜单场景初始化 `CGameScene__vf16_Init @0x509d70` 入队曲目 101 并 `PlayBGM(0,0)`：菜单音乐（docs/audio.md）。移植：每个菜单界面进入时 `rm8MusicPlay(101, true)`（主菜单、小游戏返回、退出回地图菜单；其余界面沿用）。
* [V] logo：`Logo_LoadTexturesAndInitQuads @0x507690` 读 `texture\prt\logo\c_002`、`c_003`，建 4 个 512×512 居中四边形；`CLogoScene__vf1 @0x507990` 渐入（alpha 0x20→0xFF）、停留、渐出，第一次渐出结束（计数 > 0）时调 `App_SwitchToStartMovieOrMenu @0x507b70`：先试开场 Bink（`CApp_SwitchToStartMovie(0)`），失败则进菜单 1。`CLogoScene__vf2 @0x507ad0` 只画 `c_002`（计数 < 2 时画第 计数 张，计数从 0 到 1 就切走）。移植：渐入 1 s / 停留 1 s / 渐出 1 s（[A] 时间），之后直接进主菜单；`RM8_NOLOGO=1` 跳过。

## 2. 各菜单

### 2.1 mainmenu（状态 1，`mainmenu.ui`）
* [V] vf10 `@0x508440`：按钮 ID 1..5 注册点击，ID 100 注册返回回调。`Cls_5e8190__vf12_OnMessage @0x508b50`：1 → 单人（`setup+0 = 1`）进状态 8；2 → 多人（`setup+0 = 0`）进状态 8；3 → 状态 5；4 → 状态 6；5 → `App_PostQuit`。
* [V] 五个六边形按钮各配一个图标精灵（`Ui_CreateMenuItemObjects @0x508730`，图片对表 `0x5E7B7C`：{104,105}{133,134}{107,108}{110,111}{157,158}，控件 ID 6..10 的位置）；鼠标在按钮上时绕 Y 轴 720°/s 转到 360°，转到 90° 换图（`Ui_UpdateMenuItemRotation @0x508f60`），离开时倒转。[A] 用宽度缩放 |cos| 表示旋转。
* [V] 按钮 ID：1 故事模式（多人图标为「多人模式」）、2 多人、3 选项设置、4 练习区（小游戏）、5 退出（靴子图标）。移植里主菜单 ID 1 = 单人，2 = 多人。
* 返回键 / Esc：退出程序 [A]。

### 2.2 single / mult（状态 8）
* [V] `Cls_5e8000_vf16 @0x50d9e0`：`setup+0 == 0` 用 mult，否则 single（`single.ui` / `mult.ui`，布局相同，图标表 `0x5E87EC`：{446,447}{455,456}{449,450}{452,453}{1051,1052}）。
* [V] `Cls_5e8000_vf12 @0x50db90`：1 新游戏（mult → 状态 3，single → 状态 2）、2 读档（状态 9）、3 拼图图鉴（状态 10）、4 影片欣赏（状态 7）、5 返回主菜单。
* [A] 3 / 4 未做：点击只写日志。

### 2.3 chsMapMenu（状态 3，`chsMapMenu.ui`）：地图与开局选项
* [V] 进入 `Cls_5e81e0__vf16 @0x503100`：`setup+4 = 0`，`Game_InitOptions @0x5031c0` 复位选项：总资金 100000、点券 50、交通工具 0（步行）、时限 2（不限）、胜利条件 5（〔string.bin#7046〕），胜利条件箭头 ID 0x11 禁用；自由模式（`setup+0 == 0`）显示 6 行选项标签（IDs 61..66 / 控件 13..18 箭头），故事模式隐藏。`Game_PopulateMapList @0x503510`：自由模式按 `Event_GetEventByType(1..11)`（gamedate.bin，第 3 张地图默认锁）启用地图按钮。[V] 自由模式未解锁的地图按钮是**隐藏**（`vf+0x1c`），移植已按 `rm8ProfileMapUnlocked` 实现（新档巴黎隐藏；`RM8_UNLOCK_ALL=1` 全开），见 `docs/characters.md` 第 3 节。
* [V] 点击 `Cls_5e81e0__vf12_OnEvent @0x503d20`（仅在状态 1 即“选择中”时）：ID 1..11 选地图（`setup+4 = id`，更新文字，禁用已选按钮）；0xC 返回；0xD..0x12 展开 / 收起该行的选项列表（`Cls_5e81e0_ShowOptionList @0x504640`）；0x47..0x4C 在列表里选值（`Game_SetOption @0x504940`）；0x76 确认（`Cls_5e81e0__vf10_OnConfirm @0x503e90`：需 `setup+4 > 0`；自由模式进状态 2）；0x13 → 状态 0xB（boxmenu）。
* [V] 选项取值（`Game_SetOption`、字符串 `StringTable+0x6d64..` = string 7001..7046）：

| 行 | 控件 | 写入 | 取值（文本 string） |
|---|---|---|---|
| 起始现金 | 0xD / 标签 0x3D | `setup+0x0C` | 50000 / 100000 / 200000（7001..7003） |
| 起始点券 | 0xE | `setup+0x10` | 0 / 50 / 100（7011..7013） |
| 交通工具 | 0xF | `setup+0x14` | 0 步行 / 1 机车 / 2 汽车（7021..7023） |
| 游戏时间 | 0x10 | `setup+0x18` | 0 三个月 / 1 六个月 / 2 不限时（7031..7033）；选 2 时胜利条件强制 5 并禁用 0x11 |
| 胜利条件 | 0x11 | `setup+0x1C` | 0 〔string.bin#7041〕 / 1 〔string.bin#7042〕 / 2 〔string.bin#7043〕 / 3 〔string.bin#7044〕 / 4 〔string.bin#7045〕 / 5 〔string.bin#7046〕（7041..7046） |
| 下载地图 | 0x12 | `setup+4 = 0x15 + n` | `map\download*.map` 按文件顺序（`Map_LoadMapList @0x4a8ce0`；`Map_LoadDownloadMap @0x4a9020` 取名 = 文件名） |

* [V] 地图文字：`CUi_SetMapSelectionText @0x502b70`：标签 98 = 地图名（stage.bin 节名，`scenes/stages.txt`）、标签 99 = string 6600 + id（地图介绍）、图片 ID 51 = uvlist `DAT_005e76bc[id]` = 799、800、801、813..820（id 1..11）；下载地图图片 0x45B 且无文字。鼠标悬停地图按钮（回调 5 / 6）时显示悬停地图的文字，移开回到已选地图。
* [V] 返回（`vf11 @0x5042a0`）：有展开的列表则先收起，否则回 single / mult 菜单（`Cls_5e81e0_ShowGameMenu @0x504310`）。
* [A] 选项列表（原版是 listbox 71..76，白底、悬停黄）用运行时控件重画：行下弹出，点击条目选值；已选地图用 uvlist 293 的标记图。故事模式的 0x13「储藏箱」按钮（boxmenu）隐藏，未做。
* 开局文字用 `rm8UiWrapText`（ui.cpp 新增）折行。

### 2.4 chsRoleMenu（状态 2，`chsRoleMenu.ui`）：选角色
* [V] 控件：ID 1..16 为 16 个角色六边形按钮（图 174..189，悬停图 269..284，头像 uvlist 887 + id 显示在 ID 51）；ID 61..76 为“已选”皇冠（图 1095，原版用于角色解锁 / 通关标记，移植里标已选）；ID 17 开始（✓）、18 返回（✗）；座位控件：25..28 玩家标签（PLAYER）、29..32 电脑标签（COM）、34..37 删除座位小按钮，19..23 / 33 服装（1..4P 服装色按钮，`Cls_5e8140_UpdateRolePanel @0x505a80` 按 `Event_GetLevel(role)` 显示可用服装）。
* [V] 点击 `Cls_5e8140__vf12_OnCommand @0x5065f0`：1..16 加角色（最多 4 个座位，`setup+4 == 0x65` 即小游戏 1 时最多 2 个；同角色同服装不能重复；新座位 `+8 = (已有座位里有人类 ? 1 : 0)`，即第一个是人类，之后都是电脑，`+4 =` 服装 0..）；0x19..0x1C → `Role_SetItemField8(seat, 0)` 变人类；0x1D..0x20 → 变电脑（`Role_SetItemField8 @0x505eb0`：变电脑要求至少还有 2 个人类）；0x22..0x25 删除座位（座位下标 − 1，若没有人类则第一个座位变人类）；ID 17 → `Cls_5e8140__vf10_StartGame @0x506a30`；18 → `vf11 @0x5069b0`。
* [V] 显示：`Cls_5e8140_LoadAndDisplayRoles @0x505fb0`：标签 98 = 角色名（roleinfo 节名，string 600 + …），标签 99 = string 6900 + id（角色介绍），选中座位播放角色语音（`Cls_5e8140_PlayRoleSample @0x506b20`，样本 = `StringTable + 32000 + (角色 +0x38C + 0x1D)*4`）。[A] 语音与 3D 角色模型（原版用 `Role_CreateMeshObject` 现场画角色）未做；座位上画角色的六边形头像代替。
* [V] 开始（`@0x506a30`）：必须有至少 1 个人类；`setup+4 < 0x65`：自由模式需座位数 > 1 → 开始游戏（`CApp_StartNewGame @0x4057c0`）；故事模式仅 1 个座位 → 状态 3；`setup+4 ≥ 0x65`（小游戏）→ `CApp_SwitchToMiniGame(setup+4 − 100)`。返回（`@0x5069b0`）：`setup+4 < 0x65` → 自由模式回状态 3 / 故事回状态 8；否则回状态 6。
* [A] 开始时座位不足 2 人只提示（原版什么也不做）。服装 0..3 按角色 stage、服装 4（泳装）按 stage ≥ 3 + 9 块拼图解锁，未解锁的角色 11..16 灰显，已全部实现并核实（`docs/characters.md` 第 2 / 3 节，`RM8_UNLOCK_ALL=1` 全开）。
* 返回键 / Esc = ID 18。

### 2.5 optionMenu（状态 5，`optionMenu.ui`）
* [V] `CInitConfig__vf16_InitUI @0x50aa20`：6 个滑块用 init.txt 初值：1 音乐（`music`，0..5）、2 音效（`sound`，0..5）、3 速度（`speed − 2`，1..5 → 游戏速度 3..7）、4 难度（`difficuty`，1..3）、5 全屏（`fullscreen`，0..1）、6 `character − 3`（1..3，init.txt `character` = 4..6；含义 [H]：角色 / 画质档，图标是灯泡）。`Config_ApplySetting @0x50a710` 在每次滑动时立即应用：1 `Music_SetVolumeLevel`、2 `Sound_SetVolumeLevel`、3 `InitConfig_SetSpeed(v + 2)`、4 写 init 难度、5 `CApp_SetFullscreenMode`、6 写 init `character`。返回按钮 ID 7（`vf12 @0x50ac30` → `vf11 @0x50ac60`）：`CInitConfig__vf18 @0x50ab40` 保存 `Config\init.txt` 并回主菜单。
* [A] 选项存到 `<数据目录>/rm8_settings.txt`（游戏目录只读；键名同 init.txt，另有 `simplified`）；启动时先读游戏的 `Config/init.txt` 再读 rm8_settings.txt。速度写 `rm8GameSpeed`，难度写环境变量 `RM8_DIFFICULTY`（events.cpp 首次使用时缓存，故第一局之前设置才生效），音量 / 全屏通过 `Rm8MenuHooks::applyOptions` 钩子交给 app / audio（音频目前没有设置音量的接口，见第 6 节）。
* [A] 新增第 7 项「文字：简体 / 繁體」按钮（`rm8ZhEnable`），写入 `simplified`；`RM8_LANG` 环境变量优先。
* [A] 滑块是 .ui 里的 slider 控件，但 ui.cpp 不支持：在滑块上叠了一个滑块把手图（uvlist 212）并自己处理拖动。

### 2.6 ltgameMenu（状态 6，`ltgameMenu.ui`）：小游戏
* [V] `Cls_5e80a0__vf12_OnMessage @0x508350`：按钮 1..6；1 和 4 先进角色菜单（`setup+4 = 100 + n`，选完人后 `CApp_SwitchToMiniGame(n)`，`@0x506a30`）；2、3、5、6 清股票数据后直接 `CApp_SwitchToMiniGame(n)`（`CMiniGameHost_vf16 @0x50ddd0`）；7 返回主菜单并清 `setup+4`（`@0x5083e0`）。悬停（回调 5 / 6）`u_Menu_ShowItemInfoByIndex @0x508030`：标签 10 = string 6965 + n（游戏名）、标签 11 = string 6971 + n（介绍），图片 ID 51 = 977 + n。
* [A] HI-SCORE 数字（number.tga，`Cls_5e80a0_InitNumberTexture @0x507ca0`）不显示；游戏本身走 `Rm8MenuHooks::startMiniGame` 钩子。

### 2.7 slmenu（状态 9，`slmenu.ui`）：读档
* [V] `CSaveSlots_vf16 @0x50cf20`：槽 1..10 读 `save\record_NN.sav`（单人）/ `save\record_mNN.sav`（多人，`setup+0 == 0`）的头：日期（`+2/+6/+8/+10` 年 / 月 / 日 / 时分，string 7103「%d月%d日%d時%d分」→ 标签 40 + n）、地图（`+0x10`，图片 945 + 地图，≥ 12 用 1116 → 控件 30 + n）、角色数 `+0x14` 与角色号 `+0x18..`（头像 `npc\<pic>_12` → 控件 47 + 4n + k）；空槽禁用。`CSaveSlots_vf12 @0x50d490`：点槽 → `CApp_LoadSavedGame(path)`；ID 21 返回（`@0x50d560` → 状态 8）。
* [A] 读写存档由存档模块做（`docs/save_port.md`）：槽信息 / 读档走 `Rm8MenuHooks::saveSlotInfo / loadGame`（`rm8_app.cpp` 设置；loadGame 只排队，棋盘在主循环里重建）。注：槽头的 `+2/+6/+8/+10` 是月 / 日 / 时 / 分（string 7103 没有年）。

### 2.8 未做的菜单
* finalMenu（状态 7）`@0x506f20 / 0x506fc0`：16 个角色头像按钮 + 电视画面，`Event_GetLevel(i) >= 4` 才可点，点击 `CApp_PlayMovieRange(i − 1)` 放通关影片（Bink）；ID 0x12 返回。影片不在范围内。
* picmenu（状态 10）`@0x50acb0 / 0x50ad50 / 0x50af60`：拼图图鉴（string 171），16 个角色，`Event_GetItemData` 取已收集拼图，0x12 返回。
* boxmenu（状态 0xB）`@0x501a70 / 0x502050`：两个十字形格子之间搬移卡片 / 地产，现金 / 点券（string 7201 / 7202），故事模式的储藏箱。

## 3. 开局设置 setup（`&DAT_00632de8`，`Singleton_GetPtrArray_00632de8 @0x4083f0`）

| 偏移 | 含义 | 移植 `Rm8Setup` | 菜单里怎么来 |
|---|---|---|---|
| +0x00 | 故事（单人）标志 | `story` | 主菜单 1 / 2 |
| +0x04 | 地图号（>100 = 小游戏 n） | `map` | 地图菜单 / 小游戏菜单 |
| +0x08 | **关卡层 0..3**（来自 pass 配置的 `level`，自由模式 0；旧称「剧情标志」是误读，见 `story_mode.md` 第 0 节）。故事模式的菜单流程（角色菜单故事规则、候选图、host 演出、finalMenu）已在移植里实现：见 `story_mode.md` 第 4 节 | — | — |
| +0x0C | 起始资金总额 | `cash` | 地图菜单行 1（默认 100000） |
| +0x10 | 起始点券 | `tickets` | 行 2（默认 50） |
| +0x14 | 起始交通工具 | `vehicle` | 行 3（默认 0） |
| +0x18 | 时限 | `timeLimit` | 行 4（默认 2） |
| +0x1C | 胜利条件 | `winCond` | 行 5（默认 5） |
| +0x20/+0x2C | 玩家数组（每项 12 字节：角色 Id、服装、0 人类 / 1 电脑） | `seats[]`（`Rm8Seat`） | 角色菜单 |

`Game_InitGame @0x4a1090`（docs/rules_land.md 1.7）使用：现金 = 存款 = `+0x0C / 2`；点券 = `min(+0x10, 9999)`；交通工具 = `clamp(+0x14 + 1, 1, 3)`；结束日期 = 今天月份 + 3（时限 0）或 + 6（时限 1），超过 12 减 12，年份不变；时限为 2 无结束日期；到期且为自由模式时 `Game_FindLeaderByWinCondition @0x4a6090` 按 `+0x1C` 选胜者。

## 4. 移植接入（共享文件改动）

* `turn.h` / `game.cpp`：`Rm8GameOptions` + `rm8GameSetOptions`（默认值 = 原默认值 = 菜单出现前的行为）；`rm8GameNewGame` 用它设现金 / 点券 / 骰子数 / 物价基数（`g_base = cash`）/ 结束日期；`rm8GameOnNewRound` 末尾在结束日检查胜者（赢家文本 string 7057，[A] 结局画面未做：显示文字并置 `g_over`，游戏继续显示）。〔string.bin#7045〕（4）因移植没有股票而按 0 处理，规则 [H] 完全照 0x4a6090。测试钩子 `RM8_END_DAYS=n`：结束日改为 n 天后。
* `roles.cpp` / `turn.h`：`rm8RolesSetHumans(mask)`（菜单里的人类座位，原有 `humanSeat` 参数不变）、`rm8TurnSetDiceMax`（起始交通工具 → 骰子数）。
* `ui.cpp` / `ui.h`：仅新增函数——`rm8UiHideAll`、`rm8UiAddWidget`（运行时控件）、`rm8UiCreateCanvas`、`rm8UiSetTint`（图片着色 / 淡入）、`rm8UiSetBColor`、`rm8UiSetColor`、`rm8UiGetScreenRect`、`rm8UiWrapText`；以及控件的 `tint`、`dcolor`（`DisableCapFontFColor`，禁用时的文字色）字段。
* `hud.cpp`：gmaindlg 按钮 10 → `rm8MenuGameSystemOpen(true)`（原版 `@0x4a2e10` 状态 6 时 → 任务 0x68 gsys）。
* `rm8_app.cpp`：`menu.h`；`--map / --shot / --human / --cycle / --frames / --minigame` 任一出现或 `RM8_NOMENU=1` 都跳过菜单（`--menu` 强制进菜单，供测试）；其余情况启动即进菜单；菜单结束后按 setup 载入地图、`rm8RolesSetHumans` / `rm8GameSetOptions` / `roleIds`；菜单里不转发事件到棋盘；游戏里 Esc / 安卓返回键 = 打开 / 关闭系统菜单（菜单局），非菜单局保持原来的“退出”；系统菜单退出确认后卸载棋盘回到地图菜单（`backToMenu`）。
* `CMakeLists.txt`：两个源列表加 `app/menu.cpp`；`android/deploy.py`：共享文件元组加 `stages.txt`。
* 新数据：`port/scenes/stages.txt`（`scripts/export_stages.py`，由 stage.bin 导出：id、地图夹、气候、名称）。

### 系统菜单（gsys.ui）
* [V] gsys（`0x5E3F70`，vf10 `@0x4AA630`）按钮 ID 1 托管 → supply.ui（`@0x4aa810`）、2 存档、3 读档、4 设置、5 关闭、6 退出 → `CUi_event__ShowUI @0x4927b0` 入队 `1(0,0x86)`、`0x1C(exit 对话框)`、`0x78`；`0x78` 的确认值非 0 时 `Game__ExitMainLoop @0x490010` 把游戏状态置 3，`CGamePlayScene__Game_OnUpdate @0x4a17b0` 见状态 3 → `u_CApp_SwitchToGameScene(setup+0 == 0 ? 3 : 2)`（自由模式回地图菜单，故事模式回角色菜单）。
* [A] 移植：5 关闭、6 退出做了（退出回地图菜单；故事模式同样回地图菜单）；2 / 3 存读档接到 `save_ui.cpp` 的槽位对话框（`docs/save_port.md` 第 7 节）；1 托管见 `docs/autoplay.md`（supply.ui → 消息 0x79 → `Role_EnableAutoPlay`），4 设置见 `infoscreens.md`。游戏不暂停（原版 gsys 任务运行时事件任务占住回合流程，同样不推进角色，移植未复刻）。

## 5. 测试

* 环境变量 `RM8_MENU_AUTO` 在菜单里自动操作（逗号分隔，每步间隔 6 帧）：`single` / `multi` / `option` / `minigame` / `load`（主菜单）；`new`（single/mult 菜单的新游戏）；`map:N`（选地图）；`opt:行:项`（地图菜单某行选项，行 0..5，项 0..）；`role:ID`（加角色）；`start`（角色菜单 / 地图菜单的开始 / 确认）；`click:ID`（直接发控件点击，运行时控件 ID 从 5000 起）；`back`；`wait:帧`；`shot:名`（保存 `menu_<名>.png`，目录 `RM8_MENU_SHOTDIR`，默认当前目录）；游戏中 `sys`（开系统菜单）/ `sys6`（点退出）/ `sysok`（确认退出）。例：`RM8_MENU_AUTO="multi,new,map:3,opt:0:2,opt:2:2,start,role:11,role:2,start" rm8_app <game> <scenes> --menu`。
* 其他：`RM8_NOLOGO=1` 跳过 logo；`RM8_NOMENU=1` 不进菜单。`tap:控件ID`：在控件中心发真实的触摸按下 / 抬起（`rm8UiPointerMove / Down(touch) / Up`，走命中测试，六边形按钮会等图标转完再切页，见 7.3）；`click:ID` 直接调处理函数（不走指针）。动画对比用 `RM8_MENU_SHOTDIR` + `shot:名` 间隔截图再逐像素比较。
* 验证（桌面，构建目录 build-menu）：各菜单截图逐个看过（main / mult / map / options 列表 / roles / option / load / ltgame / 游戏中 gsys + exit）；自动流程“多人 → 地图 3 → 现金 200000、点券 100、汽车、六个月、总资产 → 角色 11 + 2 → 开始”后日志为 `start money 100000 + 100000, tickets 100, vehicle 2, time limit 1, win condition 1`，棋盘正常买地；`RM8_END_DAYS=2` 时第 3 天出现 `time is up, seat 0 wins`；`--map 03 --frames 600` 仍跳过菜单并使用旧的演示座位。
* 安卓：`build-menu-android`（按 README 的配置）已编译通过；未打包、未部署。

## 6. 未做 / 待接

| 项 | 说明 |
|---|---|
| 故事模式 | single 菜单的角色→地图→过关影片流程、pass_XX 关卡、剧情 `setup+8`、储藏箱（boxmenu）。现在两个菜单都走自由模式 |
| 读档 / 存档 | `Rm8MenuHooks::saveSlotInfo / loadGame` 由存档模块设置；返回 `RM8MENU_LOAD_GAME` 时 app 认为游戏已被钩子建好（`rm8_app.cpp` 里仅关闭菜单） |
| 小游戏 | `Rm8MenuHooks::startMiniGame(n, setup)`（阻塞，返回后回到小游戏菜单）；HI-SCORE 数字 |
| 音量 / 全屏 | `Rm8MenuHooks::applyOptions`：audio.h 没有“设置主音量”接口，需要加一个（`rm8AudioSetLevels(music, sound)`），然后 app 里挂钩；全屏切换未做 |
| finalMenu / picmenu / boxmenu | 见 2.8 |
| 角色菜单 | （已做）3D 角色模型、选中语音、服装 / 泳装与解锁（gamedate.bin），见 `characters.md` |
| 系统菜单 | 托管、存 / 读档、设置、游戏暂停 |
| 地图解锁 | （已做）`Game_PopulateMapList` 自由分支：gamedate.bin 里没解锁的地图按钮隐藏（第 3 张巴黎默认锁），`RM8_UNLOCK_ALL=1` 全开 |
| 开场影片 / logo 之后的 Bink | 范围外 |
| 胜负结局 | 时间到只显示文字 |

## 7. 菜单场景动画与页面切换（`port/app/menu_anim.cpp / .h`）

### 7.1 场景结构 [V]
* `u_CApp_SwitchToGameScene @0x405650` 新建 8 字节的菜单场景 `CGameScene`（vtable `0x5E0048`，`+4` = 当前菜单对象），再 `Ui_SetActiveMenuObject @0x509fe0`。场景初始化 `CGameScene__vf16_Init @0x509d70` 建三个全局对象并放音乐：`DAT_0064b1d0`（飞行角色 `Cls_5e8638`，构造 `0x50bca0`，`CGameScene_LoadWonders @0x50bda0` 载入 21 个模型）、`DAT_0064b648`（旋转光环 `Cls_5e8270`，构造 `@0x50a570`，`CGameScene_InitSprites @0x50cb00`）、`DAT_0064b1d4`（飘浮气泡 `Cls_5e8230`，构造 `Cls_5e8230__Ctor @0x50a410`）；`u_CDigitalAudio_PlaySound(0x65)` + `PlayBGM`。析构 `Ui_CleanupGameScene @0x509ec0`。
* 帧循环：`CApp_UpdateFrame @0x403700` 调场景 `vf1(dt)`；`CApp__vf9_RenderFrame @0x403790`：场景 `vf2(0)` → 3D → `UiLib_Manager_DrawAll` → 场景 `vf2(1)`。
  * 场景 `vf1 @0x50a180`：光环更新、菜单对象 vf1、`CArticleMgr_UpdateEffects`。场景 `vf2 @0x50a1f0`：设渲染状态后转菜单对象 `vf2(1)`。
  * 主菜单 `Cls_5e8190__vf1 @0x508a70` / `vf2 @0x508ae0`、single / mult `Cls_5e8000_vf1 @0x50dab0` / `vf2 @0x50db20` 额外转发给 `DAT_0064b1d0` 与 `DAT_0064b1d4`（所以飞行角色和气泡**只在主菜单、single、mult 三个页面**出现）。其它菜单（地图、角色、选项、小游戏、读档、拼图、储藏箱）只有光环和高亮。
* **绘制顺序**：每个 UI 的 `vf10` 给控件 100（角色菜单是 0x29）挂两个回调：抬起 `0x508d50`（索引 1，点击通用处理）和**绘制后**回调 `0x508db0`（索引 0x13，`Ui_RegisterCallback @0x509390`）。绘制后回调 → 场景 `vf17 @0x50a130` → 当前菜单对象 `vf17`（主菜单 `0x508c40` → `CUi_mainmenu__vf15_HideMenu @0x508860` → 高亮精灵组 `vf2(1)`），再 `DAT_0064b648 vf2(1)`（`Cls_5e8270__vf2_Render @0x50cdb0`）。即：**在控件 100 之后、Index 更大的控件之前依次画高亮图、光环**，按钮和 logo 压在它们上面。
* 取消 / 切页时：主菜单 `vf18 @0x508a20`、single / mult `vf18 @0x50da60` 调 `Particle_ResetAll @0x50c930`（清飞行角色，计时器 = 1.0）和 `Camera_RestoreViewState @0x41b360`；光环 / 气泡 / 高亮的状态不重置（属于场景或全局实例），所以切页时光环角度连续。

### 7.2 各层动画 [V]
| 层 | 对象 / 函数 | 内容 |
|---|---|---|
| 光环 | `Cls_5e8270 @0x5e8270`，`InitSprites @0x50cb00`，`vf1 @0x50ccb0`，`vf2 @0x50cdb0` | 3 个 `uvlist 152`（`j002 6.tga`，512×512 光环），位置表 `0x5e8680`：(0, −5) (480, 300) (660, −150)，缩放表 `0x5e8698`：1.2 / 0.8 / 0.5（绕图心），转速表 `0x5e86b0`：10 / −20 / 30 度每秒，角度夹在 ±360（`Sprite_SetZRotation @0x462490`，绕图心 Z 轴，`Sprite_UpdateTransformedVerts @0x4627a0`） |
| 高亮图 | `Cls_5e7588 @0x5e7588`（单例 `CUi_boxmenu_GetInstance @0x501ca0`，名字是误称，全部菜单共用），`vf1 @0x507230`，`vf2 @0x507330` | 每个 UI 在 `vf12` 里 `Sprite_CreateMenuItemPicture @0x5073c0` 建 2 张白色六边形轮廓图叠在背景上（控件 100 / 101 的位置，图片尺寸）：主菜单 / single / mult 262、263（`0x106`，`0x107`），默认 UI（地图 `0x503060` 系、picmenu、finalMenu `@0x5093d0`）988 / 989，角色菜单（控件 0x29 / 0x2a）923 / 924，选项 1028 / 1029（`@0x50a8e0`），小游戏 1023 / 1024（`@0x508160`），读档 1003 / 1002（`@0x50ceb0`），储藏箱 1026 / 1027（`@0x501c30`）。透明度 v 初值 255、方向 −1，每帧 `v += ftol(dt × 100 × 方向)`，大于 0x50 反射成 `0xA0 − v` 并转向下，小于 0 取反并转向上，颜色 `0x00FFFFFF | v << 24`：轮廓在 0..80 之间呼吸 |
| 圆点 | `Cls_5e7bf8 @0x5e7bf8`，`Ui_InitMenuSprites @0x50b020`，`vf1 @0x50b1f0`，`vf2 @0x50b3c0` | 仅主菜单 / single / mult：4 个 `uvlist 458`（64×64 圆点）放在控件 16、15、14、13 处（.ui 里这四个控件本身 Visible=false）。每隔 2.0 s 第 0 个开始：透明度 `v` 从 1 以 600/s 涨到 200，反射回落并在反射瞬间启动下一个，第 3 个落回 0 后计时重新开始 |
| 气泡 | `Cls_5e8230 @0x5e8230`，`vf1 @0x50ba60`，`Particle_InitRandom @0x50b420`，`Sprite_UpdateMovement @0x50b680` | `uvlist 925`（`j002 9f.tga`，64×64 气泡）；每 0.2 s 且少于 16 个时生成一个：一半从上边缘 `(rand%400, −rand%100)`、一半从左边缘 `(−rand%100, rand%300)`，缩放 `(rand%90+30)/100`，速度 / 目标向量 `(rand%300+10)` 两轴，追速 ≤ 300/s，目标每隔 `(rand%60+30)/60` s（计时从不清零，之后每帧换目标，原版行为）重选，位置超出 (宽 + 50, 高 + 50) 就删 |
| 飞行角色 | `Cls_5e8638 @0x5e8638`，`vf1 @0x50c000`，`vf2 @0x50c440`，`Particle_ActivateRandomIdle @0x50c530`，`Particle_ResetAll @0x50c930` | 表 `0x5e8450` 共 21 项 × 0x14 字节 `{美术号, 动作, 模式, 缩放, 速度}`：16 个角色（art 7 / 0x17 / 0x1b / 0x27 / 0x33 / 0x47 / 0x4b / 0x5b / 0x6b / 0x6f / 0x7f / 0x87 / 0x9b / 0xa3 / 0xab / 0xbb，动作 1 或 0，缩放 1.5）+ 5 个神明（art 0x1f5 / 0x1f7 / 0x1fd / 0x201 / 0x205，缩放 1.1 / 1.1 / 1.2 / 1.1 / 1.1，前两个模式 1）。速度 50。相机恒等，窗口 0.3×0.225，起点 (−280, −170, 800)（`0x50c530` 写 `0xc38c0000 / 0xc32a0000 / 0x44480000`），**RW 相机 x 向左**：角色从屏幕右下沿着 +x 走到 x = 280（左边出屏）；模式 1 的神明在 x ≥ 140 后摆动上升（`dt × 20` 的 x 摇摆 ±(5..19)，y 每秒 +70，y > 270 出屏）。每 3 s 从「不在场且不在最近 6 个已离场」的类型里随机取一个；`Mesh_SetTexture @0x47a570` 选动作（循环），`Mesh_Rotate @0x47a790` 在创建时转 180°。颜色 / 材质按美术自带，不换服装贴图。绘制用 alpha 测试 ≥ 196（渲染状态 0x1d = 7，0x1e = 0xC4），前面清 Z（`RwCameraClear`） |
| 角色菜单座位 | `Cls_5e8140_LoadAndDisplayRoles @0x505fb0`，`Role_CreateMeshObject @0x505410`，`Cls_5e8140_RenderScene @0x505270` | 每个座位 1 个 3D 角色：美术号 = roleinfo `+0x308`（+4 即服装号为 4 时加 0xC0）= `roles.txt` 里 `role` 行的第一个美术号（fl 模型），动作 2（站立循环），转 180°，位置表 `0x5e77c8`：(116, −70, 500) (78, −90, 500) (36, −70, 500) (−6, −90, 500)（x 向左），体色贴图 `Asset_LoadCharacterTexture @0x4934a0`（服装号） |

* 动画时间：原版用帧时间 `dt`（光环 / 圆点 / 角色都是 `dt × 常数`），高亮图每帧步进取整数（帧率限制 ≈ 15 ms，约 66 / s）：[A] 移植用 60 / s。随机数：原版 `rand()`，[A] 移植用同样的 MSVC 线性同余，种子取当前时间。

### 7.3 页面切换 / 点击 [V] 与 [A]
* [V] 原版切页没有淡入淡出，也没有等待图标转完：`Cls_5e8190__vf12_OnMessage @0x508b50`（主菜单）与 `Cls_5e8000_vf12 @0x50db90`（single / mult）收到点击后先调菜单对象的 `vf18`（`+0x48`，关闭：隐藏对话框、释放图标精灵、`Particle_ResetAll`、`Camera_RestoreViewState`），随即 `Ui_SetActiveMenuObject(n) @0x509fe0` → `Obj_CreateByTypeAndInit @0x509910`（new 对象 → `vf16` 载入 .ui）→ 删旧对象 → `Cursor_SetVisible(1)`，全部在同一次消息里完成。
* [V] 六边形按钮图标的转动只由鼠标悬停驱动：`Ui_UpdateMenuItemRotation @0x508f60` 比较 `*(DAT_006570d8 + 0x4c8)`（UiMgr 的悬停控件）与该图标的按钮：悬停时角度 `+= dt × 720`（< 90° 时画原图，≥ 90° 换成悬停图并 `+180`，到 360° 停），离开时倒转（< 270° 换回原图 `−180`，到 0° 停）。所以完整的一次翻转是 90° + 90° = **0.25 s**，鼠标先悬停再点击时通常早已转完，页面立刻切换。
* [A] 手机上手指没有悬停：点下去的同时图标才开始转，原样切页会在转动前就换页。移植：触摸点击（`SDL_FINGERUP`、`tap:` 测试令牌）六边形按钮 1..5 时，如果图标还没转完，**等转完（角度 = 360°）再过 0.15 s（`TURN_HOLD`）才执行原来的点击处理**，等待期间图标继续转（即使手指已抬起）；桌面鼠标点击和 `click:` 令牌照原版立即切页。实现：`menu.cpp` 的 `itemsClick / Pending / PointerGuard`。
* [A] 装饰控件（光环 / 高亮图 / 圆点，`menu_anim.cpp` 在每个 UI 里用 `rm8UiAddWidget` 建）全部 `rm8UiSetIgnore(true)`，不拿指针（主菜单曾经就是图标盖住按钮吃掉点击）。六边形图标覆盖层（`itemsInit`）同样忽略指针。

### 7.4 移植实现（[A]）
* `menu_anim.cpp`：`rm8MenuAnimEnter(ui)`（按 UI 名建装饰控件，见上表；控件 Index = 控件 100 的 Index + 1，同 Index 动态控件先于 .ui 里的画，所以先高亮后光环、都在按钮之下）、`Update(dt)`、`Render(cam)`（3D 角色用恒等相机 + 窗口 `0.3×0.225`（宽屏按 4:3 区域换算）画一遍，再用 `rm8UiOverlay*` 画气泡，宽屏时在 4:3 区域外盖黑边，因为原版的菜单就是 4:3）、`rm8MenuAnimSeats`（角色菜单座位）、`Leave`（切页：清飞行角色）、`Reset`（菜单场景新建：光环 / 高亮 / 气泡重置）。飞行角色的 21 个模型在菜单运行中每帧补载一个（避免一次载入卡顿）。
* `ui.cpp / ui.h`（只增）：控件 `rot`（图片绕图心旋转，`quadRot`）、`rm8UiSetRot`、`rm8UiPicSize`、`rm8UiName`、`rm8UiOverlayBegin / Pic / Rect / End`（盖在所有 UI 之上的即时绘制）。`menu.cpp`：`setScreenUi` 前后调 `rm8MenuAnimLeave / Enter`，`rm8MenuUpdate / Render / Open / Close` 转发，角色菜单 `rolesRefresh` 用座位 3D 角色代替六边形头像（加载失败时才用头像）。`CMakeLists.txt` 两个源列表加 `app/menu_anim.cpp`。
* 验证（构建目录 `build-anim`，`--menu --size 1600x720`，`RM8_NOLOGO=1`，`RM8_MENU_SHOTDIR`）：同一页面间隔 1–2 s 的截图，光环区域像素逐张变化（光环辐条角度逐张转动）；`tap:2` 按压序列截图：图标宽度收窄 → 换成悬停图 → 恢复 → 才换页（约 0.4 s）；各页（选项 / 小游戏 / single / 拼图 / 读档 / 地图 / 角色）都用 `tap:` 走真实指针路径进出；角色菜单 3 个座位站立动画（`vf1` 逐帧）。安卓：`build-anim-android` 中 `menu.cpp / menu_anim.cpp / menu_extra.cpp` 编译通过（未打包）。
