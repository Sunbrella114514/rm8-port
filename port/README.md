# 安卓原生移植：技术验证（librw）

> **⚠️ 本仓库不含《大富翁8》的任何游戏文件，也不含由它们转换出的数据。** 运行需要你**自己合法拥有**《大富翁8》（推荐 Steam 正版：https://store.steampowered.com/app/2185870/），
> 并用 `extractor/` 里的提取器 `rm8_extract` 在你自己的电脑上把你的游戏数据转换出来（`game\` + `scenes\`）；下文提到的 `scenes\`、
> `game\` 都指提取器的输出，不在仓库里。提取输出和含有这些数据的 APK 只限本人使用，不得分享或上架。本项目与游戏权利人无关。
> 见 [NOTICE.md](NOTICE.md)、[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)；代码依 GPL-3.0-or-later（[LICENSE](LICENSE)）。
> 下面的“用法 / 构建”里出现的开发机路径（`E:\...`、`<dev>\...`）只是开发环境，换成你自己的路径即可。

## 快速开始（普通用户；开发者请跳到后面的技术说明）

支持的游戏：**Steam 简体中文版**（商店页：<https://store.steampowered.com/app/2185870/>）。以下步骤在 Windows 10 / 11 上做，手机版也在电脑上生成。

**电脑版**
1. 在 Steam 买好并装好《大富翁8》（装好即可，不用运行）。
2. 到发布页下载 `rm8_extract.exe` 和 `rm8-port-src-….zip`，放在同一个文件夹（不要解压）。
3. 双击 `rm8_extract.exe`。Windows 弹出蓝色的“已保护你的电脑”（SmartScreen）时点“更多信息 → 仍要运行”——程序没有付费数字签名，不是病毒。
4. 按提示回答（都可以直接回车）：游戏文件夹（Steam 版自动找到）→ 输出文件夹 → 要什么（1 = 电脑版）→ 是否迁移原版存档。
5. 等它做完（第一次需要联网下载编译工具和 ffmpeg，共约 350 MB）。**双击输出文件夹里的 `rm8.bat` 开始玩。**

**安卓手机版**：同样的步骤，第 4 步“要什么”选 2（或 3 = 都要）；会显示 Google 的 Android SDK 许可协议，必须接受才能构建安卓版。
得到 `rm8_personal_DO_NOT_SHARE.apk`（含你自己的游戏数据，**不要分享**）后复制到手机安装，第一次打开会先解压数据（手机留 3 GB 以上空间）。
请备份 `%APPDATA%\RM8\apk_key`（签名密钥），以后更新要用同一把密钥。

**常见问题（完整版见仓库根目录 README.md）**
* **会下载什么？** 只下载公开的开发工具和第三方开源库（CMake、Ninja、llvm-mingw；安卓版另有 JDK、Android NDK / build-tools / platform；ffmpeg；SDL2、librw 等源码），固定版本、校验 SHA-256，装进私有文件夹（`%LOCALAPPDATA%\RM8`），不装进系统、不要管理员权限；游戏文件不会被下载或上传，没有任何统计。完整地址清单见 `THIRD_PARTY_NOTICES.md`。
* **存档在哪？** 电脑版在 `%APPDATA%\RM8\`（`save\record_*.sav`、`rm8_gamedate.bin`、`rm8_settings.txt`），不在输出文件夹里；手机版在应用数据目录 `Android/data/com.rm8.port/files/`（卸载应用会删除，请先备份）。原版存档可以迁移（交互时会问；命令行 `--saves`），只读取、不覆盖已有的不同存档。
* **为什么不能分享个人 APK / 输出文件夹？** 里面是你自己游戏的数据（和存档），权利属原权利人，只能你本人使用。
* **许可证？** 我们的代码是 GPL-3.0-or-later，只涵盖代码，不涵盖游戏内容；第三方库见 `THIRD_PARTY_NOTICES.md`。
* **权利人联系？** 请在本仓库的 Issues 页面提出，我们会尽快处理（包括下架相关内容）。

---

# 技术说明

目标：用开源的 RenderWare 重写库 librw（MIT 许可，支持 OpenGL ES 2.0+）直接使用大富翁8 的原版 3D 资源，并在安卓真机上运行。
结论：**能。全部资源可加载，15 张原版地图可渲染，安卓真机（Adreno 830）已跑通。**

## 结果

### 1. 解析验证（`loadtest/rm8_loadtest.cpp`，librw NULL 平台，不需要显卡）
直接从原版 `Clump/clump0000.dat` 读取全部 RenderWare 资源：

| 类型 | 成功 | 失败 |
|---|---|---|
| 模型 .dff | 1180 | 0 |
| 动画 .anm | 4312 | 0 |
| UV 动画 .uva | 31 | 0 |

共 19209 个 atomic、205,285 个顶点、152,208 个三角形，其中 351 个是蒙皮几何体。librw 运行时没有输出任何错误或警告。

### 2. 渲染验证（`viewer/rm8_view.cpp`，librw GL3 + GLFW）
直接从原版资源包读取模型和贴图（贴图按材质名在 tex0000/tex0001 中查找 TGA），离屏渲染后保存为 PNG，结果在 `results/`：

| 文件 | 内容 | 说明 |
|---|---|---|
| a01_fr.png | 大老千（行走，右前） | 角色是 2.5D 纸片人，由 17 个贴图平面组成；透明和层次正确 |
| a06_fr.png | 孙小美 | 同上 |
| bld_m01s01.png | 地图建筑：赛马场 | 蒙皮模型，按绑定姿势显示 |
| bld_m05a04.png | 地图建筑：油田 | 普通模型 |
| dta01_02_front.png | 大老千剧情舞台（〔string.bin#3823〕） | 49 个 atomic、11 张贴图，全部正确 |

所有用到的贴图都找到了，没有缺失。

### 3. 整张地图渲染（`scripts/export_scene.py` + `viewer/rm8_scene.cpp`）
先用 Python 把 `.map` + `.ltm` 导出成场景（`scenes/<图号>/scene.txt` 加 64 张烘焙好的地面贴图），再由 rm8_scene 用 librw 组装地形、道路、水面和全部摆件，离屏渲染成 PNG。

**15 张原版地图（01–11、download201–204）全部渲染成功**：64 个地形叶块、道路、全部摆件加载，0 个模型失败。总览见 `results/all_maps.png`，单图见 `results/all/`；香港图另有俯视、斜视和近景（`results/map01_*.png`）。

| 部分 | 来源 | 把握 |
|---|---|---|
| 地形高度、顶点色 | .map 顶点网格；顶点色经 `Map_UpdateTileVertexColors`(0x422bb0) 写入顶点 diffuse | 已验证 |
| 光照贴图（阴影） | .ltm 段 A，按四叉树叶块取用，UV = 普通仿射变换 | 已验证（见下文“修正”） |
| 地面贴图 | landtex.bin 中的 land 贴图 × 光照图 ÷ `light` 值 | 近似：每格 64 像素、land 0 按 1 处理 |
| 道路 | route 图集，`Route_FindTexCoord`(0x436650) 的连接码表 + 旋转表 | 已验证 |
| 摆件 | role.bin 的 art id → dff/贴图目录；变换照 `FUN_0043cb90` | 已验证（物件与道路、地形高度一致） |
| 水面 | 高度/颜色来自 .map；贴图 = `water\entironment<id>`（天空反射图） | 贴图选择已验证；画法是近似 |
| 灯光 | .map 头的环境光、平行光与两个角度 | 已验证 |

art id 能查到但记录为空（无模型）的是不可见标记物，按设计不画：6102、6105、6118、6123。原版数据本身缺一张贴图：`m08s06.dff` 引用的 `m08s06_1.tga` 在贴图包里不存在（地图 08、download204 会报这一处）。

**本轮修正的逆向结论**（已同步到 `docs/map_format.md`）：
- `Map_InitGrid`(0x420cb0) 传给四叉树的根盒子是**反向**的（min = +W·25，max = −W·25），所以第 0 个子节点是 (x−, z−) 象限。旧实现把整片光照图转了 180°：叶编号集合不变、叶中心也照样映射到 (0.5, 0.5)，所以之前的检查都没发现。修正后，光照图 UV 就是标准的 `u = x·m0 + y·m4 + z·m8 + m12`。交叉验证：物件处的阴影暗度从 14.1 升到 34.1，15 张图全部叶中心吻合（误差 ≤ 0.012）。
- 水面贴图编号索引的是 `CWater_Initialize`(0x476a30) 加载的 `water\entironment0..7`，不是 `article\water\waterNN`（后者是旧的猜测）。

**还没复刻的**（会影响观感，但不影响数据正确性）：
- 地形悬崖：游戏用 `cliff\cliffNN.dff` 模板按角高度变形拼出地面，这里用平滑高度网格代替，所以海岸是斜坡而不是陡崖。
- 水面效果：游戏是“反射相机 + 凹凸 + 焦散动画”，这里只是一张半透明贴图。
- 地块建筑（房子等）是运行时按游戏状态生成的，地图文件里没有，所以不画。

用法：
```bash
.venv/Scripts/python.exe -I scripts/export_scene.py "<游戏目录>" 01 port/scenes/01
port/build-gl3/rm8_scene.exe "<游戏目录>" port/scenes/01 out.png --cam <yaw> <pitch> <距离系数> [--target x z] [--size WxH]
```
输出目录必须已存在；写文件失败会报错并返回 1。

### 4. 安卓真机（`app/rm8_app.cpp` + `app/scene.cpp`，SDL2 + librw GLES）
同一份代码同时编成电脑版 `rm8_app.exe` 和安卓 APK `rm8_viewer.apk`（arm64-v8a，Android 7.0+，要求 GLES 3.0）。

**真机实测（骁龙 8 至尊版 / Adreno 830，Android 15）**：
- 能正常启动、读取数据，渲染出香港图（截图见 `results/device_map01*.png`，日志见 `results/device_log.txt`）。
- 香港图加载 109ms，2376×1080 分辨率下约 182 fps。
- 触摸操作可用：单指旋转、双指缩放和平移、双击切换地图、返回键退出。
- Adreno 的顶点 uniform 上限是 256 个 vec4，因此蒙皮着色器自动降到 48 根骨骼，超过的模型改用 CPU 蒙皮（见第 5 节）。
- 开启动画后：画面锁定 120 fps（屏幕刷新率）。不锁帧时约 330 fps，每帧 CPU 耗时为动画更新 0.2ms、渲染提交 1.7ms；其中地图 05 有 20 个模型走 CPU 蒙皮。

**电脑上提前验证安卓路径**（结果都与 Core 3.3 的参考图逐像素一致）：
- `RW_GL_FORCE_GLES=1` / `30` / `20`：分别用 GLES 3.1、3.0、2.0 的着色器。
- `RW_SKIN_MAXBONES=48`：模拟手机的骨骼上限。
- `--cycle 30`：每 30 帧换一张图，15 张图来回切换。Release 和带断言的 Debug 构建都跑通，没有崩溃。

**手机数据目录**：`/sdcard/Android/data/com.rm8.port/files/`，其中放 `game/Clump/clump0000.dat`、`game/Texture/tex0000.dat`、`game/Texture/tex0001.dat`，以及 `scenes/<图号>/`（共约 760MB）。卸载应用会一并删除这些数据。
- 缺资源包时屏幕显示红色，没有场景时显示橙色。
- 详细信息用 `adb logcat -s rm8` 查看。

**一键部署**（手机打开 USB 调试并授权）：
```bash
.venv/Scripts/python.exe -I port/android/deploy.py "<游戏目录>"           # 安装 + 推数据（已有的跳过）+ 启动 + 抓日志和截图
.venv/Scripts/python.exe -I port/android/deploy.py "<游戏目录>" --no-data # 只更新 APK
```

### 5. 摆件动画（`app/scene.cpp`）
15 张图共 1156 个摆件挂上了待机动画，0 失败；另有 48 处带 UV 动画。
验证图：`results/anim_gulls_skinned.png`（海鸥群，蒙皮 76 根骨骼）、`anim_flowers_boy.png`、`anim_bbq.png`、`uvanim_bbq_steam.png`。

| 部分 | 来源 | 把握 |
|---|---|---|
| 播哪段动画 | `role.bin` 第 2 个子表（编号 + `.anm` 路径），取 0 号作为待机循环 | 路径已验证；“0 号即待机”是推断（1/2 号是 Act02/Act03 之类的事件动作） |
| 帧结构 | frame#0 是 clump 根（摆放变换），frame#1 是 HAnim 层级根，动画只驱动层级节点，不会覆盖摆放 | 已用 `rm8_probe` 核对 |
| 推进方式 | 游戏使用 RW 标准接口 `RtAnimInterpolatorSetCurrentTime`（0x56c230）和 `AddAnimTime`（0x56c050），时间单位为秒 | 已验证 |
| 速度、起始时刻 | 速度 1、从 0 开始、循环播放 | 近似 |
| UV 动画 | INI 字段 `HaveUVAnim`（+0x24）和 `UVAnimPath`（+0x534）；加载模型前把 `.uva` 设为当前字典 | 字段已验证；librw 的 GL3 不渲染 MatFX 的 UV 变换，所以改在 CPU 上直接写顶点 UV |

**三类模型的处理：**
- 刚体层级模型：每帧把插值得到的局部矩阵写回节点帧。
- 蒙皮模型：交给 GPU 蒙皮。
- 超过 GPU 骨骼上限的模型：用与着色器相同的矩阵在 CPU 上蒙皮，再增量上传顶点和法线。用 `RW_SKIN_MAXBONES=48` 对比 GPU 和 CPU 两条路径，输出逐像素一致，并另行确认 CPU 路径下模型确实在动。

**踩到的坑**：蒙皮模型挂上层级后，骨骼矩阵在第一次更新前是未初始化的，第 0 帧会出现满屏乱三角形。现在加载完成后会立即按 t=0 摆一次姿势。

**测试开关**：`--time t`（截图前先推进到第 t 秒）、`RM8_NO_UVANIM=1`（关闭 UV 动画）、`RM8_NOVSYNC=1`（不锁帧）。

### 6. 地块、事件牌、开局房子（`scripts/plots.py`）
按游戏加载时的规则复刻了地块放置，详见 `docs/map_format.md` 1.4 节。
- 场景里新增：白色粉笔框的地块垫片、道路格上的六边形事件牌（银行、问答、卡片店、点券等），以及新开局时每条街 1/3 地块上的住宅。
- 香港图共 58 个地块、47 块事件牌、11 栋开局房子。
- 开局房子在原版里每局随机，这里固定随机种子，所以位置和原版不会完全一样。
- 截图：`results/map01_overview.png`、`map01_closeup.png`、`map01_street.png`、`all_maps.png`。

**已知还缺的**（都已在代码里找到出处）：
- 摆件的烘焙光照：`.ltm` 第二段，每个摆件一张 64×64 光照图。游戏用第 2 级纹理阶段以 MODULATE2X 叠加（`Render_CustomShader` 0x43a830、`Render_DrawObjectWithLight` 0x40ee40），UV 由世界坐标乘光照矩阵得到。librw 不支持，需要自写渲染管线。
- 摆件上挂的粒子特效（role.bin 第 5 个子表的挂点，配合粒子表）。
- 悬崖模板地形、真实水面效果（反射、凹凸、焦散）。
- 特殊建筑（电力公司、点券中心、魔法屋、法院）：配置里的 art 10000 不存在，event.bin 里也没有 13~16 号事件，开局时可能本来就只有一块空地，需要对照原版确认。
- 玩家角色、UI、文字。

### 7. 与原版逐像素同机位对照（2026-11-14）
- `scripts/grab_window.py richman out.png`：用 PrintWindow 截取原版游戏窗口（DXVK 窗口也可以）。游戏在后台时会暂停渲染，加 `--front` 会短暂切到前台再截。
- `scripts/game_camera.py`：用 ReadProcessMemory 只读地读出运行中游戏的相机（`*(*(0x61adfc)+4)`，帧 LTM 在 +0x50，视窗在 +0x68，近/远裁剪面在 +0x80/+0x84）。用 `rm8_scene --camfile` 以完全相同的相机渲染，物体位置可以逐像素对上。
- `scripts/label_articles.py`：用游戏相机把摆件投影到图上并标注 art id，用来识别物体。
- 对照图：`results/compare_game_original.png`、`compare_port_same_camera.png`、`compare_stack.png`。

**据此修正的内容：**
- **地面**：原来全图画成了沙地。实际规则是：平地格按四个角的顶点贴图字混合（`Map_RebuildTileGroundMesh` 0x422990、`Material_InitRender` 0x433440），地貌图集是 32 个 64×64 格（左半为 16 种过渡掩码，右半为完整变体），按贴图号升序叠画，编号从 0 开始，即 landtex 的顺序。程序运行时把这些格子拼进 2048² 的图集页。
- **地形光照**：光照图以 MODULATE2X 叠加（`CVertexBufObj_RenderElement` 0x42c2e0 → `Render_DrawObjectWithLight` 0x40ee40），原先“除以 light 值”是错的。`app/lmpipe.cpp` 是自写的光照图渲染管线（GL3/GLES 都可用，含蒙皮版本），以后摆件光照图也走这条管线。
- **悬崖**：`scripts/cliffs.py` 复刻了 14 个模板 × 4 个朝向（码表 `DAT_005e0aa0`；模板 4–7 的根帧带旋转，要乘上帧矩阵），用反距离权重按四角高度变形；地貌字低位为 1 时换用 10–13 号模板（石块墙），并改用 cliff2 贴图。全部 56 个“模板 × 朝向”的几何与码表校验一致。
- **水面**：`app/water.cpp`。每帧用镜像相机渲染倒影（天空环境图加摆件），用 caust00–31 焦散图做凹凸扰动（系数取地图里的 0.04），按投影方式贴到水面，透明度 0.8，与 `CWater_Render` 0x477e90 的纹理阶段设置一致。近似之处：倒影中不含地形（没有裁剪平面）。
- **动画**：事件牌改为 vf23(动作 3, 播放 1 次)，即播完停住；开局房子改为 (动作 2, 0 次)，即循环；地图摆件起始相位取 rand()%10/10 × 时长（`RenderAnim_ApplyTransform` 0x43cb90）。

### 8. 动画冻结 bug（2026-11-14 修复）
“场景动画大量缺失”的主因是我自己的 bug：`rm8UpdateScene` 处理“只播一次”的动画时，把**所有动画共用的 dt** 截断了。事件牌（只播一次，时长 0.165 秒）一播完，排在它后面的所有摆件动画就此冻住。改为每个动画各自计算步长后，原版里在动的物体（彩虹机器滚筒、彩虹圈、船、魔法屋的勺子和帽尖、花树等）在同一相机下都对上了。
- 排查方法：在同一相机下分别对原版和移植版连续截帧，算出运动区域图，逐个比对；再配合 `RM8_DEBUG_ANIM=名字`（打印节点和 atomic 矩阵）、`RM8_DEBUG_RENDER=顶点数`、`RM8_HIDE_VERTS=顶点数`（隐藏某个部件）三个调试开关。
- 顺带确认：BG02 彩虹机器的滚筒是 48 顶点的节点 2（旋转），彩虹圈的“转动”其实是机身材质的 UV 滚动，拉杆每 4.33 秒拉一次。

## 还没做的
- 事件动作（Act02/Act03）、角色走路，以及动画切换和混合。
- 摆件的烘焙光照图（`.ltm` 第二段）：管线已就绪，尚未接入。
- 粒子特效（例如魔法屋锅里冒泡）：粒子表的参数含义还没逆向。
- 奇数级别顶点的悬崖“3”标记（09、download203 有少量格子退回为斜坡）；水面倒影中不含地形。
- **游戏逻辑**：目前只是地图浏览器，还没有 UI、文字、声音和玩法。

## 构建方法
**最省事的办法**：用提取器一站式构建（`rm8_extract.exe <游戏> <输出> --target win|android|both`，缺的工具自动装进私有缓存，见 `extractor/README.md`）。下面是手动构建，给开发者用。

开发机的工具链放在 `<tools>`（用提取器构建时对应的固定版本由 `extractor/rm8x/toolchain.py` 安装）：
- 电脑端：llvm-mingw 20261104、CMake 4.5、Ninja 1.13。
- 安卓端：`android-sdk/` 下有 NDK r29、build-tools 36.1、platforms/android-35、platform-tools；打包时用 JDK 21。下载文件都已核对 Google / GitHub 公布的校验值（`scripts/verify_downloads.py`）。

第三方源码**不在仓库里**，由 `extern/fetch_deps.py` 下载到 `extern/`（固定版本 / 提交，SHA-256 校验，只需要 Python 标准库）：librw @18532c2（MIT）、GLFW 3.5.1、SDL2 2.32.10（zlib）、CPython 2.4.6、stb_truetype、dr_mp3、pl_mpeg、OpenCC 词典；我们自己写的 CPython 配置文件在 `deps_local/cpython24/`。**构建前先运行一次**：

```bash
python extern/fetch_deps.py        # 下载 + 校验 + 给 librw 打补丁 + 放入 deps_local 里的文件；--verify 只检查
```

**librw 补丁**：`patches/apply_librw_android.py`，可重复执行；`fetch_deps.py` 下载 librw 后会自动运行它（对全新下载的上游源码打补丁，结果与之前手工打过补丁的 `extern/librw` 逐文件一致）。共四类改动：
- 骨骼上限，以及 uniform 数据池扩容。
- SDL2 后端改成真正逐个尝试 GL 上下文；新增 GLES 3.0 支持。
- `readTGAMemory`：直接从内存解码 TGA。
- 安卓上链接 GLESv3 和 EGL。

```bash
T=/d/ai/tools
export PATH="$T/llvm-mingw-20261104-ucrt-x86_64/bin:$T/cmake-4.5.0-windows-x86_64/bin:$T/ninja:$PATH"
MW="-G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=x86_64-w64-mingw32-clang -DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-clang++"
# 解析验证
cmake -S . -B build-null $MW -DLIBRW_PLATFORM=NULL && cmake --build build-null && build-null/rm8_loadtest.exe "<游戏目录>/Clump/clump0000.dat"
build-null/rm8_probe.exe "<游戏目录>" 'article\element\Mys021.dff' 'article\element\Mys021.anm'   # 打印帧树、HAnim 层级、蒙皮和动画信息
# 渲染成 PNG（GLFW）：rm8_view、rm8_scene
cmake -S . -B build-gl3 $MW -DLIBRW_PLATFORM=GL3 -DLIBRW_GL3_GFXLIB=GLFW && cmake --build build-gl3
build-gl3/rm8_view.exe "<游戏目录>" out.png 'article\richrole\a01\a01_act01_fr.dff' --yaw 0 --pitch 10
# 交互程序，电脑版（SDL2）
cmake -S . -B build-sdl2 $MW -DLIBRW_PLATFORM=GL3 -DLIBRW_GL3_GFXLIB=SDL2 && cmake --build build-sdl2
build-sdl2/rm8_app.exe "<游戏目录>" scenes [--map 01] [--shot out.png] [--frames n] [--cycle k]
# 安卓
cmake -S . -B build-android -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=$T/android-sdk/ndk/r29/build/cmake/android.toolchain.cmake \
      -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-24 -DLIBRW_PLATFORM=GL3 -DLIBRW_GL3_GFXLIB=SDL2 && cmake --build build-android
