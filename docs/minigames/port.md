# 小游戏移植（port/app/minigame.* 与 mg_*.cpp）

对应原版 `Gamelib\ltlgame_18~23.dll`（快手小厨师、记忆挖宝、打地鼠、插海盗、分裂球、踩蛋）。DLL 的框架（宿主服务对象 `CMiniGameHost_2` vtable 0x5E8928、
游戏对象 8 槽虚表 Init / GetResult / Update / Draw / KeyDown / KeyUp / MouseButton / MouseMove）被原样镜像：每个游戏是对应 DLL 的逐函数转写，
注释里用 `[V] 0x1000xxxx` 标 DLL 地址（已在反编译/反汇编中核对）、`[H]` 标推测、`[A]` 标移植适配。规则细节以 mg_NN.cpp 的注释为准，本文只给总览。

## 1. 文件与分工

| 文件 | 内容 |
|---|---|
| `app/minigame.h` | 对外 API（见 3） |
| `app/minigame.cpp` | 生命周期、输入路由、[A] 触屏覆盖层、宿主奖励规则（`MiniGameHost_Cleanup` @0x50E300）、电脑结果生成（`MiniGame_ExecuteGame` @0x4FCB10）、脚本化输入 |
| `app/mg_core.h` | 内部接口：`mg::` 宿主服务（精灵、3D 模型、音效、字符串、简介面板）与 `MgGame` 基类 |
| `app/mg_gfx.cpp` | 精灵（文章记录 + uvlisttable 图片）、2D 绘制批处理、音效/音乐/字符串封装、MSVC `rand()` |
| `app/mg_3d.cpp` | 3D 模型文章（企鹅、木桶、木剑）、游戏相机、世界到屏幕投影（host +0x1c） |
| `app/mg_standalone.cpp` | `--minigame N` 测试入口（窗口循环、`--shot`、SDL 按键翻译） |
| `app/mg_18.cpp … mg_23.cpp` | 六个游戏 |
| `scripts/export_minigame.py` | 从 role.bin + uvlisttable.tb 导出 `scenes/minigame/sprites.txt`（`spr` 精灵记录 171 条中的 162 条，`mdl` 3D 模型 9 条） |
| `mgtest/` | 只含小游戏模块的独立构建（不依赖 roles/cards/events/python）；`GUIDE.md` 是写/审一个游戏的操作指南 |
| `tools/lgshow.sh` | 去噪后查看命名反编译的片段 |

共享文件的改动（均为小插入）：`CMakeLists.txt`（两个源码列表各加 `minigame.cpp mg_gfx.cpp mg_3d.cpp mg_standalone.cpp mg_18~23.cpp`）、`rm8_app.cpp`
（`#include "minigame.h"`、`--minigame N` 参数、map 加载前的一个 `if(miniGame > 0){...}` 分支）、`scene.h/scene.cpp`（新增 `rm8PackRead`：读贴图包原始条目，命中测试要读 alpha）。

## 2. 精灵与原版框架的对应（均已核对）

- 小游戏里的 `host+0x50 CreateObject(id)` 创建的是 **文章（article）**：`CMiniGameHost_vf20 @0x50F750 → CArticleMgr vf16 @0x43C470 → CArticleDFF_Load @0x455BD0`。
  文章记录来自 `Config\newOut\role.bin`；记录 +0xC == 1 的是精灵：`Sprite_Init(x=rec+0x640, y=rec+0x644, 颜色 -1, uvlist 号=rec+0x638)` @0x461E70，图片是
  uvlisttable.tb 的一条记录（帧 UV、时长、`loop_start_frame`、`loop_count`）。其余（企鹅 0x642/0x643、木桶 0x711、木剑 0x712~0x717）是带 .dff/.anm 的 3D 文章。
