# 点击背景加速 / 跳过（原版的 vf13 机制）

> 目的：原版在棋盘上等待（NPC / 角色对话框、信息框、特效动画、事件界面、加盖动画、破产动画、神出场）时，点击背景（或按几个键）会提前结束等待。本文从反编译还原**一次点击到底做了什么**，并说明移植版的实现（`app/skip.cpp`）。
> 依据：`out/final_v2/RichMan8_named.c`（函数名只作索引，结论按代码和反汇编判断；`scripts/fnv2.py` 看函数，`scripts/exe_dis.py` 看汇编，`scripts/consts.py` 读 vtable / 键表）。
> 置信度：**[V]** 已读代码或反汇编核对；**[H]** 代码证据强，含义是推断；**[M]** 只读了一部分。地址写作 `@0x…`。

## 0. 结论

1. 原版**没有渐进式的“加速”**。点击的效果全是“**跳到结束**”：把等待计时清零、停掉附属的音效 / 特效，让状态机立刻进入下一步。唯一一种“加速”是按 `.` 键（第 4 节）。
2. 入口 `CApp_OnMouseButton @0x4059F0`：**每次鼠标左键按下都调用场景的 vf13**（`CGamePlayScene__vf13_StopMoving @0x4A3060`，名字不准，实际是“跳过当前等待”），**与点在哪里无关**（点在 UI 控件上也调，只是 UI 吃掉点击时不再调场景的点击处理 vf9）；右键按下先调 vf13，没处理再调 vf11（取消模态框）；小键盘回车（键码 0xA7）合成左键按下，所以也跳过。
3. vf13 没有任何“只对人类”“只在某个状态”的判断。它按固定顺序问：**场景任务**（事件界面 / 加盖动画）→ **第一个在等待的角色**（任何座位，人或电脑）→ **神的出场演出**。一次点击只处理其中一层的一个对象。

## 1. 输入路径 [V]

### 1.1 鼠标

窗口过程 `CApp__vf4_WndProc @0x403AA0` 把 Windows 消息译成按键码，调 `CApp_OnMouseButton(code) @0x4059F0`：

| 消息 | code | 场景 vf9（+0x24） | vf13（+0x34） |
|---|---|---|---|
| WM_LBUTTONDOWN 0x201 | 0x110 | 调（UI 已吃掉点击，即 this+0x238 != 0 时**不调**） | **无条件调**（返回值忽略） |
| WM_LBUTTONUP 0x202 | 0x10 | 同上 | 否 |
| WM_RBUTTONDOWN 0x204 | 0x120 | 调 | `CApp_HandleInputEvent(0) @0x405CF0`：先 vf13，返回 0 才调 vf11（+0x2C，`CGamePlayScene__vf11 @0x4A2CF0`：把状态 7 的角色的“取消”标志 +0x228 = 0、+0x224 = 1，即模态框按“否”） |
| WM_RBUTTONUP 0x205 | 0x20 | 调（0x20 位总是调） | 否 |
| 中键 0x207 / 0x208 | 0x140 / 0x40 | 调 | 否 |
| 滚轮 0x20A | — | `CApp_OnMouseWheel @0x4059A0` → 场景 vf8 `CGamePlayScene__OnMouseClick @0x4A2840`（只是缩放 / `AI_SetWaitFlag`，不跳过） | 否 |

场景 vf9 = `CGamePlayScene__OnKeyInput @0x4A28B0`：左键按下 → `Game_HandleCardClickInGame @0x4A2940`（状态 6 选卡目标、状态 7 选建筑）；左键松开 → `Game_HidePlayerInfoPanel @0x4A2AD0`。**这两个才是“点击场景”，跳过不经过它们。**

### 1.2 键盘

`CApp_OnKeyDown @0x405A90`，键码由 DirectInput 表 `0x5DF5F0` 转换（`DInput_PollKeyboardBuffered @0x401B20`，已读出表）：

