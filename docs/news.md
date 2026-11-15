# 新闻系统（News）

范围：自由模式每隔一周的周六新闻（事件 0x11）、节日新闻（消息 0x3C，id 0x2E..0x3A）、警察 / 小偷 / 幸運卡片 / 寶箱的角色新闻（消息 0x3C，id 0..11），以及全部新闻效果。故事模式的剧情新闻只列接口（见 §9）。
代码：`app/news.cpp / news.h`；数据 `scenes/news.txt`（`scripts/export_news.py`）；移植侧钩子见 §8。标记：[V] 反编译读到并给出地址，[H] 推测，[A] 移植的适配。反编译里 `News_EventPrisonAmnesty / HospitalFire` 的名字对调了（状态 4 = 医院、5 = 监狱，见 §5）。

## 1. 触发（`Game_OnTurnStart @0x4a3b50`，每个新的一天）

顺序 [V]：日期 + 物价指数 + 股票 + 每月 1 日 NPC 刷新 + 地块 → 第 6 天起每月 1 日月结算（事件 0x52）/ 每月 15 日乐透（0x62）→ `Game_CheckFestival @0x4a5fb0` → 若无节日且前面没起任务，`Game_TriggerEventByDate @0x4a6780`。

- **节日**：`Festival_LoadTable @0x4a5ba0(1, yyyymmdd, 星期)` 返回节日号 1..13；有则对当前角色入队 `0x3C(节日号 + 0x2D, 座位 + 1)`（`Role_EnqueueMsg`，排在 0x68 回合开始之前），**日期新闻跳过**（月结算 / 乐透起了任务时节日仍然检查）。
  - `config\festival.bin`（6 块 × 32 年，2003..2034，每行 8 位日期 + CRLF，头 3 行 = 6 / 32 / 20030000）：块 0..5 = 春节 2、元宵 3、端午 8、七夕 9、中秋 10、重阳 11（函数返回值）。
  - 固定日期（`param % 10000`）：0101 → 1 元旦，0214 → 4 情人节，0308 → 5 妇女节，0401 → 6 愚人节，0501 → 7 劳动节，1225 → 13 耶诞；11 月 22..28 日且星期 = 4 → 12 感恩节（第四个周四）。
  - 函数开头第一次读文件（取第 `年 - 2003` 行与日期比较，命中返回 0xC）读到的是文件头或别的年份的日期，永远不命中 [V]（逐行推演）。
- **周六新闻**（`Game_TriggerEventByDate`）：`星期 == 6` 且 `(已进行天数 / 7)` 为奇数 → `Game_UseCard(0x11, 0)` = 事件任务 0x11，当前角色状态 9。移植的星期 1..7（7 = 周日，第一天用系统星期，0 = 周日）：从周日开局的话第 7、21、35、49… 天出新闻（实测日志：11-21、12-05、12-19、01-02）。
- 故事模式第二条分支（第 10 天后，座位 0 的 `+0x378 < 1` 且 (角色, 层) ∈ {(6,1) (7,3) (10,0) (11,1) (13,0)}）：`News_OnUpdate(0x1e, -1)` = 怪兽新闻（§9）。

## 2. 事件任务 0x11 与呈现（`Cls_5e20a8`，vtable 0x5E20A8，构造 `GameState__Ctor @0x493820`）