- 精灵位置 = **中心**（`CArticleMgr vf40 @0x43FB00`：offset = pos − 中心），缩放绕中心（`Sprite_UpdateTransformedVerts @0x4627A0`），尺寸 = 贴图尺寸 × UV 跨度
  （`Sprite_GetPixelSize @0x4622E0`，与 uvlist 的 w/h 经全部 263 帧核对一致）。动画状态 2（播完）的精灵不画（`Sprite_Render @0x462580`）。
  帧推进 `Article_AdvanceAnimFrame @0x468090`：`loop_count == 1` 播一遍，0 或 ≥2 无限循环（计数器 +0x20 初值 0，只有 DLL 经 host +0xA8 写入后才计数）。
- 宿主槽位对照：+0x58 SetAction（仅 3D 文章）、+0x64 缩放、+0x68 位置、+0x6C 旋转（精灵 = Z 轴旋转）、+0x84 颜色（`Sprite_SetColor`）、+0x90 取尺寸、
  +0x9C/+0xA0 当前帧、+0xA4/+0xA8 循环计数、+0xAC/+0xB0 状态、+0xB4 点命中（逆变换 + 矩形 + alpha > 0x32 [H]）、+0x114 播放音效（音量截到 1.0）、+0x128 光标位置。
- 虚拟分辨率固定 **1024×768**（原版 `CApp_SetFullscreenMode @0x405DA0` 的两种模式之一），DLL 里 `W*0.00125` 之类的缩放系数照原样算出 1.28；输出按 `H/768` 等比缩放并在两侧留黑边
  （手机宽屏上居中）。
- 3D 相机：`Camera_SaveViewState @0x41B2E0`（宿主在 Init 前调用）把游戏相机置为单位矩阵（原点朝 +z），视窗 0.3×0.225；RenderWare 镜像 x（`rwCameraBeginUpdate`：
  `inv.right.x = -inv.right.x`），所以世界 +x 在屏幕**左侧**：`sx = 512 − x/z·1706.7`，`sy = 384 − y/z·1706.7`（`mg::project`，host +0x1C，`CMiniGameHost_vf7 @0x50F1A0`）。
  各游戏的转向逻辑与这一映射自洽（企鹅走向点击格）。灯光 [A]：环境光 0.75 + 一盏平行光；模型 alpha 测试 ≥196 [H]（同棋盘上的角色）。
- 随机数：MSVC `rand()`（`seed*214013+2531011` 取 16~30 位），每局按 `rm8MiniStart` 的种子重置（DLL 多处用 `timeGetTime` 重播种，[A] 不照搬以便复现）。
- 按键码为原版输入层码（DirectInput 事件：只有按下/抬起，没有自动重复）：小写 ASCII 字母/数字/空格，`0x93~0x96` = ↑↓←→，`0xAB` = Enter，`0x80` = ESC，
  小键盘 1~9 = `0x99~0xA1`（`0x98 + n`，DLL 20 号的查表；SDL 一侧的对应 [H]：宿主 `CMiniGameHost_vf5` 原样转发）。
- 鼠标：宿主 `CMiniGameHost_vf9 @0x50EE30` 传给 DLL 的是 0x40 = 左键按下、0x400 = 左键放开；触摸 = 左键（按下/抬起）。

## 3. API（minigame.h）

```cpp
bool rm8MiniInit(scenesRoot);                       // 载入 sprites.txt（rm8UiInit 之后）
bool rm8MiniStart(game, players, seed = 0);         // players[0] = 轮到的玩家；Rm8MiniPlayer{seat, human, charId, pic, name}
void rm8MiniUpdate(dt);                             // dt 钳到 0.1
void rm8MiniRender(cam);                            // 在 cam->beginUpdate/endUpdate 之间调用；自己画黑底，并调用 rm8UiRender（简介面板），请先隐藏其他 UI
void rm8MiniKey(code, down); void rm8MiniPointer(kind 0/1/2, x, y, id, touch);   // 屏幕像素
bool rm8MiniDone();                                 // Update 返回 false
bool rm8MiniFinish(Rm8MiniResult &out);             // GetResult（5 个 int/人：座位、得分、flagA、评价档、flagB）并销毁
std::vector<Rm8MiniReward> rm8MiniRewards(out);     // 宿主奖励规则（点券、卡片 card.bin 下标、住院天数、消息串号、表情、最高分）
Rm8MiniReward rm8MiniComputer(game, difficulty, rand);   // 电脑玩家：不运行 DLL（0x4FCB10）
bool rm8MiniCursorHidden(); int rm8MiniKeyFromSdl(sym);
```

