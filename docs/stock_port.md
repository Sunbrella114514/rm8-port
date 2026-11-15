# 股市移植说明（app/stock.h、stock.cpp）

> 依据：`out/final_v2/RichMan8_named.c` 逐函数阅读（名字约一半不准，以代码为准）、`scripts/exe_dis.py` 反汇编核对浮点常量与取整。
> 标记：`[V]` 已读代码并核对，`[H]` 推测，`[A]` 移植时的适配。独立验证见第 7 节。

## 1. 数据（stock.bin → port/scenes/stocks.txt）

- `scripts/export_stocks.py <游戏目录> port/scenes/stocks.txt`：144 节，按文件顺序一行一支（`stock c id initprice totalnum vivacity 名字`）。`android/deploy.py` 的共享文件元组已加入 `stocks.txt`。
- `Stock_LoadFromINI @0x4b8e20` [V]：只收 `c == 地图号` 或 `c == 100` 的节（每图 12 + 通用 10 = 22 支；`c = -1` 电力公司、`c = 0` 点券中心从不进交易所）。先全部放进**备选数组**（+0x20），再 16 次 `rand() % 备选数` 随机挑到**上市数组**（+0x10），所以 16 支上市、6 支备选。`totalnum` → +0x354（剩余股数），`initprice`/`vivacity` 用 sscanf `%f`，历史最高 / 最低初值 = 发行价，类别 +0x370 = 地图号（下载地图 [H] 取 ≥ 0x15，只有通用股）。
- `id = 1`（ATI / ACER / ELSA 等 5 支真实公司）：+0x374，`Stock_ResetStock` 直接返回，**永不退市**。
- `Stock_InitPrices @0x4b9450` [V]：价格 = 发行价，动量 / 停牌 / 平稳计数清零，然后**连续 300 次 `Stock_UpdatePrices`**（所以开局价格已经走了 300 天）。

股票对象（0x37C 字节，stock.cpp 的 `Stock`）：+0x100 发行价、+0x104..+0x343 最近 144 天（+0x104 = 昨价）、+0x344/+0x348 历史最高 / 最低、+0x34C 现价、+0x350 活跃度、+0x354 剩余股数、+0x358 停牌天数、+0x35C/+0x360 个股事件涨跌幅 / 剩余天数、+0x364 动量、+0x368 当日涨跌幅、+0x36C 平稳计数、+0x370 类别、+0x374 id、+0x378 最大持股座位（-1 无）。管理器（DAT_006403c4）：+0x30 休市天数、+0x34/+0x38 全市场事件涨跌幅 / 天数、+0x3C 大盘涨跌比。

## 2. 每日更新 `Stock_UpdatePrices @0x4b9a00`

公式见 `gameplay_algorithms.md` 第 4 节（已逐行核对）。本次补充、更正的细节：

- **价格取整 `Stock_RoundPrice @0x4b9870`** [V]（常量 5/15/50/150、100、10 在 0x5e0128 等处读出）：`ftol` 截断；< 5 取 0.01；< 15 取 0.05（先 ×100 截断再按个位 < 5 舍、≥ 5 进到 5）；< 50 取 0.1；< 150 取 0.5；≥ 150 取整数。**只有"正常日"分支取整**，个股事件、全市场事件的价格不取整（但同样封顶 999.99）。
- **常数类型**：`0.4` 是 double（0x5e4c70），`0.85`、`999.99`、`-10`、`0.5`、`3`、`2` 是 float；中间量在 x87 上是扩展精度，移植用 double 中间量、每次存入字段时转 float。
- **历史移位**：只对"未停牌"的股票做（`memcpy(+0x108, +0x104, 0x23c)`），停牌股历史不动；`+0x104` 在三个分支里是"昨价"（正常分支先把 +0x104 设为现价再检查退市，事件 / 全市场分支是先检查再设）。
- **大盘噪声** `M = (rand() % 84 - 40) / 10`，只在没有全市场事件时抽取；有事件时天数先 -1。
- **全市场事件的原版 bug** [V]（`0x4b9dc6` 反汇编）：全市场分支里**每处理一只股票又把 +0x38 减 1**，天数很快变成负数，之后 `+0x38 != 0` 恒成立，事件永不结束（每天 ±10%）。移植按原样保留（`g_mktDays--`）；新闻 1 / 11 现在会触发它（docs/news.md）。该分支也不更新 +0x368（当日涨跌幅显示的是旧值）。
- 个股事件分支：`+0x368 = +0x35C`、`现价 = (+0x35C + 100) × 昨价 / 100`、`+0x360 -= 1`；`+0x360 == 0` 时恢复正常。
- 大盘涨跌比 `+0x3C = (Σ现价 - Σ昨价) / Σ昨价`，显示在股市板的"大盘指数"。

