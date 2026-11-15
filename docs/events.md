# 棋盘事件（events.cpp / event_bank.cpp / event_shop.cpp）

事件 = 路面节点上的图标（`Rm8RoadNode::event`，7/8 不是事件）。本文记录原版流程（带函数地址）、移植版实现范围、测试开关和未完成项。
标记：`[V]` 已在反编译中核对（附地址），`[H]` 推测，`[A]` 移植版的适配/简化。反编译文件 `out/final_v2/RichMan8_named.c`，名字只有一半可信，以代码为准。

## 1. 任务框架

### 1.1 原版流程 [V]
* 停在节点 → `CPlayerTag__OnStopAtTerra @0x485c00` 入队 `0x23`（NPC）、`0x1D`（落点）→ `Role_OnArriveNode @0x4861a0`：节点有事件图标且不是糊涂神附身（shape 6）→ `Role_TriggerLandEvent(事件号) @0x486140`，**不再入队 0x12/0x11**（事件格没有地块）。
* `Role_TriggerLandEvent`：事件号 > 0；电脑（+0x1B0 != 0）→ `AI_OnLandEvent @0x4f79c0`；人类 → `Game_UseCard(事件号,0)`（= Game_StartEventTask `@0x4a4160`）→ `CreateGameObject @0x4b1bc0` 按事件号建任务对象放到场景 +0x34，成功则角色状态 = 9（`CPlayerTag_SetStatusCode9 @0x4860b0`）。构造/加载（vf16）失败时对象被删，事件被忽略。
* 每帧 `CGamePlayScene__Game_OnUpdate @0x4a17b0` 调任务的 vf1（Update）：返回 0 = 继续，**bit31 = 结束** → `Game_EndTurn @0x4a48d0`：所有角色状态 9 → 8（`Role_UpdateStatusTransition @0x4860d0`），任务删除。回合随后照常：队列空 → 等 1 秒 → 0x66 换人。基类 vf1（`@0x407760`）直接返回 0x80000000，所以没有自己 Update 的任务（公园、卡片点、点券点）在下一帧结束。
* `AI_OnLandEvent @0x4f79c0` 的 switch：1 `Card_BuySell`，2 `AI_Event_Question`，5 `AI_Event_Bank(1)`，6 `Lottery_BuyTicket`，0xD `Auction_Vendue_Process`（名字不可信，实际是魔法屋的 AI），0xE/0xF/0x10 法院/电力公司/点券中心的 AI，0x12~0x17 小游戏（`MiniGame_ExecuteGame`，失败时落到 default），99 `AI_Event_Bank(0)`；**default（3、4、10~12 等）与人类相同：`Game_UseCard` 建任务，状态 = 9**。
* 过路银行（事件 99）：`u_CPlayerTag_UpdateTurnCountdown @0x485a20`（走路每经过一个节点的 vf18）：非糊涂神附身、步数未走完、节点事件 == 5 → `Role_TriggerLandEvent(99)`；成功则站立、停走路音效，之后由消息 `0x26`（vf26 `CPlayerTag__BeginMoveSteps` 继续走剩余步数）接着走。人类：银行任务结束时（`CUi_bank__Update @0x4c1c40`）入队 0x26；电脑：`AI_Event_Bank(0)` 入队 0x26 并把状态设为 8。

### 1.2 移植版 [A]
* `events.cpp`：`rm8EventLand`（落点）、`rm8EventPass`（过路银行）、`rm8EventUpdate`（每帧）、`rm8EventClear`、`rm8EventMessage`（事件相关的角色队列消息）、`rm8EventTurnStart`（回合开始贷款到期）。任务基类 `ev::Task`（`event_int.h`）：`update(dt)` 返回 true = 原版 vf1 的 bit31；`endTask()` = Game_EndTurn。
* 共享文件的小改动：
  * `roles.cpp`：`#include "events.h"`；`animateAlongRoute` 在 `--r.steps` 后插入过路银行钩子（`rm8EventPass`，成功则站立并返回，状态由事件置 9/8）；`0x67` 的 step 5 调 `rm8EventTurnStart`（原版贷款到期检查位置，`Role_RunTurnStartStep` 的 step 5）；新增消息 `0x26`（`beginMoveSteps(r.steps)`）。
  * `game.cpp`：`rm8GameMessage` 末尾 `return rm8EventMessage(...)`（消息 4、7、8、0x47、0x4E）。
  * `ui.cpp` / `ui.h`：按**控件名**操作的 `rm8UiXxxN`（bank.ui / cardShop.ui 的控件 ID 全是 0）、`rm8UiOnClickName`、`rm8UiSetAlignN`；`edit` 类型控件也画文字。
  * `CMakeLists.txt`：两处源文件列表加 `app/event_bank.cpp app/event_shop.cpp`。

## 2. 简单事件

| 事件 | 原版（地址） | 内容 | 移植 |
|---|---|---|---|
| 10 / 11 / 12 点券点 | 构造内联，vtable 0x5E4620，+4 = 0x32 / 0x1E / 10（`@0x4b1bc0`）；vf16 `Cls_5e4620__ShowInfo @0x4bc990` | 文字 str 5311 / 5312 / 5313 "%s得到N點券"（`Role_QueueTextMsg`）+ 入队消息 `7(N,0)` = `Role_AddTicket @0x489ed0`（点券上限 9999，N>10 时 70% 表情） | 完全实现（表情未做） |
| 4 公园 | vtable 0x5E4668，vf16 `@0x4b1890` | 只入队文字 str 5304 "%s在公園休息" | 完全实现 |
| 3 卡片点 | vtable 0x5E46B0，vf16 `@0x4b1750` | `Card_SelectByDifficulty(0) @0x4ae270` 选卡（r = rand()%10；等级上限 r/7 + 1 + r/9；在 level ≤ 上限的卡里随机一张）；文字 str 5303 "%s獲得卡片"，入队 `0x65(30)`、消息 `4(卡索引, 1)` = `Role_ReceiveCardWithUi @0x488e80`：显示 getcard.ui（卡名 label1、图 `card\<texName>` label2）60 帧，再等 30 帧，加入手牌；手牌满（8）且是电脑时换掉点券最低且低于新卡的那张 | 实现；手牌满的人类同电脑处理（原版弹 throwcard.ui 选弃牌，未做）；70% 表情未做 |