| 键 | DIK | 键码 | 效果 |
|---|---|---|---|
| 小键盘回车 | 0x9C | 0xA7 | **合成左键按下**：给 UI 覆盖层发 WM_LBUTTONDOWN，再调 `CApp_OnMouseButton(0x110)`，即同样触发 vf13（松开时合成 0x202 / 0x10） |
| Esc | 0x01 | 0x80 | `CApp_HandleInputEvent(1)`：**先 vf11（取消模态框），没处理再 vf13（跳过）** |
| 空格 / 回车 | 0x39 / 0x1C | 0x20 / 0xAB | 场景 vf5 `CGamePlayScene__OnEvent @0x4A1E80`：当前角色状态 6 → `Role_StartRollDice(0)`，否则 vf10（`@0x4A2BA0`：状态 7 的模态框按“是 / 确定”）。**不跳过** |
| `.` | 0x34 | 0x2E | 场景 vf5：`InitConfig_SetSpeed(0x32) @0x406180`（第 4 节） |
| `-` `=` / 小键盘 `-` `+` | | 0x2D 0x3D / 0xA5 0xA6 | 镜头缩放，不相关 |

没有任何别的键触发 vf13。

## 2. 场景 vf13：一次跳过做什么 [V]

`CGamePlayScene__vf13 @0x4A3060`（vtable `0x5E00E8` 槽 13）：

```
if (scene+0x34 != 0)                        // 场景任务（事件界面、加盖动画、新闻动画…）
    return task->vf13();                    // 只问任务，不再问角色和 NPC
if (g_RoleMgr->Role_TryStopMoveUntilDone()) // @0x4986B0，按座位顺序，第一个成功的就停
    return 1;
return NPC管理器->Collection_FindFirstByVf60();   // @0x49DEA0，对每个 NPC 调 vtable+0x60，首个非 0 返回 1
```

### 2.1 角色：`Role_StopMoveAndIdle @0x484790`

只对状态 `+0x1B8` 为 **5（等待）/ 0xD / 0xE（动画片段 + 等待）** 的角色生效，否则返回 0：

1. `+0x1E8`（等待计时）= 0；
2. `Anim_StopSample @0x47B030`：停掉角色的语音样本（`+0x50`，`Anim_PlaySound @0x47AFF0` 记下的句柄）；
3. `Anim_ReleaseSprite @0x47B240`：释放角色的特效摆件（`+0x54`，`Anim_CreateSprite @0x47B090` 创建）和特效的音效（`+0x58`）；
4. 调 `*(this+0x4C)` 的 vtable[0]（`Cls_5e3140__vf0 @0x47B310`，“检查并清零”`+0x5C`：姿势标志）；[H] 等待自然结束时（`Role_TickWaitTimer @0x4850B0`）也调它；
5. 状态 0xD / 0xE（电影片段）还要：棋盘场景标志 `DAT_0061ade0+0x350 == 9` 时 `CDigitalAudio_StopBGMWithFade(0)` + `ManageBGMTransition(-5)` 恢复棋盘音乐，并 `MovieClip_List_Destroy`；
6. 状态 = 8，返回 1。下一帧状态机取队列里的下一条消息（例如信息框的 0x1C“隐藏”）。

等待本来怎么结束（对照）：`Role_TickWaitTimer @0x4850B0` 每帧 `+0x1E8 -= dt`，到 0 **且** `+0x50 == 0`（语音已停）**且** `+0x54 < 0`（特效已释放）才回状态 8。所以原版的等待 = 计时、语音、特效三者都结束；跳过 = 三者一起强制结束。