任务对象 = 场景任务槽（`g_GameState + 0x34`，移植：events.cpp 的 `g_task`，`createTask` 的 `case 0x11`）。加载（vf16 `@0x4B4170`）：
1. `News_InitDialog @0x4b41b0`：主播 = 文章 1052（`article\NPC\N24D_Act01.dff`，动作 0..2）放在相机前：`位置 = 相机位置 + 15·s·右 − 8·s·上 + 80·前`，缩放 `0.3·s`（`s` = 800 宽屏 1.0，否则 0.88；移植恒 0.88）。`news.ui`（`Plane` 280,124 / 490×435：`Label1` = 电视机边框图 514，`Label` = 横幅 516，`Label2` = 标题文字 ID 1，字号 16，居中）由 `News_Container` 的 vf+8 (`UI_DialogShowMessage @0x4e78b0`) 创建并写字，`News_OnClean @0x4b4ee0` 的 vf+0x1c(−1) 隐藏。
2. `rand() % 5 == 1`：**动物模式** `News_AnimalMode @0x4b4560`（文章 960 `news\x60` = 雪花屏、标题空、主播动作 2 播一遍、音效 0x7E + 0x7F 循环，1.8 秒）；否则 **播报模式** `News_BroadcastMode @0x4b4490`（文章 922 `news\x61` = 彩条、标题 string 4901「下面播報新聞內容！」、音效 = 4901 的 `@` 号、主播动作 0 播一遍，2.6 秒）。
3. 每帧（`vf1 @0x4B4690`）：计时器 − dt；到时 `Cls_5e20a8__Cleanup @0x4b8d10` 停音效，然后：动物 → 播报；播报 → `News_SpecialEvent @0x4b4620`（选新闻，§3，图 = 文章 `900 + id`，3.8 秒，模式 3）；模式 3 → `News_OnClean`，返回 0x80000000（任务结束，`Game_EndTurn @0x4a48d0`：状态 9 → 8）。所有时长 × `Anim_ApplyDifficultyScale`（`1 − min((速度 − 1) / 10, 1)`）。
4. 渲染（`vf2 @0x4B4770`）：先画图（文章 `900 + id`，`News_CreateTextEffect @0x4b4f60`：位置 = 相机位置 − 0.75·上 + 60·前，缩放 `0.05·s`，绕 X 转 −俯仰角、绕 Y 转 偏航 − 180，动作 0 循环），清 Z，再画主播（所以主播永远在图前面），最后 Im2D 四个格子（`News_InitDialog` 算的 4 个 240×280 的屏幕矩形，颜色 0，意义不明 [H] 遮罩），UI 在 3D 之后画（电视机边框盖在图上，3D 图从边框的屏幕洞里露出来）。
5. 角色新闻 / 节日新闻（`News_OnUpdate @0x4b4da0`）不走 1、2：直接 `News_InitDialog` + 效果 + 图 + 3.8 秒（模式 3），任务挂 `g_GameState + 0x34`、`CPlayerTag_SetStatusCode9`。

## 3. 选新闻（`News_SelectRandomNew @0x4b4850`）[V]

- 第一次：表 `0x5E4A58`（44 个 id：1..21、23..45）`[rand() % 44]`。`News_EventSwitch` 返回 0（条件不满足）则循环重选：表 `0x5E4B58`（38 个 id：1..20、25..42 —— 所以**重选永远不会选医院类新闻 21 23 24 43 44 45**）`[rand() % 38]`。
- 历史 `DAT_00604520`（5 个，初值 −1 结束标记，进程内全局）：选中的 id 若在最近 5 次里，换成 `rand() % 44`（**裸数 0..43，不经表**，所以 22、0 等无效 id 也可能出现，`EventSwitch` 的 default 返回 0 = 再重选）；接受后历史整体后移一位，新 id 放最前。
- 概率：第一次均匀 1/44；有条件 / 半概率的新闻（见 §4 表）被拒绝后概率流向表 2 的 38 个 id。医院类 6 条各有 1/2 的拒绝率；"第 6 天 / 16 天 / 15 天" 起的新闻开局几天不出现；拍卖（29）在移植里永远被拒（§8）。

## 4. 新闻表（`News_EventSwitch @0x4b4920`；string = 300 + id，图 = 文章 900 + id；金额都 × 物价指数 PI）