## 3. 银行（事件 5；99 = 过路、禁用 loan 按钮）

### 3.1 原版 [V]
* 任务 `Bank_InitContainer @0x4b2c60`（vtable 0x5E4740，参数 = 是否过路，存 +0x40）；加载 `CUi_bank__LoadUI @0x4c1590`（bank.ui；3D 银行职员 article 0x419 画进 "NPC" 平面；过路时 loan 按钮 `SetEnabled(0)`）；Update `@0x4c1c40`；按钮 `Bank_OnButton @0x4c1240`（按控件**名字**分发）。
* 状态：`+0x3C` phase（0 欢迎 → 1 菜单 → 9 关闭），`+0x34` mode（0 存、1 取、2 贷款、3 贷款额度、4 还款、5 结束、6 完成），`+0x38` 输入金额，`+0x8` 计时（`Anim_ApplyDifficultyScale @0x47b3b0` 的 1.6 × `(1 − min((speed−1)/10,1))`），`+0x20` 当前语音。
* 文字/语音（string.bin 文本末尾 `@N` 是 snd0000.dat 的声音 id）：气泡 "bobo"：4601 欢迎 → 计时到且语音播完 → 4602（过路 4603）；退出 4604。
* 按钮：数字 0~9（`Bank_LoanButton @0x4c2020`，有输入框时 amount = amount×10 + d，超过 `Bank_GetMaxAmount @0x4c2640` 截断；mode 3 时 1/2/3 = 贷款 10/20/30 万）、`00`（×100 `@0x4c2510`）、`Max`（`@0x4c2900`）、`back`（÷10 `@0x4c29a0`）、`Clear`（清零 `@0x4c25e0`）、`ok`（`Bank_Transfer @0x4c2a50`）、`saving`/`get`/`loan`/`refund`/`cancel`（存/取/贷款/还款/退出）。
* 最大额：存 = 现金，取 = 存款，贷款额度 200000（显示用），还款 = min(贷款, 现金+存款)。
* 存款 `Bank_DepositUI @0x4c2ed0`：标题 5402，信息 5411/现金（无现金 5418）；取款 `@0x4c3240`：5404、5401/存款（无存款 5407）；`ok`：现金 −N、存款 +N（`0x47e720`/`0x47e6c0`），然后 `Bank_OnComplete @0x4c42a0`：5410 "〔string.bin#5410〕"，4604 谢谢，1.6 s 后关闭。
* 贷款 `Bank_LoanUI @0x4c35b0`：已有贷款 → 5409；否则说明 5412（利率）+5413（期限 3 个月，到期不还加罚 20%）+5414，`ok` 后 `Bank_LoanOptions @0x4c4160` 显示 5415/5416/5417，按 1/2/3 → `Money_TakeLoan @0x47e970`（存款 += 额度，记录贷款额+日期）；已有贷款时再借被拒。
* 还款 `Bank_RepayUI @0x4c38e0`：无贷款 5408；有贷款：现金+存款 > 贷款 → `Bank_LoanRepay @0x47ea60` 立即还清（存款优先扣）并完成，否则显示 5419 + 5403 "您無錢還款"。
* 贷款规则：`Bank_ComputeInterest @0x47eae0`（日期差 `Date_CalcDaysDiff @0x431680`）距到期天数 = 到 (借款月 + 3) 同日的天数；`Bank_RollbackLoan @0x47eb70`：每到借款日（过一个月）收本金 10% 利息（最多 2 次），满 3 个月后清贷款，应付 本金×10% + 本金；`Role_CheckLoanDueAndForceRepay @0x483170`（回合开始 step 5）：人类在剩 5/3/1 天时提示 str 5423；应付额 > 0 时提示 5421（利息）/5422（本息）并入队 `0x4E(额)` = `Role_SubMoney(额, 1, 0)`（存款优先扣）。
* 电脑 `AI_Event_Bank @0x4f9000`（参数 1 = 停在银行，0 = 过路）：停在银行且无贷款 → Python `Borrow(座位)`，> 0 则借 `Money_TakeLoan`、文字 str 253、入队 0x47；否则 Python `Access(座位)`：≠ 0：< 0 存 −x（`Money_Deposit @0x47e870`，str 251），> 0 取 x（`Money_Withdraw @0x47e8f0`，str 252），入队 0x47；过路时再入队 0x26 并把状态设 8。返回 0 表示没动作（过路则继续走）。

### 3.2 移植版
全部流程、文字、语音 id（用 `rm8SndPlay`）、贷款规则都按上面实现（`event_bank.cpp`，贷款记录在该文件内，`rm8EventLoan(seat)` 供外部读本金）。
[A] 未做：输入框的键盘输入（`Bank_OnAmountInput @0x4c1f70`，触屏用数字键）。破产判定仍只看现金+存款，未计贷款（`Role_CheckBankruptcy @0x48ee10` 未对照）。

## 4. 卡片商店（事件 1）

### 4.1 原版 [V]
* 任务类 vtable 0x5E4A10（构造 `0x4B3C60`，名字 GameModeMgr 是错的）；加载 `Cls_5e4a10__vf16__Setup @0x4e6180` = `CardShop_ResetState @0x4e59f0` + `vf16__Init @0x4e5aa0`：12 个槽各 `Card_SelectByDifficulty(1)`（等级上限 r/7+1）；cardShop.ui：`card0..11`（64×64 卡图，路径 `card\<texName>`）、`cardPriceN`（卡名）、`LabelN`（价格）、`ticket`（点券数）、`Plane1`、`Exit`、`bobo`（气泡）、`hideCard`（出卡口）。
* 价格 `CardShop_CalcCardPrice @0x4e6f90`：card.bin ticket；有卡片奇迹时本人 ×4/5、他人 ×6/5。点券不足时价格变灰（`CardShop_UpdatePriceColors @0x4e6d00`）。
* 流程（Update `@0x4e6240`，phase `+0x64`）：欢迎 5201 → 5202 "請隨意選擇" → 购物（phase 2）；悬停卡：气泡显示 6501+卡索引的说明文字，移开回 5202；点卡 `CardShop_OnCardPurchase @0x4e53b0`：点券不足 → 5204（Plane1 图 0x208 闪 0.6 s），手牌满 → 5203，否则音效 0x8D、卡片消失、`hideCard` 显示该卡 0.6 s，之后 `Card__TriggerApproxCondition @0x4900d0`（加卡 + `Role_SubTicket(价格)`）；`Exit`（`CardShop_OnCloseDialog @0x4e6c30`）→ 5205 谢谢，1.3 × 系数后结束。
* 电脑 `Card_BuySell @0x4fc8d0`：目标手牌数 rand()%4+5，`Card_SelectCheap @0x4fd1b0`（难度 `InitTxt_GetParam(7)`：<3 `Card_SelectByDifficulty(1)`；≥3 先 50% 在喜好表 `0x5E6F00` = {0,1,2,8,11,12,17,24,26,31,34} 里找买得起且未持有的，再随机最多 10 次）买到点券不够为止；买到则入队 `0x65(30)`、文字 str 281 "%s買賣卡片"。

