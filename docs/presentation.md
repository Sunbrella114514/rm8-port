# 角色表现：表情、台词、相遇反应、特效播放

范围：`CPlayerTag`（角色，vtable 0x5e1f00）的表情 / 台词 / 互动消息、`speech.ui` / `gtinfo.ui` / `movieWord.ui` 三种说话界面、通用特效（eff 摆件）播放器。
置信度标记：[V] 已读代码核实，[M] 读了代码但名字 / 用途是推断，[H] 假设，[A] 移植适配。函数名以 `functions.tsv` 为准，其中不少原名是错的，本文按代码行为重新命名。
移植实现：`port/app/present.h` / `present.cpp`；数据由 `scripts/export_roles.py` 新增的行类型提供（见第 7 节）。

---

## 1. 一句话结论

1. **“表情”不是单独的系统**：一条消息 `0x5A(类别)`，对应 string.bin 里该角色的第 `类别` 条文字。这一条文字同时携带三样东西：台词文本、语音样本号（`@N`）、动作号（`$N`）。
   - 显示：头像对话框（带头像 `npc\p_NN_0X.tga` 的 gtinfo 框，3 秒）。
   - 声音：`@N` 播放 snd0000.dat 的样本 N。
   - 动作：`$N` 是**角色动画号**（role.bin 第 2 个子表的编号，Act(N+1)），循环播放；role.bin 第 3 个子表（面部贴图表）随动画时间换脸。
   - 所以“换脸”= 播放 3..14 号动画，脸部贴图换成 `A01_head_f_ActNN.tga`，roles.cpp 里已有的 `applyFace` 就是这一机制。
2. **谁触发**：加钱 / 扣钱 / 付租 / 免租 / 买地 / 加盖 / 拆屋 / 点券增减 / 破产 / 卡片台词（card.bin 的 speakNo）等。几乎都是 70% 概率入队 `0x5A`，类别按金额分档（第 3 节表）。
3. **相遇反应**：自由模式（非闯关）**没有**“同一点上两个角色说话”。该对白（`sp_NN.bin`）整段被 `&DAT_00632de8 +0`（闯关模式标志）关住。自由模式里角色之间唯一的互动是 `0x2E`“交接”：付租 / 夺地时，对方先说话再把控制权还回来。**路过**别人（不停下）没有任何反应代码。
4. **特效播放器**：`Anim_CreateSprite @0x47b090`。在目标点上方 30、朝镜头拉近 20 的位置放一个 eff 摆件，播放动画 0 一次，播完（回调）或 4 s / 10 s 超时后删除，同时播一个样本。粒子类摆件（6005 等）是粒子发射器。

---

## 2. 表情与台词的数据