**哪些等待是状态 5**（都被跳过）：消息 0x65（`Role_BeginWaitSeconds @0x485080`，参数帧数 / 60 再乘速度系数 `Anim_ApplyDifficultyScale @0x47B3B0` = `1 − min((speed − 1)/10, 1)`，speed 默认 4 → 0.7）。0x65 的来源：信息框 0x1B（`Role_ShowInfoDialog @0x488AE0`：队首插 `0x65(180)` + `0x1C`，同时 `Anim_PlaySound` 播语音）、说话 0x5A（经 `Role_ShowInfoDialog`）、特效 0x2C / 0x52（`Role_ShowEffectAnimAtSelf @0x48B2B0` / `AtTerra @0x48B320`，`0x65(60)`，特效摆件 10 s / 4 s 寿命）、0x53（`Role_MsgHandler_PlayAnimOrWait @0x48E8C0`）、每回合末尾的 `0x65(60)`、领卡框 `0x65(60)`、事件落点后的 `0x65(30)` 等。状态 0xE：破产动画 0x7F（`CPlayerTag__OnMovieDrama @0x492F30`，14 s × 速度系数）；状态 0xD：剧情开场 0x7E（`@0x492E40`）。

**不会被跳过的**：掷骰（状态 3）、走路（状态 1）、模态框（状态 7，等玩家）、状态 6（等玩家掷骰）、状态 9 里没有 vf13 的任务（公园、卡片点、点券点、系统菜单任务、小游戏宿主）、状态 10（NPC 演出，除了下面的神）。

### 2.2 场景任务的 vf13 [V]

`CreateGameObject @0x4B1BC0` 建的任务挂在场景 +0x34。任务基类 vtable 槽 13（+0x34）默认是 `Obj_Return0 @0x4F6230`。各类的实现（`+8` 是等待计时，语音句柄在各类自己的偏移）：

| 事件 / 任务 | vtable | vf13 | 条件（不满足 = 返回 0，不跳） | 动作 |
|---|---|---|---|---|
| 1 卡片商店 | 0x5E4A10 | `@0x4E6A20` | 阶段 `+0x64 == 2`（购物中）且 `+0x50 == 0`（无语音）→ 不跳 | 计时 `+4` = 0，停语音 `+0x50` |
| 2 问答点 | 0x5E46F8 | `@0x4BF350` | 无条件 | `+8` = 0，停语音 `+0x24` |
| 5 / 99 银行 | 0x5E4740 | `@0x4C44F0` | 阶段 `+0x3C == 1`（菜单）且 `+0x20 == 0` → 不跳 | `+8` = 0，停语音 `+0x20` |
| 6 乐透 | 0x5E4788 | `@0x4C7AE0` | `+0x34 == 2` 且 `+0x20 == 0` → 不跳 | `+8` = 0，停语音 |
| 0xD 魔法屋 | 0x5E47D0 | `@0x4D3860` | `+0x4C == 1` 且 `+0x28 == 0` → 不跳 | `+4` = 0，停语音 `+0x28` |
| 0xE 法院 | 0x5E4860 | `@0x4D7950` | `+0x90` 为 1 或 2 且 `+0x74 == 0` → 不跳 | `+8` = 0，停语音 `+0x74` |
| 0xF 电力公司 | 0x5E48A8 | `@0x4DA390` | `+0x30 == 2` 且 `+0x34 == 0` → 不跳 | `+8` = 0，停语音 `+0x34` |
| 0x10 点券中心 | 0x5E48F0 | `@0x4DFA90` | `+0x34 == 2` 且 `+0x20 == 0` → 不跳 | `+8` = 0，停语音 `+0x20` |
| 0x50 / 0x51 拍卖 | 0x5E4980 | `@0x4E4F50` | 无条件 | `+8` = 0，停语音 `+0x6C` |
| 0x5A~0x5C 奇迹 | 0x5E4938 | `@0x4E15B0` | 无条件 | `+8` = 0，停语音 `+0x128` |
| 0x62 乐透开奖 | 0x5E4818 | `@0x4CA120` | 无条件 | `+8` = 0，停语音 `+0x20` |
| 加盖 / 拆房动画（`NPC_CreateAndDisplay @0x4F4B50`） | 0x5E6618 | `@0x4F4EC0` | 无条件 | vf4 销毁特效摆件，停样本 2（`+0x20`），状态 `+0x18 < 2` 时 `NPC_CompleteInteraction @0x4F4D70`（`Land_OnEnter` 写入新等级、新模型 + 样本 0x7A），vf16 `Building_UpdateBuildingVisual @0x4F4A40`（待机动作，标记完成） |
| 新闻 / 抓捕动画（`NPC_CreateRandom_Type1..3 @0x4A4510 / 0x4A45F0 / 0x4A46D0`，art 0x526 / 0x527 / 0x178A [H]） | 0x5E3AF0 / 3B40 / 3B90 / 3BE0 | `RoleAnimation_SkipToEnd @0x4B1320` | 状态 `[6] == 0` → 不跳 | 状态 1 时先触发角色动作（vf17），状态 = 2；时间 `[7]` = 20.0 → 下一帧结束 |
| 0x11 新闻场景 | 0x5E20A8 | `@0x4B4760` | 空实现，返回 0 | — |
| 3 / 4 / 10~12 / 0x61 / 0x65~0x6B / 0x12~0x17 小游戏 | 各自 | `Obj_Return0` | — | 不跳 |
| 剧情旁白（`CUi_SpeakAside`，Python `Movie.SpeakAside`） | 0x5E91C8 | `@0x514CA0` | 无条件 | `+8` = 0，停语音 `+0x44` [M：是否挂在场景 +0x34 没查] |

