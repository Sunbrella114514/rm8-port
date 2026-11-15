# 音频（CAudioMgr / Miles Sound System）与移植实现

状态标记：`[V]` 已在反编译里核对（附地址）；`[H]` 推测；`[A]` 移植里的取舍。反编译来源 `out/final_v2/RichMan8_named.c`（函数名只有一半可信，以代码为准）。
移植代码：`port/app/audio.cpp` / `audio.h`；测试方法见第 7 节。

## 1. 数据包与实际格式

| 包 | 内容 | 实测 |
|---|---|---|
| `Sound/snd0000.dat`（格式 B，`snd%`） | 2400 个样本，ID 1..6430（不连续） | 全部是 RIFF/WAV 的 **PCM 单声道**：1700 个 22050 Hz/16 位、699 个 44100 Hz/16 位、1 个 22000 Hz/8 位（ID 6203）；没有 ADPCM；条目第 4 个字段（压缩后大小）全部为 0；总时长 4464 s（解码后约 787 MB，最长 ID 223 = 110 s） |
| `Music/mus0000.dat`（格式 B，`mus%`） | 35 个 MP3，ID：1–15、21–26、41–50、101–104 | 全部是 MPEG-1 Layer 3、128 kbps、44.1 kHz、立体声、无 ID3 头；时长 13–178 s |

[V] 样本读取（`CAudioMgr_LoadSampleFromFile @0x470a30`）：`fseek(+0x1c 偏移)`；条目第 4 个字段 `+0x24 == 0` 时直接读 `+0x20`（大小）字节，否则读 `+0x24` 字节再 `RW_Data_Decompress`（本游戏没有这种条目）。`_AIL_file_type`：1 = PCM WAV 直接用，2 = ADPCM 先 `AIL_decompress_ADPCM`（本游戏没有），其余类型丢弃（`CAudioMgr_PlaySample @0x4703a0`）。

## 2. 初始化与音量

- [V] `CApp__vf7_Initialize @0x402f50` → `Sound_InitMiles @0x404cf0`：`CAudioMgr_InitializeSystem(2, 1000000, "sound\\", "SND%04d.dat", 0xAC44 = 44100, 0x10 = 16 位, 2 声道)`（`@0x46fb90`，`AIL_quick_startup`），驱动选 “Miles Fast 2D Positional Audio”（`@0x404cf0` 里按名字找），随后 `Sound_SetVolumeLevel(InitTxt_GetParam(8))`。→ 移植的设备格式也是 44100 Hz / 16 位 / 立体声。
- [V] 音乐：`Music_InitMgr @0x404f20`：`CDigitalAudio_Initialize("music\\", "MUS%04d.dat")`，`CDigitalAudio_SetField10(10)`（见 4.3，播放模式 10 = 顺序），`Music_SetVolumeLevel(InitTxt_GetParam(9))`。
- [V] 主音量：`Sound_SetVolumeLevel @0x406230` 把 `音效管理器+0x284 = level / 5.0`（只接受 0..1）；`Music_SetVolumeLevel @0x4061e0` → `CDigitalAudio_SetMasterVolume @0x476250`：`音乐管理器+0x1F8 = level / 5.0`。level 来自 `Config\init.txt [param]` 的 `sound` / `music`（0..5）。`InitConfig_LoadInitTxt @0x401080` 的默认值：sound = 4、music = 3；本机 init.txt：sound=4（0.8）、music=1（0.2）。游戏内“设置”界面（setting.ui）改的就是这两个值（`InitConfig_SetParam(8/9)`）。
- [V] 最终音量 = 调用方给的音量 × 主音量：样本 `AIL_set_3D_sample_volume(音量 × +0x284)`（`@0x4703a0`），音乐 `AIL_set_stream_volume_levels(曲目音量 × +0x1F8)`（`CDigitalAudio_PlayBGM @0x473710`）。
- [A] 移植：`rm8AudioInit` 读 `<游戏目录>/Config/init.txt` 的 `sound` / `music`（缺失用默认 4 / 3）。

## 3. 音效：`CAudioMgr_PlaySample`（`@0x4703a0`）

调用形式（全部 110 处调用者）：`CAudioMgr_PlaySample(id, x, y, z, maxDist, minDist, volume, loops)`，返回样本对象指针（0 = 失败）。反编译里经常夹着一个假的 `Renderer_GetInstance(...)` 调用，那是反编译器把同一组参数重复显示，不是真调用。