### 4.2 移植版
全部按上面实现（`event_shop.cpp`）。[A] 未做：`hideCard` 的出卡位移动画（原版每帧 `SetPosY(ftol(…))`，反编译丢失了浮点表达式）、卡片奇迹价格修正。卡价/卡等级从 card.bin 导出的表（`events.cpp` 内，`rm8CardPrice` 有值时优先用它）；卡名用 `rm8CardName`（cards 模块未装入时为空）。难度默认 3（用户的 init.txt `difficuty=3`），环境变量 `RM8_DIFFICULTY` 可覆盖。

## 4b. 问答点（事件 2，AskPoint）

### 原版 [V]
* 任务 `Question_InitContainer @0x4b2ae0`（vtable 0x5E46F8）；vf16 `Cls_5e46f8__Init @0x4bd5f0` = `Question_InitUI @0x4bcbe0`（奖惩文字 str 4505+等级 / 4516+等级，`%s` = 角色名）+ `Question_LoadResources @0x4bd1f0`（AskPoint.ui，主持人 article 0x417，欢迎语 str 4501，40% 概率播语音）；题库 `Config\questions_normal.ini`（201 题，`answer=0` = A 为正确答案，显示时 `Question_ShuffleAnswers @0x4bd960` 把四个答案随机放到四个按钮）。
* 流程（Update `@0x4bd610`，+0x34）：0 → 4502 "5 秒内回答"，动作 1 → 幕布两步（图 0x306 / 0x307）→ 状态 3 答题：倒计时 5 秒按 dt/1.5 递减（`Question_UpdateTimer @0x4bef10` 数字图 0x37..0x33、0x3C）；按钮 ID 1..4（`Cls_5e46f8__OnAnswer @0x4bde90`），对 → `Question_OnCorrect @0x4becd0`（音效 0x142，rewardLight，str 4503，动作 2，`Question_ApplyReward @0x4bdef0`），错/超时 → `Question_OnWrong @0x4bedf0`（0x140，punishLight，4504，动作 3，`Question_ApplyPunish @0x4be790`）；4.9 / 6.9 × 系数后结束。
* 奖惩等级 `Question_PickRewardLevel @0x4bf0d0`（0..9，概率随点券/地产变化）：0..2 现金 500/1000/3000（消息 0x17 / 0x16），3/4 点券 30/50（消息 7 / 8），5 一张卡（消息 4 / 5），6 加盖/拆自己一块地，7 其他人各失一张卡 / 自己失一半卡，8 向每人收/送 1000，9 一块地。
* 电脑 `AI_Event_Question @0x4f7fa0`：等级同上，Python `Answer(0)` != 0 为答对；奖惩消息入队，文字 str 264 / 265 "答題正確 / 答題錯誤 <奖惩文字>" 插到队首。

### 移植版
`event_ask.cpp`；题库由 `scripts/export_questions.py` 导出到 `scenes/questions.txt`。等级 6~9 需要地产逻辑，且对没有地产的玩家 `PickRewardLevel` 本来不会选到（[V] 其概率分支都要求地产数 > 0），所以只实现了 0..5。[A] 玩家有地产时等级 6..9 的奖励/惩罚未做（会退化为等级 0）。

## 4c. 3D 主持人 / 店员（银行 article 0x419、卡片店 0x41F、问答 0x417）

* 原版 [V]：加载器里 `CArticleDFF_Load(art)`（role.bin 的 NPC 模型：N24A、N25C、N23G）+ `Article_CalcClumpBoundingSphere` + `RwRasterCreate(256,256)` + `OrthoCam_Create(256, ..)`；每帧 UI 回调 0x13（`u_UI_DrawArticleSprite_6446f0 @0x4c11b0`、`LittleGame_DrawScene_G640958 @0x4e5120`、`u_UI_DrawArticleSprite_6446ec @0x4bcad0`）把摆件渲染进栅格（`UI_RenderArticleToRaster @0x4c4460`：渲染状态 0x1D = 5，alpha 参考 0xC4）再画到 "NPC" 控件矩形。动作 `Article_SelectByTypeAndSetProperties(n, 1, 0)`（播一次）：银行 0 欢迎 → 1 菜单（过路 2）→ 3 退出；卡片店 0 → 1 → 2 手牌满 / 3 点券不足 / 4 告别；问答 0 → 1 → 2 答对 / 3 答错。声音即各 string 的 `@id`。
* 移植 [A]：`event_npc.cpp`：离屏 256×256 相机栅格（照 `rm8RenderReflection`）+ `rm8UiSetTextureN` 贴到 "NPC" 控件；模型数据由 `scripts/export_event_arts.py` 导出到 `scenes/event_arts.txt`（art 0x417..0x420）；原版正交相机参数没有解出，改为按包围盒自适应取景。**朝向**：原版店员站在面板左侧朝右（用户在真机上对照确认），移植版在贴图时水平镜像（u 1→0，`ui.cpp` 的 `picTex` 分支）；该翻转对所有 NPC 一致，之后接入的其他任务（乐透 0x41B、魔法屋、法院等）用同一函数自动得到相同朝向。

## 5. 测试开关