接入棋盘时（主管后续做）：事件 18~23 的人类玩家调 `rm8MiniStart`；每帧 `rm8MiniUpdate` + `rm8MiniRender`，`rm8MiniDone()` 后 `rm8MiniFinish` 取结果、`rm8MiniRewards` 得到奖励
并按 `MiniGameHost_Cleanup` 的顺序入队消息；电脑玩家用 `rm8MiniComputer`（事件 21 在有人类玩家时不模拟：`MiniGame_ExecuteGame` 开头的守卫）。

奖励规则（`MiniGameHost_Cleanup @0x50E300`，均为 [V]）：
- 18：点券 = 得分；≥180 发卡 idx 0x12，100~179 发 0x14（card.bin 下标，Id = 下标+1）。消息串 5314。
- 19：点券 = 得分（>0 时）；≥500 发 0x12、0x2C、0x14；**只要 >0 再发 0x2F**（汇编如实，疑似原版 bug/参与奖）。消息 5315。
- 20：≥150 发 0x12，100~149 发 0x11。消息 5316。
- 21：逐人加减点券；得分 >50 者为“大赢家”（5318）；flagA = 被刺中的海盗（住院 3 天，串 5317）；flagB（第一刀即中）发保镖卡 0x2E（串 5303）。
- 22：≥150 发 0x2C；=999 发 0x2C、0x2F、0x12。消息 5319。
- 23：150~159 发 0x2E，≥160 发 0x30。消息 5320。
- 评价档 0/1/其它 → 反应 4/1/2（`FUN_00484890`）；`FUN_004aa5d0(事件−0x11, 得分)` 记个人最高分。
电脑玩家（`MiniGame_ExecuteGame @0x4FCB10`，difficuty < 3 / ≥ 3）：18 `%60+20` / `%100+40`；19 `%60+30` / `%100+40`；20 `%50+10` / `%80+30`；
21（无人类）50% 住院 3 天（5317）否则 +90（5318）；22 `%120+1` / `%180+20`；23 `(%10+1)*10` / `(%8+3)*10`。

## 4. 各游戏概况

| 游戏 | 文件 | 主要来源（DLL 地址） | 操作 | 触屏 [A] |
|---|---|---|---|---|
| 18 快手小厨师 | mg_18.cpp | Init @1002cc20、Update @1002fb50、DrawAndCollide @10033100（计分在绘制里）、DrawHud @10031b50、Chef_Update @10034660、Disruptor_Move @10034b00、GetResult @1002fa60 | 厨师 A/D/空格；扰乱者（二号人类）←/→/Enter；模式 0/1/2 | 左/右/跳按钮；二号人类时另加扰乱者左/右/扔 |
| 19 记忆挖宝 | mg_19.cpp | 状态机 SetState @10006f90 / Timeout @10003060 / Start @100017f0（汇编），Grid_Create @10004500、Penguin_*、Hud_*、粒子 | 鼠标点格 | 点按即点击 |
| 20 打地鼠 | mg_20.cpp | Init @100025a0、Update @10001c40、Draw @10001ea0、Field_Update @10004d20、OnClick @10005840、OnNumpad @10002cf0 | 鼠标点击；二号玩家小键盘/数字键放扰乱者 | 点按；九宫格按钮（小键盘布局，仅比赛中） |
| 21 插海盗 | mg_21.cpp | init @10005060、update @100048e0、draw @10004c10、Barrel_Stab @10001950、状态机 @10006420 / @10002390、GetResult @100031d0 | 鼠标点洞；A/D、←/→ 或点箭头转桶 | 箭头精灵可点（先按点击位置更新悬停再处理点击） |
| 22 分裂球 | mg_22.cpp | Init @100025a0、Update @10001dd0、Draw @10001f70、Manager_Update @10009690、SplitBall @10008a60、道具 @10008c90、GetResult @100034b0 | A/D 移动、空格射击；秘籍 peng（0x8C p e n g Enter） | 左/右/大开火按钮；秘籍无按钮 |
| 23 踩蛋 | mg_23.cpp | Init @10041480、Update @10043850、DrawHud @100423a0、PipeEggMgr_* @10037bf0~1003ab00、Clown_* @1003e590~1003ef50、GetResult @10043d80 | 鼠标按住起跳；二号人类 ↑/↓ 摇管子；ESC（0x80）结束 | 仅二号人类时显示 ↑/↓ 按钮 |