### 退市 `Stock_ResetStock @0x4bc330`
昨价 < 10 且 id ≠ 1：所有角色该股持仓**直接清除、不退款**（`Stock_RemoveAllOrSome(slot)`），对象重置（剩余 10000、历史 / 现价 = 发行价、最高 = 最低 = 发行价、动量等清零），放到备选队尾；备选队首的股票接替这个位置（备选为空时就是原对象再重置一次）。退市通知 `str 6320` 在 `Game_OnTurnStart @0x4a3b50` 末尾格式化进一个缓冲区后**从未显示**（循环用计数当下标、没有排队文字的调用）——移植同样不显示，只写日志。实测所有股票发行价 ≥ 50 且价位修正很强（现价 < P0/3 时下跌被强制反弹），正常波动几乎不会跌破 10，退市主要靠黑卡 / 全市场事件。

### 时机 `Game_OnTurnStart @0x4a3b50` [V]
天数为 0 的第一天只把天数置 1；之后每个新日：`Game_AdvanceOneDay` → `Game_UpdatePriceIndex`（净资产含股票市值）→ **星期 < 6**（周一～周五，及首日的 0 = 周日）时 `Stock_CheckUpdateTimer @0x4b9240`：休市计数 +0x30 > 0 则减 1，否则 `Stock_UpdatePrices`。周末（6、7）不更新、休市计数也不减。移植：`game.cpp rm8GameOnNewRound` 在物价指数之后调 `rm8StockNewDay(g_wday)`。

## 3. 持仓与交易

持仓记录 0x10 字节：{股票、股数、**上次买入价**(+8)、总成本(+0xC)}，按买入先后排列（`Role+0x334` 是条数，Python 的 `Role.Stock(r, i)` 就是第 i 条）。

| 函数 | 要点 |
|---|---|
| `Stock_BuyPartial @0x498dd0` | n = min(想买, 剩余)；剩余 -= n；**返回值是 ftol(想买数 × 价)，用的是请求数而不是 n**（原版 bug，AI 买不满时多付）；已有持仓：股数 += n、上次价 = 现价、成本 += n × 价；否则新增记录 |
| `Stock_SellPartial @0x499010` | n 为 -1 或超过持有 = 全卖；成本按比例缩小；股数归零则删记录；剩余 += n；返回 ftol(n × 价) |
| `Stock_SellAll @0x4992c0` | 全部持仓退回市场（剩余 += 股数），返回逐条累加的 ftol；调用者决定付不付钱：破产清算（`0x48f500`）不付，AI（`0x4f9310`）和新闻"〔string.bin#341〕"（`0x4b64e0`）付存款 |
| `Stock_UpdateBiggest @0x4bc710` | 剩余 = 10000 → 无董事长；否则严格大于当前最大者才换（只比未破产者） |
| `Stock_CalculateTotalValue @0x4994c0` | Σ ftol(股数 × 现价)（净资产 `Role_CalculateNetCash @0x490140`、胜利条件 4、Python `Role.StockValue`） |

**人类交易** `Stock_ExecuteTrade @0x4dcd90` + 输入处理（`0x4dc440..0x4dcc70`）：
- 一律走**存款**账户（`Bank_AddMoney(.., 1)` / `Bank_WithdrawMoney(.., 1)`，存款不够时扣现金）。
- 最大可买 `Stock_GetMaxTradeQty @0x4dd450` = min(ftol((float)存款 / 现价), 剩余股数)，只看存款；最大可卖 = 持有数。数字键 `n×10+d`、`00` = ×100、`Max`、`back` = ÷10、`clear`、`ok`、`cancel`，输入都夹到上限。
- 买入金额 = `Float_RoundToInt(股数 × 价)`（四舍五入），< 1 则不成交；买后若无董事长则本人当董事长，否则 `UpdateBiggest`。
- 涨停（当日涨幅 ≥ 10）不能买（`str 6315`），跌停（≤ -10）不能卖（`str 6316`）：`Stock_ShowDetail @0x4c5a40` + `0x4bc690/0x4bc6d0`。
- 特殊股（类别 -1 / 0，按 1000 股为单位，`str 6314`）在交易所列表里不出现，代码分支不移植。

