# 地产层规则（移植规格）

> 目的：让移植版用 C++ 按原版实现地块、街道、建筑、过路费、买地/加盖、破产、拍卖、日结算等“地产层”规则；电脑决策仍走原版 Python（`AI_xx.py` 的 `TBuy`/`TBuild` 等）。
>
> 依据：
> - `out/final_v2/RichMan8_named.c`。函数名只作索引，结论都按代码判断；名字错的会在文中注明。
> - 关键处用反汇编核对（`scripts/exe_dis.py`）。Ghidra 常丢掉 thiscall 的 `this` 和栈参数，凡是涉及“对谁调用”的结论都看过反汇编。
>
> 置信度：
> - **[V]**：逐行读过代码，或已用反汇编核对。
> - **[H]**：代码证据强，但含义是推断的。
> - **[M]**：只读了部分代码，或含义不确定。
> - 文中写明“推测”的，都是猜测。
>
> 前置文档：`docs/turn_flow.md`（消息队列、状态机、落地序列）、`docs/gameplay_algorithms.md`、`docs/map_format.md`、`docs/config_files.md`、`docs/ui_system.md`（事件任务族、模态对话框）。
>
> 本文新增的脚本：
> - `scripts/strfn.py`：打印反编译函数，并自动把 `StringTable_Get()+偏移` 注成 string.bin 文本；`--id N…` 查字符串。
> - `scripts/msg_senders.py`：按反汇编列出所有 `Role_EnqueueMsg/EnqueueMsgAt` 的调用点及其命令号。
>
> 写法约定：
> - 金额 `PI` = 物价指数 `g_GameState+0x30`。
> - “现金”= 角色 +0x290，“存款”= +0x294。
> - `/` 表示 C 的有符号整数除法（向 0 截断）。

---

## 0. 结论速览

| 项 | 规则 | 依据 |
|---|---|---|
| 加盖费（一次） | 住宅 250·PI，商业 500·PI | `Terra_BuildUnitCost @0x4efd50` [V] |
| 地价 | `PI × ( 加盖费·等级 + 基价·地价% ·街道系数 / 10000 )`<br>基价：住宅 3000，商业 5000<br>注意等级部分实际乘了 **PI²** | `Terra_CalcPrice(1) @0x4efc80` [V] |
| 住宅过路费 | `PI × Σ( RentTable[等级] × 街道系数 / 100 )`<br>Σ 对象：本块 + 同街同地主的其他住宅<br>RentTable = 500,1000,2500,5000,8000,12500 | `Terra_CalcRent(1) @0x4efda0` [V] |
| 地产奇迹 | 付租者拥有：−20%；地主拥有：+20%（`r*20/100` 截断） | `@0x486d9e` [V] |
| 神明 | 财神：免租；穷神：×2（在奇迹调整之后） | `Role_PayToll @0x487c80` [V] |
| 免租 | 街道查封、地主不在正常状态（含被糊涂神附身）、地主破产、付租者隐形（+0x1E4>0） | `@0x486580`、`@0x4868e0` [V] |
| 租金流向 | 付租者先扣现金，不足部分扣存款，扣到 0 为止；地主**全额存入存款**（不足部分凭空产生） | `@0x487f20`、`@0x47e720` [V] |
| 连锁超市 | Σ(地主所有超市的等级) × 1000·PI × 骰子(1..6) | `@0x4f76d0`、`@0x4f6c80` [V] |
| 饭店 | 等级 × 2000·PI × 骰子，并住 1 天 | `@0x4f7720`、`@0x4852e0` [V]/[H] |
| 现金不足 | 买地/加盖只看**现金**（不看存款）；付款则先现金后存款。**没有自动变卖地产或股票，也没有自动贷款** | [V] |
| 破产 | 已进行天数 ≥ 1，且 现金 + 存款 ≤ 0（只对当前行动者检查） | `Role_CheckBankruptcy @0x48ee10` [V] |
| 破产后的财产 | 地产全部变成无主（保留房屋等级）；随机 ≤3 块进入“破产拍卖”；股票退回市场；卡片清空 | 第 3 节 [V]/[H] |
| 物价指数 | 每天更新：`PI = min(99, 平均净资产 / 开局平均资金)`，只升不降 | `@0x4a6a00`、`@0x4a1090` [V] |
| 月结算（每月 1 日，第 6 天起） | 存款利息 10%<br>金钱奇迹：拥有者 20%，其他人 5% | `@0x4afa40` [V] |
| 开局资金 | 选项 50000 / 100000（默认）/ 200000，**一半现金、一半存款** | `Game_SetOption @0x504940`、`Role_InitState @0x480d30` [V] |

---

## 1. 运行时数据模型

### 1.1 全局对象

| 全局 | 内容 |
|---|---|
| `g_GameState = *(0x632dcc)` | 棋盘场景对象 CGamePlayScene（vtable 0x5e00e8）。字段：<br>+0x04..+0x10 当前年 / 月 / 日 / 星期（第一天的星期取自系统，0 = 周日；之后在 1..7 间循环，7 = 周日）<br>+0x14 已进行天数<br>+0x18..+0x28 **结束日期**（不是开局日期，见 5.1）<br>+0x2C **物价基数** = 开局时的平均（现金 + 存款）<br>+0x30 物价指数 PI（初值 1）<br>+0x34 当前事件任务（对话框 / 动画任务，见 ui_system 5.3）<br>+0x48 游戏状态：1 进行中，2 / 3 结束类（推测 3 = 失败）<br>[V] `Game_InitGame @0x4a1090`、`Game_InitUI @0x4a14a0` |
| `g_TerraMgr = *(0x63aba0)` | +0x00 道路点（StPoint）数组，+0x0C 道路点数（`StPoint.Sum`）<br>+0x10 地块（Terra）数组，+0x1C 地块数（`Terra.Sum`）<br>+0x30 奇迹建地数组，+0x3C 其数量<br>+0x40 / +0x44 事件为 8 / 7 的道路点（医院 / 监狱）<br>+0x48 场景包围盒<br>[V] `TerraMgr_CreateAll @0x4f07d0`、`Land_CreateObject @0x4f1320`、`py_StPoint_Sum` |
| 街道表 `&DAT_00632dd4`（`Singleton_GetPtrArray_00632dd4`） | street.bin 每节一条，按文件顺序排列（下标 0 是 `ID=0` 的空名街道，price 100）。见 1.3 |
| 奇迹表 `*(0x6403bc)` | 3 项 × 0x24 字节。见 1.5 |
| 开局设定 `&DAT_00632de8`（`Singleton_GetPtrArray_00632de8 @0x4083f0`） | +0x00 闯关模式标志<br>+0x04 地图号<br>+0x0C 开局资金总额<br>+0x10 开局点券<br>+0x14 交通工具（0 步行、1 机车、2 汽车）<br>+0x18 时限（0 三个月、1 六个月、2 不限）<br>+0x1C 胜利条件（0..5）<br>+0x20 / +0x2C 玩家数组<br>[V] `Game_SetOption @0x504940`、`Game_InitOptions @0x5031c0` |

### 1.2 地块对象 Terra（0x98 字节，vtable 0x5e660c）

由 `u_BuildObj_CreateHouse @0x4f1680`（名字不准，它就是“创建地块”）创建，并由 `Terra_Init @0x4eeb20` 初始化 [V]。

| 偏移 | 含义 | 初值 / 来源 |
|---|---|---|
| +0x04 | 地块上模型（摆件）句柄：房屋或地主旗；-1 = 无 | -1 |
| +0x38 | 商业建筑对象（1.4），住宅为 0 | 0 |
| +0x3C | 低 16 位：地块类型（terra.bin id：1 住宅建地，2 〔string.bin#14〕）<br>高 16 位：建筑类型 id（building.bin：1 住宅，2 研究所，3 连锁超市，4 饭店）；0 = 未建 | 低位来自格子 `+0x44 & 0xFF` |
| +0x40 | 地块组号（同一次摆放的格子共享，即格子 +0x40） | 地图 |
| +0x44 | 等级 0..5（0 = 空地） | 0 |
| +0x48 | **地价百分比**：初值 100；新闻“炒楼 / 闹鬼”每次 ±10%（`Terra_AdjustPriceByPercent @0x4eed70`：`v = v*p/100 + v`） | 100 [V] |
| +0x4C | 地主座位号，-1 = 无主 | -1 |
| +0x50 | 街道表下标（`Container_Find(格子+0x60)`；按 street.bin 的 ID 查找，找不到时为 0）。只有住宅建地有街道；〔string.bin#14〕落在下标 0（price 100） | 地图 [V] |
| +0x54 | 0x80000000（用途未查） | |
| +0x88..+0x90 | 位置；+0x94 朝向 yaw | 格子 +0x4C..+0x58 |