| id | string / 文字 | 条件 → 效果 [V] |
|---|---|---|
| 1 | 301 股市一路長紅 | `〔string.bin#6317〕天数 < 1` 且星期 < 6：`Stock_SetField34_38(+10.0, 1)`（全市场 +10%，1 天；stock_port.md：原版 bug 使它每天 ±10% 永不结束，移植保留） |
| 2 | 302 獎勵股票大戶 | 第 6 天起；股票市值最大的角色（> 0）：`Role_TakeMoney`：现金 +(rand%5+1)×1000×PI（0x17 排在该角色队列最前，再 `PairWithCurrent`） |
| 3 | 303 銀行加發10% | 每个未破产角色队首 `0x5C(存款×10/100, 1)`（进存款） |
| 4 | 304 表揚大地主 | 第 16 天起；地块最多（> 0）的角色 +(rand%5+1)×2000 |
| 5 | 305 海外投資成功 | 随机未破产角色 `+ (rand%10+1)×2000`（`News_EventGetMoney`，0x17） |
| 6 | 306 銀行補助土地最少者 | 第 16 天起；地块最少（可为 0）的角色 +(rand%5+1)×2000 |
| 7 / 14 | 307 / 314 | 随机角色 `−(rand%3+1)×1000`（0x16 现金优先，**罚款进乐透奖池** `+0x50 += 金额`） |
| 8 | 308 醫院發生火災 | 状态 4（医院）的角色停留天数 `+0x1D8 = 0`（下回合开始出院） |
| 9 | 309 監獄大赦 | 状态 5（监狱）同上 |
| 10 | 310 銀行獎勵儲蓄大戶 | 第 6 天起；〔string.bin#7043〕（> 0）+(rand%5+1)×1000 |
| 11 | 311 股市崩盤狂跌 | 同 1，−10% |
| 12 | 312 海外投資虧損 | 随机角色 −(rand%10+1)×2000（进奖池） |
| 13 | 313 網路大當機 | 休市天数 < 1：休市 `rand%7+1` 天（不检查星期；`Stock_Array_SetField(天数, −1)`） |
| 15 / 18 / 38 | 315 / 318 / 338 | 随机角色 −(rand%5+1)×1000（进奖池） |
| 16 | 316 | −(rand%7+1)×1000 |
| 17 | 317 駭客入侵 | 每个未破产角色存款 −(rand%10+1)×2000×PI（不低于 0，**立即**生效；每人进奖池） |
| 19 | 319 卡片被小狗咬壞 | 随机角色且持卡 ≥ 1：队首 `0x05(−1, 1)`（随机丢 1 张） |
| 20 | 320 外星人抓去 | 随机**状态 0** 的角色：队首 `0x5F(3, 0)`（UFO，消失 3 天，§6） |
| 21 / 23 / 45 | 321 / 323 / 345 住院 1 天；24 → 324 住院 2 天；43 / 44 → 343 / 344 住院 3 天 | `News_EventSpecialEvent`：先 `rand() & 1 == 1`（半概率），再随机状态 0 的角色：队首 `0x18(天数, 0)`（住院） |
| 25 | 325 颱風（art 0x178B） | 有主且有房的地块 ≥ 3 块：随机一块，其主人队列 `0x52(art, 地块)`（特效）、`0x4A(地块, 1)`（拆房，地主保留）；当前角色先 `0x2E(主人 + 1, 0)` 交接 |
| 27 | 327 〔string.bin#327〕（0x178A） | 同上 |
| 28 / 31 / 32 | 328 〔string.bin#328〕 / 331 洪水 / 332 地震（0x178C） | 同上 |
| 26 / 34 | 326 房屋鬧鬼 / 334 房地產商人炒樓 | 随机一块地（任意）：当前角色队首 `0x4F(地块)`（巫师闪烁），`Terra_AdjustPriceByPercent(∓10)`：`+0x48 = +0x48 × p / 100 + +0x48`（C 整数除，100 → 110 → 121 / 90 → 81…） |
| 29 | 329 拍賣公有土地 | 无主地块 > 4 块：当前角色队尾 `0x4F(地块)` + `0x40(地块)` → 事件 0x50 拍卖 |
| 30 | 330 怪獸襲擊 | 有房且地主不是角色 10 的地块任意一块：当前角色队首 `0x31(地块)`（怪兽，§6） |
| 33 | 333 房屋拆遷 | 同 25 类（art 0x178C）+ 先算好 `Terra_CalcPrice(1)`，地主队列第 3 位 `0x5C(价格, 1)` 补偿进存款 |
| 35 / 36 | 335 祖先遺產 / 336 痰盂 | 随机角色 +(rand%5+1)×1000 / ×2000（`News_EventGetMoney`） |
| 37 | 337 〔string.bin#337〕 | 第 15 天起：每个未破产角色 `CPlayerTag__SetDeferredAction(0)`（停留标志 +0x1D0，下回合原地停一回合） |
| 39 | 339 逃漏稅 | 随机角色 −(rand%5+1)×2000 |
| 40 | 340 過生日 | 随机未破产角色且持卡 < 5：其它每个持卡角色（扫描前 `角色数` 个卡槽）各取一张（`rand()%3 == 1` 换候选）交给他（队列 `0x04(卡索引, 1)`），无人给则失败 |
| 41 | 341 股市操作失誤 | 休市 < 1 且星期 < 6：每人 `Stock_SellAll`，所得进存款；总额 > 0 才成功 |
| 42 | 342 儲蓄金最少 | 第 6 天起；存款最少者 +(rand%5+1)×2000 |