| 变量 | 作用 |
|---|---|
| `RM8_EVENT_TEST=a,b,c` | 座位 0（人类，`--human 0`）每次在等骰子（状态 6）时依次"走到"这些事件：`bank`（5）、`bankpass`（99）、`shop`（1）、`ask`（2）、`park`、`cardpoint`、`point50`、`point30`、`point10`；全部是 `rm8EventLand(0, 事件号, 当前节点)` |
| `RM8_EVENT_AUTO=n1,n2,...` | 任务 UI 就绪后每 0.8 s 按下一个控件名（例：`saving,1,2,3,00,ok`；商店 `card0,card1,Exit`） |
| `RM8_NPC_DUMP=file.png` | 把第一个 3D 主持人的离屏栅格写成 png（调试取景） |
| `RM8_NPC_BACK=1` | 从模型背面取景（调试） |
| `RM8_DEBUG_EVENTS=1` | 日志：落点、点券、银行动作、贷款、收卡 |
| `RM8_DIFFICULTY=n` | 电脑买卡难度（默认 3） |

没设置时开关无任何影响。

## 6. 验证
见报告：全电脑模拟（3 张图 × 3000 s 游戏时间，约 230 次事件落点/图，回合推进到第 136~142 天，无卡死）；人类路径用测试开关截图验证（银行存/取/贷款/过路、商店购买、点券/卡片点、公园）。

## 7. 未做
* （第三批已做：0x50/0x51 拍卖、0x5A..0x5C 奇迹、超市/饭店/研究所、住旅馆、AskPoint 等级 6..9，见第 9 节。）
* 表情（`Role_AddMoney/AddTicket` 的 70% 随机表情）、throwcard.ui。

## 8. 第二批事件（法院、乐透、魔法屋、电力公司、点券中心）

共同点：任务类、AI 分支和消息全部按反编译实现；对话框控件很多 ID 全为 0，一律按控件名操作（`rm8UiXxxN`）；所有装饰控件设为 ignore（`uiTapOnly`），只有按钮可点；3D 店员/主持人由 `event_npc.cpp` 离屏渲染后贴到 "NPC" 控件。测试钩子 `RM8_EVENT_TEST=court|lottery|draw|magic|power|ticket`，`RM8_EVENT_AUTO` 默认走真实触摸路径（`rm8UiPointerDown/Up` 打在控件中心；`RM8_EVENT_CLICK=1` 才直接调任务），`RM8_EVENT_REMAP=5:14` 把全电脑模拟里的某个事件号换成另一个（压力测试罕见事件），`RM8_EVENT_POINTS=n` 给人类加点券。

特殊建筑的事件触发 [V]：`BuildObj_CreateEvent @0x4f19e0`——地块字 100/101/102/103 的建筑没有图标，但其所在道路格触发事件 0xF/0x10/0xD/0xE。导出器 `scripts/export_scene.py` 已把这些事件号写进对应的 `rnode`（`scenes/*/scene.txt` 已重新生成，只有 rnode 的事件号列变化）。

### 8.1 法院 0xE（event_court.cpp）
* 任务 0x4B3260（vtable 0x5E4860，名字是错的），`CUi_court__vf16_OnLoad @0x4d4de0`、Update `@0x4d5a60`、点击 `Court_OnAccuserSelected @0x4d4c90`、`Court_StartTrial @0x4d43b0`（三个罪名按钮 Com/Card/Land，ID 1/3/5）、`Court_ProcessAccusal @0x4d6510`、退出 `@0x4d62d0`。法官 article 0x411，原告/被告是角色模型（roleinfo artId1 第一个）。
* 状态 +0x90：0 欢迎 4001 → 1 选被告（其他未破产角色头像 RolePic%d，4002）→ 2 选罪名（悬停说明 4033/4035/4034，提示 4003）→ 3（0.3 s）"%s控告%s%s"（4004，罪名名 4030 藐視電腦 / 4032 亂用卡片 / 4031 強收地租）→ 6 判决 → 7 结束。
* 判决 [V]：〔string.bin#4030〕 60 % 成功→被告坐牢 5 天（文字 4005，消息 `0x5d(5,原告+1)`，双方 0x2e），失败→原告坐牢；〔string.bin#4032〕 70 % 成功，3 天；〔string.bin#4031〕 60 %，罚款 5000 现金（4028/4029，消息 0x16）。AI `AI_Event_Court @0x4f8810`：随机选一个其他角色，Python `Count`（0/1/2 对应三个罪名，其他值不控告），概率同上（〔string.bin#4031〕罚款乘物价指数）。
* 保释金 `CPlayerTag__OnPrisonBailDeposit @0x492ac0`（消息 0x5d）：保释金 = 天数 × 2000 × 物价指数，付得起时人类被问（4025，okcancel，答案写入消息 0x5e）；电脑在现金+存款 ≥ 30000×PI 时 50 % 自己付（4027，`@0x492c90`）；否则消息 0x19 坐牢。移植：`bailMessage`。
* [A] 原版 OrthoCam 取景未解出，改用自适应取景；判决后坐牢走 cards.cpp 的 0x19。

### 8.2 乐透 6 与开奖 0x62（event_lottery.cpp）
* 管理器 `Stock_StdVector__ctor @0x4c7c10`（DAT_006403d0）：32 个号码（`Stock_ClearAndInit @0x4c8180` 压入 1..32）、每个角色持有的号码、奖池 +0x50（初始 物价指数×5000，`@0x4c7d40`）。买号 `Stock_BuyCard @0x4c8200`：现金 ≥ 1000，号码未被买，奖池 +1000，消息 0x15(1000) 扣现金。
* 买号任务 0x4B2DE0（vtable 0x5E4788）：letou.ui（32 个号码按钮 ID 1..32，Quit，bonus 显示 "$奖池"），欢迎 4801 → 4802 → 4803；点号码 `Letou_OnLandEvent @0x4c7020`：成功音效 0x86 / 失败 0x71，随后告别 4804。AI `Lottery_BuyTicket @0x4f8290`：现金 ≥ 5000 时随机空号，文字 262。
* 开奖：`Game_TriggerHolidayEvent @0x4a66b0`——每月 15 日（`Game_OnTurnStart @0x4a3b50`，第 5 天之后，有人买过号时）为当前角色启动事件 0x62（`rm8EventDaily`，game.cpp 每个新日调用）。任务 0x4B30E0（vtable 0x5E4818）：letouErnie.ui；4805 → "奖金总额%d元"（4806）→ 4807 → 转盘（`CUi_letouErnie__vf1_Tick @0x4c99f0`：加速 tAcc 3~6 s、减速、位置 1..33 循环，起点 rand()%32+1）→ `Letou_Ernie_ProcessWinner @0x4c9c60`：持有该号码的角色得奖池现金（4808/4809），奖池重置，所有票作废；无人中奖则奖池 += 5000×物价指数（4810/4811）。
* [A] 角色颜色边框/拖尾和滴答音效（样本 id 反编译丢失）未做；转盘高亮是半透明方块，已买号码显示买家头像。