## 4. 界面与开启方式

- **开启**：HUD 底栏按钮 7（`gmaindlg` ID 7，仅状态 6 可见）→ `CGamePlayScene vf12 @0x4a2e10` case 7 → `Game_DispatchCardAction(0x61) @0x4a4420` → `Game_UseCard @0x4a4160`：周末（星期 ≥ 6）`str 6317`「〔string.bin#6317〕」，休市计数 > 0 时 `str 6318`「〔string.bin#6317〕%d天」，文字入队、状态 8、再入队 100；否则创建股市板任务（`CreateGameObject @0x4b1bc0` case 0x5E..0x61，vtable 0x5E45D8，状态 9）、清队列、入队 100（关板后状态 9 → 8，再由 100 回到掷骰前）。
- **股市板** `StockBoard.ui`（`CUi_StockBoard__vf16 @0x4dbcc0` + `Stock_RefreshUI @0x4db120`）：大盘涨跌比、存款、持股市值、4 个座位头像、每股一行（名称 / 现价 / 涨跌 / 已售 / 本人〔string.bin#6304〕 / 4 个座位持股）；名称颜色涨停红、跌停绿（`0x4dc310`），现价与涨跌红涨绿跌平白（`0x4dc240`）。行点击 → 个股界面（`Stock_TradingOnOK @0x4dad60` → `0x4dc1c0` → `Stock_InitUI @0x4c6830`，板隐藏）。`Quit` 关板（`0x4dbf00`）。
- **个股界面** `SingleStock.ui`：左列 16 个股票按钮（选中项白字 + 图 802）、走势图 `Stock_DrawChart @0x4c4d20`、买进 / 卖出、存款、名称 / 成交价 / 涨跌 / 交易量（= 10000 - 剩余，`0x4c66d0`）、董事长与其他 3 名股东（`0x4c5f10`）。
- **走势图** [V]：`lineLabel` 区域（design 135,37 863×536）画 31 竖 × 21 横网格（色 0x8c006400），四边各留 16；横轴 145 个点（144 天历史 + 现价，每点 w/145）、纵轴 = 历史最高..最低；6 个纵轴标签（黄）+ 18 段白色短划；折线色 (0x80,0x80,0xff)；月份标签（橙，`%d月`）首个在 `ftol(144 - 2×日/7)`，其后每 22 天一个、月份递减。**[A]** 用运行时控件（`rm8UiAddWidget` 的色块）画：每天一个竖条代替线段，网格 / 虚线同样是色块。
- **交易对话框** `tradeStock.ui`：标题 `str 6312`（买）/ `6313`（卖）、Edit 数字、键盘按钮名即上面的键。
- 文字：`str 6301..6320`、`282/283`（AI）、`527/528/558`（卡片文字）都在 `scenes/ui/strings.txt`。

## 5. 与其它系统的连接

- **红 / 黑 / 钻石卡**（卡 23 / 22 / 47，`Role.OnEvent(自己, 95 / 96 / 94, 1)` → `Role_TriggerLandEvent` → 事件 0x5F / 0x60 / 0x5E）：钻石 +10% 共 5 天，红 +10% 1 天，黑 -10% 1 天（`AI_OnLandEvent @0x4f79c0` 的参数）。
  - 电脑：`Card_SelectBest @0x4fcf40`：红 / 钻石 = 自己持仓里价值最大的那支；黑 = 其他未破产角色持仓里价值最大、且自己没有的那支（`Obj_SetFields35C_360 @0x4bc620` 写 +0x35C/+0x360）；文字 `str 558 / 528 / 527`（角色名），入队 100。无合适股票时什么都不做，原版仍返回 1。
  - 人类：`Game_UseCard` 先查休市（同上，失败 = 文字 + 状态 8，`rm8StockEvent` 返回 false，python.cpp 入队 0x3b 卡片返还）；开市则打开股市板并进入"选股"模式，行点击 = `u_MiniGame_ResetRoleMsgQueue @0x4b19a0`（写事件、**清队列**、入队 100），所以 python.cpp 随后排的 0x3b 被清掉 = 卡片消耗；点 `Quit` 不清队列 = 0x3b 留下 = 卡片返还（移植里直接调 `rm8CardRestore`）。