`PairWithCurrent`（`Role_EnqueueMsgPairWithCurrent @0x4b8a00`，反汇编核对）：对象 ≠ 当前角色时，对象队首 `0x2E(当前 + 1, 1)`，当前角色队列的 `位置` 处 `0x2E(对象 + 1, 0)`——任务结束后当前角色把控制权交给对象，对象做完自己的消息再交回。

## 5. 节日新闻（`News_EventDispatcher @0x4b7f80`，图 = 文章 900 + id，id = 节日号 + 0x2D）

| 节日 | id / string | 效果 [V] |
|---|---|---|
| 1 元旦 | 0x2E / 346 | 每个未破产角色：文字 5321「%S獲得%d元」→ 等 30 帧 → `0x17(5000, 现金)` |
| 2 春节 | 0x2F / 347 | 音效 0x68 循环（烟花，任务结束停）+ 50000 |
| 3 元宵 | 0x30 / 348 | 状态 4（医院）全部出院 |
| 4 情人节 | 0x31 / 349 | `Card_OnGetCard(1, 0)`：女性角色（roleinfo `+0x304 == 0`）各得 1 张（文字 5303，`0x04(−1, 1)`） |
| 5 妇女节 | 0x32 / 350 | 女性角色得存款 10% 的现金（`Role_GetMoneyByPercent(10, 0)`） |
| 6 愚人节 | 0x33 / 351 | `Role_BroadcastEvent`：每个未破产角色入队 `0x49`（= `Role_TriggerLandEvent(0x11)`，各自回合起一个周六新闻任务）+ `0x1E`（镜头） |
| 7 劳动节 | 0x34 / 352 | `Card_OnGetCard(1, 2)`：交通工具（`+0x22C`）< 2 的走路角色各 1 张 |
| 8 端午 | 0x35 / 353 | 全员 55 点券（文字 5322 + `0x07`） |
| 9 七夕 | 0x36 / 354 | 男性角色（sex 1）各 1 张 |
| 10 中秋 | 0x37 / 355 | 状态 5（监狱）全部释放 |
| 11 重阳 | 0x38 / 356 | 全员 99 点券 |
| 12 感恩节 | 0x39 / 357 | `Role_DoAction`→`ClearPossessedGod @0x48edc0`：每人 `+0x1D8 = 0`（所有状态天数清零，下回合离开）；附身 窮神 / 衰神 / 糊塗神（2 / 3 / 6）的 `+0x1DC = 0`；`CleanupTarget` 清炸弹 |
| 13 耶诞 | 0x3A / 358 | `Card_OnGetCard(1, 9)`：全员随机 1 张 |

这些效果是**追加到各角色队列尾部**的（`Obj_EnqueueMsg_Alt`）：当前角色马上处理，其它角色在自己的回合开始时才拿到（所以元旦当天其它三人下一轮才加钱，和原版一致）。

## 6. 效果用到的队列消息（`Role_DispatchQueuedMsg @0x481df0`）