### 2.1 string.bin 文本格式 [V] `StringTable_LoadFromIni @0x4015e0`
- 字符串表 8000 项；每项有三个数组：文本指针 `[id*4]`，`+32000` = `@` 后的整数，`+64000` = `$` 后的整数，初值都是 −1。
- 文本里 `\` → 换行，`^` → 空格；第一个 ASCII `@` 把文本截断，`@` 后 `%d` 读入样本号；之后的 `$` 后 `%d` 读入动作号。**没有 `@` 就不会解析 `$`**。
- 例：`嘿嘿，連老天都幫我！@601 $5` = 文本、语音样本 601、动作 5。

### 2.2 每个角色一个 200 条的块 [V] roleinfo `strId`（+0x38c）
- 16 个角色：`strId = 600 + 200 × (Id − 1)`（600、800、…、3600，已核对全部 16 项）。
- 台词类别 n = 字符串 `strId + n`。所有角色类别含义相同。样本号恰好等于字符串号（601 → snd00601.wav 存在，601..662、664、666..720 都有），语音按角色分开，**与 sex 字段无关**（没有找到任何用 sex 选语音的代码 [M]）。
- 已知类别（来自触发代码，第 3 节）：1 / 2 买地 / 加盖心情，4 / 5 吃亏，7 / 8 / 9 收钱，0xA / 0xB / 0xC 付钱，0xD / 0xE / 0xF 免租，0x10 满级，0x11 / 0x12 地盘，0x1A 破产。其余（0x13..0x3E）是卡片使用台词：card.bin 的 `speakNo` 就是这个类别号（泡泡卡 speakNo 85 → 字符串 685「銅牆鐵壁」，见 cards.md）。
- 角色选择界面也用这块：`Cls_5e8140_PlayRoleSample @0x506b20` 播放 `+32000` 数组的第 `strId + 0x1D`（类别 29，「出馬」台词）的样本 [V]。移植里 `rm8SpeechParse(strId + 29).voice` 可直接取。

### 2.3 动画与面部贴图 [V]
- `CPlayerTag vf5 = CPlayerTag__vf5_SetTextureState @0x48a270`（名字不准，实为 SetAnimPose）：逻辑动作 0..14；冬眠状态只接受 2 / 10；`+0x5C = (动作 ≥ 3 且 ≠ 10)`；步行时不重映射（机车 / 汽车 / 附身有重映射表 0x5e1dd0 / 0x5e1d94，移植只有步行模型，未实现）。
- 然后 `Mesh_SetTexture @0x47a570` → `Article_SelectByTypeAndSetProperties @0x445860(动作号, 次数, 层)`：次数 0 = 循环，1 = 播放一次，≥ 0x80000000 = 用 role.bin 记录里的默认（+0x120，全是 1）。
- 动画的面部换贴图：role.bin 第 3 个子表（`face` 行），`Article_Update @0x448540` 按动画时间换 `face`（或 `head`）部件的贴图，roles.cpp 的 `applyFace` 已实现。
- 动作号：0 走、1 掷骰、2 站、3 待机（Act04，2.665 s）、4..13 = Act05..Act14（0.4~1.7 s 的各种表情动作）。string 的 `$N` 取值 3..12。

---

## 3. 谁触发表情 / 台词

全部是入队 `0x5A(类别, 0)`：`thunk_FUN_00484890 @0x484c30` = `Role_EnqueueMsg(0x5A, 类别, 0)`（追加到队尾），`thunk_FUN_00484930 @0x484c50` = `Role_EnqueueMsgAt(位置, 0x5A, 类别, 0)`（插入）。随机是 `FUN_005c5802() % 100 < 0x46`（70%），在 port 里记作 `rm8PresentChance70()`。

| 触发 | 地址 | 条件与类别 | 把握 |
|---|---|---|---|
| 收钱 `Role_AddMoney(额, 账户, 表情)` | 0x489b80 | 表情标志且 70%：≥5000 → 7，≥1000 → 8，否则 9；插入队首（位置 0） | V |
| 付钱 `Role_SubMoney(额, 账户, 表情)` | 0x489ac0 | 表情标志且 70%：≥ 5000×PI → 0xB，≥ 1000×PI → 0xA，否则 0xC；先在 0 号位插 `0x65(30)`，表情在 1 号位 | V |
| 消息 0x16 / 0x17 | 0x481df0 | 分派时带表情标志 1（0x5B / 0x5C 不带） | V |
| 过路费 `Role_TransferRentToOwner` | 0x487f20 | 付款人 60% 表情（`SubMoney(.., rand%100<60)`），地主取反的 40%（`AddMoney(.., !pe)`）；地主有表情时双方入队 `0x2E` 交接（第 5 节）；样本 0x7B（boardfx 负责） | V |
| 免租 `Role_CheckRentExempt` | 0x486580 | 地主状态 1..6，或状态 0 且附身糊涂神（6）：提示文字后 70%：≥5000×PI → 0xF，≥1000×PI → 0xE，否则 0xD，追加到队尾。查封（街道 +0x108）与破产（7）没有表情 | V |
| 财神 / 路霸等免费 `Role_PayToll` | 0x487c80 | 金额 ≤ 0 且 70%，类别同免租（移植无神明，未接） | V |
| 买地 `Role_BuyLandConfirmed` | 0x487030 | 衰神：70% 类别 `4 + rand%2`（位置 1）；其余：免费（福神）70% → 1；付费后本街持有 ≥3 块 → 0x11，否则土地原有建筑（等级≠0）70% → 1；福神时位置 1，其余 0 | V |
| 加盖 `Role_BuildOnLand` | 0x487380 | 衰神：已扣的钱不退，等 30 帧（位置 1），70% 类别 `4 + rand%2`（位置 2），不盖；自己的地：70% → `1 + rand%2`；新等级 5 → 0x10 且先插 `0x52(0x177A, 地块)`（烟花，位置 0），表情在 1 号位；否则本街 ≥3 块 → 0x12 | V |
| 拆屋 `Role_StartLandLevelDown` | 0x4875e0 | 70% 且是自己的地：类别 5 | V |
| 夺地 `Role_TakeLandOwnership(t, b≠0)` | 0x487250 | 70% 且有原地主：原地主类别 4，双方 `0x2E` 交接；音效 1 | V |
| 收卡 `Role_ReceiveCardWithUi` | 0x488e80 | b≠0 且队首不是 0x04 且 70%：类别 `1 + rand%2`（有卡面界面时位置 2），否则 `0x65(30)` | V |
| 点券 `Role_AddTicket` / `SubTicket` / `SubTicketEmoteAny` | 0x489ed0 / 0x489f90 / 0x48a030 | 加：> 10 且 b==0 且 70%：`0x65(30)` + 类别 2（原代码把等待帧数 30 复用成了判断量：`(30<50)+1 = 2`）；减：> 9 且 70%：`(额<50)+4` = 5 或 4；EmoteAny 去掉金额下限 | V |
| 破产 `Role_CheckBankruptcy` | 0x48ee10 | 清队列 → 文字 str91 → `0x5A(0x1A)` → `0x7F`（剧情） | V |
| 卡片 | cards.md | `speakNo[0]` 在使用时 `0x5A`，`speakNo[1..2]` 传给 `CardPy Execute(param, sa, sb)` | V（cards 模块） |
| Python `Role.Speak(role, 类别)` | 0x4fe3d0 | 入队 `0x5A(类别)` | V |

`Game_TurnNextRole @0x4971a0` 在换人时调用 `Role_RemoveQueuedMsgCmd5A @0x484ad0`，删掉新当前角色队列里残留的 `0x5A` [V]（它的 ECX 是新角色，反编译没有显示）。

---

## 4. 消息 `0x5A` 的执行：台词框 [V] `CPlayerTag__OnNpcInteract @0x490f40`（误名）

```
0x5A(a = 类别, b = 遍数)：状态 == 6 或 1（冬眠）时什么都不做
 b == 0：镜头对准角色（Camera_SetFocusPoint），以 b = 1 把自己插回队首 → 下一帧再执行
 b != 0：
   字符串 = strId + a；  图片 = "npc\<picName>_%02d"，序号 = min($N − 3, 9)；  语音 = @N
   Role_ShowInfoDialog @0x488ae0 → gtinfo 框（头像在控件 2，文字在控件 1）；有语音则 Anim_PlaySound
   插入 0x65(180 帧) 与 0x1C（隐藏），都插队首：0x65 在 0 号，0x1C 在 1 号
   状态 == 0 且 $N > 0：vf5($N, 0, 0)，动作循环