只有地块类型 1、2 会建成 Terra。4~9 号建地建成奇迹建地对象（`@0x4f1820`）；100~103 号建成电力公司、点券中心、魔法屋、法院等特殊建筑（见 map_format.md 1.4）。

### 1.3 街道记录（street.bin 一节）

加载函数：`@0x42d210`（旧名 `Card_LoadPriceConfig`，实际读 street.bin）[V]。

| 偏移 | 含义 |
|---|---|
| +0x000 | ID（street.bin 的 `ID=`） |
| +0x004 | 名称 char[256]（文字里的 `%s` 街名） |
| +0x104 | **街道系数**（price，单位 %） |
| +0x108 | 查封剩余天数（>0 = 查封，免租；查封卡 `Street.Close(s,9)` 写入 9） |
| +0x10C | 涨价倍率（float，初值 1.0） |
| +0x110 | 涨价剩余天数 |

**涨价 `Street.Flow(s, pct)`**（消息 0x30 → `Terra_SetHeight @0x42d4c0`，旧名不准）[V]：
- 设新倍率 c = pct/100.0；
- `price = ftol(c / 旧倍率 × price)`；
- 写入 +0x10C = c，+0x110 = 9。
- 涨价卡 CardPy_29 用 `Street.Flow(s,200)`，即街道系数翻倍 9 天。

**每天倒计时** `Terra_UpdateHeightAll @0x42d650` [V]：
- +0x108、+0x110 各减 1；
- +0x110 减到 0 时恢复：`price = ftol(price / 倍率)`，倍率 = 1.0。ftol 是截断，所以价格可能比原值少 1。
- 地面贴图：查封贴 `land_close.tga`；涨价贴 `land_up.tga`（倍率 ≤1 时贴 `land_down.tga`）。

### 1.4 商业建筑对象（Terra+0x38，0x24 字节）

由 `@0x4f5de0`（旧名 `Card_CreateUI`）按建筑类型 2/3/4 创建，在 `Land_OnEnter` 第一次把商业地建成 1 级时生成。

| 偏移 | 含义 |
|---|---|
| +0x00 | vtable：研究所 0x5e6d28 / 连锁超市 0x5e6c48 / 饭店 0x5e6bd8 |
| +0x04 | 地主座位号（`Land_SetOwner` 同步） |
| +0x08 | 等级（`Land_OnEnter` 同步） |
| +0x14.. | 研究所：研发状态（存档里额外 8 字节） |

**本对象同时是“事件任务”**：落地时由 `Game_SelectCard @0x4a40a0`（旧名不准）挂到 `g_GameState+0x34`。

| 虚函数 | 作用 |
|---|---|
| +0x40 | 开始 |
| +0x44 | 基础价 |
| +0x04 | 每帧更新 |

**+0x44 基础价** [V]：
- 研究所：恒为 0；
- 连锁超市：`Σ(该地主所有建筑类型 3 的地块等级) × 1000 × PI`（`@0x4f76d0` + `TerraMgr_SumLevelByOwnerType3 @0x4f1270`）；
- 饭店：`本店等级 × 2000 × PI`（`@0x4f7720`）。

`Terra.Rent`（Python）对商业地返回 `基础价 × 4`（`Terra_CalcRent` 的非住宅分支）。原版 C++ 收费**不用**这个值。

### 1.5 奇迹

**奇迹表** `*(0x6403bc)`（`Wonder_Manager_Init @0x4e1670`），3 项，每项 0x24 字节 [V]：

| 偏移 | 含义 |
|---|---|
| +0 | 编号：0 金钱奇迹，1 地产奇迹，2 卡片奇迹 |
| +4 | byte：本图有此奇迹 |
| +8 | 拥有者座位号，-1 = 无 |
| +0xC..+0x18 | 4 个座位已投资的份额（float） |
| +0x1C | 基数 50000 |
| +0x20 | 每次份额 0.1 |

- `Wonder_CheckConstruction(i) @0x4e1800`：不存在返回 -1；拥有者已破产返回 -2；否则返回拥有者座位号。
- **投资** `Wonder_CalcInvestmentCost @0x4e18d0`：
  - 每次花 `round(50000×0.1) = 5000`，**不乘 PI**；只能用现金，现金不足则不能投资（str210）。
  - 份额 += 0.1；全部座位份额之和 ≥ 0.999 时，**本次投资者**〔string.bin#206〕。[H]（完成时由 `@0x4e09c0` 发 0x34）
- **效果**：
  - 0 号（金钱）：月息，见 5.3；
  - 1 号（地产）：租金 ±20%，见 2.6；
  - 2 号（卡片）：点券价 ∓20%（str208，本文未展开）。
- 地图上的奇迹建地对象（存档里的 Wonder：+0x3C 阶段、+0x44 拥有者、+0x48 投入）与这张表的关系未逐行核对 [M]。

### 1.6 角色上与地产 / 金钱有关的字段

| 偏移 | 含义 | 依据 |
|---|---|---|
| +0x1B0 | 电脑控制 | [V] |
| +0x1B4 | 身份状态（7 破产） | [V] |
| +0x1BC | 附身：1 财神、2 穷神、3 衰神、4 福神、5 土地公、6 糊涂神… | [V] |
| +0x1C0 | 路霸卡受益人座位号（-1 无）。地主有此值时租金转给受益人 | `Role_PayToll` [H] |
| +0x1D4 | 本局已做过破产判定 | 分发器 [V] |
| +0x1E4 | **隐形剩余回合**（隐形卡 → 消息 0x46 设为 10，每回合开始减 1）。期间不付过路费、不进连锁超市 / 饭店 | `@0x48ed30`、`@0x48ed70` [V] |
| +0x1F4 | 角色颜色 RGBA（地块着色用） | `Land_SetOwner` [V] |
| +0x290 / +0x294 | 现金 / 存款（上限 999,999,999，下限 0） | [V] |
| +0x298 | 贷款额；+0x29C..+0x2AC 借款日期（年、月、日、星期、天数）；+0x2B0 高 16 位 = 已收息月数 | `Money_TakeLoan @0x47e970` [V] |
| +0x2B4 | 8 个卡片槽 | [V] |
| +0x324 | 点券（开局上限 9999） | [V] |
| +0x338 / +0x33C | 本局累计收租 / 付租（月结算展示用） | `@0x487f20` [V] |
| +0x340 | “倒楣天数”（月结算展示用） | [M] |

### 1.7 建立过程

**载入地图**（新局与读档相同）：
1. `Game_LoadMapData` 读 `Map\NN.map`；
2. `TerraMgr_CreateAll @0x4f07d0` 对每个格子调用 `Land_CreateObject @0x4f1320`；
3. 格子 `+0x44&0xFF ∈ {1,2}` 时按组号去重建 Terra（`@0x4f1680`），`∈[4,9]` 时建奇迹建地，同时建道路点（`Tile_CreateInstance`）。

摆放细节见 map_format.md 与 `scripts/plots.py`。[V]

**配置**（`Game_LoadAllConfigTables`）：
- street.bin → 街道表（`@0x42d210`）；
- building.bin → 建筑表（`Terrain_FindById @0x4396d0`，记录 +0x104 = scale、+0x108..+0x118 = Level1..5artId）；
- terra.bin → 地块类型尺寸。

**新局** `Game_InitGame @0x4a1090` [V]：
1. 创建角色：`Role_InitState @0x480d30`：
   - 现金 = 存款 = `setup+0x0C / 2`；
   - 点券 = `min(setup+0x10, 9999)`；
   - 交通工具 = `clamp(setup+0x14 + 1, 1, 3)`；
   - 隐形、破产判定等清零。