../.venv/Scripts/python.exe -I android/build_apk.py      # javac → d8 → aapt2 → zipalign(16KB) → apksigner，输出 rm8_viewer.apk
```
`rm8_view` 运行时会短暂弹出一个窗口（librw 的 GLFW 后端需要窗口来创建 GL 上下文），保存 PNG 后自动退出。APK 用 `android/debug.keystore` 签名，只用于调试。

## 踩过的坑
- `Clump::streamRead` 要求流已经读过 CLUMP 块头。另外 librw 的 `StreamMemory::seek` 默认是绝对定位，`seek(-12)` 不是“后退 12 字节”。
- 相机帧缓冲必须显式指定颜色格式：`Raster::CAMERATEXTURE | Raster::C8888`，否则 `toImage` 无法读回。
- librw 原版的 `readTGA` 只接受文件路径。电脑上的 `rm8_view` 仍然先写临时文件再读；共享场景代码改用补丁加的 `readTGAMemory`，因为安卓上没有 `%TEMP%`。
- **骨骼数上限**：librw 的 GL3 蒙皮着色器写死了 64 根骨骼，而游戏里有 8 个模型超过 64 根（最多 194 根，例如 `article\element\mys021.dff` 76 根、`article\anm\08\dta08_032_a08.dff` 194 根），超出部分会写坏内存导致崩溃。现在的做法：
  - 缓冲区上限提到 256，着色器数组大小在运行时按 `GL_MAX_VERTEX_UNIFORM_VECTORS` 决定，结果导出为 `rw::gl3::skinMaxBones`。
  - 超过上限的模型由 `scene.cpp` 改走默认管线。
- **uniform 数据池**：librw 的全局 uniform 池只有 2048 个 float，256 根骨骼放不下。Release 构建关了断言，会**静默越界**，图照样能渲染出来，直到换了代码布局才崩溃。Debug 构建用断言抓到了这个问题，池子已扩到 8192。教训：改 librw 之后要跑一遍 Debug 构建。
- librw 的 SDL2 后端原本只检查窗口有没有建成，不检查上下文。在安卓上，Core 3.3 的窗口照样能建成，但上下文建不出来，结果拿到的是空上下文。
- `rw.h` 没有头文件保护，同一个 .cpp 不能包含两次；统一经由 `app/scene.h` 引入。
- **安卓数据权限**：adb 新建的目录属于 shell 用户（uid 2000，权限 2770），应用进不去，表现为 `cannot open ...`。`deploy.py` 推送后会把这些目录改成 0775（这些目录属于 shell，adb 有权限改）。
- `writePNG` 在目录不存在时不报错，曾经导致“打印成功但没有文件”；rm8_scene 和 rm8_app 现在都会检查输出文件。
- Git Bash 会改写以 `/sdcard` 开头的参数，用 adb 时要设置 `MSYS_NO_PATHCONV=1`；从 Python 调用 adb 则不受影响。