| 参数 | 含义 | 证据 |
|---|---|---|
| `id` | snd0000.dat 的条目 ID；范围检查 `0 <= id <= +0x250`（最大 ID），音频可用标志 `+0x1c`（`waveOutGetNumDevs() != 0`，`Audio_DetectWaveOutDevice @0x46fcb0`），样本表项非空，否则返回 0 | `@0x4703a0` |
| `x, y, z` | 3D 位置：`AIL_set_3D_position`；同时记入节点；朝向 = 监听器位置 − 样本位置，上向 (0,1,0) | `@0x4703a0` |
| `maxDist, minDist` | `AIL_set_3D_sample_distances(max, min)`：游戏里固定 1000.0 / 0（UI 钩子 `@0x4EC680` 和 `py_Movie_PlaySound @0x5133C0` 用 1000.0 / 50.0） | `@0x4703a0` |
| `volume` | 乘主音量后 `AIL_set_3D_sample_volume`；游戏里几乎都是 1.0，仅打字机音效 `0x74` 用 0.5（`CUi_UpdateCharAnimation @0x410b30`，[L]） | `@0x4703a0` |
| `loops` | `AIL_set_3D_sample_loop_count`：**1 = 播一次，0 = 无限循环**（Miles 约定）；游戏也传 `0xFFFFFFFF`（`@0x47ba00`、`@0x49a290`），按“无限”处理 [H] | `@0x4703a0`；0 的用例 `@0x48b1f0` 走路循环 |

其余设置 [V]：`+0x15` 标志为真时锥角 (180°, 120°, 0)；`effects_level = 1.0`，遮挡 / 阻挡 / 排除都为 0；最后 `AIL_start_3D_sample`，用户数据（槽 0）记下 `id`（`Vehicle_UpdatePos @0x43a280` 靠它判断摆件音频列表里某个 id 是否已在播，没播就以 loops = 1 重播，等于循环；谁创建这些元素没有查清，见第 8 节）。

**3D 位置实际没有用上** [V]：
- 所有游戏逻辑里的调用位置都是 (0,0,0)（`CAudioMgr_PlaySample(id,0,0,0,1000.0,0,1.0,loops)`）；
- 监听器位置 `Audio_Set3DPosition @0x470060` 没有任何调用者，一直在原点；
- 只有小游戏宿主有逃生口 `CMiniGameHost_2_vf71 @0x510cd0` → `CAudioMgr_Set3DSamplePosition @0x471190`（6 个小游戏 DLL 用，不在移植范围）。
→ 结果就是普通 2D 播放，移植不做 3D 定位 [A]。

**停止 / 暂停**：
- [V] `CAudioMgr_StopSample(handle) @0x471730`：音量置 0 再 `AIL_end_3D_sample`；`handle == 0` 时对全部播放中的样本做同样的事；然后回收已结束的样本（`CAudioMgr_UpdatePlayingSamples @0x470fb0`，每帧由 `CApp_UpdateFrame` 调用）。
- [V] 窗口失活（`CApp__vf4_WndProc @0x403aa0`，`WM_ACTIVATE`）：`Audio_Stop3DSample(0) @0x471d60`（全部样本暂停）+ `CDigitalAudio_StopMusic(0) @0x474b60`（音乐暂停并记下位置）；重新激活时 `Audio_Resume3DSample(0) @0x471db0` + `u_CDigitalAudio_ManageBGMTransition(-5/-4) @0x474070` 恢复。
- [V] 样本缓存：第一次播放时读入并缓存，总占用上限 `+0x29c = 1000000`（`InitializeSystem` 第 2 个参数；只统计需要解压的 ADPCM，见 `@0x4703a0` 末尾），超限时 `u_CAudioMgr_ProcessFreeSample @0x470e40` 释放最久未用且没人在播的。本游戏没有 ADPCM，所以实际上永不释放。

Python 侧：`Movie.PlaySound(id, count)`（`py_Movie_PlaySound @0x5133C0`，剧情脚本最常用的函数，368 处）= `CAudioMgr_PlaySample(id,0,0,0,1000.0,50.0,1.0,count)`，返回句柄；`Movie.PlayMusic(id)`（`py_Movie_PlayMusic @0x513430`）= `CDigitalAudio_PlayBGMFaded(id,0,0,0,1.0,0,0,0)`。

## 4. 音乐：`CDigitalAudio`