### 8.3 魔法屋 0xD（event_magic.cpp）
* 任务 0x4B2F60（vtable 0x5E47D0，旧名 Court_InitContainer 是错的）：`Cls_5e47d0__vf16_Init @0x4cb400`、Update `@0x4cb580`、转盘 `Cls_5e47d0__vf1_Tick @0x4cb890`、效果 `MagicHouse_ApplyMagicEffect @0x4cdb10`、文字 `MagicHouse_GenerateDescription @0x4d12f0`。主持人 article 0x412。
* 流程：欢迎 3901 → 3902 "〔string.bin#3902〕"（点角色头像，`MagicHouse_ShowRoleSelection @0x4ca940`）→ 3903 咒语，选择条在 4 行效果之间转动并减速（初速 12~20，减速 2~4）→ 停在哪行就对被选角色施该效果 → 3904 告别。4 个效果编号互不相同，各取 0..11（难度 ≥ 3 时 10 % 概率 +12，`@0x4cba90`）。
* 效果 [V]：0/1 ±5000 现金，2/3 ±10 % 存款，4/5 持股涨停/跌停 3 天，6/7 加盖/拆毁一层己方房屋，8/9 ±50 点券，10 得一张卡，11 失一张卡，12 与现金最少者〔string.bin#3917〕，13 失一半现金，14/15 存款，16/17 股票，18 与随机角色〔string.bin#3923〕，19 〔string.bin#3924〕，20 与点券最少者〔string.bin#3925〕，21 失全部点券，22 与卡最少者互换卡，23 失全部卡。互换走消息 0x27（`Role_SwapAssetsWithRole @0x48bf70`，类型 0..5），炸地产走 0x35(0,n)（`@0x487780`）。
* AI `Auction_Vendue_Process @0x4f84c0`（名字是错的，其实是魔法屋 AI）：随机选一个其他角色和 `rand()&3` 号位，说明文字插到队首。
* [A] 持股相关的效果 4/5/16/17 因股票模块没有对应接口而只显示文字不起作用；炸毁用等级清零，无爆炸动画。

### 8.4 电力公司 0xF 与点券中心 0x10（event_power.cpp）
* 两个公司各有一个管理器：4 个浮点持股比例（按座位）、董事长（持股最多者，`PowerCo_SelectBestStock @0x4da930`；第一次买入者直接当董事长）、点券中心另有消费等级。〔string.bin#5701〕 = 1 − 总和（`@0x4da760`）。买 10 % 花 物价指数×5000，从存款扣，存款必须多于这个数（`PowerCo_TryBuyStock @0x4da790` / `TicketCenter_CheckBudget @0x4dff50`）。
* 电力公司（任务 0x4B33E0，`CUi_PowerCo__vf16_OnLoad @0x4d8720`、Update `@0x4d9180`）：非董事长要交电费 = 己方地租（Terra_CalcRent(1)/物价指数）之和 /100×10（`Role_CalcOwnRentSumTenPercent @0x48c630`），现金扣除；`PowerCo_PaymentDivide @0x4daaa0`：每个"持股 × 金额 ≥ 1"的角色把**整笔金额**加到存款（原版代码加的是 param_1 而不是份额，按原样保留 [V]），付费者本人通过消息 0x5c 得到 ftol(金额 × 份额)。随后若还有剩余可买：4404 询问，买入后 4405，董事长易主则 4406，结束 4407。
* 点券中心（任务 0x4B3610，`CUi_ticketCenter__vf16__Init @0x4dda70`、Update `@0x4de640`）：非董事长要先掷骰子，消费 = 点数 × 等级（`TicketCenter_CalcMultiplier @0x4e0270`：总持股 ≤0.2 → 1，≤0.4 → 2，≤0.6 → 3，≤0.8 → 4，<1 → 5，=1 保持原值），点券 `Role_SubTicketEmoteAny`（消息 0x4B），按份额分给股东（`@0x4e0340`）。文字 4201..4209、4226。
* AI：`PowerCo_PayElectricity @0x4fc150`、`TicketCenter_PayTickets @0x4fc490`：先付（文字 269 / 272 + 消息 0x16 / 0x4B），剩余 > 5 % 且存款 ≥ 物价指数×20000 时买 10 %（270 / 273），成为董事长再等 30 帧并 271 / 274。
* [A] 股票柱用矩形（高度 130 px = 100 %），比例文字代替 J209_4 数字图；点券中心的骰子现在也是 go 单骰面板 + 真 3D 骰子（见 9.7）。

### 8.5 其他修复
* 3D 主持人的层次/朝向：离屏渲染把模型绕 Y 转 180° 再取景（`RM8_NPC_FRONT=1` 看错误一面）；此前的整图水平镜像去掉了（镜像不会改变层次，反而让左右反了）。银行、卡片店、问答、新事件共用。
* 卡片店在手机上买不了的原因：卡片控件 `card%d` 在 .ui 里是 Enabled=false，原版加载器（`vf16__Init @0x4e5aa0`）对每张卡 `UiLib_Widget_SetEnabled(1)`；移植版漏了，已补。装饰控件设 ignore，防止吞掉点击。

## 9. 第三批事件（拍卖、奇迹、超市 / 饭店 / 研究所、AskPoint 6..9）