对 `lg_NN.md` 的更正（以反编译为准；细节见各 mg_NN.cpp 注释与各 agent 的核对）：
- 19：`Grid+0xd48` 在 @100048e3 被写成 8（反编译把它当不可达块），记忆阶段真的洗牌 8 轮（约 3.5 秒）；状态 1（简介）不扣倒计时；0x649 是陷阱时烧黑的企鹅精灵，不是镐子；
  暗码判定比文档宽（`xjea` 也通过）；无连击时最终得分为 0 的怪行为按代码复现（静态推导，未动态验证）。
- 20：命中后“已出洞时间 ≤0.2 → 生命 = 已出洞时间、已出洞时间 = 1.2”（文档写反）；阶段按 `ftol(场内计时)`；最后 5 秒倒数条件是整数秒 ≤5；数字集是 0x83F 起（金红）与 0x834 起（蓝白）；0x84D/0x84E 创建后从不绘制；DLL 从不创建 03 号目录里缺失的几张贴图。
- 22：缩放系数是 `W*0.00125` 与 `H/600`（1024×768 下都是 1.28）；秘籍只改基础间隔 +0x168，当前间隔要等快/慢道具到期才变 0.2；快炮道具同时提高移速；倒数 5 秒是同一红色数字画在顶部中央；背景星球/岩石会下落。
- 23：“蛋堆过高提前结束”是确定规则（堆顶超过左管口时左管停止并立即结束；最上两蛋水平距离 >37×缩放则蛋堆崩塌 1~1.5 秒后结束）；左管蛋在原版里站地面踩不到（管口 y≈138，起跳最高 y≈176），要站在蛋堆上或由二号玩家压低管子；
  踩蛋计分/飘分有原版怪癖（`erase` 之后才判断蛋的类型）照搬；预警色是红色。
- 21：玩家是棋盘的角色纸偶模型（role 的 br/fl/fr 视角），选海盗用的 art 503 是 3D 模型；0x713 是剑模型（剩余木剑列与头像交叉剑）；0x70F 绿 X、0x70E 红 O。

## 5. 测试入口与钩子

`rm8_app <game> <scenes> --minigame N [--shot out.png --time T --size WxH]`，或独立构建 `mgtest`（见 mgtest/CMakeLists.txt；`build-mini-mg/rm8_mgtest.exe …` 同参数）。
`--shot` 以 1/30 秒步进到 T 秒（每步 Update + 无渲染的 Draw，因为 18/20 等把逻辑放在 Draw 里），结束时输出结果与宿主奖励；不加 `--shot` 是交互窗口，结束 0.5 秒后记录结果并退出（Esc 退出，23 号 Esc 属于游戏）。

环境变量（只影响测试，默认无输出）：
- 通用：`RM8_MINI_SEED`、`RM8_MINI_PLAYERS`（如 `hccc`，h 人类 c 电脑，默认 21 号 4 人、22 号 2 人、其余 1 人）、`RM8_TOUCH=1`（始终画触屏覆盖层）、
  `RM8_MINI_SCRIPT="2.5:click:512,400;3:down:a;4:up:a;5:key:space;6:pdown:x,y;7:pup:x,y"`（游戏秒；坐标为 1024×768 空间）。