### 4.1 曲目与流
- [V] 打包的 MP3 通过 Miles 流 API 读（`AIL_open_stream`，文件名是 `"\\\\<文件句柄>,<大小>.mp3"` 的小技巧，先 `SetFilePointer` 到包内偏移）；`AIL_set_stream_loop_count`；`AIL_start_stream`（`CDigitalAudio_PlayBGM @0x473710`、`CDigitalAudio_PlayBGMFaded @0x473a80`）。
- [V] 包索引（`CDigitalAudio_LoadMusicPackFile @0x473140`）：16 字节条目 `{ID, 偏移, 大小, —}`，按 ID 登记；第 4 个字段不读。

### 4.2 两种播放入口
| 入口 | 参数 | 行为 |
|---|---|---|
| `CDigitalAudio_PlayBGMFaded(id, 淡出毫秒, 起始位置, loop, 音量, …)` `@0x473a80` | Python `Movie.PlayMusic` 传 `(id,0,0,0,1.0,0,0,0)` | 立即停掉当前曲，开新曲，**loop = 0 = 无限循环**，音量 1.0 × 主音量；`param_2 != 0` 时先淡出再换（`@0x473a80`、`Audio_UpdateMusicStreamVolume @0x474870`） |
| 播放列表：`u_CDigitalAudio_PlaySound(id, …, 音量, loop=?…)` `@0x473530` 入队，`CDigitalAudio_PlayBGM(索引, 0)` `@0x473710` 开始 | 列表项：`+0x0C` 循环次数初值 1（只播一次）、`+0x14` 音量 1.0、`+0x1C` 曲目 ID | 棋盘音乐用这一种 |

### 4.3 棋盘音乐选曲规则 [V]
`Game_LoadMapData @0x4a31f0`（新局 `Game_InitGame @0x4a1090` 与读档都走这里；地图加载完后）：
1. `CDigitalAudio_StopAllBGM @0x474ca0`（清空播放列表）；
2. 若地图号 `param_1 < 12`：入队曲目 **`param_1`**；但开局设定里的闯关模式标志（`&DAT_00632de8` +0x00，`Singleton_GetPtrArray_00632de8 @0x4083f0`）为真且地图号 = 3（魔都）时入队曲目 **15**；地图号 ≥ 12（下载地图 21 起）不入队这一条；
3. 入队曲目 **`气候 + 0x0B`**：气候 = stage.bin 的 `clime`（地图记录 +0x210，`@0x4a8ce0`（Map_LoadMapList）的加载循环里 `local_170[0x84] = clime`）；
4. `CDigitalAudio_PlayBGM(0, 0)`：从列表第 0 项开始，每项只播一次。

一首播完（`CDigitalAudio_UpdateMusic @0x473f40`，流状态 2）→ 因为播放模式 `+0x10 == 10`（初始化时 `CDigitalAudio_SetField10(10)`），调用 `CDigitalAudio_PlayNextBGM @0x4742f0`：顺序表是 0..n−1，**取表里第一个不是刚播完那首的曲目**（n = 1 时重播同一首）。所以：n = 2 时两首交替，一直循环，不会随机；模式 6 是 `CDigitalAudio_PlayRandomBGM @0x475230` 的随机洗牌，本游戏不用（只在 +0x10 == 6 时走）。

地图对应曲目（stage.bin 的 clime：1/1/3/1/3/1/2/2/3/3/2，对应地图 1–11）：

| 地图 | 曲目 A | 曲目 B（clime + 11） | 备注 |
|---|---|---|---|
| 01 香港 | 1 | 12 | |
| 02 台北 | 2 | 12 | |
| 03 巴黎 | 3（闯关模式 15） | 14 | |
| 04 东京 | 4 | 12 | |
| 05 阿拉伯 | 5 | 14 | |
| 06 上海 | 6 | 12 | |
| 07 〔string.bin#3823〕 | 7 | 13 | |
| 08 好莱坞 | 8 | 13 | |
| 09 夏威夷 | 9 | 14 | |
| 10 〔string.bin#3826〕 | 10 | 14 | |
| 11 游乐场 | 11 | 13 | |
| download201–204 | — | 13 | 下载地图的气候 = 文件名 "download" 后第一位数字（`Map_LoadDownloadMap @0x4a9020`，限制在 1..3），四张都是 2；只有一首，循环 |

[V] 同一函数在菜单场景初始化（`CGameScene__vf16_Init @0x509d70`，类 vtable 0x5E0048 实为菜单场景，见 ui_system.md）里入队曲目 `0x65 = 101` 并 `PlayBGM(0,0)`：菜单音乐，不属于棋盘。