2. `g_GameState+0x48 = 1`，PI = 1。
3. 物价基数 +0x2C = `Game_CalcAvgPlayerWealth @0x4a6b30` = 未破产角色（现金 + 存款）的平均值。
4. 当前日期 = 系统日期，已进行天数 = 0；结束日期 = 当前日期的月份 +3（时限 0）或 +6（时限 1）。超过 12 时减 12，**年份不变**（原版疏漏）。因为 `Date_CalcDaysDiff @0x431680` 只按月份循环计算，所以不影响结果。
5. `Game_StartNewRound @0x4962a0`：
   - 每人加 3 张卡（参数 8、10、11）[M]；
   - 闯关模式时，0 号角色：现金 = 储藏箱现金 + pass 的 start_cash，存款 = start_fund，点券 = 储藏箱点券 + start_ticket [H]；
   - 入队 0x68。

**开局资金选项**（`Game_SetOption @0x504940`）[V]：
- 总额 {50000, 100000, 200000}；`Game_InitOptions` 默认 100000，即**现金 50000 + 存款 50000**；
- 点券 {0, 50, 100}，默认 50；
- 交通工具：默认步行；
- 时限：默认不限；
- 胜利条件：默认 5（〔string.bin#7046〕）。

**读档**：先按地图重建 Terra，再按存档覆盖 owner、+0x48、类型 | 建筑、等级，并用 `Land_OnEnter` 规则重建模型（save_format.md 第七、八节）。

### 1.8 价格函数（全部 [V]，已对照反汇编）

```
BuildUnitCost(t) = (t.type==1 ? 250 : 500) * PI                                   @0x4efd50

CalcPrice(t, mulPI) =                                                             @0x4efc80
    base  = ((t.type==1 ? 3000 : 5000) * t.pct48 * street[t.street].price) / 10000   // 先乘后除，一次截断
    v     = BuildUnitCost(t) * t.level + base
    return mulPI ? v * PI : v
  // 买地、显示的“購買價格”、Terra.Charge（无主时）、拍卖底价都用 mulPI=1。
  // 净资产（物价指数）用 mulPI=0；月结算“總資產”用 1。
  // 注意：mulPI=1 时等级部分是 250·PI·level·PI（PI²），原版就是这样。

CalcRent(t, chain) =                                                              @0x4efda0
    if t.type == 1:
        p = street[t.street].price
        r = RentTable[t.level] * p / 100
        if chain: for u in 所有地块, u.id40 != t.id40 且 u.type==1 且 u.street==t.street 且 u.owner==t.owner:
                       r += RentTable[u.level] * p / 100
        return r * PI
    elif t.building: return t.building->vf44() * 4      // 仅 Python Terra.Rent 用
    else: return 0
```

注意：地主为 -1 的空地也会进入连锁累加，但只有“他人地”分支才调用，所以不影响结果。RentTable 在 0x5e65b0。

**算例**（PI = 1，街道系数 120，3 级住宅，同街同地主还有一块 1 级地）：
- 租金 = (5000×120/100 + 1000×120/100) × 1 = 7200；
- 若 PI = 2：租金 14400；同一块地的地价 = (500×3 + 3000×100×120/10000) × 2 = (1500 + 3600) × 2 = 10200。

---

## 2. 落地（0x1D → 0x12 → 0x11）

### 2.1 顺序（接 turn_flow 2.3 第 10 步）

```
停下 → 入队 0x23(点)、0x1D(点)
0x1D Role_OnArriveNode @0x4861a0
  ├─ 点上有事件图标且未附身糊涂神 → Role_TriggerLandEvent(事件 id)（银行、卡片店……与地产无关）
  └─ 否则 Role_TriggerLandLevelEvent(点) @0x4863f0：点的格子 +0x44&0xFF ≥ 4（奇迹建地）
         → 事件 0x5A + (v−4)/2（0x5A 金钱 / 0x5B 地产 / 0x5C 卡片奇迹的 wonder.ui）
     不成立时：入队 0x12(点)；若点上有地块（点+4），再调用 NPC_LandProperty_OnTrigger(座位)
         → 视觉高亮任务（2.9），状态 = 9
0x12 Role_OnArriveCommercialBuilding @0x486260 → 可能开商业建筑任务（状态 9）；最后入队 0x11(点)
0x11 Role_ProcessArriveLand @0x4868e0 → 买 / 盖 / 付租（2.3）；最后视情况入队 0x22(地块)
```

控制流全部 [V]。状态 9 期间不取消息，任务结束后由 `Role_UpdateStatusTransition` 回到 8，再继续取 0x11 等后续消息（turn_flow 2.2）。

`Land_GetLevel @0x4f0600` 读的是“道路点所在格子”的 `+0x44&0xFF`。哪些道路格子会带奇迹建地的类型值，取决于地图加载时怎样给道路格子写邻接信息，这一点没有追 [M]。

### 2.2 0x12：商业建筑 `@0x486260` [V]

条件：
- 点上有地块 t，且 `t.owner ≥ 0`、`t.level ≠ 0`、`t.type == 2`；
- 再按建筑类型（`t.+0x3C >> 16`）判断：

| 建筑 | 何时进入 | 内容 |
|---|---|---|
| 2 研究所 | **本人**的地，且未附身糊涂神 | 研发卡片。人类打开 graduate 界面，电脑随机研发（`@0x4f6360`，str263）[M] |
| 3 连锁超市 | **他人**的地，且未隐形（+0x1E4<1），且 `Role_CheckRentExempt(t,0)==0` | 消费流程（下表） |
| 4 饭店 | 同上 | 住宿流程（下表） |

条件成立时，`Game_SelectCard(建筑) @0x4a40a0` 调用建筑 vf+0x40（`Hotel_BeforeVisitor @0x4f6ba0`，超市与饭店共用）打开界面（market.ui / hotel.ui），任务挂到 `g_GameState+0x34`，状态 = 9。**无论是否进入，最后都入队 0x11(点)。**

**超市 / 饭店流程** `Hotel_ProcessVisitor @0x4f6c80`（每帧，两类共用，字段为建筑对象 dword 下标）[V]：
1. 状态 1 → 2 → 10：显示欢迎语与“请选择 xx”。
   - 字符串：超市 5000+n、饭店 5100+n，n = 1..5；董事长免付的提示是 5006 / 5106。
2. 状态 10：掷单骰。
   - 人类在界面里点；电脑在 vf10 返回 0 时直接 `vf12(rand()%6+1, 1)`。
   - vf10 = `Hotel_CheckVisitor @0x4f7550`：角色状态为 9 时调用 `Role_StartDiceSceneCount1` 掷单骰（turn_flow 1.3），点数经场景 vf12 交给 `Hotel_InitiateStay @0x4f7610` → `[6] = 点数`，进入状态 3。
3. 状态 3：`[7] = vf+0x44()`（1.4 的基础价），显示 5003 / 5103“您选择的…为 %d，〔string.bin#5003〕 %d”。
4. 状态 4：`[7] *= 点数`，显示 5004 / 5104“请缴纳 %d 元”。
5. 状态 0xB：按当前行动者调整。
   - 带保镖（vf25 ≠ 0，莎拉专属）→ 0（str556）；
   - 附身 1 财神 → 0（str407）；
   - 附身 2 穷神 → ×2（str409）；
   - 否则在闯关模式下，若 0 号座位且角色 Id 2（萊娜）→ `×7/10`（str53）。
6. 状态 5：若 `[7] > 0`，当前行动者 `Role_SubMoney([7], 现金优先, 表情)`，地主 `Role_AddMoney([7], 存款)`。
7. 状态 6：关界面，返回 vf+0x6C。
   - 饭店的 vf27 `@0x4f7750` 再入队 **0x13** → 角色 vf34 `CPlayerTag__EnterHotelStatus @0x4852e0`：走到饭店（状态 11），然后身份状态 = 3、+0x1D8 = **1 天**。
   - 超市无此步。

**公式**：
- 超市 = Σ(地主所有超市等级) × 1000 × PI × 点数；
- 饭店 = 饭店等级 × 2000 × PI × 点数。

### 2.3 0x11：`Role_ProcessArriveLand(点) @0x4868e0` [V]

设 t = 点上地块（没有则返回 0），me = 当前角色，s = me.附身。