- 18：`RM8_MG18_LOG`、`RM8_MG18_SIMDRAW`；19：`RM8_MG19_LOG/LAYOUT/BOT/START`；20：`RM8_MG20_PREDRAW`、`RM8_MG20_TESTSCORE`；
  21：`RM8_MG21_TRAP/PIRATE/HIT/NEVER/AUTOSTART/LOG`；22：`RM8_MINI_BOT/FORCE_POWERUP/FORCE_AFTER/BALLS/SCORE/CHEAT/TIME`、`RM8_MG22_LOG`；23：`RM8_MG23_TRACE/BOT/SCORE0/PILE/PILEX`（各文件头有说明）。

验证方法：`--shot` 渲染（简介、Ready/Go、中局、结束横幅、评级）、脚本输入驱动得分/结束/结果路径并与代码对照、各游戏的机器人/强制布局钩子；
构建：桌面 `build-mini`（完整 rm8_app）与 `build-mini-mg`（mgtest）通过，Android arm64（`build-mini-android`，NDK r29）链接通过（未打包/部署）。

## 6. 偏差 [A]

- 虚拟 1024×768 等比缩放 + 黑边；精灵半纹素内缩（D3D9 按像素中心采样，GL 双线性会在 1.28 倍缩放时在图块接缝漏出邻近纹素）。
- 精灵动画时钟由 DLL 自己的 `spUpdate(dt)` 推进（原版读 `timeGetTime`）；`timeGetTime` 的其余用法改为游戏时钟；DLL 的重播种改为按局种子。
- 18/20/22/23 的 `getResult` 在没有执行到评价档绘制时补算评价档（`--shot` 步进路径；真实循环里 Draw 先执行，结果相同）。
- 触屏覆盖层（只在触屏设备/未见键盘时显示）、小键盘九宫格、箭头点击的悬停更新；`Rm8MiniPlayer.outfit` = 描述符 +0x50 = `role+0x1ac`（2026-11-15 新增，`event_mini.cpp` 填）：21 号 outfit 4 = 泳装，模型 `图标 − 1 + 0xC0`、不贴服装贴图（`mg_21.cpp playerCtor @10005cf0`）；独立测试 `RM8_MINI_OUTFIT=4`。
- 灯光、3D 模型 alpha 测试阈值（196）、点命中的 alpha 阈值（0x32）为 [H]（原版无法抓取对照）。
- x87 扩展精度：23 号链式运算用 double 再存 float，其余用单精度，个别 `ftol` 可能差 1。

## 7. 未解决 / 待确认

- 音效 id 与音频文件的对应由音频模块负责；各游戏用的是 DLL 传给 `host+0x114` 的第一个参数。
- 小键盘 1~9 = 0x99~0xA1 的宿主侧键码表未单独核对（DLL 一侧已确认）。
- 21 号 `PushIntro` 里 cmd+0x24/+0x28 是栈垃圾（取 (0,0)，选海盗阶段会先在几名玩家间快速跳 ~5 秒）；2D 精灵的深度写入（原版 2D 带深度测试/写入，Ready 横幅会遮住桶）在 mg_21 内自行处理，mg_gfx 的 2D 是无深度的。
- 21 号加载 roles.txt/npcs.txt 的角色模型使用 mg_21.cpp 内的本地加载器（与 mg_3d.cpp 同样的帧操作），尚未并入 mg_3d。
- 19/21 号依赖的 3D 相机参数（视窗 0.3×0.225、近/远 1/5000）来自棋盘相机；原版在小游戏里是否改过远近面未核实。
- 与原版的逐帧画面对照（`scripts/grab_window.py` 方法）未做：小游戏没有可直接取的相机，且不得操作运行中的原版。