（“+8 = 0”的含义：任务的 Update（vf1）每帧 `+8 -= dt`，到 0 且语音句柄为 0 才推进下一步，见 `CUi_bank__Update @0x4C1C40`。清零计时 + 停语音 = 下一帧就推进；**不是**直接跳到界面结束：银行的欢迎语被跳过后进入菜单，等玩家操作。）

`0x5E81E0` 的 vf13 `@0x5041F0`（旧名 OnSkipMovie）属于前端菜单场景（选图 / 选项菜单，`docs/menus.md`），不在棋盘场景里，本文不处理。

### 2.3 神的出场演出（NPC vtable +0x60）[V]

NPC 的 vtable 槽 +0x60：神（npcinfo 1..6，vtable 0x5E33A8 / 3448 / 34E8 / 3588 / 3628）是 `Cls_5e33a8__OnReady @0x49BAD0`，其余 NPC 类都是 `Obj_Return0`。神：状态 `+0x68 == 1`（互动中）且阶段 `+0x8C < 3` 时：计时 `+0x90` = 0、停样本、释放特效摆件（出场特效 6122）、`+0x8C = 3`（跳过登场动作和问候语，下一帧直接 `Role_SetPossession` + 神自己的效果）。返回 0（所以右键时还会接着调 vf11）。

## 3. 谁能被跳过：汇总

| 等待 | 被跳过的原因 | 说明 |
|---|---|---|
| NPC / 角色说话、信息框 | 角色状态 5（0x65(180)） | 语音停；框由随后的 0x1C 消息隐藏 |
| 特效（0x2C / 0x52）、0x53 音效等待 | 角色状态 5 | 特效摆件被释放 |
| 回合末尾 1 s 等待、落点 0x65(30) 等 | 角色状态 5 | 同上，任何座位（含电脑） |
| 事件界面的开场白 / 结束语 / 结果等待 | 任务 vf13（第 2.2 节） | 菜单等待玩家时不跳 |
| 加盖动画 | 任务 0x5E6618 vf13 | 立即写入新等级 |
| 破产动画 | 角色状态 0xE | 音乐也恢复 |
| 神出场 | NPC +0x60 | 直接进入附身 |
| 掷骰 / 走路 / 模态框 / 小游戏 | 否 | |

## 4. `.` 键：唯一的加速 [V：算术；H：设计意图]

场景 vf5 在键码 0x2E 调 `InitConfig_SetSpeed(0x32) @0x406180`：读 init.txt 的 speed（`InitTxt_GetParam(6)`），小于 0x32 就 +0x32，否则 −0x32，写回配置（只改内存，菜单保存选项时才落盘）。后果：
- 等待系数 `1 − min((speed − 1)/10, 1)` 变成 0：每一个 `Anim_ApplyDifficultyScale` 的等待（0x65、任务的 1.6 s 等）都变成 0；
- 走路的 dt 倍数 `(speed − 1)/4 + 1`（`CPlayerTag__vf3_Update @0x481500`，只在状态 1）：speed 4 → 54 时 = 14.25 倍。