### 4.4 其他音乐（移植未做）
| 曲目 | 位置 | 用途 |
|---|---|---|
| 101 | `CGameScene__vf16_Init @0x509d70` | 主菜单 [V] |
| 102 (0x66) | `Cls_5e49c8__vf16_ShowBalanceUI @0x4af2e0`：`CDigitalAudio_StopMusic(0)`（暂停棋盘乐并记位置）→ `PlayBGMFaded(0x66)`；`Balance_Cleanup @0x4af650`：`StopBGMWithFade(0)` + `u_CDigitalAudio_ManageBGMTransition(-5)` 恢复棋盘乐 | 月结算（余额）界面音乐 [V]，名称 [H] |
| 小游戏 | `CMiniGameHost_vf16 @0x50ddd0`（`StopMusic(0)`、状态 9）、`MiniGameHost_Cleanup @0x50e300`（`StopBGMWithFade`/`ManageBGMTransition(-4)`）；`CMiniGameHost_2_vf66 @0x510d30` = `PlayBGMFaded(param_1)` | 小游戏自己选曲，结束后恢复棋盘乐 [M] |
| 剧情 | Python `Movie.PlayMusic(id)`（67 处，例如 45） | 过场音乐，循环 |

## 5. 样本号（按事件）

下面“位置”是调用 `CAudioMgr_PlaySample` 的函数地址；“名称”来自 functions.tsv，只有 [V] 标出的描述是我读过代码确认的，其余按 [H]。

| 事件 | 样本 | loops | 位置 | 移植 |
|---|---|---|---|---|
| 骰子落地 | **9** | 1 | `Dice_UpdateRoll @0x47d090`：`0.04 s` 门限内、帧号 **恰好 == 12** 且音频管理器存在时 [V] | 已做（`dice.cpp`） |
| 走路循环 | 角色 `wlkSndId[…]`：多数 51 / 52 / 53，萊娜等 4 人 51 / 78 / 53，1 人 51 / 79 / 53，2 人 51 / 51 / 53 | **0（无限）** | `CPlayerTag__vf20 @0x48b1f0`，`BeginMoveSteps @0x484f90` 调用，下标 = 交通工具 − 1（`+0x22C − 1`），附身（`+0x1BC != 0`）一律 0；已有句柄（`+0x48`）时不再播 [V]；`vf21 @0x48b260` 停止并清句柄，`vf19 OnStopAtTerra @0x485c00` 停下时调用 [V] | 已做（`roles.cpp`） |
| 转向 | **0xB** | 1 | `Path_ComputeDistance_0047b5f0 @0x47b5f0`（重选下一个路点并转身，`Cls_5e27d8_OnAngleChange_0047de40 @0x47de40` 调用） | 未做：移植没有掉头逻辑 |
| 事件图标迎接（落脚点） | **0x7D** | 1 | `Article_PlayApproachEffect @0x4ee3c0`：距离 < 40、图标上没人、`param_3 != 0` 时图标播动作 0 并播 0x7D [V] | 未做：移植还没有事件图标迎接/离开（`Article_PlayDepartEffect @0x4ee4f0`） |
| UI 按钮：鼠标移入 | `.ui` 的 `InSound`（本游戏用到 11、111、112、114、117、118、136、146、147） | 1 | `UiLib_Title_OnStateChanged @0x5c3460`（`+0x1F0`）→ 钩子 `[DAT_006570d8+0x30]` = `u_Audio_PlaySampleDefaultParams @0x4ec680` = `PlaySample(id,0,0,0,1000.0,50.0,1.0,1)`；条件：控件启用且可见，id ≥ 0 [V] | 已做（`ui.cpp`，仅 `type=button`） |
| UI 按钮：按下 | `DownSound`（257 处，常见 117） | 1 | `UiLib_Button_OnClick @0x5c31f0`（`+0x1F4`，左键、控件启用） [V] | 已做 |
| UI 按钮：移出 | `OutSound` | 1 | `UiLib_Title_OnMouseExit @0x5c3620`（`+0x1F8`）；本游戏的 .ui 没有设置 | 未做（无数据） |
| UI 滑块 / 列表 / 复选 / 单选 | `InSound`（滑块 9 处）、`DownSound`、`ChangeSound` | 1 | `0x5af390`、`0x5af7f0`、`0x5b99e0`、`0x5be430`、`0x5c1d00`、`0x5c28c0`、`0x5c29c0`（各类控件的同一钩子，偏移不同） | 未做 |
| 文字逐字出现 | 0x74，音量 0.5 | 1 | `CUi_UpdateCharAnimation @0x410b30`（先 `StopSample` 上一个）[L] | 未做 |
| 购得/建造地块、建筑完成 | 1、2、3、4、6、0x1E、0x7A、0x67 | 1 | `Building_CompleteWithSound @0x4f4240`（1）、`Cls_5e6898__vf3_FinishAndPlaySound @0x4f5690`（4）、`Role_TakeLandOwnership @0x487250`（1）、`NPC_* @0x4f4b50 / 0x4f4d70 / 0x4f4f20 / 0x4f51c0`（2、0x7A、3、6）、`Building_AnimateMultiple @0x4f3b20`（0x1E）、`Building_SpawnEffectArticle @0x4f4590`（0x67） | 未做（规则层事件），名称 [H] |
| 付租 / 〔string.bin#5303〕 / 卡牌 | 0x7B、0x7C、0x71、0x82、0x2E、0x21、0x1F、0x31、0xD、0x23、0x40 | 1 | `Role_TransferRentToOwner @0x487f20`（0x7B）、`Role_ReceiveCardWithUi @0x488e80`（0x7C）、`Card_HandleSwap @0x4ae8a0`（0x7C）、`Game_HandleCardClickInGame @0x4a2940`（0x71）、`Balance_ShowPlayerBalance @0x4b0670`（0x82）、`CPlayerTag_3__vf32_CreatePathPlaySound @0x4803e0`（0x2E）、`CPlayerTag__*`（0x21、0x1F、0x31、0xD、0x23）、`Cls_5e31e0__BeginAttach @0x49caf0`（0x40）等 | 未做，名称 [H] |
| 神仙 / 精灵 / 新闻 | 0x68（循环）、0x7E / 0x7F（循环）、0x73（循环）、0x11（loops = −1）、10 (0xA)、0xF | 0 / −1 / 1 | `Spirit_Spawn @0x49af80`（0x68）、`News_EventDispatcher @0x4b7f80`（0x68）、`News_AnimalMode @0x4b4560`（0x7E、0x7F）、`Dice_ShowRollingImagePlaySound @0x4e8780` / `Ui_SetImage_PlaySample @0x4e8ca0`（0x73，循环）、`UI_CreateImageObject @0x49a290`（0x11，炸弹图）、`Role_SetTurtleDays @0x48b670`（10）、`Role_PutOthersToSleep @0x48c330`（0xF） | 未做，名称 [H] |

