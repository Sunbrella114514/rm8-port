# Bink 影片、拼图图鉴、储藏箱（移植说明）

标记：`[V]` 已在反编译里核对（附地址）；`[H]` 推测；`[A]` 移植里的取舍。代码：`app/video.*`、`app/collect.*`、`app/menu_extra.*`，以及 `menu.cpp` / `rm8_app.cpp` / `audio.*` 里的少量接入（见第 6 节）。
反编译来源 `out/final_v2/RichMan8_named.c`，辅助脚本 `scripts/menu_fn.py <地址>`、`scripts/gamedate.py`、`scripts/convert_videos.py`。

## 1. Bink 影片（开场、制作人员名单）

### 1.1 逆向结论
* [V] 游戏里只有两部 Bink：`Bink\Start.bik`（开场，800x600、30 fps、106 s、单声道 32 kHz）和 `Bink\Staff.bik`（制作人员名单，99.8 s、单声道 44.1 kHz）。路径来自 `Movie_LoadStartAnimation @0x516750`：`sprintf("bink\\%s", table + index * 0x100)`，表在 `0x5e92e0`（0 = `start.bik`）和 `0x5e93e0`（1 = `Staff.bik`）。其他剧情影片（通关影片等）不是 Bink，是 Python 脚本驱动的 3D 剧情（`movie.txt`，`Movie_InitEngine @0x515780`）。
* [V] 开场触发：`CApp__vf7_Initialize @0x402f50` 末尾先 `CApp_SwitchToLogoScene @0x4051d0`；logo 第一次淡出结束时 `CLogoScene__vf1 @0x507990` 调 `App_SwitchToStartMovieOrMenu @0x507b70`：`CApp_SwitchToStartMovie(0) @0x4052c0` 失败（返回 0）才进主菜单（状态 1）。
* [V] `init.txt` 的 `binkplay`：`InitTxt_GetParam(10)`（配置偏移 0x28，`InitConfig_LoadInitTxt @0x401080`，缺省 1）为 0 时 `CApp_SwitchToStartMovie` 直接返回 0，不播放（开场和名单都受它控制）。本机 `Config\init.txt` 为 `binkplay=1`。
* [V] 播放流程：`Movie_LoadBinkVideo @0x516330`（BinkOpen，Bink 自己的 DirectSound 声音，BinkBufferOpen 800x600，窗口不是 800 宽时 `BinkBufferSetScale` 拉伸到客户区）→ 每帧 `CStartMovieScene__ProcessBink @0x516830` → `Movie_UpdateBinkPlayback @0x5165d0`（`BinkWait` 未到时间就 `Timer_ThrottleSleep`，到了 `Movie_RenderBinkFrame @0x5161a0`：BinkDoFrame / CopyToBuffer / BufferBlit / NextFrame）→ 最后一帧显示完 `Movie_FinishBinkPlayback @0x516890`。
* [V] 结束后去向：`CApp_SwitchToGameScene((index != 1 ? -7 : 0) + 8)`，即开场影片 → 主菜单（状态 1），制作人员名单 → single / mult 菜单（状态 8）。
* [V] 跳过：场景的 vf11 `@0x407a20` 就是 `Movie_FinishBinkPlayback`。`CApp_HandleInputEvent @0x405cf0` 在鼠标右键按下（`CApp_OnMouseButton` 码 0x120）和键码 0x80 时调它；键码 0x80 = DirectInput 扫描码 1（Esc），查表 `0x5df5f0`。左键与其他键没有反应。[A] 移植：Esc / 返回键、点击、触屏轻点都跳过（手机没有右键）。
* [V] 制作人员名单谁触发：`Movie_Finish @0x515d40`（一段剧情影片结束时）：标志 `+0x2c != 0` → `CApp_SwitchToStartMovie(1)`，失败回状态 8。标志来自 `CApp_PlayMovieRange(role - 1, 1)`（`@0x405400` → `Container_PushRangeData @0x515c50`），只有游戏状态 5（故事模式通关，`CGamePlayScene @0x4a17b0` 里 `Event_SetTerrainData(0x10, 1)`，角色 15 另加 `(0xc, 1)`）这一处第二个参数是 1。影片欣赏（finalMenu）用参数 0，播完回状态 7，不放名单。即：**名单只在故事模式通关后出现**，故事模式不在移植范围，所以移植里没有正常入口（见 1.4）。
* [H] `Movie_LoadBinkVideo` 全屏时会读写一个 `bink.txt`：存在则 `fclose` 后直接返回失败（上次全屏播放崩溃遗留的标记），否则新建，播放成功后删除。看起来是“上次崩在影片里就不再播放”的保护。移植不复刻。