再按一次切回。[H] 这是一个“快进”开关（电脑回合看着太慢时用），原版没有界面提示。

## 5. 移植实现

### 5.1 钩子

- `app/skip.h / skip.cpp`（新文件，已加进两处 CMake 源文件列表）：
  - `bool rm8SkipRequest()`：按第 2 节的顺序：`rm8EventRunning()` → `rm8EventSkip()`（任务 vf13）；否则 `rm8BoardFxBuildSkip()`（加盖动画）；否则 `rm8TurnSkipRequest()`（角色）；否则 `rm8NpcSkip()`（神）。
  - `bool rm8SkipTap(cam, x, y, onUi)`：棋盘上的一次点击。非 UI：`rm8CardsOnTap`（选卡目标，优先）→ `rm8SkipRequest` → `rm8BoardFxTap`（点场景摆件）；UI 吃掉的点击：只调 `rm8SkipRequest`（在 UI 的点击动作之前，和原版一样），返回 false。
  - `void rm8SkipToggleFast()`：`.` 键，`rm8GameSpeed += (< 50 ? 50 : -50)`。
- `roles.cpp`：`int rm8TurnSkipRequest(float *left)`（= `Role_TryStopMoveUntilDone`）：按座位顺序第一个状态 5 的角色：`waitT = 0`、`rm8PresentSkipSeat`、状态 8；再问 `rm8BoardFxGhostSkip`（破产动画，移植里用状态 9 代替 0xE）。`rm8RolesUpdate` 每帧调 `rm8SkipUpdate`（只有测试环境变量时才有动作）。
- `present.cpp`：`rm8PresentSkipSeat(seat)`（`Anim_StopSample` + `Anim_ReleaseSprite`：停该座位的语音，释放它最后一个 0x2C / 0x52 特效，新增 `SeatState::fx`）。
- `boardfx.cpp`：`rm8BoardFxBuildSkip()`（`Cls_5e6618__vf13 @0x4F4EC0`，新增 `BuildTask::sample`：样本 2 的句柄，被跳过时停）、`rm8BoardFxGhostSkip(seat)`。
- `events.cpp / event_int.h`：`ev::Task::skip()`（默认 false），`rm8EventSkip()`、`rm8EventSkipDialogs()`（[A] 领卡框在移植里用计时器隐藏，等待被跳过时一起隐藏）；银行 / 商店 / 问答 / 法院各一个 `skip() override`，条件和动作照第 2.2 节。
- `npc_act.cpp`：`rm8NpcSkip()`（神出场；`godClick` 现在保存特效句柄 `o.fx`）。
- `rm8_app.cpp`：触摸短按（< 250 ms、移动 < 2 %）和鼠标左键点击（没拖动）走 `rm8SkipTap`；鼠标右键点击（移动 ≤ 6 像素）调 `rm8SkipRequest`；小键盘回车调 `rm8SkipRequest`；`.` 调 `rm8SkipToggleFast`。

### 5.2 与原版的差异 [A]

1. 原版在**按下**时跳过；移植在**抬起**时（触屏上按下可能是拖动棋盘）。顺序保持：UI 的点击动作之前先跳。
2. 点击被某一层跳过后，移植**不再**触发“点场景摆件”（`rm8BoardFxTap`）；原版会两个都做。
3. 神的 vf60 对原版点击返回 0（点击继续往下走），移植里吃掉这次点击。
4. Esc 不接：移植里 Esc / 返回键是系统菜单。空格 / 回车仍是掷骰。`.` 和右键只有桌面有，安卓没有键盘 / 右键。
5. 移植的角色等待只数计时（`roles.cpp` 的 `case 5`），不等语音和特效结束（原版 `Role_TickWaitTimer` 要三者都结束）。所以特效 / 语音可能比等待长；跳过时把它们一起停掉。
6. （2026-11-15 已补：警车 / 救护车 / UFO 的点击跳过 `rm8NpcTransferSkip` / `rm8NewsSkip`，`RoleAnimation_SkipToEnd @0x4b1320`；拆房任务 vf13 `@0x4f50e0` 也已接）没做：新闻 / 抓捕动画（0x5E3AF0 系，移植里没有这些任务）、剧情电影状态 0xD、旁白 `SpeakAside`（故事模式的 `movie.cpp` 做完后要接：电影等待放进 `rm8TurnSkipRequest`，旁白任务实现 `ev::Task::skip` 或同级钩子）、乐透 / 魔法屋 / 电力公司 / 点券中心 / 拍卖 / 奇迹等任务（移植里还没有；做的时候照第 2.2 节实现 `skip()`）。