| 消息 | 处理 | 移植 |
|---|---|---|
| 0x16 / 0x17 (a, 账户) | `Role_SubMoney / AddMoney(a, b, 带表情)` | events.cpp（已有） |
| 0x5B / 0x5C (a, 账户) | 同上**不带表情** | news.cpp |
| 0x04 / 0x05 / 0x07 / 0x08 | 收卡 / 丢卡 / 加减点券 | events.cpp（已有） |
| 0x18 / 0x19 | 住院 / 坐牢（救护车 / 警车演出，NPC 模块） | cards.cpp / npc_act.cpp（已有） |
| 0x2E (a, b) | 接力 | present.cpp（已有） |
| 0x31 (地块) | `CPlayerTag__OnParkEvent @0x48d310` → 自由模式落到 `u_NPC_Chest_Construct @0x4ef000`：怪兽文章 0x1774（6004 `eff\Eff_07`）放在地块上，音效 6，1.5 秒后（`Cls_5e6758__vf1 @0x4f52e0`）`BuildObj_ResetAll(1,1)`：房屋消失（等级 0），地主保留 | news.cpp：`rm8FxPlayAtTerra(地块, 0x1774, 6)` + 等 90 帧 + `0x4A` |
| 0x40 (地块) | `Role_TriggerEvent50or51 @0x487bc0`：`Game_UseCard(0x50, 地块)` 公有地拍卖 | news.cpp 钩子 `rm8NewsAuctionHook`（events 侧还没有拍卖，所以新闻 29 被拒） |
| 0x49 | `Role_TriggerLandEvent(0x11)` | news.cpp：`rm8EventLand(座位, 0x11)` |
| 0x4A (地块, b) | `u_Role_ResetTerraEffect @0x4876c0`：地主是自己则说话类 4；b == 0 `Land_ResetBuildAndOwner`，否则 `BuildObj_ResetAll(1,1)`（拆房，地主保留） | news.cpp（在 cards.cpp 之前处理，cards 把 0x4A 吞掉了） |
| 0x4F (地块) | `Role_ProcessWizardMsg @0x487b60`：`NPC_CreateWizard`（`Cls_5e6938 @0x4f5d00 / @0x4f5d50`）地块每 0.4 秒透明度 0x60 / 不透明交替，5 次（2 秒），期间角色状态 9 | news.cpp：镜头对焦 + 等 120 帧 [A]（闪烁没画） |
| 0x5F (天数, 0) | `NPC_CreateRandom_Type3 @0x4a46d0`：UFO（art 0x178A，缩放 1.0，无音效，`+0x20 = 2.4` 秒时 `RoleAnimation_TriggerAction` = 角色 vf+0x6C `ClearFlag1C8 + ResnapToNode` 被抬走，`+0x24 = 4.0` 秒时 `Cls_5e3be0__vf18 @0x4b16a0` = `Role_SetVanishStatus(天数) @0x48ae10`） | news.cpp：UFO 特效文章 + 144 帧后状态 6 + 96 帧 |

**消失状态 6**（`Role_SetVanishStatus @0x48ae10` / `Role_TickVanishStatus @0x48ae80`）：状态 6、`+0x1D8 = 天数`；每回合开始 step 9：天数 < 1 → 状态 0、入队 `0x61`（再次处理落点）；否则提示 string 359「%s消失還有%d天。」、天数 − 1、状态 8（无表情）。cards.cpp 的 `rm8CardsStatusStep` 加了状态 6（提示格式 359）；`rm8CardsVanish / rm8CardsSetStatusDays` 新接口。roles.cpp：状态 6 不画（`CPlayerTag__Render @0x485d00` 只在 `vf+0x20`（`CanMoveNormally @0x4932e0`：状态 ≤ 2…）成立时画）。

## 7. 角色新闻（警察 / 小偷 / 幸運卡片 / 寶箱；消息 0x3C(id, 座位 + 1)）

`CPlayerTag__OnLotteryEvent @0x48e7e0` → `News_OnUpdate(id, 座位 b − 1)`（`@0x4b4da0`）→ `News_OnSpecialEventUpdate @0x4b4e40`：id < 9 `News_EventMinigame(3, id, 座位) @0x4b5ef0`；9 `News_EventThief @0x4b7bc0`；10 `News_EventLuckyFind @0x4b7d10`；11 `News_EventTreasureChest @0x4b7e50`；0x2E..0x3A `News_EventDispatcher`（§5）；其它 0。