### 1.2 实现
* 离线转换：`scripts/convert_videos.py`（ffmpeg 9.0.2 essentials，下载在 `<tools>\ffmpeg_dl\`，SHA-256 与官网公布值一致，解压到 `<tools>\ffmpeg\`，只运行其中的 ffmpeg / ffprobe）。参数：`mpeg1video -q:v 5 -g 15 -bf 2 yuv420p -aspect 4:3` + `mp2 128k`（声音保持原采样率与单声道）。输出 `port/scenes/video/start.mpg`（75.6 MB）、`staff.mpg`（59.4 MB）。脚本转完会用 ffprobe 核对：帧数 3180 → 3180、2993 → 2993，时长差 < 0.1 s（实测 106.00 → 105.95 s、99.77 → 99.72 s），否则报错。与源（解码后的 Bink）的 SSIM：q5 约 0.987（前 10 s）。游戏目录只读，没有修改。
* 解码：`extern/pl_mpeg/pl_mpeg.h`（phoboslab/pl_mpeg，MIT，直接下载放在自己的目录）。
* `app/video.cpp`：`rm8VideoOpen / Update / Render / Playing / Close`。画面写进一张 800x600 的 RGBA 纹理（librw GL3 的 raster 自下而上，所以倒着写），按 4:3 留黑边铺满屏幕（[A] 原版拉伸到 4:3 客户区）。
* 声音走移植的混音器：`audio.cpp` 新增通用的推送流 `rm8AudioStreamOpen / Push / Played / Queued / Close`（浮点交错采样，线性重采样到 44.1 kHz，在 SDL 回调里混合；音量 [A] 满格，原版 Bink 也是自己的满音量）。
* 同步：声音时钟为主时钟。`Update` 把声音队列补到 0.4 s，视频帧按 `audio 已播放秒数` 取：时间到的帧解码并显示，已经迟了的帧解码后丢弃。没有声卡时退化为墙钟。这样画面与声音天然同步，后台暂停（`rm8AudioPause`）时时钟一起停。
* 菜单接入：`menu_extra.cpp` 的影片界面 = 一个全屏透明按钮的画布，点击 / 轻点 / Esc 都是跳过；开始时停菜单音乐 [A]；结束后按 1.1 的去向（主菜单 / single-mult 菜单，后者先播菜单音乐 101）。`binkplay` 读游戏 `Config\init.txt`（`RM8_NOVIDEO=1` 强制关闭，[A] 测试用）；`scenes/video/*.mpg` 缺失或打不开时按 `0x507b70` 的回退直接进菜单。`RM8_NOLOGO=1` 会连开场一起跳过。
* 公开接口：`rm8MenuPlayStaffCredits()`（`menu.h`）：故事模式通关逻辑接入时调用。

### 1.3 验证（桌面，build-media）
* 开场整段（`RM8_MENU_AUTO=opening,...`）：日志 `video: finished, 3179 frames decoded, 3179 shown, 106.02 s (audio 106.02 s), worst lag 0.114 s`，随后进入主菜单（截图 `menu_after.png`）。帧数 3179 / 3180：pl_mpeg 少吐出最后一帧（B 帧收尾），不影响时长。画面起始、中段抽帧都正确（月亮 / 俯视街景，方向没有颠倒）。
* 制作人员名单：能打开、淡入正确；跳过（`back` 令牌 = Esc）→ 回 single / mult 菜单。
* 声音：设备正常打开（`audio: ... 44100 Hz`），声音时长 106.02 s 与视频一致；实际听感未做（本环境无法听）。

### 1.4 没做 / 待接
* 制作人员名单没有正常入口（故事模式通关）。测试：`RM8_MENU_AUTO=staff`。
* 没有复刻 `bink.txt` 崩溃保护、全屏拉伸（移植保持比例）。
* 手机上 135 MB 的 mpg 要经 `android/deploy.py` 推（已加 `scenes/video` 目录）；pl_mpeg 在手机上的解码速度未实测（800x600，主线程）。

## 2. 拼图图鉴（picmenu，状态 10）与进度存档

### 2.1 进度数据在哪（逆向）
* [V] 拼图和“角色等级 / 解锁”都在 **`Config\gamedate.bin`**（181 个 int32，724 B；`GameDate_Load @0x4a99e0` / `GameDate_Save @0x4a9d60`，格式见 `docs/README.md` 2.5 和 `scripts/gamedate.py`）。反编译里它的访问函数叫 `Event_*`，其实是“GameDate”对象（`@0x61ae08`）：

| 访问函数 | 地址 | 对象偏移 | 含义 |
|---|---|---|---|
| `Event_GetTerrainData / SetTerrainData` | 0x4aa1e0 / 0x4aa200 | `role * 0x40` | 角色解锁标志（1..10 初始即 1，11..16 为 0） |
| `Event_GetLevel / SetMaxLevel` | 0x4aa230 / 0x4aa250 | `role * 0x40 + 4` | 故事进度 0..4（只升不降；finalMenu 要求 ≥ 4） |
| `Event_GetItemData / SetItemData / CountOwnedItems` | 0x4aa2e0 / 0x4aa300 / 0x4aa290 | `role * 0x40 + 0xC + k * 4`，k = 1..9 | **拼图第 k 块**（`gamedate.py` 里叫 cleared 的那 9 个 int 就是它） |
| `Event_GetEventByType / SetEventData` | 0x4aa190 / 0x4aa1b0 | `0x800 + i * 4` | 地图 1..11 的解锁标志（`Game_PopulateMapList @0x503510`；初始除 3 外都是 1） |
| `Event_GetQuestData / SetQuestData` | 0x4aa5b0 / 0x4aa5d0 | `0x840 + i * 4`，i = 1..9 | 小游戏最高分（ltgameMenu 的 HI-SCORE，`@0x507f20`；小游戏宿主写入，取较大值，上限 9999） |
| `Event_SetBitFlag` / `SetFlag2` / `SetFlag3` / `IncrementCounter` | 0x4aa380 / 0x4aa470 / 0x4aa510 / 0x4aa420 | `0x880 / 0x888 / 0x88c / 0x884` | 角色解锁链：位 0..10 全满 → 解锁角色 15（0xf）；flag2 的 0..3 位 → 角色 11（0xb）；flag3 按 0、1、2 顺序 → 角色 14（0xe）；计数 > 3 → 角色 13（0xd）；故事通关 → 角色 16（0x10），角色 15 通关另加角色 12（0xc）。触发它们的游戏事件 [H] 多在故事模式 |

* [V] 每次修改都立即 `GameDate_Save`（整文件重写）。布尔值存成 exe 里的魔数表（`0x5E3D44 / 0x5E3D84 / 0x5E3DC8`，`0x2e1` / `0x112` 两个地图标志）。`Event_InitializeData @0x4aa0b0` 是文件缺失时的初值。
* [V] 拼图怎么得：
  * 板上 NPC 17（拼图 NPC，类 `0x5e2b50`，对象 0x98 字节）：`Cls_5e2b50__OnRoleClick @0x49d100`：角色点击它时 `Role_EnqueueMsgAt(0, 0x53, 0x41, 0)` + `Role_EnqueueMsgAt(1, 0x58, NPC+0x94, 0)`，`NPC+0x94` 就是这块拼图的编号（1..9；`Event_SpawnNpcInstance @0x49dd20` 创建时传入，存档时写在 NPC 17 的额外 int，`Cls_5e2b50__SaveState @0x49d070`）。
  * 生成：`Event_ProcessLanding @0x49db20`，**只在故事模式**（`setup+0 != 0`）：每关每角色的配置（角色记录 `+0x3a0` 列表，`+0x28 / +0x2c` 两个编号），对还没拥有的拼图（`Event_GetItemData == 0`）生成 NPC 17。
  * 宝箱 NPC（`CPlayerTag__OnTreasurechest @0x48f9d0`，故事模式）把配置里（`+0x48 / +0x4c`）没拥有的拼图也用消息 0x58 发出。
  * 任务成就：`Role_CheckMissionsByCharacter @0x4835e0` → `CPlayerTag__OnStockMarketAccess @0x491a10(拼图号, 条件号)`（净资产 > 999999、点券 ≥ 500、街道全买…共 0x13 种条件，按角色与故事章节表选），满足时发消息 0x58。仅故事模式、座位 0。
* [V] 消息 0x58 的处理 = `CPlayerTag__OnPuzzleCardUse @0x491830`（拼图号 1..9）：`Event_SetItemData(角色, 拼图号)`（原来为 0 才置 1 并存盘），再 `sprintf(str434 "%s獲得%d號拼圖。", 角色名, 拼图号)`（反汇编 `0x4918c6..0x4918e6`：两个参数，名字来自 `CPlayerTag__GetNameReference @0x492e00`），`Msg_CreateTextEntry(文字, 0, -1, 图片 uvlist = (角色-1)*9 + 0x12d + 拼图号)`，`Role_EnqueueMsgAt(0, 0x1b, …)` 放在队首的提示框。
* [V] 拼图用途：`Cls_5e8140_UpdateRolePanel @0x505a80`：角色等级 ≥ 3 且 `Event_CountOwnedItems == 9` 才能选该角色的泳装造型（按钮 0x17）；服装按钮 0x13..0x16 随等级（0..3）解锁。字符串 171 就这么写。

### 2.2 picmenu 界面（`picmenu.ui`）
* [V] `Cls_5e7f60__vf16_ShowPicMenu @0x50ad50`：标签 30 = string 171；`PicMenu_FillItemSlots(0) @0x50af60`（拼图格 0x15..0x1d 全清）；按钮 1..10 可用，11..16 在 `Event_GetTerrainData(role) != 0` 时可用。
* [V] `Cls_5e7f60__vf12 @0x50aed0`：点角色 1..16 → `PicMenu_FillItemSlots(role)`：格 `0x14 + k`（k = 1..9）显示 uvlist `(role - 1) * 9 + 0x12d + k`（拥有）或清空；0x12 返回 → `vf11 @0x50af10` → `Ui_SetActiveMenuObject(8)`。入口：single / mult 菜单的按钮 3（`Cls_5e8000_vf12 @0x50db90`）。
* [A] 角色 11..16 在“已解锁，或已有拼图”时可点（移植的角色菜单全部角色可玩，所以拿到了拼图的未解锁角色也能看）。
* [A] 获得拼图的提示框只有文字，没有原版里的拼图小图（移植的 0x1B 提示框只有文字）。

### 2.3 实现（`collect.cpp`）
* 进度数据放在数据目录（`scenes` 的上一级，和 `rm8_settings.txt` 同处）的 **`rm8_gamedate.bin`**，格式与原版 `gamedate.bin` 字节兼容（魔数表照抄 exe，已用 `scripts/gamedate.py dump` 读回验证）。游戏目录只读，不写回。文件不存在时从 `Event_InitializeData` 的初值开始；`RM8_IMPORT_GAMEDATE=1` 且没有移植进度文件时，一次性读 `Config\gamedate.bin`（测试：本机那份是“全通关”）。
* 读取接口（`collect.h`）：`rm8ProfileLevel`（Event_GetLevel）、`rm8ProfileSetMaxLevel`、`rm8ProfileRoleUnlocked / UnlockRole`、`rm8ProfileMapUnlocked`、`rm8ProfileQuest / SetQuest`（小游戏最高分）、`rm8PuzzleHas / Count / Pic`。角色菜单的服装 / 泳装可以直接用 `rm8ProfileLevel` 与 `rm8PuzzleCount`（未接，见 5）。
* **`rm8PuzzleCollect(seat, piece)`**：NPC 17 被角色拾取时调用（`piece` = NPC+0x94）。做 `Event_SetItemData`（保存）+ 队首 0x1B 提示框（string 434，角色名 + 编号）；返回是否新拼图；非法编号（不在 1..9）返回 false 且什么都不做；已拥有的拼图不改数据但仍弹提示（原版不检查）。另有 `rm8PuzzleGrant(role, piece)`（无座位、无消息，测试 / 其他模块用）。
* `rm8_app.cpp` 启动时调 `rm8CollectInit(数据目录, 游戏目录)`（不依赖菜单，`--map` 等测试启动方式也有持久化）。

### 2.4 验证
* `piece:5:3,piece:5:7,piece:5:3,piece:5:10,piece:12:1`（测试令牌）：第 3、7 块新、重复的 3 与非法的 10 拒绝；`rm8_gamedate.bin` 724 字节，`gamedate.py dump` 读回为角色 5 `[3, 7]`、角色 12 `[1]`；重启后图鉴仍显示这些块（截图 `menu_p12.png`，角色 11、13..16 灰掉）。导入原版“全通关”进度后图鉴 9 块拼出完整人像（`menu_pic1.png`）。
* 测试后已删除试验用的 `rm8_gamedate.bin / rm8_box.bin`。

## 3. 储藏箱（boxmenu，状态 0xB）

### 3.1 逆向结论：它是故事模式的“携带箱”
* [V] 数据对象 `&DAT_006405d0`（`Game_GetBinaryDataManager @0x494110`，类名在反编译里叫 BinaryData；存档里的“sflb”段，`Game_SaveBinaryData @0x4abd60`，仅 `setup+0 != 0` 即故事模式写入，84 字节）：

| 偏移 | 内容 | 存档 |
|---|---|---|
| `+0` | 到达过的最高关卡（`Game_UpdateRecordStats @0x4abe20`，同时 `Event_SetMaxLevel(角色, 关)`） | 4 B |
| `+4` → | `{存放现金, 携带现金}`（`Game_AllocateBinaryData @0x4abc90`） | 8 B |
| `+8` → | `{存放点券, 携带点券}` | 8 B |
| `+0xC` | 存放卡片 8 格（card.bin Id，0 空） | 32 B |
| `+0x2C` | 携带卡片 8 格 | 32 B |

* [V] 取得：宝箱 NPC（NPC 11，`CPlayerTag__OnTreasurechest @0x48f9d0`）按关卡配置给“存放”侧加现金（string 7251 “獲得%d元現金”）、点券（7252）、卡片（最多 4 张，string 7253 “獲得%s”）；关卡里把手牌放进箱子：`Role_StoreCardToBox @0x488de0`（扔牌对话框选项 2，`Cls_5e3ca8__Event_OnThrowCardSelection @0x4aeb20`；满 8 张提示 string 112 “儲藏箱已滿”，`CardArray_FindSlotAndAdd @0x4abed0`）。
* [V] 使用：选关菜单（chsMapMenu）故事模式的按钮 0x13（`Game_InitOptions @0x5031c0` 在非故事模式隐藏它）→ 状态 0xB。界面（`boxmenu.ui`）两个十字格子：左 = 存放（ID 4..11），右 = 携带（12..19）；标签 22 / 27 = 现金（左 / 右）、25 / 28 = 点券；21、24 是“現金”“點券”（string 7201 / 7202，`@0x501a70`）。
  * 点左边一张卡 → 移到右边第一个空格（`Cls_5e7f10__vf12_RefreshCards @0x502700`，`SlotArray2C_FindSlotAndAdd @0x4abf50`）；点右边一张 → 移回左边（`RefreshSlots @0x5027e0`）；按钮 3 / 2 = 全部 → / ←；按钮 0x17 = 现金（携带 = 存放，存放 = 0，需存放 > 0，`RefreshDisplay @0x502580`）；按钮 0x1a = 点券（`RefreshLand @0x502640`，名字里的“Land”其实是点券）；按钮 1 = 关闭 → 状态 3（`vf11 @0x502000`）。现金 / 点券只有存放 → 携带这一个方向（赋值而不是累加，原版如此，实际不会丢数据，因为携带侧在进入关卡时清零）。
* [V] 生效：`Game_StartNewRound @0x4962a0`（故事模式开局）：携带的卡 → 第一个角色的牌堆（`Role_AddCardToDeck`），携带现金 / 点券加给它（`Role_SetMoney(0, …)` / `Role_SetTicketAmount`，连同关卡自己的起始钱 / 牌），然后 `u_Obj_ResetSlotArrays @0x4abfd0` 清空携带侧。离开选关菜单（`Cls_5e81e0_ShowGameMenu @0x504310`，故事模式）：`u_Obj_ApplyPendingChanges @0x4ac020` 把携带侧并回存放侧，再回角色菜单（状态 2）；`vf11_OnBack @0x5042a0` 在 `box.stage > 0` 时禁止返回。选角色时（`Cls_5e8140__vf12 @0x5065f0` 添加角色）`Game_ClearBinaryData @0x4abcc0` 清箱子（新的故事存档）。
* 结论：**箱子是故事模式的跨关卡携带系统**，内容来自故事关卡的宝箱 / 扔牌，随故事存档保存。自由模式不用。

### 3.2 实现
* 数据与操作在 `collect.cpp`：`Rm8Box`（布局同上）、`rm8BoxStoreCard`、`rm8BoxAddReward`（宝箱奖励）、`rm8BoxMoveCash / MovePoints / MoveCard / MoveAll`、`rm8BoxMergePending`、`rm8BoxApplyToSeat`、`rm8BoxClear`。持久化 [A]：数据目录的 `rm8_box.bin`（84 字节，布局同存档的 sflb 段），每次改动都写；存档模块以后可以直接把这 84 字节放进故事存档。
* 界面 `menu_extra.cpp`：照 `boxmenu.ui` 与上面的逻辑；卡片图 `prt\card\Dxx.tga`；鼠标悬停卡片显示名称 + 说明（[A]，原版的悬停回调在控件 100）。入口：选关菜单的按钮 0x13，**只在 single（故事）菜单的新游戏里可见**（与 `Game_InitOptions` 一致）；返回回选关菜单（状态 3）。
* 生效 [A]：`rm8_app.cpp` 的 `startFromMenu`：`setup.story` 时开局后 `rm8BoxApplyToSeat(0)`（携带的卡 / 钱 / 点券给 0 号座位，携带侧清零）。关卡自己的起始钱 / 牌（pass 配置）不在这里。
* 验证：`single,click:1,boxfill,click:19,…`：左侧 3 张卡 + 现金 30000 + 点券 120；点按钮 0x17、0x1a、3 后全部到右侧（现金 / 点券归零 / 3 张卡）；点右边第一张回到左边；关闭回选关菜单（截图 `menu_box1.png` / `menu_box3.png`）。

### 3.3 没做 / 待接
* 故事模式本身（角色 → 关卡 → pass 配置、宝箱 NPC、扔牌选项、关卡结算、通关影片、箱子清空 / 并回、`Game_UpdateRecordStats` 与 `Event_SetMaxLevel`）。移植里 single 菜单仍走自由模式流程；箱子只能靠测试令牌 `boxfill` 或后续的故事模块（调用 `rm8BoxAddReward / rm8BoxStoreCard`）填充。
* 箱子没有放进存档文件（存档模块未做）。界面里 ID 100 / 101 的装饰角色图（`vf12_SetupChars @0x501c30`，uvlist 0x402 / 0x403）没画。

## 4. finalMenu（影片欣赏，状态 7）
* [V] `Cls_5e8050__vf16 @0x506fc0`：按钮 1..16 在 `Event_GetLevel(role) >= 4` 时可点；点击 `Cls_5e8050__vf12 @0x507100` → `CApp_PlayMovieRange(role - 1, 0)`（Python 脚本剧情，不是 Bink）；0x12 返回（状态 8）。**未做**：通关影片是脚本 3D 剧情，范围外。读取接口 `rm8ProfileLevel` 已提供。

## 5. 共享文件改动
* `app/audio.h` / `audio.cpp`：新增 `rm8AudioStream*`（推送流，混音回调里 `mixStream`；无 SDL 的构建里是空桩）。
* `app/menu.h`：加 `rm8MenuPlayStaffCredits()`。
* `app/menu.cpp`：`#include "menu_extra.h"`；`Scr` 加 `S_EXTRA`；logo 结束 → `goExtra(RM8X_MOVIE_START)`（原来直接进主菜单）；single / mult 菜单按钮 3 → picmenu；选关菜单按钮 19（0x13）故事模式可见、点击进 boxmenu；`go()` 后加 `g_extraEnv` / `goExtra`；`rm8MenuRender` 先画影片；`autoStep` 加扩展令牌；`rm8MenuInit` 调 `rm8MenuExtraInit`；`rm8MenuPlayStaffCredits`。
* `app/rm8_app.cpp`：`#include "collect.h"`；启动时 `rm8CollectInit`；`startFromMenu` 里故事模式 `rm8BoxApplyToSeat(0)`。
* `CMakeLists.txt`：两个源列表加 `app/video.cpp app/menu_extra.cpp`。`android/deploy.py`：推送 `scenes/video`。
* 新文件：`app/video.*`、`app/menu_extra.*`、`app/collect.*`（原桩）、`extern/pl_mpeg/pl_mpeg.h`、`scripts/convert_videos.py`、`scripts/unzip_to.py`（安全解压）、`port/scenes/video/*.mpg`。

## 6. 测试
`RM8_MENU_AUTO` 新令牌：`opening`（开场影片）、`staff`（制作人员名单）、`boxfill`（往箱子放演示内容）、`piece:<角色>:<n>`（`rm8PuzzleGrant`）；其余沿用（`click:3` 在 single / mult 菜单 = picmenu，`click:19` 在故事选关菜单 = 箱子，`back` = Esc）。`RM8_NOVIDEO=1` 关闭影片，`RM8_IMPORT_GAMEDATE=1` 导入原版进度。
例：`RM8_NOLOGO=1 RM8_MENU_SHOTDIR=. RM8_MENU_AUTO="single,wait:5,click:3,wait:5,click:5,shot:p5" rm8_app <game> scenes --menu --frames 300`。

## 7. 未做 / 待定
| 项 | 说明 |
|---|---|
| 故事模式 | 箱子的来源（宝箱 / 扔牌）、拼图的故事来源（NPC 17 生成、任务成就）、通关 → 制作人员名单 |
| 拼图 NPC 17 | 板上部分由 NPC 模块调用 `rm8PuzzleCollect(seat, NPC+0x94)`；自由模式原版不生成 NPC 17（`Event_ProcessLanding` 仅故事模式） |
| 角色菜单 | 服装按 `rm8ProfileLevel`、泳装按 `rm8PuzzleCount == 9` 解锁（`@0x505a80`）尚未接入；地图解锁 `rm8ProfileMapUnlocked` 同 |
| 小游戏最高分 | `rm8ProfileQuest / SetQuest` 已提供，ltgameMenu 的 HI-SCORE 显示与小游戏写入未接 |
| finalMenu | 通关影片是脚本剧情，未做 |
| 手机 | 影片解码速度与声音延迟未在手机实测；Android 构建已编译（build-media-android） |