**A. 无主（t.owner < 0）**
- s ∈ {5 土地公, 6 糊涂神} 时什么都不做。
- 否则：
  - `price = CalcPrice(t,1)`；
  - `Role_CheckCashShortNotify(price) @0x486510`：**现金** < price 时入队 `0x1B(str2 "您的現金不足")` 并结束本分支；
  - 人类：
    - 文字：住宅用 str1(街名, price)；商业用 str1(str14“商業建地”, price)；
    - 入队 `1(文字, 0x80)`、`0x1C(CUi_okcancel)`、`9(t, 0x80000000)`；
  - 电脑：`AI_AskTBuy(t) @0x4fbfe0` = Python `TBuy(t)`，返回非 0 时入队 `9(t, 1)`。

**B. 本人的地（t.owner == me.座位）**
- 前提：`s ≠ 6` 且 `t.level < 5`。
- `cost = BuildUnitCost(t)`；做同样的“现金不足”检查。
- 人类：
  - 等级 0 的住宅：str3(街名, cost)，对话框 okcancel（0x80）；
  - 等级 0 的商业地：str5(街名)，对话框 **CUi_business（0x81，选建筑）**；
  - 等级 > 0：str4(街名, 等级, 建筑名 `Land_GetName`, cost)，okcancel；
  - 入队 `1(文字, 0x80/0x81)`、`0x1C(对话框)`、`0xB(t, 0x80000000)`。
- 电脑：`AI_AskTBuild(t) @0x4fc080` = Python `TBuild(t)`，返回非 0 时入队 `0xB(t, 1)`。

**C. 他人的地**
- 只处理 `t.type == 1`（住宅），且 me 未隐形（+0x1E4 < 1）。
- `r = CalcRent(t,1)`，再按地产奇迹调整：
  ```
  w = Wonder_CheckConstruction(1)
  if w == me.座位:       r = r - r*20/100
  elif w == t.owner:     r = r*20/100 + r
  ```
- `r > 0` 且 `Role_CheckRentExempt(t, r) == 0` 时：
  - 入队 `0x1B(str6(街名, 地主名, r))`；等级 > 0 时改用 str7(街名, 等级, 建筑名, 地主名, r)；
  - 入队 `0x1F(r, t.owner)`。
- **商业地不在这里收费**，已在 0x12 收过。

**D. 收尾**
- 若 `s ≠ 4`，或 t 是本人的地，入队 `0x22(t)`（2.8）。
- 因此被福神附身时，停在无主地或他人地不入队 0x22。

### 2.4 人类界面接口

| 用途 | 消息序列 | 界面 | 返回值 |
|---|---|---|---|
| 买地 | `1(文字,0x80)` → `0x1C(ui)` → `9(t, R)` | okcancel.ui（CUi_okcancel 0x493a80） | 确定 = 1，取消 = 0 |
| 加盖 / 升级 | `1(文字,0x80)` → `0x1C` → `0xB(t, R)` | 同上 | 1 / 0 |
| 商业空地选建筑 | `1(文字,0x81)` → `0x1C` → `0xB(t, R)` | business.ui（CUi_business 0x493c40，按钮控件 2/3/4） | R = 控件 id = 建筑类型 2 研究所 / 3 超市 / 4 饭店；取消 = 0 |
| 他人地提示 | `0x1B(文字)` | ginfo/gtinfo | 只显示 |

机制 [V]：
- **消息 1**：`Role_OpenModalUi(kind=b&0xFFFF, 文字, …) @0x4883c0`。显示对话框，记录 +0x21C = kind，状态 = 7。
- **结果写回**：
  - 确定键 / 按钮 → 场景 vf10 `@0x4a2ba0` 写 +0x228 = 1；
  - 取消 → vf11 `@0x4a2cf0` 写 0；
  - business 的按钮 → vf12 `@0x4a2e10` 写控件 id。
  - 写入后 +0x224 = 1，`Role_CloseModalUi @0x4889b0` 把结果写进队列里第一条 b == 0x80000000 的消息，状态回 8。
- **0x1C**：`Obj_CallVfuncWithFF(ui)` 调用对话框 vf7(−1)，即隐藏。
- **0x9 / 0xB**：b == 0 时什么都不做。
- **0x1B 文字框**：
  - 第一次执行时把自己以 b = 1 插回队首，并让镜头对准角色；
  - 第二次 `Role_ShowInfoDialog @0x488ae0` 显示对话框，再在队首插入 `0x65(180 帧 = 3 s)` 和 `0x1C(隐藏)`。

### 2.5 电脑：Python 回调

**调用方式** [V]：
- `AI_CallScript(n, 地块指针) @0x4f7810` → `Python_CallFunction`；
- 参数为 `PyInt(地块对象地址)`，返回 `PyInt_AsLong`；函数不存在时为 0。
- 16 个 `AI_xx.py` 的 TBuy / TBuild 完全相同：
  ```python
  def TBuy(param):  s = Role.Shape(Role.CurRole()); return 0 if s == 3 or s == 5 else 1
  def TBuild(param): return 0 if Role.Shape(Role.CurRole()) == 3 else 1
  ```
- 返回非 0 时，C++ 入队 `9(t,1)` / `0xB(t,1)`。电脑在空商业地上 b = 1 不在 2..4 之间，所以**建筑类型随机** `rand()%3+2`（2.7）。

### 2.6 过路费的付款链

**`0x1F(r, owner)` → `Role_PayToll @0x487c80`** [V]：
1. me.附身 1 财神：r = 0，队首提示 str407。
2. 附身 2 穷神：r = r×2，提示 str409。
3. 否则若 me.vf25()（莎拉带保镖）：r = 0，提示 str567。
4. r < 1：70% 概率入队表情 0x5A（按**原租金**分档：≥5000·PI 用 0xF，≥1000·PI 用 0xE，否则 0xD），结束。
5. 地主的路霸受益人 `owner.+0x1C0 ≥ 0` 且受益人未破产：调用 `Role_TransferRentRoadblock(r, 受益人, owner) @0x488100`。me 付 r，受益人存款收 r，提示 str566。
6. 否则在队首后（有提示时是位置 1）插入 `0x20(r, owner)`。

**`0x20(r, owner)` → `Role_TransferRentToOwner @0x487f20`** [V]：
- 先 `Role_TryUseFrameupCardOnRent(r) @0x4906c0`（嫁祸卡，Id 24）：
  - 条件：r ≥ 10000（不乘 PI）且手上有该卡；
  - 人类：打开选人界面（0x84，str569），结果经 `0x4D(r, 目标+1)`；
  - 电脑：若 (现金 + 存款) ≥ r 且 ≥ 50000·PI，只有 25% 概率使用；否则必用；目标在其他未破产角色中随机，提示 str529，入队 `0x4D(r, 目标+1)`。
  - 用了嫁祸卡：本函数只给地主**存款**加 r（+0x338 += r）。
  - `0x4D` 处理函数 `@0x490a40`（旧名 `OnLuckyGodEvent`）：
    - b−1 ≥ 0：删掉 me 的嫁祸卡，目标 `Role_SubMoney(r, 存款优先)`，目标 +0x33C += r；
    - b−1 < 0（取消）：me 照常付现金。
- 否则：
  - `Role_SubMoney(r, 现金, 60% 表情)`，me +0x33C += r，音效 0x7B；
  - 地主 `Role_AddMoney(r, **存款**, 40% 表情)`，+0x338 += r；
  - 地主有表情时，双方再入队 0x2E 互动。

**扣款规则** `Bank_WithdrawMoney @0x47e720` [V]：
```
acc=现金时: 现金 -= r; if 现金 < 0: 存款 = max(0, 存款 + 现金); 现金 = 0
acc=存款时: 存款 -= r; if 存款 < 0: 现金 = max(0, 现金 + 存款); 存款 = 0
```
- **付不起的部分直接消失，地主照收全额**。
- 加钱 `SFLB_MoneyAdd @0x47e640`：上限 999,999,999。

**免租** `Role_CheckRentExempt(t, r) @0x486580` [V]，返回 1 = 免付：

| 条件 | 提示 | 返回 |
|---|---|---|
| 街道查封（street.+0x108 ≠ 0） | str10“〔string.bin#10〕” | 1 |
| 地主身份状态 0 且未附身糊涂神 | — | **0（照付）** |
| 状态 0 但附身糊涂神，或状态 1..6（冬眠、饭店、医院、监狱、消失等） | str9“%s狀態異常，〔string.bin#9〕” | 1 |
| 状态 7（破产） | 无 | 1 |
| 地主座位无效 | 无 | 1 |