### 9.1 土地拍卖 0x50 / 0x51（event_vendue.cpp，vendue.ui）
* 任务：`CreateGameObject @0x4b1bc0` case 0x50 / 0x51 → `Wonder_InitContainer @0x4b3910`（vtable 0x5E4980，名字是错的，其实是 CUi_vendue）：参数 地块、`Terra_GetBuildingModelId`、动画（住宅 2，否则 0）、底价 = `Terra_CalcPrice(1) / 2`，+0x5C = 1 表示破产拍卖（出价者不含当前破产者）。加载器 `CUi_vendue__vf16__Init @0x4e2fc0`、Update `@0x4e4020`、跳过 vf13 `@0x4e4f50`（无条件：计时器清零、停语音）。
* 起拍价 = 底价 / 2（`+0x58 = +0x54 / 2`）；每次出价加 物价指数 × {100, 500, 1000}（按钮 `100` `500` `1000` `pass`；物价指数 > 1 时按钮文字变成 "100 X n"，`Wonder_OnNumInput @0x4e1ae0`、`Wonder_OnUIEvent @0x4e1a90`）。按钮可用性 `Vendue_ShowOptions @0x4e25e0`：现金 ≥ 当前价 + 物价指数 × 档位。
* 流程（+0x4C）：公开拍卖 1 欢迎 4301（带语音）→ 2 "〔string.bin#4302〕%s" 4302（商业地 4310 + 建筑名）→ 3 "〔string.bin#4303〕%d元" 4303 → 4 "请出价！" 4304 + 采样 0x8E、轮流出价；破产拍卖从 0 开始，先说 4309 "〔string.bin#4309〕！"。轮到电脑 2.0 s 后 `Vendue_AIBid @0x4e22c0`（见代码里的概率表：现金不足最低档就放弃；`价格/底价 <= rand()%10+1` 才加价）；人类出价 `Vendue_OnBidAccepted @0x4e2230`，放弃 `Vendue_OnNoBidders @0x4e2140`（采样 0x93，该角色出局）。
* 结束：所有人放弃（`Vendue_CheckWinner`）→ 状态 7 "〔string.bin#4308〕！" 4308；其余人都出不起最低档加价且已有出价者（`Vendue_FindNextBidder`）→ 状态 5 "成交！" 4306（采样 0x8F）→ 给得标者队首入队 `0x41(地块, 价格)`（`Role_PayAndTakeTerra @0x487c20`：聚焦、现金优先扣款无表情、`Land_SetOwner`），得标者不是当前回合角色时加 0x2E 互相转向 → 状态 6 "恭喜您競拍成功！" 4307 → 结束。
* 触发：公开拍卖 = 新闻 29（`News_EventAuctionPublic @0x4b6e80` 入队 `0x4F(地块)` + `0x40(地块, 0)`，news.cpp 的 `rm8NewsAuctionHook` 由 events.cpp 设置）；破产拍卖 = `Role_CheckBankruptcy @0x48ee10`：破产者拥有的前 9 块地（9 块之后，有建筑的地块以 `候选等级 < 该地等级 且 候选等级 <= rand()%5` 换掉低等级候选），随机取 3 块（不重复），每块入队 `0x4F(地块)` + `0x40(地块, 1)`，最后 0x73；`0x72` 清算在拍卖之前入队，所以拍卖落槌时地块已无主（game.cpp `rm8GameCheckBankruptcy`，`rm8EventMessage` case 0x40 / 0x41）。破产检查只算 现金 + 存款（`local_8 = +0x290 + +0x294`，不含贷款 / 股票），已核对。
* [A] 拍卖师（art 0x415）和拍品（building.bin 的模型，空地用 art 0x27C）用离屏相机栅格，拍品以 160°/s 旋转（`Vendue_RotateImage @0x4e4fe0`）、加了一点俯角；头像边框的闪烁（`Ui_DrawRolePictureFrameAnimated @0x4e1bd0`）用动态 plane 控件。

### 9.2 奇迹 0x5A / 0x5B / 0x5C（event_wonder.cpp，wonder.ui）
* 地图格：道路格的地块字低字节 4..9 → `Land_GetLevel @0x4f0600` = (值-4)/2 → `Role_TriggerLandLevelEvent @0x4863f0` 触发事件 0x5A + 奇迹号（只在没有事件图标的格子上，`Role_OnArriveNode @0x4861a0`）；`scripts/export_wonders.py` 把每张图的 `wsite` / `wplot` / `wfinal`（地图 stage.bin 的 wonder=a,b,c）导出到 `scenes/<图>/wonders.txt`。
* 管理器 DAT_006403bc（`Wonder_Manager_Init @0x4e1670`）：3 条记录 {启用、拥有者(-1)、每个角色的持股(float)、成本 50000、比例 0.1}。任务 `Cls_5e4938__vf16__Init @0x4e05d0`（MagicHouse_InitContainer 的另一个实例）：已有拥有者则不开界面（`Game_UseCard` 失败，继续 0x12）；否则直接 `Wonder_OnPurchase @0x4e11a0`（人类电脑一样，没有确认框）：投资 = round(50000 × 0.1) = 5000 现金（不足：str 210 "現金不足，無法投資"），文字 204 "%s投资%d元"，入队 0x33（工地特效）；持股合计 100 %（剩余 < 0.001）时该角色〔string.bin#206〕：文字 206 + 说明（207 金钱 / 209 地产 / 208 卡片）。
* 工地：消息 0x33 → `u_Role_StartEventAtStPoint @0x486430` → `u_TerraObj_StartStaged @0x4ee690`：特效 art 0x17E7 + 采样 0x7C，结束后若 阶段 <= 完成百分比 / 25 则 `TerraObj_AdvanceStageModel @0x4ee700` 换下一阶段模型（building.bin id 5/6/7 的 1..4 级，第 5 阶段 = 该地图的奇迹成品 art）。
* 效果 [V]：金钱奇迹 — 月结利息拥有者 存款/5（20 %），其他人存款/20（5 %）（`Balance_ShowDepositInfo @0x4afa40`，game.cpp 的月初结算）；地产奇迹 — 拥有者付租减 20 %、收租加 20 %（`Role_ProcessArriveLand @0x4868e0` 他人地分支，`rm8EventWonderRent`）；卡片奇迹 — 拥有者买卡 ×4/5，其他人 ×6/5（`CardShop_CalcCardPrice @0x4e6f90`，卡店状态 1 的气泡 5206 / 5207，event_shop.cpp 的 `price()`）。拥有者破产时 `Wonder_CheckConstruction` 返回 -2（效果消失），但卡片奇迹的原版价格函数读的是原始拥有者字段（含破产者）。
* 跳过 vf13 `@0x4e15b0`：无条件，计时器清零。