- **电脑交易** `AI_StockTrading @0x4f9310`（`Role_StayOrBeforeRoll @0x484610` 的 0x25(1)，市场开放时）：持仓市值 > 0 且 `SSellAll` ≠ 0 → 全卖进存款、文字 `str 283`、冷却 5；冷却中直接返回；否则 `SSell` 循环（**`while (0 < 返回值)`，0 号股永远不会被卖**，原版行为）卖出，再 `SBuy` → `SBuySum(股)` > 0 就买（`Stock_BuyPartial`，扣存款），做了任何事文字 `str 282` 并返回 1（调用者再入队 0x25(2)）。`roles.cpp` 的 0x25 分支已接入。
- **Python** `Stock` 模块 6 个函数 + `Role.Stock / StockSum / StockPrice / StockValue`（`python.cpp` 里 `kImplStock`）：`CurPrice` / `AvrPrice`（144 天历史的 float 累加 / 144，不含现价）/ `Role.StockPrice`（上次买入价）返回 float，其余 int；`Stock.Open()` = 星期 < 6 且休市计数 < 1。`rm8PyCallAI` 原来只接受 int / long 结果，但 `SBuySum` 返回 float（`m*(per+80)/(cp*100)`），原版用 `PyInt_AsLong` 会截断，已补 float 分支（否则电脑永远买不了）。
- **净资产 / 物价指数 / 胜利条件**：`rm8StockValue` 加进 `game.cpp` 的物价指数、胜利条件 1（总资产）、4（股票）。破产**判定**不计股票（原版同，`Role_CheckBankruptcy @0x48ee10`）。
- **破产清算** `0x48f500`：`settle()` 里调 `rm8StockSellAll(seat)`（股票退回市场，不付钱）。

## 6. 未做 / 待定

- **新闻**（`News_EventStockBoom 0x4b6320 / Crash 0x4b63a0`：全市场 ±10% 天数 1；`News_EventNetworkOutage 0x4b6420`：休市；`News_EventStockLiquidate 0x4b64e0`：所有人套现）：已由 `news.cpp` 调用（docs/news.md §4：新闻 1 / 11 / 13 / 41，条件：休市天数 < 1 且星期 < 6，13 只看休市天数）：`rm8StockMarketEvent(pct, days)`、`rm8StockCloseMarket(days)`、`rm8StockLiquidateAll()`、`rm8StockClosedDays()`（注意全市场事件的原版 bug，见第 2 节：触发一次后每天 ±10% 永不结束）。
- **电力公司 / 点券中心持股**（事件 0xF / 0x10，不在交易所）：是另一套"浮点持股比例"系统（`DAT_006403cc[座位]` / `DAT_006403c8[座位]`，4 个 float + 董事长指针 +0x10；`PowerCo_TryBuyStock @0x4da790`：每次花 物价指数 × 5000 存款买一份并选最大股东 `PowerCo_SelectBestStock @0x4da930`；破产时清 0 并重选），与 `stock.bin` 的 22 支股票无关，属于事件任务（events 的活），这里没做。
- 存档（`stocks` 段 `Stock_SaveData @0x4bc0f0`、角色持仓 `0x4997e0`）：移植还没有存读档。
- `Role_SwapAssetsWithRole @0x48bf70` 的"股票互换"（type 4）：没有调用方，没做。
- 股市板头像的座位色边框（`Ui_DrawRolePictureFrame2 @0x4daec0`，4 像素）没画。
- 走势图用色块近似（见第 4 节），斜率很大时比原版的线粗。
- HUD 数字精灵在原版里属于 gmaindlg 层、会被对话框盖住；移植里数字是 UI 之后画的，所以 `hud.cpp` 在股市界面打开时跳过它们。

## 7. 验证