```

- **队列与时序**：每帧只处理一条（状态 8），所以台词显示期间别的消息等着；0x65 的帧数除以 60 再乘等待系数 `1 − min((speed−1)/10, 1)`，默认速度 4 约 2.1 秒。语音是整句样本（约 3 秒），不随对话框消失而停止，只在该角色下一次 `Anim_PlaySound` 时被换掉 [V `0x47aff0`：先 StopSample 再 Play，句柄存 +4]。
- **没有图片的文本**用 ginfo 框（`0x1B` 的普通提示），有图片用 gtinfo 框 [V `Role_ShowInfoDialog` 按条目 +0x100 / +0x200 选择]。条目 `+0x100`（图片名）优先：`vf+0xc(名字, 2)` 按文件名取图（故事第 1 天孫小美的提示用 `npc\p_18_01`，移植 `rm8GamePushTextPicFile`）；否则 `+0x200 >= 0` = uvlist 图号（`vf+0x10`，拼图小图，`rm8GamePushTextPic`）。
- **序号取值（已更正）**：`0x490f40` 里文本和语音取 `strId + 类别`，但 `$N` 是从下标 `(strId + 类别 − 600) % 200 + 600` 读的，**永远落在第一个角色的块（601..720）里**，所以所有角色在同一类别下共用同一个动作 / 头像序号。头像 = `npc\<picName>_%02d`，序号 `$N − 3`（封顶 9）；该类别在 601..720 里没有 `$` 时读到 −1，序号 −4，图片不存在（空白）。此前“只有大老千有 `$`”的结论漏看了这个取模下标，已纠正。
- **动作不会自动结束**：`vf5(N, 0, 0)` 的次数是 0 = 循环；`+0x5C = 1` 使 `Role_TickIdleAnim @0x48a3f0` 不再触发待机动作。要等下一次位姿改变（开始走路、停下 `vf5(2,0,0)`、掷骰）才回到站姿 [V]。这是读代码的结论，没有在实机观察，若外观不对（角色整轮一直循环动作）再回头核对 [M]。
- 界面坐标 [V gtinfo.ui]：根面板 (330, 440) 384×138，头像控件 (3, 1) 128×128，文字 (141, 24) 222×95，字号 16，居中；背景 Plane1 贴图 904（`popo_1.tga` 右半）。屏幕固定在下方正中，**不跟着角色走**。

### 4.1 头顶气泡（剧情专用）[V] `Movie.RoleSpeak @0x512b70`
- 剧情角色的子对象（vtable 0x5e9060）`vf0 = CUi_movieWord_vf0 @0x514030`：首次加载 `movieWord.ui`，把 `string.bin[id]` 的文本设给标签 `word`（白字）和 `word2`（阴影）并显示；`vf1 @0x514190` 隐藏。
- 每帧 `Cls_5e8cf8_vf3 @0x511c10`：角色头上方一点用 `Point_TransformWorldToScreen` 投影到屏幕，`SetPosX(x − 控件宽/2)`，`SetPosY(y)`，即气泡**跟着头顶走**。头顶高度取自网格（`mesh+0x2C × 2 × mesh+0x10 + y`），移植无法解出这两个字段 [H]：用固定 105 单位。
- movieWord.ui：256×128 面板，贴图 1087（`j235.tga`），文字 (15, 10) 227×108，字号 16，垂直居中。
- `Movie.SpeakAside`（旁白）用 SpeakAside.ui 的 `asideWord` / `asideWord2`（`@0x515200`），不属于角色。`Movie.RoleSpeak` 取 `string.bin[id]` 的文本，**不播放 `@N` 语音，也没有口型**（旁白与口型同步的描述是旧文档的推测 [M]）。
- 移植：`rm8RoleSpeak(.., RM8SPEAK_BUBBLE)` 用同一贴图，位置每帧跟随（`rm8PresentRenderLate`）。

---

## 5. 角色之间的反应

### 5.1 同一点上相遇（停下时）[V] `CPlayerTag__OnStopAtTerra @0x485c00`
顺序：站立动作 → 停走路音 → 状态 8 → 炸弹倒计时 → `Role_RunPoliceThiefDogAction`（警察 / 小偷 / 野狗附身，NPC 模块）→ 否则 `CPlayerTag__GetRandomBankruptRole(0, 0) @0x48ebb0`（误名：在状态 ≤ 0、计时 `+0x1E4 < 1`、座位≠自己、**当前节点相同**的角色中随机一个，随机数 `rand % 个数`）→ `Role_StartNpcDialogForRole @0x48c950` → 入队 `0x23`、`0x1D`。
- `u_Role_QueueTagEntryMsgs @0x48ca30` 的第一个判断是 `*(&DAT_00632de8) != 0`，**闯关模式标志为 0 时直接返回**。该标志在 `Game_LoadMapData @0x4a31f0` 的曲目选择里也是同一个（audio.md），开局设定 +0x00。所以自由模式 / 对战里，相遇不会触发任何对白。
- 闯关模式：`Config_LoadNpcSpeech @0x494e70` 把 `config\speech\sp_%02d.txt`（每个角色一个，每个 15 节，节名 = 对方，`pId` = 对方 Id，`num` 行，`rlId<k>` = 说话者 Id，`text<k>`）读进该角色的 roleinfo（+0x390）。`CPlayerTag_FindAndMarkEntry @0x493770` 取**第一个** `pId` 等于对方 Id 的节，`+4` 标记未用过才返回并置 1，所以每个“我 → 对方”的对白每局只发生一次（标记没有重置路径，`Config` 重新载入才清）。
- 入队：`0x54(我的Id, 对方Id)` → 每行一条 `0x55`（说话者 = 我 → 左）或 `0x56`（右），文字为 `text<k>`、**无语音**（语音号 −1）→ `0x1C`。
  - `0x54 = Ui_SetSpeechPortraits @0x48cb70`：`speech.ui` 的头像 1 / 2 设为 `npc\p_%02d_01.tga`（角色 Id），清空两个文字。
  - `0x55 = Ui_ShowSpeechLeft @0x48ccb0`，`0x56 = Ui_ShowSpeechRight @0x48cdb0`：左文字（控件 3）或右文字（控件 4）设文字、另一侧清空，然后 `0x65(180)`。
  - speech.ui [V]：根面板 (212, 399) 601×235，头像 (5, 67) / (465, 67) 128×128，文字 (139, 30) 190×84 与 (257, 136) 196×91，背景贴图 782（512×256）+ 783（128×256）。
- 另有 `drama.bin`（剧情对白，`Role_QueueNpcDialog @0x48c700`：rlId 0x13 = 旁白文字，其余同 sp）仅用于闯关剧情（破产、骰子事件），未做。
- 移植：`rm8PresentOnStop(seat)` 实现，**默认关闭**，`rm8PresentSetStory(true)` 或环境变量 `RM8_STORY=1` 打开（测试用）。

### 5.2 `0x2E` 交接（自由模式也有）[V] `u_Role_StartInteractWithRole @0x48d040` + `Role_TryEnterState8 @0x48d0e0`
- `0x2E(a = 对方座位 + 1, b)`：对方状态为 0 时改为 8（并在 b == 0 且对方就是当前回合角色时，若它的队列里有 `0x25` / 100 则镜头对准它），成功则**自己**状态改 0（暂停）。
- 用法是一对消息：A 的队首放 `0x2E(B+1)`，B 的队尾放 `0x2E(A+1)`。A 暂停，B 把自己队列里的 `0x5A` 表情 / 台词说完，再用 `0x2E(A+1)` 叫醒 A。
- 触发：付租（地主有表情时）、夺地（`TakeLandOwnership(b≠0)`，70%）、送神卡 `OnSendGodCard @0x490df0` 等。队列为空的 8 状态角色会入队 `0x65` + `0x66` 直接换人，所以交接必须成对入队。
- 这些消息入队到**别的角色**的队列（反编译里没有显示对象，由参数含义判断）[M]。

### 5.3 路过 [V]
`vf18 = u_CPlayerTag_UpdateTurnCountdown @0x485a20`（经过一个道路点）只检查炸弹、NPC（`NpcMgr_CheckPassNode @0x49e090`）、警察 / 小偷 / 野狗、银行。**没有任何角色之间的反应**，也没有语音。

### 5.4 同点错开
已在 turn_flow.md 第 4 节实现（`Game_MoveRoleToPosition` / `Game_UpdateRoleDirection`），与表情无关。

---

## 6. 特效播放器

### 6.1 消息 [V]
| 消息 | 函数 | 行为 |
|---|---|---|
| `0x2C(艺术号, 样本)` | `Role_ShowEffectAnimAtSelf @0x48b2b0` | 镜头对准角色；`Anim_CreateSprite(art, 角色位置, 动作 0, 次数 1, 回调, 0, 样本)`；队首插 `0x65(60)`。Python `Role.PlayPrt(role, 6122, 30)` 即此 |
| `0x52(艺术号, 地块)` | `Role_ShowEffectAnimAtTerra @0x48b320` | 镜头对准 `地块+0x88`；样本按艺术号：0x177A → 0x68，0x178A → 0x65，0x178B → 0x66，0x178C → 0x67，其余 −1；同样 `Anim_CreateSprite`；`0x65(60)` |
| `0x53(样本, 帧)` | `Role_MsgHandler_PlayAnimOrWait @0x48e8c0` | 帧 < 0：只播样本；否则 `Anim_PlaySound`（换掉上一个）并 `Role_BeginWaitSeconds(帧/60)` |

### 6.2 `Anim_CreateSprite @0x47b090(艺术号, 位置*, 动作, 次数, 回调, 参数 6, 样本)`
1. 先 `Anim_ReleaseSprite @0x47b240`（释放这个角色上一个特效并停它的声音）。
2. 位置 = 给定位置 + (0, **30**, 0)，再沿镜头水平前向 `Camera_GetForwardXZ` 反方向（朝镜头）拉近 **20**。
3. `CArticleMgr vf+0x40(位置, 艺术号, −1, 1, −1)` 创建摆件，句柄存对象 +8；`vf+0x88` 旋转 yaw = 镜头 yaw − 180（朝向镜头，同角色）。
4. 动作 ≥ 0：`vf+0x5C(句柄, 动作, 1, 回调)` = `Article_SelectByTypeAndSetProperties(动作 0, 播一次)`，**回调 `Role_FindWithCondition @0x480650` 在动画播完时释放特效**（遍历角色调用 `thunk_FUN_0047b240` = 释放）；寿命上限 10 s，没有动画 / 回调时 4 s，都乘等待系数 `1 − min((speed−1)/10,1)`（`Anim_ApplyDifficultyScale @0x47b3b0`，`Anim_ReduceDuration @0x47b340` 递减，到 0 释放）。
5. 样本 `CAudioMgr_PlaySample(样本, ..., 1)` 播一次，句柄存对象 +0xC，释放时停掉。

### 6.3 艺术号与模型
- role.bin 6000..6399：`eff\`（6000 Gwin、6001 Eff_10、6002 Eff_01 …）、`destiny\`、`mend\`、`gods\`。
- 粒子类摆件（role.bin 主体 +0x10 == 1，粒子表号在 +0x64C）：6005 / 6013 / 6101.. / 6107 / 6116..6124，没有模型，创建时变成粒子发射器（particles.md）；6023 / 6024 / 6028 的第 1 个子表是挂在 `[@1]` 骨骼上的子粒子（效果 25 / 25 / 11）。
- 建造 / 拆除特效（rules_land.md 6.2）：6001 / 6002（住宅时缩放 0.5），样本 2 / 3 / 0x7A，由事件任务负责；本模块提供 `rm8FxPlay(.., once)` + `rm8FxFinished`。

---

## 7. 移植实现要点

### 7.1 数据（`scripts/export_roles.py` 新增行，旧行不变）
```
rstr <角色Id> <strId>                       roleinfo strId
sp <角色Id> <pId> <行数>                    sp_%02d 的一节（15 × 16 = 240 节）
spl <rlId> <文字>                           随后 <行数> 行
fxart <艺术号> <dff|-> <texdir|-> <粒子效果号|-1> <uva|-> <序号>:<anm> ...    6000..6399 共 71 个
fxhook <艺术号> <骨骼挂点> <粒子效果号>
```
`roles.cpp` 的 `rm8RolesLoadDefs` 末尾调用 `rm8PresentLoadDefs`；语音与动作取自 `ui\strings.txt`（string.bin），不进 roles.txt。

### 7.2 接口（`present.h`）
- `rm8RoleExpression(seat, id, seconds)`：播放动画 id（循环，换脸）；seconds > 0 [A] 到时回站姿。
- `rm8RoleSpeak / rm8RoleSpeakStr / rm8RoleStopSpeak`：立即显示（INFO 框或头顶气泡）并可播语音。
- `rm8RoleSay / rm8RoleSayAt`：入队 `0x5A`（`Role.Speak`、卡片、规则用）。
- `rm8PresentMessage`（game.cpp 最先调用）：`0x5A 0x2C 0x2E 0x52 0x53 0x54 0x55 0x56`，并旁路观察 `0x16 0x17 7 8 0x4B` 的表情。
- `rm8PresentMoney / Rent / RentExempt / Buy / Build / LandTaken / OnStop`：规则里的触发点。
- `rm8FxPlay / Finished / Active / Stop / Move / StopAll / PlayAtRole / PlayAtTerra`：特效播放器。

### 7.3 与原版不同的地方 [A]
- 单一 INFO 框，与原版相同（一次只能显示一个）；气泡每个座位一个。
- 台词框文字按控件宽度折行（UI 库不自动折行）。
- 角色的车辆 / 附身动作重映射、冬眠限制没有（移植只有步行模型）。
- 对白 `drama.bin`、剧情 `0x7F`、神明（衰神 / 福神）分支未做。

### 7.4 未确认
- 表情动画是否真的循环到下一次位姿改变（第 4 节）。
- 头顶气泡的高度公式。
- `0x2E` 入队对象（第 5.2 节）。

---

## 8. 神 / 附身相关的反应（补充）

读代码的结论：**神附身本身不入队任何角色台词**（`Role_SetPossession @0x48b6e0`、`Cls_5e33a8__Animate @0x49bce0`、`BeginCollection / BeginAttach / BeginBless` 里都没有 `0x5A`），附身者的“反应”全部来自下面几处，这些在移植里原先缺失或不完整 [V]：

| 场合 | 地址 | 反应 |
|---|---|---|
| 神的问候（屏幕中央白字 + 语音 `@5301..`） | `0x49bce0` 阶段 2 | NPC 模块已有 |
| 卡片附身（財神 / 窮神 / 衰神 / 福神 / 土地 / 糊塗 / 警察 / 小偷 / 野狗 / 路霸卡） | CardPy_34, 36..44 的 `Reaction(param, sa/sb)` → `Role.Speak` | 出牌者 `speakNo[0]`，目标 `sa`（目标就是出牌者）或 `sb`，随后 `LostFocus` 互相 `0x2E`：已有，实测通过（例 财神卡：类别 89 / 91） |
| 衰神附身：丢一半卡 | `Role_QueueRemoveRandomCards @0x489470` → 每张 `Role_ShowAndRemoveCardInSlot @0x489220` | 每丢一张弹卡片窗（`getcard.ui`，60 帧）；队列里没有后续 `0x05` 时 70% 类别 `4 + rand%2`（队尾）。**本次补上**（原先在 cards.cpp 里无声删除） |
| 福神附身：得两张卡 | `Role_ReceiveCardWithUi @0x488e80`（`b = 1`） | 队首紧跟另一个 `0x04` 时不表情，否则 70% 类别 `1 + rand%2` 代替 `0x65(30)`。**本次补上** |
| 窮神确认：付钱给其它所有人 | `Cls_5e34e8__OnConfirm @0x49c120` | `Role_SubMoney(.., 0, 1)` 带表情（財神的 `AddMoney` 带表情已有）。**本次补上** |
| 财神免过路费 / 衰神买地加盖 / 福神土地公占地 | `@0x487c80 / 0x487030 / 0x487380 / 0x487250` | NPC 模块已有 |
| 离开 | `Role_TickPossessionExpire @0x483350` | str433 + `0x2C(0x17EA, 0x1E)` + `0x5A(0x18)`（NPC 的 good == 0）：已有 |
| 野狗咬人 → 住院 / 警察 → 坐牢 | `@0x48a570 / 0x48a9c0` | 送入时队首 `0x5A(4)`；住院 / 坐牢 / 睡眠的每日提示后 40%（`rand%100 < 0x28`，旧注释的 28% 是看错十六进制）类别 0x15 / 0x14 / 0x16（`@0x48a820 / 0x48ac70 / 0x48aff0`）。**本次补上** |

附身者说话时头像仍是**角色自己**的 `npc\<pic>_NN`，表情动作按 `0x5E1D94` 重映射（动作 3..13 → 站立 2），所以被附身的角色看不到换脸动作，只有文字、头像和语音 [V `CPlayerTag__vf5 @0x48a270`]。

---

## 9. 开局自我介绍（每个角色依次登场）

自由模式和闯关模式一样：棋盘开始后，**第一轮（日期 0）每个角色只做自我介绍，不掷骰**；全部介绍完才绕回 0 号座位，开始真正的第一天（`Game_OnTurnStart(1)` 在第一次绕回时只把天数置 1，rules_land.md 5.1 的“开局持续两轮”就是这一轮）[V]。

流程（每个角色一个回合，座位顺序）：
1. 开局 `Game_SelectRandomTerritory @0x496180` 给每个角色调用 vf24 = `CPlayerTag__vf24_PlayEffectSetStatus8 @0x483110`：放到起点并把**身份状态设为 8**。0 号座位镜头对准它；`Game_StartNewRound @0x4962a0` 给 0 号座位入队 `0x68`。
2. `0x68` = `Role_LeaveStatus8 @0x483540`，身份状态 == 8 的分支（否则入队 `0x67`，即普通回合）：状态 8 → 0，`Game_MoveRoleToPosition`、镜头瞬间对准该角色，`Role_ClearFlag1C8`（角色纸片人**不画**，见 `CPlayerTag__Render @0x485d00` 的 `vf8 = CanMoveNormally @0x4932e0` 要求 +0x1C8 ≠ 0），入队 `0x57`、`0x7C`、`0x5A(0x1C)`。**没有** `0x67` 的回合开始步骤，也不掷骰。
3. `0x57` = vf36 = `Role_SpawnCharacterModel @0x48b410`：在 `角色位置 + roleinfo 偏移(+0x344)` 创建摆件 **art `0x180 + Id`**（384 + Id，+0x1AC == 4 的另一套服装用 400 + Id），role.bin 里是 `RichRole\A06\A06dc.dff`（“dc” = 登场：骑独轮车转盘子进场的整段表演动画，动作 0，播一次），yaw = 镜头 yaw − 180，缩放 = roleinfo 缩放（1.3 1.6 1），`Asset_LoadCharacterTexture(.., 角色, 服装, 前向)` 换身体贴图；`Anim_ApplyDifficultyScale(10)`；回调 `Role_FindWithCondition @0x480650` 在动画播完时释放它；队首插 `0x65(60)`。
4. 等待状态 5 的结束条件（`Role_TickWaitTimer @0x4850b0`）：计时到 **并且** 角色的特效句柄（+0x50 / +0x54）都空了，所以要等整段登场动画播完才继续。
5. `0x7C` = `Role_SetFlag1C8`：纸片人重新出现。
6. `0x5A(0x1C)`（类别 28）：第 4 节的台词框：`strId + 28` 的文本和语音（孙小美“為我加油吧！@1628”、大老千“〔string.bin#628〕，啊哈哈！@628”…），头像序号和动作号用第一个角色块里同类别的 `$5`（头像 `_02`，动作 5，只对未附身者播）。显示 180 帧（×等待系数），之后队列空 → 等 60 帧 → `0x66` 换下一个角色（镜头在每个角色的 `0x68` 和 `0x5A` 第一遍都重新对焦）。

移植：`roles.cpp` 每个角色出生带 `intro` 标志（= 身份状态 8），`0x68` 分支 / `0x57` 由 `present.cpp` 的 `rm8PresentIntroBegin / introModel` 执行；`export_roles.py` 的 `fxart` 行加了 art 385..416；等待状态 5 增加 `rm8PresentSeatFxBusy`（对 `0x2C` 的角色特效同样生效，原版同一个条件）。`0x7B / 0x7C` 在 present 里同时作为“纸片人隐藏 / 显示”的标志。测试开关 `RM8_SKIP_INTRO=1` 跳过介绍轮（原先依赖开局立即掷骰的测试脚本可以加它）。

## 10. 更正：表情动作的结束、跳过时的登场模型

- **表情不会一直循环**（更正第 4 节“动作不会自动结束”）：`vf5(n, 0, 0)` 确实是循环播放、没有结束回调，但 `+0x5C` 标志置位，而 **`Role_TickWaitTimer @0x4850b0` 等待结束时、`Role_StopMoveAndIdle @0x484790` 跳过时，都会调用 `*(this+0x4C)` 的 vtable[0] = `Cls_5e1ef8__vf0_FindAndProcess @0x48a330`：标志置位就 `vf5(2, 0, 0)`（站立）并清标志** [V]。所以表情持续的时间 = 紧跟着它的那个 `0x65(180)` 等待（文字框显示期间）；等待结束、点击跳过都回站姿。待机动作 3 另有自己的结束回调（`Role_IterateAndCall @0x4806f0` 同一个函数）。
- 等待结束还要求 `+0x50 == 0`（该角色的语音样本播完）和 `+0x54 < 0`（特效 / 登场模型已释放）；移植的 `rm8PresentSeatFxBusy` 现在包含语音。
- 跳过（点击）：`Role_StopMoveAndIdle` 的 `Anim_ReleaseSprite @0x47b240` 释放角色的摆件句柄，**登场模型也是同一个句柄**，所以跳过自我介绍时模型随之消失、纸片人由随后的 `0x7C` 显示（只有一份）。移植：`rm8PresentSkipSeat` 现在释放该座位的全部 Fx（含登场模型）。验证：`RM8_SKIP_TEST="2.5"` 和 `auto:0.35`（连续快速跳过所有介绍）截图都只有一个角色；日志 `present: seat N expression 5 ends -> stand`（`RM8_DEBUG_PRESENT=1`）。