| id | 文字 | 图（文章） | 效果（队首插入对象队列，再 `PairWithCurrent`） |
|---|---|---|---|
| 0..8 | 418 + id（警察 0..7 + 8 = 警察抓小偷）+ `@061` | 6201 + id（`destiny\N07m01..09`） | `0x19(3, 0)` 坐牢 3 天 |
| 9 | 428「〔string.bin#428〕，丟失一半點券！」`@062` | 6210 | `0x08(点券 / 2)` |
| 10 | 431「真幸運，撿到兩張卡片！」`@064` | 6211 | `0x04(−1, 1)` ×2（位置 0、1；交接在 2） |
| 11 | 432 `@066` 点券 250 | 6212 | `0x07(250)` |

`@061 .. 066`：**新闻本身不播**（BUGS #29，已核对 `News_Event*` 都没有 `CAudioMgr_PlaySample`）：`@061` 就是警车警笛（snd00061 与警车样本 0x47 = 71 同为 6.18 秒），它在新闻任务结束后的抓捕动画里播；幸运卡 / 宝箱的 0x41 / 0x42 由 NPC 在新闻前播（消息 0x53）。失败（对象不存在）返回 0，任务被丢弃，什么都不发生。

**角色的反应**（都是队列消息的副作用，不是新闻自己做的）：获得 / 〔string.bin#5602〕（0x17 / 0x16，带表情）按 `Role_AddMoney / SubMoney @0x489b80 / 0x489ac0` 70% 概率说话（≥ 5000 → 类 7、≥ 1000 → 8、其它 9；损失按 PI×5000 / PI×1000 → 类 0xB / 0xA / 0xC，说话前等 30 帧），present.cpp 观察这两个消息；点券（0x07 / 0x08）同理；`0x5B / 0x5C`（利息 / 拆迁补偿 / 新闻 3）不带表情；房屋被毁的地主（`0x4A`）说话类 4（沮丧）；被抓 / 住院 / 消失的角色自己的流程（`0x19 / 0x18` 的 `SendToPrison / SendToHospital` 末尾说话类 4）；被新闻点名的角色和当前角色之间靠 `0x2E` 接力，所以别人的新闻不会打断当前角色的回合顺序。新闻本身不动相机（只有 `0x1E` 对焦、巫师 / 怪兽 / UFO 对焦到地块或角色）。

## 8. 移植实现

- **文件**：`news.cpp / news.h`（任务、3D 场景、全部效果、触发、消息、测试钩子），`scripts/export_news.py` → `scenes/news.txt`（`nart` 文章行 72 条：901..958、922、960、1052、6201..6212；`fest` 6 行节日日期），`android/deploy.py` 已加入推送列表。CMake 两个源文件列表都加了 `app/news.cpp`。
- **接入点**（都是小改动）：
  - `game.cpp`：`rm8GameOnNewRound` 里月结算之后 `rm8NewsNewDay(dayEvent)`（`dayEvent` = 乐透 `rm8EventDaily` 起了任务 或 第 6 天后的每月 1 日——原版月结算起任务就跳过日期新闻）；`rm8GameMessage` 里（present 之后、cards 之前）`rm8NewsMessage`；`rm8GameClear` 里 `rm8NewsClear`；新增 `rm8GameTerraAdjustPct`。
  - `events.cpp`：`createTask` 的 `case 0x11: makeNewsTask(seat)`（电脑走 AI_OnLandEvent 默认分支，也起同一个任务）。
  - `npc_act.cpp`：`newsEvent` 改为调用 `rm8NewsStart(座位, id, 对象)`（原来是文字 + 语音放进信息框），新增 `rm8NpcClearPossessed`。
  - `roles.cpp`：`rm8RolesUpdate` 调 `rm8NewsUpdate`、`rm8RolesRenderLate` 调 `rm8NewsRender`、状态 6 不画、`rm8TurnSex`；`rm8_app.cpp`：载入 `news.txt`；`cards.cpp / stock.cpp / event_lottery.cpp` 各加一个小接口（状态天数 / 消失 / 休市天数 / 奖池加钱）。