## 6. 移植实现（`port/app/audio.cpp`）

[A] 用 SDL2 音频 + 小型软件混音器代替 Miles：
- 设备：`SDL_OpenAudioDevice`，44100 Hz / S16 / 立体声（不允许 SDL 改格式，由 SDL 自己转到硬件格式），桌面 1024、安卓 2048 采样缓冲。设备打不开或没有数据包时全部接口静默（返回 0）。
- 样本：按需 `fseek` + `fread` 读 `Sound/snd0000.dat` 的条目（不整包加载），`SDL_LoadWAV_RW`（内存）+ `SDL_ConvertAudio` 转成 44100 立体声 16 位，缓存 48 MB（LRU，播放中的不踢）。
- 混音：32 路同时发声；每路有音量（调用方音量 × 主音量，播放时定死，与游戏一致）、循环次数（`loops`：0 / 负数 = 无限，n = n 次）、句柄；整数累加后截幅。
- 音乐：`Music/mus0000.dat` 条目整个读入内存（≤ 4.2 MB），用 dr_mp3（公共领域，`port/extern/dr_libs/dr_mp3.h`，v0.7.5）在混音回调里边解码边混；播放列表按 4.3 的规则交替（曲目播完在回调里直接切下一首，不需要主线程参与）。
- 线程：所有公共函数在游戏线程调用；改混音状态时 `SDL_LockAudioDevice`；磁盘读取和解码都在锁外。
- 暂停：`rm8AudioPause(true/false)` = `SDL_PauseAudioDevice`，在 `rm8_app.cpp` 里跟着 `SDL_APP_*BACKGROUND/FOREGROUND` 走。
- 接口（`audio.h`）：`rm8AudioInit/Shutdown`、`rm8SndPlay(id, loops, volume)`、`rm8SndStop(handle)`、`rm8SndStopAll`、`rm8SndPlaying`、`rm8MusicPlay(id, loop)`、`rm8MusicStop`、`rm8MusicPlayBoard(地图名, 闯关模式)`、`rm8AudioPause`。
- 没有 SDL 头文件的构建（GLFW 的 `rm8_scene`、`rm8_view`）里 `audio.cpp` 是空实现（`__has_include(<SDL.h>)`）；`rm8_scene` 本来就不初始化音频。
- 调试：环境变量 `RM8_AUDIO_TRACE=1` 在日志里打印每次播放 / 停止 / 换播放列表（默认关闭）。`SDL_AUDIODRIVER=dummy` 可以在没有声卡时跑。
- 接入点：`rm8_app.cpp`（`rm8AudioInit`、加载地图后 `rm8MusicPlayBoard`、后台 / 前台暂停、退出 `rm8AudioShutdown`；`--shot` 离屏模式不开音频）、`dice.cpp`（样本 9）、`roles.cpp`（走路循环，`scenes/roles.txt` 新增 `snd <角色号> <步行> <机车> <汽车>` 行，由 `scripts/export_roles.py` 生成）、`ui.cpp`（按钮 `InSound` / `DownSound`）、`android/deploy.py`（推送 `Sound/snd0000.dat`、`Music/mus0000.dat`、`Config/init.txt`）。