除“照付”和“破产”两种情况外，返回 1 前都有 70% 概率入队表情（0xD/0xE/0xF，分档同上）。

### 2.7 消息 9 / 0xB 的执行

**`9(t, b≠0)` → `Role_BuyLandConfirmed(t) @0x487030`** [V]：
- `price = CalcPrice(t,1)`。
- **衰神（3）**：
  - 照扣 price（`SubMoney_NoEmote`，现金优先），**不得地**，提示 str412；
  - 70% 概率表情 4 或 5；返回 0。
- 否则：
  - `Role_TakeLandOwnership(t, 0)`：`Land_SetOwner(座位)`，音效 1；
  - **福神（4）**：price = 0，提示 str414；
  - price > 0 时扣款（现金优先）；
  - 表情：本人在该街持有 ≥3 块 → 0x11；否则等级 ≠ 0 时 70% 概率表情 1。
- **不检查原主**：购地卡（CardPy_01 → `Terra.Buy`）在他人地上也走这里，原主**拿不到钱**，房屋等级保留。

**`0xB(t, b≠0)` → `Role_BuildOnLand(t, b, 收费=1) @0x487380`**（t == 0 时随机取自己 0..4 级的地）[V]：
1. 收费时先扣 `BuildUnitCost(t)`（现金优先）。
   - **衰神**：已扣的钱不退，提示 str412，等 30 帧，70% 概率表情，返回（不加盖）。
2. 新等级与类型：
   - 住宅：类型 1，等级 +1；
   - 商业等级 0：类型 = b（2..4），b 不在 2..4 时取 `rand()%3+2`，等级 → 1；
   - 商业等级 > 0：类型不变，等级 +1。
3. 新等级 < 6 时调用 `NPC_CreateOrLandEnter(类型, 等级) @0x4eeee0`（旧名不准，作用是开始加盖动画）：
   - 能播动画：任务挂 `g_GameState+0x34`，状态 = 9，**等级在动画中途才写入**（6.2）；
   - 不能播：立即 `Land_OnEnter`。
4. 本人的地：等级到 5 时表情 0x10，并在队首插入 `0x52(0x177A, t)` 特效；否则本人在该街 ≥3 块时表情 0x12，或 70% 概率表情 1 / 2。
5. 福神在这里**没有免费**处理，免费靠 0x22（2.8）。

### 2.8 `0x22(t)` → `Role_ApplyGodOnLand @0x48bb50` [V]

仅当 me 身份状态 < 3：
- **福神（4）**：t 是本人的地且等级 < 5 时，提示 str415，在位置 1 插入 `0xC(t, b)`（免费加盖）。
  - 住宅或已建商业地：b = 1；
  - 商业空地：b = `rand()%3+2`。
- **土地公（5）**：t 不是本人的地（含无主地）时，提示 str416，在位置 1 插入 `0xA(t,1)`，即 `Role_TakeLandOwnership(t,1)` **免费占地**，保留房屋；70% 概率与原主互动。

福神叠加效果：
- 停在自己的地上：先问一次付费加盖，再免费加盖一次；
- 停在无主地上：买地免费，但当回合不会再免费加盖（0x22 在入队时就被跳过）。

### 2.9 落地高亮 `NPC_LandProperty_OnTrigger @0x4eedd0`（纯视觉）[V]

t 有主且 me 未隐形时：
- 他人住宅：同街同地主的住宅全部高亮（`BuildObj_SetAlpha(1)`），无房的地主旗播放动作 1；
- 商业建筑：建筑播放动作 1。

不改任何数值。

---

## 3. 现金不足、贷款、破产、拍卖、结束

### 3.1 现金不足

- 买地 / 加盖前只比较**现金**；不够就提示 str2，不给选项（人类和电脑都一样）。
- 被动付款（租金、超市、饭店、罚款）先扣指定账户，不足部分从另一账户扣，两个账户都扣到 0 为止。
- **没有**自动卖房、卖股、抵押或强制贷款的代码路径（全库没有相关字符串或函数）。
- 存取款、贷款只能在银行事件（event 5 / 99：bank.ui，电脑走 Python `Access` / `Borrow`）里主动进行。

### 3.2 贷款（只读其规则）[H]

- **建立**：`Money_TakeLoan @0x47e970`。没有贷款时才能借；金额**进存款**，记下借款日期。
- **可选金额**：人类 100000 / 200000 / 300000（str5416）；电脑 `Borrow` 返回值。
- **每回合开始** step 5：`Role_CheckLoanDueAndForceRepay @0x483170`。
  - 人类在距到期 5 / 3 / 1 天时提示（str5423）。
  - `@0x47eb70` 判定（m = 当前月 − 借款月，按 12 取模）：
    - m < 3：每过一个月的同一日，收贷款额的 10%（str5421）；
    - m > 3，或 m == 3 且日 ≥ 借款日：收回本金 + 10%，贷款清零（str5422）。
  - 扣款经 `0x4E(金额)` → `Role_SubMoney(金额, 存款优先)`。
  - 说明文字写“到期加罚 20%”，代码只收 10%。

### 3.3 破产 `Role_CheckBankruptcy @0x48ee10` [V]

**触发**：
- `Role_DispatchQueuedMsg @0x481df0` 每次取消息前判断，且只在以下条件都成立时调用：本角色是当前行动者、+0x1D4 == 0、队列里没有 0x2E。
- 判定：`g_GameState+0x14 ≥ 1` 且 `现金 + 存款 ≤ 0`。返回非 0 时置 +0x1D4 = 1。
- 不计股票和地产。

**闯关特例**（setup 标志 ≠ 0 且座位 0）：
- 金泡菜（Id 11）第一次破产被救：现金设为 100000，返回 0；
- 莎拉（Id 16）：清队列，入队 `0x75(0,1)`。

**一般情况**：
1. 清空队列，显示 str91“%s資金周轉不靈破產”，表情 0x1A，入队 0x7F（剧情）。
2. 统计其他未破产角色：h = 人类数，c = 电脑数。

| 情况 | 入队 | 结果 |
|---|---|---|
| h == 0 | `0x74` | `CPlayerTag__AskContinueNextRound @0x48f8e0`：普通模式置 `g_GameState+0x48 = 3`（游戏结束）；闯关模式问 str93“是否接關？”（okcancel → `0x76`） |
| h == 1 且 c == 0 | `0x73`，然后**清空队列**，再入队 `0x75(0,1)` | 0x73 被清掉，所以破产者不会被标记为 7，直接进入胜利 / 结束演出 `0x75 → @0x48ff70`（旧名 OnParkCleanup） |
| 其他 | `0x72`；从自己的地里选拍卖品（最多 3 块），每块入队 `0x4F(t)`、`0x40(t,1)`；最后 `0x73` | 见下 |

**选拍卖品**：
- 先收集前 9 块自己的地；
- 之后遇到等级 ≠ 0 的地，若候选里某块等级更低，且该候选的等级 ≤ `rand()%5`，就替换它；
- 最后无放回地随机抽 3 块。

**0x72 `CPlayerTag__OnRoleSettlement @0x48f500`**（财产清算）[V]/[H]：
- 本人所有地块 `Land_ClearOwner`：owner = -1，地面颜色复位，等级 0 的地拆掉旗子；**房屋等级与建筑保留**。
- 本人拥有的奇迹：复位。
- 乐透号码清空 [M]。
- 电力公司 / 点券中心持股清零，并重选董事长 / 最大股东。
- 股票全部退回市场（`Stock_SellAll @0x4992c0`：剩余股数 += 持股，**不给钱**）。
- 卡片清空，显示破产头像 `npc\%s_14`。

**0x4F(t)**：`Role_ProcessWizardMsg`（旧名不准）。镜头对准 t，t 闪烁（`NPC_CreateWizard` → `BuildObj_SetAlpha`）。[H]

**0x40(t,1)**：`Game_UseCard(0x51, t)`，即**破产拍卖**（3.4）。