### 9.3 超市 / 饭店 / 研究所（event_build.cpp，market.ui / hotel.ui / graduate.ui）
* 进入 `Role_OnArriveCommercialBuilding @0x486260`（game.cpp `arriveCommercial`，替换了之前的随机骰子扣费）：地块有主、有建筑、商业；研究所 = 本人的地且未附身糊涂神；超市 / 饭店 = 他人的地、未隐形、`Role_CheckRentExempt == 0`；成立则 `Game_SelectCard`（建筑对象 vf16）开界面、状态 9，最后照常入队 0x11。建筑对象 `Card_CreateUI @0x4f5de0`（vtable 0x5E6D28 研究所、0x5E6C48 超市、0x5E6BD8 饭店）。
* 超市 / 饭店共用 `Hotel_BeforeVisitor @0x4f6ba0` + `Hotel_ProcessVisitor @0x4f6c80`：状态 1 欢迎（5001 / 5101，3D 店员 art 0x41D / 0x41E）→ 2 请选择（5002 / 5102）→ 10 掷骰（人类点 "色子" 按钮，电脑 `rand()%6+1`）→ 3 "消费指数/房间等级为 %d, 〔string.bin#5003〕 %d"（5003 / 5103）→ 4 价格 = 基本价 × 点数（5004 / 5104）→ 0xB 调整：保镖 0（556）、财神 0（407）、穷神 ×2（409）、闯关模式 0 号座位莱娜 ×7/10（53）→ 5 付款（现金优先带表情）、地主存款收款；文字 5005 / 5105 → 6 结束，饭店再入队 0x13。基本价：超市 = 地主所有超市等级之和 × 1000 × 物价指数（`@0x4f76d0`），饭店 = 等级 × 2000 × 物价指数（`@0x4f7720`）。
* 饭店住宿：消息 0x13 → `CPlayerTag__EnterHotelStatus @0x4852e0`（状态 3、+0x1D8 = 1 天，cards.cpp `rm8CardsEnterHotel`）；之后每个回合开始步骤 9 → `Role_TickHotelStay @0x4851a0`：还有天数且当前格地块是饭店：str 52 "%s住旅館中\還有%d天"，天数 -1；否则天数清零、状态 0 继续。（走进 / 走出饭店的步行 2026-11-15 已补：消息 `0x13` → 状态 `0xb` 走向「节点与建筑的中点」并缩到一半，到了才是状态 3；`0x14` → 状态 `0xc` 走回节点，见 `characters.md` 第 6 节，`roles.cpp` `rm8TurnHotelWalk`；住满后 `0x14` 在 `0x25(0)` 之前入队。）
* 研究所：`Cls_5e6d28__vf16_StartInstituteResearch @0x4f6360`：人类 graduate.ui，5 张卡（卡 id {2,9,17,15,19}，〔string.bin#5901〕 {2,3,5,7,10}，表在 0x5E6DE4 / 0x5E6DF4），建筑等级 n 只显示前 n 个；点卡片按钮 `Graduate_OnUIEvent @0x4e71a0` 选中（图片显示在 Title2、悬停说明 5901..5905 `UI_ShowGraduate @0x4e7060`），"確認" = `TrySelectCard @0x4f6770`（换卡才重置天数），"離開" = `CheckAndTransition`；欢迎 4101、选择 4102、再见 4103。电脑：`rand() % 等级` 选卡，文字 263 插到队首。每个新日 `Institute_DisplaySuccess @0x4f6890`（`TurnStart_UpdateTerrain @0x4f11f0`）：天数 -1，到 0 时拥有者收到文字 4104 + 消息 4(卡, 1)。
* 跳过：`Hotel_BeforeVisitor` 类的 vf13 是 `Card_UpdateVisualization @0x4f5ff0`（计时器清零、停语音，总是视为处理了）。

### 9.4 AskPoint 等级 6..9（event_ask.cpp）
* `Question_PickRewardLevel @0x4bf0d0` 完整复现：范围 100（没有卡片 80），没有"等级 0..4 的己方地 且 等级 1..5 的己方地"减 15，没有"有无主地 且 己方有空地"减 5；r < 15 → 0，< 25 → 1，< 30 → 2，< 45 → 3，< 50 → 4，有卡 < 65 → 5，再 15 宽 → 6（要地）、5 宽 → 7（要卡）、5 宽 → 8、5 宽 → 9（要地），其余 0。
* 奖励 `Question_ApplyReward @0x4bdef0` / 惩罚 `Question_ApplyPunish @0x4be790`：6 = 己方随机一块等级 0..4 的地免费加盖（`0xC(地块, 1)`）/ 己方随机一块有房的地降一级（`0xD`）；7 = 其他每个有卡的角色〔string.bin#3929〕（文字 5614、`5(-1,1)`、0x2E 轮流）/ 〔string.bin#4523〕（向上取整）；8 = 向其他人每人收 **10000**（代码里 `push 0x2710`，文字 4513 却写 1000，原版如此）/ 付给每人 1000（0x38 / 0x39 `Role_CollectMoneyFromAllOthers @0x489c30` / `Role_PayMoneyToAllOthers @0x489d50`）；9 = 随机一块无主地归自己（0x2A，`Role_StartEffectOnOtherTerra @0x4879b0`，闪烁 2 s）/ 己方随机一块空地失去拥有者（0x29，`Role_StartEffectOnOwnTerra @0x487a70`）。
* 贷款不计入破产检查（已核对 `Role_CheckBankruptcy @0x48ee10`）。

### 9.5 新状态（纯结构体，存档用）
* 拍卖：只有任务期间的临时状态，不进存档（每次从头开始；`g_slots` 只是 UI 控件缓存）。
* 奇迹：`event_wonder.cpp` 的 `g_m`（`Rec w[3]`：active、owner、share[8]、cost、rate、stage、percent）；工地阶段模型按 stage 重建。
* 研究所：`event_build.cpp` 的 `g_res`（地块号 → {卡片索引, 剩余天数}，`instituteGet / instituteSet`）。
* 饭店住宿：状态 3 + cards.cpp 的 `X(seat).days`（和睡眠 / 住院共用）。

### 9.6 测试开关（第三批）
`RM8_EVENT_TEST=auction|bauction|wonder0|wonder1|wonder2|market|hotel|institute`（`RM8_EVENT_TERRA=地块` 指定拍品，`RM8_EVENT_LEVEL=n` 指定建筑等级）；`RM8_EVENT_AUTO=100,500,pass,...`、`Button`、`Button21,Button,Button1`；`RM8_ASK_LEVEL=n` 固定 AskPoint 奖励等级；`RM8_EVENT_FOCUS=x,z,t` 配合 `RM8_SHOT_FOLLOW` 把镜头固定到某点；`RM8_NEWS_FORCE=29` 触发公开拍卖。