## 7. 验证

- 离线测试（`<dev>\scratch_audio\audiotest.cpp`，直接 include audio.cpp，`SDL_AUDIODRIVER=dummy`）：2400 个样本全部解码成功（0 失败，共 4464 s 音频、解码 7.6 s，缓存封顶 47 MB）；35 条 MP3 全部能解码（44.1 kHz 立体声）；样本 9 单次播放 280 ms（理论 279）、3 次 830 ms（理论 837）；循环样本 51 播放 2 s 后仍在播，`rm8SndStop` 后停止；同时发声上限 32；缺失 ID 返回 0；`rm8MusicPlayBoard` 对 01 / 03 / 11 / download201 / download204 生成的列表是 `1 12`、`3 14`、`11 13`、`13`、`13`，闯关模式地图 3 的第一首是 15；把解码器 seek 到曲末，自动切换 `0→1→0`；单曲不循环时播完停止、循环时重播。
- 桌面程序（`build-audio\rm8_app.exe … --map 01 --frames 1800`，dummy 驱动 + `RM8_AUDIO_TRACE=1`）：启动打印设备与音量；加载地图时列表 `1 12`；每次掷骰按顺序出现 `play 9` → `play 51 loops 0`（走路循环，停下时 `stop`）。
- 安卓：`build-audio-android` 编译通过（未打包、未部署、未上机）。

## 8. 未做 / 待确认

1. 转向音 0xB、事件图标迎接音 0x7D：依赖移植里还没有的逻辑（掉头；图标迎接 / 离开），接口 `rm8SndPlay(0xB)` / `rm8SndPlay(0x7D)` 已就绪。
2. 规则层 / 界面层的其余样本（第 5 节后半）；Python 绑定：`Movie.PlaySound(id, count)` → `rm8SndPlay(id, count)`，`Movie.PlayMusic(id)` → `rm8MusicPlay(id, true)`（`python.cpp` 不属于音频模块，没有改）。
3. 音乐淡入淡出（`PlayBGMFaded` 的 `param_2`、`Audio_UpdateMusicStreamVolume @0x474870`）、暂停并记位置 / 恢复（`ManageBGMTransition`，月结算 102、小游戏）、菜单音乐 101。
4. 滑块 / 列表 / 复选 / 单选的 UI 音效，`OutSound`（无数据），游戏内音量设置界面，桌面失焦暂停（原版有，移植只在安卓后台暂停）。
5. `loops = 0xFFFFFFFF` 在 Miles 里的确切含义 [H]（按“无限”处理）；`Audio_SetMilesPreference(0x20)`（`@0x472140`）的含义 [H]，未复刻。
6. 原版的 Miles 同时发声数量上限没有核对（`AIL_get_preference(1)`，`CAudioMgr_GetAvailableSampleCount @0x471e00`）；移植固定 32 路。
7. 摆件挂的音频：`Vehicle_UpdatePos @0x43a280`（名称不可信）对 `{摆件, 标志, 样本号, …}` 元素每次调用都检查摆件的音频列表（`+0x49c`，`Article_StopAudio @0x44d730` 销毁时逐个停止）里有没有该样本号，没有就 `PlaySample(id,0,0,0,1000,0,1.0,1)` 并入列。元素由谁创建、样本号从哪来（role.bin？Python？）没有查清，所以地图环境音（海浪、鸟叫之类）是否存在未知 [H]；移植没有做。