## 6. 验证（桌面，`build-skip\rm8_app.exe`，`--shot` 离屏、固定 30 fps 步进）

测试开关：`RM8_SKIP_TEST="<秒>,…"`（在这些游戏时间调 `rm8SkipRequest`，等于一次点击）、`auto[:周期]`（每 0.4 s 点一次，量加速效果）、`fast@<秒>`（切换 `.` 快进）；`RM8_SKIP_TRACE=1`（日志：每次跳过，每个角色等待的开始 / 结束，事件任务的开始 / 结束）；`RM8_SKIP_TEST_SDL=1`（窗口模式：测试点击改成压进 SDL 队列的触摸按下 / 抬起，走 `rm8_app.cpp` 的完整点击路径）。

命令公共部分：`build-skip
m8_app.exe <游戏目录> D:i
m8\port\scenes --map 01 --size 320x180 --shot out.png --time T`，全部是 4 个电脑角色（`--human 0` 的例外另注），speed = 4（等待系数 0.7）。

| 场景 | 命令附加环境变量 | 不点 | 点 |
|---|---|---|---|
| 说话信息框（0x5A，0x65(180) = 3 × 0.7 = 2.1 s + 随后的 0x1C 隐藏） | `RM8_PRESENT_TEST="1:say:1:7,1.1:cur:1"`，`--time 2.6`，`--size 1024x576` | t = 1.20 起等待，t = 3.33 结束（2.13 s，截图 `port
esults\skip_info_none.png` 在 t = 2.6 仍显示对话框） | `RM8_SKIP_TEST=2.0`：t = 2.03 日志 `seat 1 wait cut short (1.27 s left)`，t = 2.07 回到状态 8；`skip_info_tap.png`（同一时刻）对话框已隐藏 |
| 每回合的 0x65 等待、领卡框、落点 0x65(30) 等（全电脑，300 s，`RM8_START_CASH=500000,0`） | `RM8_SKIP_TEST=auto:0.4` | 角色处于等待的总时间 157~166 s，游戏进行到第 9~10 天 | 204 次等待被缩短，等待总时间不到 10 s（`fast`：9.6 s），游戏进行到第 18 天 |
| 加盖动画（`rm8BoardFxBuildSkip`） | 同上 | `build start`（t = 243.39）→ `build finished` 约 4~5 s | `build start` t = 140.70，同一帧 `sound 0x2` → `build complete` / `sound 0x7A` t = 140.77（0.07 s 后，新等级已写入）→ `build animation cut short` → `build task finished` |
| 事件任务：银行（人类座位 0，`RM8_AUTO_ROLL=1 RM8_EVENT_TEST=bank,shop,ask RM8_EVENT_AUTO=saving,1,00,ok --human 0 --time 60`） | `RM8_SKIP_TEST=auto:0.3` | 银行任务 t = 0.37 开始，t = 5.77 结束 | t = 0.63 欢迎语被跳过（菜单出现，**菜单等玩家时不跳**：后面按 `saving / 1 / 00 / ok` 的存款照常完成，日志 `bank done seat 0: cash 49900 deposit 50100`），t = 3.97 结束；商店 t = 17.73 开始，t = 18.03 / 18.37 开场白被跳过 |
| 破产动画（`RM8_START_CASH=1,0 RM8_BOARDFX_TRACE=1`，14 s × 0.7 = 9.8 s） | `RM8_SKIP_TEST=auto:0.5` | 开始 t = 11.20，结束 t = 21.00 | 开始 t = 5.33，结束 t = 5.83（`wait cut short (0.00 s left)`，音乐恢复） |
| 神出场（`RM8_NPC_PLACE=3@1 RM8_NPC_GO=1 RM8_AUTO_ROLL=1 --human 0 --time 25`，`RM8_DEBUG_NPC=1`） | `RM8_SKIP_TEST=auto:0.2` | meets 衰神 t = 2.40 → becomes 衰神 t = 4.83 | meets t = 2.40 → `god arrival show cut short` t = 2.63 → becomes t = 3.43 |
| 窗口模式的真实点击路径 | `SDL_AUDIODRIVER=dummy RM8_NOVSYNC=1 RM8_SKIP_TEST_SDL=1 RM8_SKIP_TEST=auto:0.5 --human 0 --frames 700`（测试点击 = 压进 SDL 队列的 `SDL_FINGERDOWN` / `UP`，经过 `rm8_app.cpp` 的 UI 优先、选卡目标、跳过） | — | t = 1.51 `wait cut short (1.71 s left)` |