- **价格序列**：`scripts/test_stock_sim.py <日志> port/scenes/stocks.txt` 是不看 C++ 代码、直接按反编译重写的独立实现（float32 每次存储取整、同一 MSVC `rand()` 序列），读取移植用 `RM8_STOCK_LOG=1` 打出的每次更新的价格 / 涨跌行：
  - 6 个种子 × 地图 01/02/05，300 次开局更新 + 若干天，**全部逐支价格与涨跌完全一致**（共约 1900 次更新 × 16 支）；
  - `RM8_STOCK_WARM=6000` 一个种子 6001 次更新一致；
  - 退市路径：`RM8_STOCK_FORCE=3,-10,60 RM8_STOCK_AFTER=100`（黑卡式 -10% 连续 60 天）两个种子都触发退市、备选接替，400 次更新与模拟一致，退市次数一致。
- **电脑交易**：25 天的电脑局日志里电脑用 `AI_xx.py` 的 SBuy / SBuySum 买入（例如 `seat 3 buys 313 和計集團 at 81.34 (cost 25459)`），红 / 黑卡也被电脑使用并作用到股票（`seat 0 forces 忠華煤氣 -10% for 1 day(s)`）。
- **界面**：`--human 0 --size 1600x900 --shot` + `RM8_STOCK_UI`：股市板、个股界面（走势图、涨停不能买提示、董事长表）、交易对话框（输入 20、确认后存款 50000 → 45600 = 20 × 220、董事长 20 股 0.20%）、卡片选股模式（`p3` 点选）均已看过截图。

## 8. 测试开关

| 环境变量 | 作用 |
|---|---|
| `RM8_STOCK_LOG=1` | 每次更新打印全部上市股票 `名字=现价/涨跌`，开局打印上市名单，电脑每步打印 SBuy 结果 |
| `RM8_STOCK_SEED=n` | 股市自己的随机数种子（默认由游戏随机数抽取；日志里 `seed` 是更新前的种子） |
| `RM8_STOCK_WARM=n` | 开局更新次数（默认 300） |
| `RM8_STOCK_FORCE=槽,涨跌%,天数` + `RM8_STOCK_AFTER=n` | 开局后给某槽股票设个股事件，再多做 n 次更新（退市覆盖） |
| `RM8_STOCK_UI=board \| single:N \| card94 \| card95 \| card96` | 座位 0（`--human 0`）等骰子时打开股市板 / 个股 / 卡片选股模式 |
| `RM8_STOCK_AUTO=名字,名字,...` | 然后依次按这些控件名（最上层对话框：`Buy`、`2`、`0`、`ok`、`Quit`、`Button3`、`p3` ...）；`--shot` 模式下钩子在最后 3 个渲染帧里触发（之后没有模拟时间），只能看到界面状态 |
| `RM8_STOCK_CLOSE=n` | 开局休市 n 天（测试休市文字） |

## 9. 共享文件改动

`game.cpp`（include；`rm8GameNewGame` 调 `rm8StockNewGame`；新的一天调 `rm8StockNewDay`；物价指数 / 胜利条件 1、4 加股票市值；`settle()` 调 `rm8StockSellAll`）、`hud.cpp`（include；按钮 7 → `rm8StockOpenFromHud`；每帧 `rm8StockTick`；股市界面打开时不画数字精灵）、`roles.cpp`（0x25 分支调 `rm8StockAiTrade`）、`rm8_app.cpp`（loadMap 里 `rm8StockSetMap`）、`python.cpp`（`kImplStock` 表与函数体；`rm8PyCallAI` 接受 float；**修了 `keep` 向量重分配使 SSO 短字符串的 stub 名失效的 bug**——`Role.Borrow` 等被报"没有属性"）、`android/deploy.py`（`stocks.txt`）。

## 10. 待确认

1. `py_Role_OnEvent` 无条件排 0x3b（卡片返还）：人类取消 / 休市时合理；电脑成功时队列是 `[文字, 100, 0x3b, 100]`，靠 `Role_StartRollDice` 清队列才不会返还——移植让 `rm8StockEvent` 对电脑返回 true 来避免 python.cpp 排 0x3b，效果相同，但 cards 那边的流程是否同样依赖掷骰清队列请 cards 确认。
2. 全市场事件的原版 bug 是否在将来的新闻实现里保留（现在保留，原样）。
3. `Stock_RefreshUI` 里"无人持股"的 `Label` 默认文字（`.ui` 的 CapText = `Label`）在原版里是否显示：移植里空座位列填空串。