**0x73 `CPlayerTag__OnRoleBankruptcy @0x48f750`**：
- 离开道路点（同格错开还原），身份状态 = 7，回合状态 = 0；
- 若自己是当前行动者则 `Role_SkipTurnToNextRole`。
- 之后 `Game_TurnNextRole` 会跳过此人。

### 3.4 拍卖（事件 0x50 公有地 / 0x51 破产，vendue.ui）[H]

- **创建**：`CreateGameObject @0x4b1bc0`：
  - +0x54 = `CalcPrice(t,1) / 2`；
  - vf16 `@0x4e2fc0` 设**起拍价** +0x58 = +0x54 / 2，即**地价的 1/4**（str4303“本次底價為%d元”）；
  - 加价按钮为 100·PI / 500·PI / 1000·PI。
- **电脑出价** `Vendue_AIBid @0x4e22c0`：令 x = 现金 − 当前价，q = 当前价 / (地价/2)。
  - x < 100·PI：放弃。
  - x < 500·PI：91% 概率出价，且需 q ≤ `rand()%10+1`；加价 100·PI。
  - x < 1000·PI：71% 概率加 500·PI，20% 概率加 100·PI，其余放弃。
  - 更多：61% 概率加 1000·PI，20% 概率加 500·PI，10% 概率加 100·PI，其余放弃。
  - 每次加价都还要满足 q ≤ `rand()%10+1`，否则放弃。
- **成交**：状态 5 时对得标者入队 `0x41(地块序号, 价格)` → `Role_PayAndTakeTerra @0x487c20`。镜头对准，`SubMoney_NoEmote(价格, 现金优先)`，`Land_SetOwner`。
- **流标**：str4308。
- 新闻“〔string.bin#329〕”（`News_EventAuctionPublic @0x4b6e80`）：无主地 > 4 块时随机挑一块，入队 `0x4F` + `0x40(t,0)`。
  - 原版 bug：遍历上限用的是**道路点数** `g_TerraMgr+0xC`，而不是地块数；越界下标由 `GetTerraByIdx` 返回 0，所以不会崩。

### 3.5 游戏结束

- **新的一天**：`Game_TurnNextRole @0x4971a0` 轮回到 0 号座位时，若至少有 1 名人类未破产，且未破产总人数 > 1，则调用 `Game_OnTurnStart(1)`，然后 `Game_FindLeaderByWinCondition @0x4a6090`。
- **胜利条件**（仅普通模式，且时间到时检查）：
  - 时间到：时限为三个月 / 六个月，且 `Date_CalcDaysDiff(当前, 结束) == 0`（`@0x4a3e00`，旧名 `Game_GetRandomValue`）。
  - 按 setup+0x1C 在未破产者中取最大（严格 >，同值取座位靠前者）：

    | 值 | 比较项 |
    |---|---|
    | 0 | 住宅地块数（`Role_CountOwnTerras(0,0,5)`，不含商业地） |
    | 1 | 总资产 `Role_CalculateNetCash(1)` |
    | 2 | 存款 |
    | 3 | 点券 |
    | 4 | 股票市值 |
    | 5 | 只靠破产（不比较） |

  - 有领先者时：str7057“遊戲時間到，%s獲勝！”，轮次交给他，入队 `0x75(0,1)`。
  - 所有值都是 0 时（例如没人有住宅地），不结束。
- **破产结束**：见 3.3（0x74 / 0x75）。

---

## 4. 地产相关消息（`Role_DispatchQueuedMsg @0x481df0`）

发送者由 `scripts/msg_senders.py` 列出（立即数），并补上 Python 侧的调用（`py_*`）。

| 消息 (a, b) | 处理函数 | 作用 | 发送者 |
|---|---|---|---|
| 9 (t, ok) | `Role_BuyLandConfirmed @0x487030` | ok ≠ 0 时付 `CalcPrice(t,1)` 买下 t（不论原主），衰神 / 福神见 2.7 [V] | 0x11、`AI_AskTBuy`、`Terra.Buy`（购地卡 CardPy_01） |
| 0xA (t, 1) | `Role_TakeLandOwnership(t, b)` | 免费占地；b ≠ 0 时 70% 概率与原主互动 [V] | 土地公 0x22 |
| 0xB (t, R) | `Role_BuildOnLand(t, R, 收费)` | R ≠ 0 时付费加盖；t == 0 时随机取己方 0..4 级的地 [V] | 0x11、`AI_AskTBuild` |
| 0xC (t, R) | `Role_BuildOnLand(t, R, 0)` | 免费加盖；R == 0 时 `Role_RestoreDeckCard`（推测为退回卡片）；t == 0 时随机 [V] | `Terra.Build`（建屋卡 CardPy_02）、福神 0x22、问答奖励、魔法屋 |
| 0xD (t, x) | `Role_StartLandLevelDown @0x4875e0` | 拆一级（6.2）；t == 0 时随机取己方 1..5 级的地 [V] | `Terra.Break`（拆屋卡 CardPy_03）、问答惩罚、魔法屋 |
| 0xE (t1, t2) | `@0x487b00` → `NPC_CreateFortuneTeller`（旧名不准） | t1 = 角色所在点的地块，t2 = 目标；地主不同时闪烁 5×0.4 s 后**交换地主**，音效 4 [V] | `Terra.Exchng`（交换卡 CardPy_04） |
| 0xF (t) | `@0x487670` → `BuildLevel_StartIncrement`（名字反了） | 特效后**拆光**（等级 0、清除建筑对象）[H] | 静态扫描未找到发送者 |
| 0x10 (街) | `@0x488360` → `TerraMgr_GetBuildableRoles @0x4f2040` | 该街**所有有主**且等级 < 5 的地块各 +1 级（免费，商业空地类型随机）；无可升级地块时退回卡片 [V] | `Street.Build`（天使卡 CardPy_30） |
| 0x44 (街) | `@0x48d2c0` → `Street_ProcessTerraBuild @0x4f2190` | 闪烁 5×0.4 s 后该街**全部**地块归自己（不付钱，含他人的房子），音效 1 [H] | `Street.Buy`（地王卡 CardPy_48） |
| 0x2F (街, 天) | `Terra_LoadCloseIcon @0x42d3b0` | 查封：街道 +0x108 = 天（下标必须 > 0，商业地的街道 0 不能查封）[V] | `Street.Close`（查封卡，9 天） |
| 0x30 (街, pct) | `Terra_SetHeight @0x42d4c0` | 涨价，见 1.3 [V] | `Street.Flow`（涨价卡，200%） |
| 0x11 / 0x12 / 0x1F / 0x20 / 0x22 | 见第 2 节 | | |
| 0x41 (序号, 价) | `Role_PayAndTakeTerra @0x487c20` | 付钱得地 [V] | 拍卖成交 |
| 0x50 (t, ok) | `Role_BuyLandSimple @0x487200` | 付 `CalcPrice` 得地，不看附身 [V] | 静态扫描未找到发送者 |
| 0x4A (t, b) | `@0x4876c0` | b ≠ 0：拆光房屋；b == 0：拆光并清除地主 [H] | 新闻“拆迁 / 火灾” |
| 0x31 (t) | `@0x48d310`（旧名 `OnParkEvent`） | 怪兽（`Terra.Dragon`）[M]，未展开 | 怪兽卡、新闻 |
| 0x15 / 0x16 / 0x5B (金额, 账户) | `Role_SubMoney(_NoEmote)` | 扣钱（账户 0 现金优先 / 1 存款优先）；0x16 带表情 | 各种 |
| 0x17 / 0x5C (金额, 账户) | `Role_AddMoney` | 加钱；0x17 带表情 | 各种 |
| 0x4E (金额) | `Role_SubMoney(金额, 存款优先)` | 贷款扣款 | 3.2 |

**Python 接口与参数** [V]（`py_Terra_* @0x5005e0..`，`py_Street_* @0x501260..`）：
- `Terra.Buy(t, r)` → 9(t,1)。
- `Terra.Build(t, r)`：
  - 住宅或已建商业地 → 0xC(t,1)；
  - 商业空地：人类开 business 选择 → 0xC(t, 结果)；电脑 → 0xC(t,1)。