- **任务两条路**：周六新闻 = events.cpp 的任务槽（`ev::Task`，结束后 events 把状态 9 → 8）；角色新闻和节日新闻 = news.cpp 自己持有的任务（`rm8NewsStart` 立刻把座位置 9，结束时置回 8）。3D 部分（`rm8NewsRender`）和标题（`news.ui`）两条路共用。
- **呈现**：真实的原版电视机界面——`news.ui` 的黄色电视机边框 + 横幅标题，3D 图从边框里的屏幕洞露出（文章 `x01..x58` 每张 2 个面片 + 动画），主播 N24D 在左下，音效 / 时长按 §2。主播画在图之后并清一次 Z。
- **[A] 与原版不同 / 没做**（2026-11-15 更新：UFO 已改为原版网格对象（角色位置、2.4 s 隐藏、4.0 s 消失、Z 清除后叠画、可点击跳过）；巫师闪烁已画（1.6 s）；怪兽改为任务（6004 + 样本 6，1.5 s 清楼，动画结束任务结束）；4 个 Im2D 格子经核对顶点色全 0 + VERTEXALPHA，原版不可见，不需要画。详见 `effects.md`）：
  - 4 个 240×280 的 Im2D 屏幕格子没画；巫师地块闪烁没画（等 2 秒）；UFO 用特效文章 `ufo.dff` 代替 UFO 对象（位置 = 特效播放器的角色位置 + (0,30,0)，不是 Mesh_SetTranslation(角色位置)），2.4 秒后角色状态 6 隐藏；怪兽用特效文章 0x1774。
  - 拍卖（29）：events 没有拍卖，`rm8NewsAuctionHook` 为空时新闻 29 总是被拒。events 侧做了拍卖后设置该钩子，并在 `rm8EventMessage` 里处理 0x40（news 在钩子为空时把 0x40 留给别人）。
  - 故事模式怪兽新闻的调用（§9）没接；故事模式例外分支没有读。
  - 日期：游戏日期来自系统日期（`game.cpp` 既有逻辑），所以固定节日在系统日期那天会立刻出现。
  - 屏幕宽度系数：原版 800 → 1.0，其它 0.88；移植一律 0.88。
- **[H]**：主播在角色新闻 / 节日新闻里没有明确的动画调用，用动作 0 循环；4 个 Im2D 格子的用途；`@061..066` 的播放。
- **测试钩子**（环境变量）：`RM8_NEWS_NOW="12,r3,f1,w,u,m1"`（一项一项：数字 = 立即执行该新闻（`News_EventSwitch`，周六新闻的路径，不含 TV 前奏）、`rN` = 角色新闻 N（0..11，对象 = 下一个座位）、`fN` = 节日 N（1..13，走 0x3C 消息）、`w` = 完整周六任务（前奏 + 随机选择）、`u` = 只放 UFO、`mN` = 怪兽打地块 N；前一条结束 1 秒后、当前角色处于状态 8 / 6 时触发）、`RM8_NEWS_FORCE=id,id`（下几次周六新闻强制第一次选这些 id，被拒则按原版重选）、`RM8_NEWS_EVERY=1`（每个新的一天都出周六新闻）、`RM8_NEWS_FEST=n`（第一个新的一天出节日 n）、`RM8_NEWS_ANIMAL=1`（总是动物模式）、`RM8_DEBUG_NEWS=1`（日志：标题、选择、被拒、每条新闻前后各角色的现金 / 存款 / 点券 / 卡数 / 状态；`=2` 另外每 0.5 秒打印等待状态）。
- **验证**：
  - `--shot` 截图（`RM8_NEWS_NOW=5 / w / r2 / u / m1`，`--time` 3..11 秒）：电视机边框 + 图 + 主播 + 标题，播报模式的彩条、动物模式的雪花、新闻 9 的牢房图、警察新闻的图（art 6203）、UFO 光柱、怪兽都对。
  - 日志 / 状态：`RM8_NEWS_NOW=f1,f2,f5,f8,f4` + `RM8_DEBUG_NEWS=1`：座位 0 现金 50000 → 55000（元旦）→ 105000（春节 +50000）→ 110000（妇女节 +10% 存款 5000）、点券 50 → 105（端午 +55），其它座位只在自己回合开始时才拿到（队列追加）；`RM8_NEWS_NOW=f1..f13` 13 个节日全部走完，愚人节后四个角色各自出了一个周六新闻；真实日历：11-21 / 12-05 / 12-19 / 01-02 周六新闻，11-26 感恩节、12-25、01-01。
  - 长模拟（全电脑，`--shot --time 1500..4000`，`RM8_NEWS_EVERY=1`）：01 号图 39 天 35 条、02 号图 116 天 102 条、05 号图 93 天 82 条、08 号图 116 天 103 条新闻，回合持续推进、无卡死、无 stuck guard；不带钩子的 2500 秒（71 天）模拟周六 / 节日如上。实窗口路径（`--frames 1500`，`RM8_NEWS_NOW=w,r1,f2,20`）与 Android（`build-news-android`）编译链接通过。