说明：游戏进行天数在两次运行之间会波动（`--shot` 模式下电脑的买地 / 事件用到了别的模块的非确定因素，同一命令两次的 `base` 分别是第 9、10 天，`fast@0.5` 是第 10~24 天），所以“加速效果”以等待总时间为准；每个跳过的日志行（`skip: t=… …`）本身就是证据。

`RM8_SKIP_TRACE=2` 记录每个座位的每次状态切换（排查用）。

## 7. 共享文件改动（均为小段插入）

- `CMakeLists.txt`：两个源文件列表加 `app/skip.cpp`（紧跟 `app/present.cpp`）。
- `turn.h`：`rm8TurnSkipRequest` 声明。
- `roles.cpp`：`#include "skip.h"`；`rm8TurnSkipRequest`（紧跟 `rm8TurnSkip`）；`rm8RolesUpdate` 第一行 `rm8SkipUpdate(dt)`（测试钩子）。
- `present.h / present.cpp`：`rm8PresentSkipSeat`，`SeatState::fx`（0x2C / 0x52 的特效句柄）。
- `boardfx.h / boardfx.cpp`：`rm8BoardFxBuildSkip`、`rm8BoardFxGhostSkip`；`BuildTask::sample`（样本 2 的句柄）；破产动画结束日志加了时间。
- `events.h / events.cpp / event_int.h`：`rm8EventSkip`、`rm8EventSkipDialogs`、`ev::Task::skip()`；`event_ask / event_bank / event_shop / event_court.cpp` 各加一个 `skip() override`。
- `npc.h / npc_act.cpp`：`rm8NpcSkip`；`godClick` 保存特效句柄。
- `rm8_app.cpp`：`#include "skip.h"`；键盘（`.`、小键盘回车）、触摸点击（UI 抬起前 / 棋盘点击改走 `rm8SkipTap`）、鼠标左 / 右键。

## 8. 未做 / 待确认

1. 原版右键 / Esc 在 vf13 没处理时还会调 vf11（模态框按“否”）；移植没有这个映射。
2. 新闻 / 抓捕动画（vtable 0x5E3AF0 系，art 0x526 / 0x527 / 0x178A）、剧情电影（状态 0xD）、旁白、乐透 / 魔法屋 / 电力公司 / 点券中心 / 拍卖 / 奇迹：移植里还没有这些任务；各自照第 2.2 节实现 `ev::Task::skip()`（电影等待放进 `rm8TurnSkipRequest`）。
3. 原版 `Role_StopMoveAndIdle` 的第 4 步（清 `+0x5C` 姿势标志）移植没有做：被跳过后表情姿势保持到下一次姿势变化（与等待自然结束一致）。
4. [H] 原版在按下时就跳过，移植抬起时才跳（防拖动误触）；是否要改成按下，需要在手机上试手感。
5. `CApp_OnKeyDown` 对 Esc 还有 `Cursor` / 其他分支没查。