`RM8_EVENT_AT=1`（配合 market / hotel：座位 0 站到该地块的节点上）；`RM8_EVENT_TEST=ticketland|powerland`（座位 0 站到点券中心 / 电力公司路格前一格，配合 `RM8_AUTO_ROLL=1 RM8_FORCE_DIE=1` 真实走一步踩上去；`RM8_EVENT_CHAIR=1` 预置座位 0 为董事长）；`RM8_FORCE_DIE=n` 人类的骰子固定为 n。

### 9.7 正交相机、单骰面板、离场台词、买卡动画（第三批补充）
* 3D 主持人用原版的 OrthoCam（`OrthoCam_Create @0x4617f0` / `OrthoCam_SetPosAndSize @0x4618e0` / `OrthoCam_RenderToRaster @0x461a30`）：256×256 平行投影，视窗 (1.1r, 1.1r) 半宽，近 0.1r、远 5r，相机在包围球中心后 1.5r；包围球按 `Article_UpdateBoundingSphere @0x443b00` 合并各 atomic 球；栅格画进控件时用 `Rect_CalcAspectUVMargins @0x45f260`（长边留满、短边按 0.9 裁，ui.cpp）。拍品用 r×1.6（`LittleGame_Rotate3D @0x4e1f40`），160°/s 旋转。朝向（绕 Y 转 180°）仍是 [A]。
* 单骰：事件对话框的骰子按钮就是玩家的 go.ui 单骰面板（`Unk_GetOrInitGlobalObjectRef @0x494180` = 0x5E2538，`Cls_5e2538__vf10_Init @0x4e8b80` 隐藏 3/4 号槽，按钮事件 `Ui_OnEvent_StartDiceScene @0x4e8a80` → `Role_StartDiceSceneCount1 @0x484d70`）：面板移到对话框骰子控件处（hud.cpp `rm8HudDieGo`），点 GO 扔一颗真 3D 骰子（roles.cpp 状态 4，`rm8TurnEventDieStart/Take`），结果回到任务；滚动时只有 go 面板隐藏（`Role_StartDiceSceneCount1 @0x484d70`：只隐藏 go 面板，对话框和店员/气泡一直在；`Hotel_InitiateStay @0x4f7610` 点数到手时再显示对话框、隐藏 go）。面板位置 = 对话框里 ID 2 的“色子”按钮（`Hotel_BeforeVisitor @0x4f6ba0` → `UI_CreateMinimap @0x4e7310` 把 go 对象移到该控件的屏幕坐标，`Ui_MarketHotel_HideWidget2AndBindCallback @0x4e7370` 隐藏按钮自己的图）。点券中心、超市、饭店都这样；电脑直接 rand()%6+1。
* 点券中心头像：份额表控件原本不随 ticketCenterPlane 隐藏，掷骰界面露出头像；现在 `showPlane()` 一起显示/隐藏。
* 买股后退不出：我给“骰子滚动”新增的状态 20 和买股后的 0x14（=20）撞了，改成 30。退出按钮：原版 `CUi_ticketCenter__vf11 @0x4dd9f0` / `CUi_PowerCo__vf11 @0x4d9bd0` 直接置状态 5，反编译里没有台词；按用户反馈改为先进状态 4 播“欢迎下次再来”（4209 / 4407，含语音）再结束 [A]。
* 费用核对（逐指令，反汇编 `PowerCo_PaymentDivide @0x4daaa0` / `TicketCenter_PaymentDivide @0x4e0340` / `Role_CalcOwnRentSumTenPercent @0x48c630`）：电费 = (Σ己方每块地 Terra_CalcRent(1)/物价指数，上限 999999999，再 /100*10)；对每个未破产股东：part = ftol(金额 × 持股)，part > 0 才有份——当前角色自己（若是股东）走消息 0x5C(part, 存款)，**其他股东收到的是整个金额**（`push ecx = [ebp+8]`，原版如此）存入存款；点券中心：part 同样 ftol，其他人 Role_AddTicket(part)（上限 9999），当前角色走消息 7。点券中心消费 = 骰子 × 等级（`TicketCenter_CalcMultiplier @0x4e0270`：份额合计 >0.2/0.4/0.6/0.8 对应 2/3/4/5，≤0.2 为 1，≥1.0 不变），人类 `Role_SubTicketEmoteAny(消费, 0)` 立即扣、`PaymentDivide(min(持有, 消费))`，电脑 `PaymentDivide(消费)` + 入队 0x4B(消费, 1)。**修了一个 bug**：消息 0x4B 没有任何处理，电脑在点券中心缴的点券一直没扣；现在 events.cpp 处理（扣点券，下限 0；表情由 present.cpp 管）。买入：成本 = 物价指数×5000，存款必须严格大于成本，先扣存款；首位买家直接成董事长，否则 `PowerCo_SelectBestStock`。
* 买卡动画：`Cls_5e4a10__vf1_Update @0x4e6240` 飞行 0.6 s 内 hideCard 的 Y 按 `Y = ftol(Y + 高 × 2.7 × dt)` 下落（出卡口 → 取卡盘 cardOut），结束时复位并交卡（event_shop.cpp）；ui.cpp 新增 `rm8UiSetRectN`。
* 拿卡音效：`Role_ReceiveCardWithUi @0x488e80` 在 Deck_AddCardByID 之后播 0x7C（`receiveCard`）。
* 语音：`plainFmt` 会去掉“@N”，主持人 say() 需要保留，新增 `plainFmtV`（电力公司 4403 / 4404、点券中心 4206）。
* 电脑答题：`AI_Event_Question @0x4f7fa0` 只有“答題正確/答題錯誤 + 奖惩文字”的文字框（字符串 264/265），已显示（截图确认）。卡片格给卡：文字框 180 帧 + 0x65(30) + 取卡框 60 帧，等待系数 0.7，和 `Cls_5e46b0__vf16 @0x4b1750` / `Role_ShowInfoDialog @0x488ae0` 一致，没有额外延迟。
* 测试：`RM8_EVENT_AUTO` 里的 `go` = 单骰面板的按钮。