- `Terra.Break(t, r)` → 0xD(t,1)。
- `Terra.Exchng(t, r)` → 0xE(角色所在点.地块, t)。
- `Street.Build(s, r)` → 0x10(s)；`Street.Buy(s, r)` → 0x44(s)；`Street.Close(s, d, r)` → 0x2F(s,d)；`Street.Flow(s, p, r)` → 0x30(s,p)。
- `Terra.Charge(t)`：无主时返回 `CalcPrice(t,1)`，否则返回 `BuildUnitCost(t)`。
- `Terra.Rent(t, chain)`：返回 `CalcRent(t, chain)`。
- 以上消息都入队到参数 r 指定的角色。

---

## 5. 新的一天与开局

### 5.1 `Game_OnTurnStart(1) @0x4a3b50`（this = g_GameState）[V]

1. 已进行天数为 0 时：只把天数设为 1 就返回。
   - 所以开局日期持续**两轮**；第一轮天数为 0，不做破产判定。
2. `Game_AdvanceOneDay @0x4a8280`：
   - 天数 +1，星期在 1..7 间循环；
   - 公历：1~7 月单月 31 天，8~12 月双月 31 天，2 月闰年 29 天（能被 4 整除且不能被 100 整除，或能被 400 整除），其余 30 天；
   - 跨月、跨年进位。
3. 刷新日期界面；`Game_UpdatePriceIndex @0x4a6a00`（指数 < 99 时）：
   ```
   avg = Σ_{未破产} NetCash(0) / 人数
   NetCash(0) = 现金 + 存款 − 贷款 + 股票市值 + Σ CalcPrice(t,0)     // @0x490140
   n = avg / g_GameState+0x2C;  if n > PI: PI = min(n, 99)
   ```
   - **更正**：gameplay_algorithms.md 写的除数“100000”，实际是开局平均资金。默认选项下恰好是 100000（50000 现金 + 50000 存款）。
4. 星期 < 6（周一 ~ 周五，也包括第一天的 0 = 周日）：`Stock_CheckUpdateTimer`。休市计数 > 0 时减 1，否则 `Stock_UpdatePrices`（gameplay_algorithms 第 4 节）。
5. 每月 1 日：`Event_OnNpcEnable`（NPC 刷新，与地产无关）。
6. `TurnStart_UpdateTerrain @0x4f11f0`：
   - 每个地块调用 `Institute_CheckCompletion`（研究所研发完成）[M]；
   - 街道查封 / 涨价倒计时 `Terra_UpdateHeightAll`（1.3）。
7. 已进行天数 > 5 时：
   - 每月 1 日：**月结算**，事件 0x52（5.3）；
   - 每月 15 日：乐透开奖，事件 0x62（号码数 < 32 时）[M]。
8. 节日 `Game_CheckFestival`：查 festival.bin，命中时入队 `0x3C(节日号+0x2D)`，播放对应新闻 [M]。
9. 否则每个**周六**，当 (已进行天数 / 7) 为奇数（隔一周一次）：新闻，事件 0x11。闯关模式还有剧情新闻。
10. 播报退市股票。

### 5.2 新闻里与地产 / 金钱有关的条目（只列地址和效果；出现概率未展开）[M]

| 函数 | 效果 |
|---|---|
| `News_EventPropertyPrice_Up @0x4b7070` / `_Down @0x4b7180` | 随机一块地的 +0x48 地价% 变化 ±10%（str334 / str326）[V] |
| `News_EventAuctionPublic @0x4b6e80` | 拍卖一块公有地（3.4）[V] |
| `News_EventDemolition @0x4b7290` | 随机一块有房的地被拆（0x4A(t,1)），地主得到 `CalcPrice(t,1)`（拆前计算，经 0x5C 进存款） |
| `News_EventBuildingFire @0x4b75e0` | 拆房，赔付规则未展开 |
| `News_EventMonsterAttack @0x4b7910` | 0x31：怪兽毁房 |
| `News_EventBankHack @0x4b5570` | 所有人存款减少 x·PI |
| `News_EventInterestRate @0x4b67b0` | 所有人得存款的 10%（经 0x5C 进存款） |

### 5.3 月结算（事件 0x52，`Balance_UIContainer`，balance.ui）[V]（完整步骤、画面、移植实现见 docs/balance.md）

- 有金钱的部分只有 `Balance_ShowDepositInfo @0x4afa40`，对每个未破产角色：
  ```
  w = Wonder_CheckConstruction(0)   // 金钱奇迹
  利息 = w < 0 ? 存款/10 : (w == 本人 ? 存款/5 : 存款/20)
  Role_AddMoney(利息, 存款)
  ```
- 其余页（损失：付租累计 +0x33C 与倒楣天数；总资产 `NetCash(1)`；本月冠军 / 悲情人物）只做展示。
- +0x33C、+0x338 在哪里清零未查 [M]。

### 5.4 开局资金

见 1.7：总额选项一半进现金、一半进存款；PI = 1；物价基数 = 开局平均资金。

---

## 6. 表现层（只列 id 与调用点）

### 6.1 地主标记与房屋模型

- **地主颜色**：`Land_SetOwner @0x4eec20` 把地块地面染成角色颜色 +0x1F4（RGBA）；无主时用白色 FF,FF,FF,FF（`Terrain_SetColorFromRGB_Off40`）[V]。
- **地主旗**（等级 0 的有主地）：`BuildObj_ResetAll(1,1) @0x4ef8c0` 放 art **1100 + 角色 Id**（例如 1101 = `sign\a01.dff`）[V]。
- **房屋模型** `Land_OnEnter(类型, 等级, 动作) @0x4ef580` [V]：
  - art = building.bin `Level{等级}artId`：住宅 611..615（`build\BK\BEZ0n.dff`），研究所 621..625，超市 626..630，饭店 631..635；
  - **住宅在 stage.bin clime > 1 的地图上用 art + 5**（616..620，`BWZ0n.dff`）；
  - 用 `CArticleMgr` vf+0x40 放在地块位置 +0x88，朝向 +0x94，动作参数 ≥ 0 时播放。

### 6.2 加盖 / 拆除动画

**加盖**：任务类 0x5e6618，`NPC_CreateAndDisplay @0x4f4b50` [V]：
1. 在建筑朝向镜头的位置放特效 art **6001**（`eff\Eff_10.dff`），播放动作 0 一次；音效 **2**（句柄保存，结束时停止）。
2. 1.5 s 后或特效回调时（`@0x4f4e40`、`NPC_PlayAnimOrComplete @0x4f4cc0`）：旧模型存在且等级 1..4 时，旧模型播放动作 1（住宅）或 3（商业）一次。
3. 回调 → `NPC_CompleteInteraction @0x4f4d70`：
   - **此时才 `Land_OnEnter(类型, 新等级)`** 换模型并写入等级；
   - 新模型播放动作 0（住宅）或 2（商业）；音效 **0x7A**。
4. 再回调 → `Building_UpdateBuildingVisual @0x4f4a40`：
   - 住宅且等级 > 0：播放动作 2；否则动作 0；
   - 任务结束（0x80000000）。

**拆一级 / 拆光**：类 0x5e66b8，`NPC_InitDisplay @0x4f4f20` [V]：
- 特效 art **6002**（`eff\Eff_01.dff`，住宅时缩放 0.5）；音效 **3**；
- 建筑播放动作 1（住宅）或 3；
- 结束时等级 −1（`BuildLevel_StepDecrement @0x4ef860`）或拆光（`BuildObj_ResetAll`）。

**天使卡（整街 +1 级）**：`Building_AnimateMultiple @0x4f3b20` [V]：
- 每块地放特效 art 0x17E9（6121）；音效 0x1E；
- 之后 `Building_ProcessAnimation @0x4f3de0` 逐块 `Land_OnEnter`。

### 6.3 其他表现

> 实现与更正见 `effects.md`：6002 拆房动画**只**用于消息 0xD；地王 / 交换 / 巫师 / 0x29 / 0x2A 的闪烁是 1.6 s（计数 2/3/4 交替，结束时才改主人 + 样本 1 / 4）；天使卡 6121 是粒子 22。

- **地王卡 / 交换卡**：地块透明度 0/1 交替闪烁 5 次（每 0.4 s），最后音效 1 / 4 [V]。
- **街道贴图**：
  - 查封：art 0x1773（6003，`eff\D05t.dff`）+ `texture\terra\land_close.tga`；
  - 涨价：art 0x17E8（6120）+ `land_up.tga` / `land_down.tga` [V]。