## 9. 给其它模块 / 故事模式的接口与观察

- `rm8NewsStart(座位, id, 对象)`：对象 < 0 = `News_EventSwitch(id)`（故事模式怪兽 `News_OnUpdate(0x1e, −1)`：`rm8NewsStart(0, 0x1E, −1)`；`Game_TriggerEventByDate` 的条件见 §1 第三条，故事模式 agent 接）；对象 ≥ 0 = 角色新闻（id 0..11）；节日 `0x2E..0x3A` 也可以。成功后座位状态 9，3.8 秒后回 8。
- `rm8NewsNewDay(otherEvent)`：每个新的一天调用一次（game.cpp 已接）。
- **观察 [V]**：`CPlayerTag__Render @0x485d00` 只在 `vf+0x20`（`CanMoveNormally @0x4932e0`：`+0x1C != 0 && +0x1C8 != 0 && 状态 ≤ 2`）成立时画角色模型，所以住院 / 坐牢（状态 4 / 5）的角色在原版里**不画**（医院 / 监狱点上画的是 NPC 模型），移植的 cards.cpp 目前只对状态 6 做了隐藏——状态 4 / 5 是否一并隐藏（`+0x1C` 的含义未读）留给 cards / NPC 模块判断。
- **观察 [V]**：新闻 1 / 11（股市大涨 / 大跌）调用 `Stock_SetField34_38(±10.0, 1)`，stock_port.md 的原版 bug（全市场分支每处理一只股票把天数减 1，天数变负数、事件永不结束）意味着触发一次后股市每天 ±10%——移植按原样保留。
- **罚款进奖池**：新闻 7 12 14 15 16 18 38 39 的罚款和新闻 17 的存款损失都加到乐透奖池（`DAT_006403d0 + 0x50`，`event_lottery.cpp` 的 `g_pool`，`ev::lotteryPoolAdd`）。

## 10. 未解决 / 待办

1. 4 个 Im2D 屏幕格子的用途（`Cls_5e20a8__vf2_Render @0x4b4770` 里 `SetRenderState(0x1D, 8) / (0xC, 1) / (1, 0)` + 四个 0x1C 字节的顶点，颜色 0）。
2. `@061 .. 066` 的播放点（NPC 模块还是新闻）；野狗的 063 / 430 没有对应的新闻。
3. `+0x1C`（`CanMoveNormally` 的第一个字段）的含义。
4. 拍卖（事件 0x50 / 0x51）做好后接 `rm8NewsAuctionHook`；故事模式怪兽新闻调用；月结算 0x52 做成任务时 `rm8NewsNewDay` 的 `otherEvent` 改由它返回。

截图（`port/results/`）：`news_broadcast.png`（播报模式彩条）、`news_animal.png`（动物模式雪花）、`news_money.png`（新闻 5）、`news_prison_amnesty.png`（新闻 9）、`news_police.png`（角色新闻：警察，art 6203）、`news_ufo.png`、`news_monster.png`。

**插图的裁剪**（BUGS #35）：插图是比电视玻璃大的 3D 平面，`rm8NewsRender` 用 GL scissor（`SDL_GL_GetProcAddress("glScissor")`，桌面和安卓通用）把它限制在边框图（pic 514，512×512）的玻璃区域 108..365 × 168..379 像素内，按 `news.ui` 的 `Label1` 屏幕矩形缩放；主播不裁剪。原版 `News_InitDialog @0x4b41b0` 另外准备了 4 个 Im2D 顶点（`+0x20`，240×280、无纹理、颜色 0、z = 最近深度，在 `vf2 @0x4b4770` 里最后画），推测是玻璃区的深度蒙板，用法未解（[H]）。