- **音效**：买地 1；付租 0x7B；拍卖 0x6F（出价）、0x93（放弃）、0x8E / 0x8F（开始 / 成交），另有按字符串号播放的语音；投资奇迹 0x90；月结算高亮 0x82。
- **表情 / 台词**：都是 `0x5A(类别)`（`@0x484c30` / `@0x484c50`）。

  | 场合 | 类别 |
  |---|---|
  | 收钱 `Role_AddMoney` | ≥5000 → 7，≥1000 → 8，否则 9（不乘 PI） |
  | 付钱 `Role_SubMoney` | ≥5000·PI → 0xB，≥1000·PI → 0xA，否则 0xC |
  | 免租 | 0xD / 0xE / 0xF |
  | 买地 | 1 / 0x11 |
  | 加盖 | 1 / 2 / 0x10 / 0x12 |
  | 衰神 | 4 / 5 |

- **数字弹出**：没有找到独立的“金额飘字”。金额只出现在提示框文字里，状态栏由 `Role_UpdateUI_00481120` 刷新 [M]。

---

## 7. 移植实现清单（按顺序）

1. **数据结构**：Terra（1.2）、街道记录（1.3）、商业建筑对象（1.4）、奇迹表（1.5）、角色金钱字段（1.6）、g_GameState 的日期 / PI / 基数 / 结束日期。从地图建 Terra 可复用 `scripts/plots.py` 的规则；商业地街道下标 = 0。
2. **金钱原语**：
   - `AddMoney(acc)`：上限 999,999,999；
   - `SubMoney(acc)`：先扣 acc，不足扣另一账户，到 0 为止，返回是否仍 > 0；
   - 表情分档（6.3）。
3. **价格函数**：`BuildUnitCost`、`CalcPrice`（保留 PI² 与整数截断顺序）、`CalcRent`（同街连锁），RentTable 常量。
4. **落地链**：0x1D → 0x12 → 0x11 → 0x1B / 0x1F / 0x20 → 0x22，严格按 2.1~2.8 的入队顺序与位置（Enqueue 与 EnqueueAt 的区别会影响对话框和表情的先后）。
5. **对话框接口**：消息 1 / 0x1C / 0x1B、状态 7、结果写入 b == 0x80000000 的消息（okcancel 返回 1/0，business 返回 2..4）。
6. **电脑**：`AI_CallScript(2/3, 地块指针)` 调原版 Python 的 TBuy / TBuild，返回非 0 时入队 9 / 0xB(t,1)。需要让 Python 能把整数“地块句柄”传回 Terra.* 接口（原版就是对象地址）。
7. **执行端**：`BuyLandConfirmed`、`BuildOnLand`（含衰神 / 福神）、`TakeLandOwnership`、`ApplyGodOnLand`、`CheckRentExempt`、`PayToll`、`TransferRentToOwner`（含路霸、嫁祸卡分支）。
8. **商业建筑任务**：超市 / 饭店的 7 个状态与单骰交接、饭店住 1 天；研究所先做成占位。
9. **消息 0xA~0x10、0x2F、0x30、0x41、0x44、0x50**（第 4 节）及其 Python 接口。
10. **新的一天**：日期推进（含“第一轮天数为 0”的细节）、PI 更新、街道倒计时、月结算利息（金钱奇迹）、15 日乐透、新闻（先接地价 ±10%、公有地拍卖）。
11. **破产**：判定时机（分发器、+0x1D4、0x2E 例外）、三种情况、0x72 清算（保留房屋）、≤3 块破产拍卖、0x73 标记与换人。
12. **拍卖任务**：起拍价 = 地价/4，加价档 100 / 500 / 1000·PI，电脑出价表，成交 0x41。
13. **游戏结束**：时限、6 种胜利条件、0x74 / 0x75。
14. **表现**：地主颜色与旗（art 1100+Id）、房屋 art 表（clime > 1 时 +5）、加盖 / 拆除特效与“动画中途才写等级”、音效 id。
15. **贷款**：月息 10%、3 个月到期强制收回，每回合开始 step 5。

## 8. 原版的怪异之处（移植时照做）

- `CalcPrice(…,1)` 的等级部分乘了 PI²。
- 付不起租时，欠款消失，地主照收全额。
- 租金、超市、饭店的收入进**存款**，不进现金。
- 购地卡买他人的地时，原主拿不到钱。
- 买地 / 加盖只看现金。
- 衰神：照付钱，但不得地、不加盖。
- “h == 1 且 c == 0”的破产分支会把刚入队的 0x73 清掉。
- `News_EventAuctionPublic` 用道路点数遍历地块。
- 贷款到期的说明写罚 20%，代码只收 10%。
- 结束日期的月份超过 12 时年份不变（不影响结果）。
- 开局日期持续两轮。

## 9. 未确认

- 0xF、0x50 两条消息没有找到静态发送者（可能从寄存器传命令号，或者是死代码）。
- `Game_StartNewRound` 里每人加的 3 张卡（参数 8/10/11）是卡的索引还是 Id。
- 闯关模式开局资金：储藏箱现金 + pass 的 start_cash / start_fund 的具体来源字段（`Game_GetBinaryDataManager` +4/+8）只看了一处。
- 奇迹：地图上的奇迹建地对象（Wonder +0x3C/+0x44/+0x48）与奇迹表的同步；投资完成时拥有者是“完成投资者”还是“最大股东”；存档样本里一次投资扣了 10000（理论值是 5000），需要实机验证。
- 研究所的研发规则（`@0x4f6360`、`Institute_CheckCompletion`），以及卡片奇迹的点券折扣。
- 拍卖：人类出价界面、出价轮转（`Vendue_NextBidder` / `FindNextBidder`）、破产拍卖（+0x5C 标志）与公有地拍卖的区别；起拍价 = 地价/4 只按反编译读出，未做反汇编核对。
- 月结算之后 +0x338 / +0x33C / +0x340 是否清零。
- ~~新闻事件的出现概率与选择规则，以及火灾、怪兽的具体赔付~~：见 docs/news.md（选择 §3、效果 §4、怪兽 / 火灾 §6）。
- 0x75 胜利演出（`@0x48ff70`：0x7E、0x7D）与 `g_GameState+0x48` 各值的确切含义。
- ~~节日的效果~~：见 docs/news.md §1 / §5。
- 以上全部来自静态分析，尚未在游戏中实测。

## 10. 盲审交叉核对

做法：把 14 个关键函数的反编译代码（已删去所有注释，即已有结论）放进 `out/rules_land_xcheck/key_functions.c`，只附结构偏移、字符串原文和 RentTable 常量，交给 sonnet 子代理独立回答 8 个问题。

| 问题 | 子代理结论 | 与本文 |
|---|---|---|
| 加盖费 / 地价 | 250/500·PI；`(单位·等级 + 基价) × PI`，并指出 **PI²** | 一致 |
| 落地三分支 | 无主（附身 5/6 跳过）、本人（≠6 且 <5）、他人（仅住宅、+0x1E4<1）；入队顺序 1 → 0x1C → 9 / 0xB，0x1B → 0x1F，最后 0x22（福神例外） | 一致 |
| 租金 | 同街同地主累加 × PI，地产奇迹 ±20%（截断），财神 0、穷神 ×2 在后 | 一致 |
| 免租 | 查封、地主状态 1..6、状态 0 + 糊涂神、破产、地主不存在 | 一致 |
| 付款 | 付者现金优先，不足扣存款直到 0；地主全额进存款，“凭空产生” | 一致 |
| 买 / 盖 | 衰神照扣钱不得地 / 不盖；福神买地免费、加盖不免费；商业类型取 b 或随机 2..4 | 一致（另指出等级要在动画任务里才写入，与 6.2 吻合） |
| 破产 | 天数 ≥ 1 且现金 + 存款 ≤ 0；三种情况；0x73 被清掉 | 一致（子代理把候选上限说成“9 块”，即本文的“先收集 9 块再替换”） |
| 怪异之处 | PI²、只查现金、欠款消失、福神不让 0x22 入队等 | 与第 8 节一致；另指出 `@0x486580` 的状态值超出 0..7 时会用到未初始化的字符串缓冲（原版潜在 bug，状态值实际不会超出） |

结论：核心公式、条件和消息顺序没有分歧。
