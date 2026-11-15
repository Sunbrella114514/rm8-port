# 大富翁8 Python 引擎 API 参考

## 概述

大富翁8游戏引擎向Python 2.4脚本暴露了130个函数，通过以下8个模块进行组织：

| 模块 | 函数数 | 调用情况 | 主要功能 |
|------|--------|---------|---------|
| **Role** | 56 | 54个被调用 | 玩家角色状态与操作 |
| **Movie** | 31 | 31个被调用 | 过场动画与演出效果 |
| **Terra** | 17 | 16个被调用 | 地块/物业管理 |
| **StPoint** | 8 | 7个被调用 | 特殊地点管理 |
| **Stock** | 6 | 6个被调用 | 股票交易系统 |
| **Npc** | 7 | 5个被调用 | NPC管理 |
| **Street** | 4 | 4个被调用 | 街道操作 |
| **CTime** | 1 | 1个被调用 | 时间控制 |
| **总计** | **130** | **124个被调用** | - |

---

## 1. CTime 模块（时间控制）

CTime模块只有一个函数，用于实现剧情脚本的时间线暂停机制。

### CTime.Wait(seconds)

**地址**: unknown（游戏引擎内置）  
**参数**: `seconds` (float) - 等待时长（秒）  
**返回值**: 无  
**调用次数**: 660次

**用途**: 暂停脚本执行指定时间。配合 `yield None` 使用，是剧情脚本实现时间线的核心机制。

**示例调用**:
```python
# 来自 da01_1.py:22
CTime.Wait(1.0)
yield None

# 来自 da01_1.py:57
Movie.SpeakAside(6865)
CTime.Wait(11.0)
yield None
Movie.StopSpeakAside(6865)
```

**说明**: 剧情脚本（da*.py）使用 `from __future__ import generators` 启用生成器支持。每个 `CTime.Wait()` 后必须跟 `yield None`，使脚本在此处中断，在指定时间后由引擎自动恢复执行。这形成了基于帧的时间线系统。

---

## 2. Movie 模块（过场动画与演出）

Movie模块提供31个函数，分类管理角色动画、物件特效、摄像机、音效和字幕。所有函数都被脚本调用。

### 2.1 角色(Role)相关函数

#### Movie.RoleCreate(roleId, x, y, z)

**地址**: 005125d0  
**参数**:
- `roleId` (int) - 角色ID
- `x`, `y`, `z` (float) - 3D位置坐标

**返回值**: 角色句柄 (int)  
**调用次数**: 56次

**用途**: 在演出场景中创建一个角色实例。返回的句柄用于后续操作该角色。

**示例调用**:
```python
# 来自 da03_1.py:42-50
r1 = Movie.RoleCreate(1, 5, 3, -60, 0)
Movie.RoleFace(r1, 3)
Movie.RoleSetScale(r1, 0.9, 0.9, 0.9)
# ...
r2 = Movie.RoleCreate(11, -20, 3, -70, 0)
Movie.RoleFace(r2, 3)
Movie.RoleSetScale(r2, 0.9, 0.9, 0.9)
```

#### Movie.RoleDel(roleHandle)

**地址**: 00512820  
**参数**: `roleHandle` (int) - 由 RoleCreate 返回的角色句柄  
**返回值**: 无  
**调用次数**: 23次

**用途**: 删除演出场景中的角色，释放资源。

#### Movie.RoleMove(roleHandle, x, y, z)

**地址**: 00512990  
**参数**:
- `roleHandle` (int) - 角色句柄
- `x`, `y`, `z` (float) - 目标位置

**返回值**: 无  
**调用次数**: 23次

**用途**: 移动角色到指定位置（带动画过渡）。

#### Movie.RoleRotate(roleHandle, direction)

**地址**: 00512a20  
**参数**:
- `roleHandle` (int) - 角色句柄
- `direction` (int) - 方向（0=右, 1=下, 2=左, 3=上，推测）

**返回值**: 无  
**调用次数**: 23次

**用途**: 旋转角色面向方向。

**示例调用**:
```python
# 来自 da04_1.py:79
Movie.RoleRotate(rtl, 1)
# 来自 da04_3.py:20
Movie.RoleRotate(rtl, 2)
```

#### Movie.RoleFace(roleHandle, direction)

**地址**: 00512a90  
**参数**:
- `roleHandle` (int) - 角色句柄
- `direction` (int) - 面向方向（0-3，具体含义同RoleRotate）

**返回值**: 无  
**调用次数**: 36次

**用途**: 改变角色面向方向（不移动）。

#### Movie.RoleSetAnim(roleHandle, animIndex, animFrame)

**地址**: 00512b00  
**参数**:
- `roleHandle` (int) - 角色句柄
- `animIndex` (int) - 动画索引
- `animFrame` (int) - 动画帧编号或模式

**返回值**: 无  
**调用次数**: 130次

**用途**: 设置角色播放指定动画。这是演出中最常用的函数之一，用于控制角色的各种动作。

**示例调用**:
```python
# 来自 da03_1.py:56
Movie.RoleSetAnim(r5, 5, 0)
# 来自 da03_1.py:92
Movie.RoleSetAnim(qian, 8, 0)
```

#### Movie.RoleSpeak(roleHandle, speechId)

**地址**: 00512b70  
**参数**:
- `roleHandle` (int) - 角色句柄
- `speechId` (int) - 台词ID（对应语音资源）

**返回值**: 无  
**调用次数**: 33次

**用途**: 让角色播放带有口型同步的台词动画。

#### Movie.RoleStopSpeak(roleHandle)

**地址**: 00512be0  
**参数**: `roleHandle` (int) - 角色句柄  
**返回值**: 无  
**调用次数**: 32次

**用途**: 停止角色的台词播放和口型同步。

#### Movie.RoleSetPos(roleHandle, x, y, z)

**地址**: 00512c40  
**参数**:
- `roleHandle` (int) - 角色句柄
- `x`, `y`, `z` (float) - 新位置

**返回值**: 无  
**调用次数**: 1次（罕用）

**用途**: 直接设置角色位置（不带过渡动画，推测）。

#### Movie.RoleSetSpeed(roleHandle, speed)

**地址**: 00512cd0  
**参数**:
- `roleHandle` (int) - 角色句柄
- `speed` (int/float) - 移动速度（数值越大速度越快，推测）

**返回值**: 无  
**调用次数**: 7次

**用途**: 设置角色移动速度。

#### Movie.RoleSetScale(roleHandle, scaleX, scaleY, scaleZ)

**地址**: 00512d30  
**参数**:
- `roleHandle` (int) - 角色句柄
- `scaleX`, `scaleY`, `scaleZ` (float) - 各轴缩放因子（1.0为原始大小）

**返回值**: 无  
**调用次数**: 50次

**用途**: 缩放角色模型大小。

**示例调用**:
```python
# 来自 da03_1.py:43
Movie.RoleSetScale(r1, 0.9, 0.9, 0.9)
```

### 2.2 物件(Art)相关函数

物件(Art)指演出中的非角色元素，如道具、背景装饰等。接口与Role类似。

#### Movie.ArtCreate(objectId, x, y, z)

**地址**: 00512db0  
**参数**:
- `objectId` (int) - 物件模型ID
- `x`, `y`, `z` (float) - 3D位置

**返回值**: 物件句柄 (int)  
**调用次数**: 318次（最常用函数）

**用途**: 在场景中创建道具或装饰物件。

**示例调用**:
```python
# 来自 da01_1.py:11-17
bj = Movie.ArtCreate(2201, 0, 0, 0)
laba = Movie.ArtCreate(602, 80, 0, -75)
gamegod = Movie.ArtCreate(2205, -40, 0, -50)
```

#### Movie.ArtDel(artHandle)

**地址**: 00512f10  
**参数**: `artHandle` (int) - 物件句柄  
**返回值**: 无  
**调用次数**: 168次

**用途**: 删除物件。

#### Movie.ArtMove(artHandle, x, y, z)

**地址**: 00513090  
**参数**:
- `artHandle` (int) - 物件句柄
- `x`, `y`, `z` (float) - 目标位置

**返回值**: 无  
**调用次数**: 54次

**用途**: 移动物件。

#### Movie.ArtRotate(artHandle, angleZ)

**地址**: 00513120  
**参数**:
- `artHandle` (int) - 物件句柄
- `angleZ` (int) - Z轴旋转角度（度数）

**返回值**: 无  
**调用次数**: 2次（罕用）

**用途**: 旋转物件。

#### Movie.ArtSetAnim(artHandle, animIndex, animFrame)

**地址**: 00512ea0  
**参数**:
- `artHandle` (int) - 物件句柄
- `animIndex` (int) - 动画索引
- `animFrame` (int) - 动画帧

**返回值**: 无  
**调用次数**: 307次（最常用函数之一）

**用途**: 设置物件动画。

#### Movie.ArtSpeak(artHandle, speechId)

**地址**: 00513190  
**参数**:
- `artHandle` (int) - 物件句柄
- `speechId` (int) - 台词ID

**返回值**: 无  
**调用次数**: 60次

**用途**: 物件的台词（可能用于拟人化道具）。

#### Movie.ArtStopSpeak(artHandle)

**地址**: 005131f0  
**参数**: `artHandle` (int) - 物件句柄  
**返回值**: 无  
**调用次数**: 58次

**用途**: 停止物件的台词。

#### Movie.ArtSetPos(artHandle, x, y, z)

**地址**: 00513250  
**参数**:
- `artHandle` (int) - 物件句柄
- `x`, `y`, `z` (float) - 新位置

**返回值**: 无  
**调用次数**: 1次（罕用）

**用途**: 直接设置物件位置（不带过渡）。

#### Movie.ArtSetSpeed(artHandle, speed)

**地址**: 005132e0  
**参数**:
- `artHandle` (int) - 物件句柄
- `speed` (int/float) - 移动速度

**返回值**: 无  
**调用次数**: 28次

**用途**: 设置物件移动速度。

#### Movie.ArtSetScale(artHandle, scaleX, scaleY, scaleZ)

**地址**: 00513340  
**参数**:
- `artHandle` (int) - 物件句柄
- `scaleX`, `scaleY`, `scaleZ` (float) - 缩放因子

**返回值**: 无  
**调用次数**: 91次

**用途**: 缩放物件大小。

### 2.3 音效与背景乐

#### Movie.PlaySound(soundId, playCount)

**地址**: 005133c0  
**参数**:
- `soundId` (int) - 音效ID
- `playCount` (int) - 播放次数（通常为1或多次循环）

**返回值**: 无  
**调用次数**: 368次（最常用函数）

**用途**: 播放音效。

**示例调用**:
```python
# 来自 da01_1.py:34
Movie.PlaySound(6208, 1)
# 来自 da01_1.py:55
Movie.PlaySound(6203, 1)
```

#### Movie.PlayMusic(musicId)

**地址**: 00513430  
**参数**: `musicId` (int) - 背景音乐ID  
**返回值**: 无  
**调用次数**: 67次

**用途**: 播放背景音乐。

### 2.4 背景图层

#### Movie.SetBackPic(pictureName, texturePath)

**地址**: 00513490  
**参数**:
- `pictureName` (str) - 图片名称
- `texturePath` (str) - 纹理路径（如 `'\\demo_tex\\'`）

**返回值**: 无  
**调用次数**: 23次

**用途**: 设置背景图层。

**示例调用**:
```python
# 来自 da01_2.py:10
Movie.SetBackPic('grass', '\\demo_tex\\')
```

#### Movie.SetFrontPic(pictureName, texturePath)

**地址**: 00513500  
**参数**: 同 SetBackPic  
**返回值**: 无  
**调用次数**: 36次

**用途**: 设置前景图层（覆盖整个屏幕的特效，如闪光）。

#### Movie.DelBackPic()

**地址**: 00513570  
**参数**: 无  
**返回值**: 无  
**调用次数**: 1次（罕用）

**用途**: 删除背景图层。

#### Movie.DelFrontPic()

**地址**: 005135a0  
**参数**: 无  
**返回值**: 无  
**调用次数**: 35次

**用途**: 删除前景图层。

**示例调用**:
```python
# 来自 da01_1.py:74
Movie.DelFrontPic()
```

### 2.5 旁白与摄像机

#### Movie.SpeakAside(speechId)

**地址**: 005135d0  
**参数**: `speechId` (int) - 旁白语音ID  
**返回值**: 无  
**调用次数**: 84次

**用途**: 播放旁白（无角色表情，通常用于叙述）。

**示例调用**:
```python
# 来自 da01_1.py:56
Movie.SpeakAside(6865)
CTime.Wait(11.0)
yield None
```

#### Movie.StopSpeakAside(speechId)

**地址**: 00513620  
**参数**: `speechId` (int) - 要停止的旁白ID  
**返回值**: 无  
**调用次数**: 72次

**用途**: 停止旁白播放。

#### Movie.SetCameraPos(x, y, z, panSpeed, rotateSpeed)

**地址**: 00513650  
**参数**:
- `x`, `y`, `z` (float/int) - 摄像机位置
- `panSpeed` (int) - 移动速度
- `rotateSpeed` (int) - 旋转速度

**返回值**: 无  
**调用次数**: 88次

**用途**: 设置摄像机位置和运动参数。

**示例调用**:
```python
# 来自 da01_1.py:10
Movie.SetCameraPos(-116, 145, -322, 17, 20)
# 来自 da01_1.py:61
Movie.SetCameraPos(0, 120, -420, 10, 0)
```

#### Movie.QuitMovie()

**地址**: 005136d0  
**参数**: 无  
**返回值**: 无  
**调用次数**: 40次

**用途**: 结束演出，返回游戏主程序。通常是剧情脚本的最后一条语句。

**示例调用**:
```python
# 来自 da01_1.py:109
Movie.QuitMovie()
yield None
return
```

---

## 3. Role 模块（玩家角色管理）

Role模块提供56个函数，管理玩家的所有属性和操作。2个函数未被调用（Hospital、Enemy）。

### 3.1 查询函数

#### Role.Sum()

**地址**: 004fd680  
**参数**: 无  
**返回值**: 玩家数量 (int)  
**调用次数**: 20次

**用途**: 获取当前游戏中的玩家总数。

#### Role.CurRole()

**地址**: 004fd6b0  
**参数**: 无  
**返回值**: 当前玩家编号 (int)  
**调用次数**: 237次（最常用函数）

**用途**: 获取当前回合的玩家编号。

**示例调用**:
```python
# 来自 AI_01.py:10
cur = Role.CurRole()
a = Role.Cash(cur)
```

#### Role.Exist(roleId)

**地址**: 004fd740  
**参数**: `roleId` (int) - 玩家编号  
**返回值**: 布尔值 (0/1)  
**调用次数**: 20次

**用途**: 检查指定玩家是否还在游戏中（未破产）。

#### Role.InScn(roleId)

**地址**: 004ff710  
**参数**: `roleId` (int) - 玩家编号  
**返回值**: 布尔值 (0/1)  
**调用次数**: 13次

**用途**: 检查指定玩家是否在当前场景中。

### 3.2 财务函数

#### Role.Cash(roleId)

**地址**: 004fd7e0  
**参数**: `roleId` (int) - 玩家编号  
**返回值**: 现金数额 (int)  
**调用次数**: 71次

**用途**: 获取玩家的现金。

#### Role.Fund(roleId)

**地址**: 004fd880  
**参数**: `roleId` (int) - 玩家编号  
**返回值**: 资产（房产等）价值 (int)  
**调用次数**: 79次

**用途**: 获取玩家的总资产价值（不含现金）。

#### Role.PriceIndex()

**地址**: 004ff9f0  
**参数**: 无  
**返回值**: 价格指数 (float/int)  
**调用次数**: 71次

**用途**: 获取当前游戏的通货膨胀指数，用于调整价格。

**示例调用**:
```python
# 来自 AI_01.py:16
p = Role.PriceIndex()
# 来自 AI_01.py:18
if a < 5000 * p:
```

#### Role.Borrow(roleId)

**地址**: 004fd920  
**参数**: `roleId` (int) - 玩家编号  
**返回值**: 借债金额 (int)  
**调用次数**: 16次

**用途**: 获取玩家的债务金额。

#### Role.PaybackDay(roleId)

**地址**: 004fd9e0  
**参数**: `roleId` (int) - 玩家编号  
**返回值**: 还款期限 (int)  
**调用次数**: 16次

**用途**: 获取玩家的还款期限（天数）。

#### Role.Ticket(roleId)

**地址**: 004fda90  
**参数**: `roleId` (int) - 玩家编号  
**返回值**: 路条数量 (int)  
**调用次数**: 1次（罕用）

**用途**: 获取玩家持有的路条（通行证）数量。

### 3.3 位置与移动

#### Role.StPoint(roleId)

**地址**: 004fdbe0  
**参数**: `roleId` (int) - 玩家编号  
**返回值**: 当前特殊地点编号 (int)  
**调用次数**: 11次

**用途**: 获取玩家当前所在的特殊地点。

#### Role.StPoints(roleId, range, direction)

**地址**: 004fdc80  
**参数**:
- `roleId` (int) - 玩家编号
- `range` (int) - 搜索范围
- `direction` (int) - 方向（1=顺时针，-1=逆时针，推测）

**返回值**: 特殊地点列表或计数  
**调用次数**: 51次

**用途**: 获取玩家周围指定范围内的特殊地点。

#### Role.Terra(roleId)

**地址**: 004fdb30  
**参数**: `roleId` (int) - 玩家编号  
**返回值**: 当前地块编号 (int)  
**调用次数**: 13次

**用途**: 获取玩家当前所在的地块。

**示例调用**:
```python
# 来自 CardPy_01.py:12
t = Role.Terra(curRole)
```

#### Role.ChgPos(roleId, direction, distance)

**地址**: 004ff140  
**参数**:
- `roleId` (int) - 玩家编号
- `direction` (int) - 移动方向
- `distance` (int) - 移动距离

**返回值**: 无  
**调用次数**: 2次（罕用）

**用途**: 改变玩家位置。

#### Role.Go(roleId, steps)

**地址**: 004fe510  
**参数**:
- `roleId` (int) - 玩家编号
- `steps` (int) - 移动步数

**返回值**: 无  
**调用次数**: 1次（罕用）

**用途**: 让玩家移动指定步数。

### 3.4 状态检查

#### Role.Shape(roleId)

**地址**: 004ffa90  
**参数**: `roleId` (int) - 玩家编号  
**返回值**: 角色形态 (int)  
**调用次数**: 52次

**用途**: 获取玩家的当前形态/状态。

**推测的返回值**:
- 附身神明编号：0 无、1 财神、2 穷神、3 衰神、4 福神、5 土地公、6 糊涂神、7 警察、8 小偷、9 野狗、16 路霸状态（详见 §11.1，已由主代理按 Become() 调用核实）

**示例调用**:
```python
# 来自 AI_01.py:55
s = Role.Shape(cur)
if s == 3 or s == 5:
    return 0
```

#### Role.Broken(roleId)

**地址**: 004ffb40  
**参数**: `roleId` (int) - 玩家编号  
**返回值**: 破产状态 (int)  
**调用次数**: 21次

**用途**: 检查玩家是否已破产。

### 3.5 卡片系统

#### Role.CardSum(roleId)

**地址**: 004ff7e0  
**参数**: `roleId` (int) - 玩家编号  
**返回值**: 卡片总数 (int)  
**调用次数**: 2次（罕用）

**用途**: 获取玩家持有的卡片总数。

#### Role.Card(roleId, cardIndex)

**地址**: 004ff880  
**参数**:
- `roleId` (int) - 玩家编号
- `cardIndex` (int) - 卡片索引

**返回值**: 卡片ID (int)  
**调用次数**: 1次（罕用）

**用途**: 获取指定索引的卡片ID。

#### Role.HasCard(roleId, cardId)

**地址**: 004ff940  
**参数**:
- `roleId` (int) - 玩家编号
- `cardId` (int) - 卡片ID

**返回值**: 布尔值 (0/1)  
**调用次数**: 33次

**用途**: 检查玩家是否持有指定卡片。

**常见卡片ID** (基于 card.bin.txt):
- `1`: 购地卡
- `2`: 建屋卡
- `3`: 拆屋卡
- `4`: 交换卡
- `5`: 查封卡
- `6`: 怪兽卡
- `11`: 检查中...（被脚本频繁检查）
- `21`: 检查中...（被脚本频繁检查，可能是特殊卡）

**示例调用**:
```python
# 来自 AI_01.py:13
if Role.HasCard(cur, 21):
    return -a
# 来自 AI_01.py:136
if Role.HasCard(cur, 11):
```

#### Role.CardLevel(roleId)

**地址**: 004ffa20  
**参数**: `roleId` (int) - 玩家编号  
**返回值**: 卡片等级或计数 (int)  
**调用次数**: 1次（罕用）

**用途**: 获取卡片信息（具体含义待验证）。

#### Role.ReceiveCard(roleId, cardId, unknown)

**地址**: 004ff480  
**参数**:
- `roleId` (int) - 玩家编号
- `cardId` (int) - 卡片ID
- `unknown` (int) - 未知参数（通常为0）

**返回值**: 无  
**调用次数**: 1次（罕用）

**用途**: 给玩家发放卡片。

### 3.6 特殊效果

#### Role.Speak(roleId, speechId)

**地址**: 004fe3d0  
**参数**:
- `roleId` (int) - 玩家编号
- `speechId` (int) - 台词ID

**返回值**: 无  
**调用次数**: 3次（罕用）

**用途**: 让玩家角色说话。

#### Role.Wait(roleId)

**地址**: 004fe470  
**参数**: `roleId` (int) - 玩家编号  
**返回值**: 无  
**调用次数**: 45次

**用途**: 等待玩家操作完成（动画播放、移动等）。通常在卡片使用后调用。

#### Role.Pause(roleId, frames)

**地址**: 004fe660  
**参数**:
- `roleId` (int) - 玩家编号
- `frames` (int) - 暂停帧数

**返回值**: 无  
**调用次数**: 12次

**用途**: 暂停玩家的行动。

**示例调用**:
```python
# 来自 CardPy_04.py:15
Role.Pause(curRole, 60)
```

#### Role.Hypnosis(roleId, duration)

**地址**: 004fe7c0  
**参数**:
- `roleId` (int) - 玩家编号
- `duration` (int) - 催眠持续时长

**返回值**: 无  
**调用次数**: 1次（罕用）

**用途**: 催眠玩家。

#### Role.PlayPrt(roleId, prtId, duration)

**地址**: 004fec10  
**参数**:
- `roleId` (int) - 玩家编号
- `prtId` (int) - 特效ID（6122可能是定身术特效）
- `duration` (int) - 持续时间

**返回值**: 无  
**调用次数**: 11次

**用途**: 对玩家播放特殊效果（如定身术、变身等）。

**示例调用**:
```python
# 来自 CardPy_26.py:16
Role.PlayPrt(curRole, 6122, 30)
```

#### Role.Bubble(roleId)

**地址**: 004ff1f0  
**参数**: `roleId` (int) - 玩家编号  
**返回值**: 无  
**调用次数**: 1次（罕用）

**用途**: 为玩家添加气泡/护盾效果。

#### Role.HasBubble(roleId)

**地址**: 004ff290  
**参数**: `roleId` (int) - 玩家编号  
**返回值**: 布尔值 (0/1)  
**调用次数**: 2次（罕用）

**用途**: 检查玩家是否有气泡保护。

#### Role.Bodyguard(roleId)

**地址**: 004ff340  
**参数**: `roleId` (int) - 玩家编号  
**返回值**: 无  
**调用次数**: 1次（罕用）

**用途**: 为玩家分配保镖。

#### Role.HasBdygrd(roleId)

**地址**: 004ff3e0  
**参数**: `roleId` (int) - 玩家编号  
**返回值**: 布尔值 (0/1)  
**调用次数**: 2次（罕用）

**用途**: 检查玩家是否有保镖。

#### Role.Latent(roleId)

**地址**: 004ff530  
**参数**: `roleId` (int) - 玩家编号  
**返回值**: 无  
**调用次数**: 1次（罕用）

**用途**: 使玩家隐形/潜行。

#### Role.Seal(roleId, sealType)

**地址**: 004ff5d0  
**参数**:
- `roleId` (int) - 玩家编号
- `sealType` (int) - 封印类型

**返回值**: 无  
**调用次数**: 1次（罕用）

**用途**: 对玩家施加封印（限制行动）。

#### Role.LostFocus(roleId1, roleId2, unknown)

**地址**: 004fe710  
**参数**:
- `roleId1` (int) - 失焦玩家
- `roleId2` (int) - 获得焦点的玩家（可能）
- `unknown` (int) - 未知参数（通常为0）

**返回值**: 无  
**调用次数**: 4次（罕用）

**用途**: 转移焦点/主角地位。

#### Role.Average(roleId, change)

**地址**: 004fe870  
**参数**:
- `roleId` (int) - 玩家编号
- `change` (int) - 变化值（正数增加，负数减少）

**返回值**: 无  
**调用次数**: 2次（罕用）

**用途**: 调整玩家的平均值/幸运值（具体含义待验证）。

#### Role.Grab(roleId, targetId, amount, mode)

**地址**: 004fed70  
**参数**:
- `roleId` (int) - 玩家编号
- `targetId` (int) - 目标玩家
- `amount` (int) - 抢夺数量
- `mode` (int) - 抢夺模式（0-4，具体含义待验证）

**返回值**: 无  
**调用次数**: 3次（罕用）

**用途**: 从目标玩家抢夺资产或卡片。

**示例调用**:
```python
# 来自 CardPy_25.py:14
Role.Grab(curRole, param, 4, 1)
```

### 3.7 转移和变身

#### Role.TrfTool(roleId)

**地址**: 004fe010  
**参数**: `roleId` (int) - 玩家编号  
**返回值**: 交通工具类型 (int)  
**调用次数**: 35次

**用途**: 获取玩家当前的交通工具。

**推测返回值**:
- `0-2`: 不同的交通工具（直升机、轿车等）

#### Role.ChgTrfTool(roleId, toolType)

**地址**: 004fe0c0  
**参数**:
- `roleId` (int) - 玩家编号
- `toolType` (int) - 新交通工具类型

**返回值**: 无  
**调用次数**: 2次（罕用）

**用途**: 更换交通工具。

#### Role.Become(roleId, newForm)

**地址**: 004fecc0  
**参数**:
- `roleId` (int) - 玩家编号
- `newForm` (int) - 新形态（0=人形, 16=..., 1=...等）

**返回值**: 无  
**调用次数**: 11次

**用途**: 变身为指定形态（如怪兽卡、恐龙等）。

**示例调用**:
```python
# 来自 CardPy_26.py:17
Role.Become(curRole, 0)
# 来自 CardPy_34.py:13
Role.Become(param, 16)
```

#### Role.Tortoise(roleId)

**地址**: 004fe160  
**参数**: `roleId` (int) - 玩家编号  
**返回值**: 布尔值/状态值  
**调用次数**: 5次（罕用）

**用途**: 查询或设置玩家的乌龟形态（推测）。

### 3.8 监狱和医院

#### Role.Prison(roleId, days)

**地址**: 004fefe0  
**参数**:
- `roleId` (int) - 玩家编号
- `days` (int) - 监禁天数

**返回值**: 无  
**调用次数**: 1次（罕用）

**用途**: 将玩家送入监狱。

#### Role.Hospital(roleId, days)

**地址**: 004ff090  
**参数**:
- `roleId` (int) - 玩家编号
- `days` (int) - 住院天数

**返回值**: 无  
**调用次数**: 0次（未被调用）

**用途**: 将玩家送入医院。**潜在可用于mod的接口**。

### 3.9 事件和状态

#### Role.OnEvent(roleId, eventType, param)

**地址**: 004feb50  
**参数**:
- `roleId` (int) - 玩家编号
- `eventType` (int) - 事件类型（94-96）
- `param` (int) - 事件参数

**返回值**: 无  
**调用次数**: 3次（罕用）

**用途**: 触发特定事件（如礼物、惊喜等）。

**已知事件类型**:
- `94`: CardPy_47.py 中使用
- `95`: CardPy_23.py 中使用
- `96`: CardPy_22.py 中使用

#### Role.Turn(roleId)

**地址**: 004fe5c0  
**参数**: `roleId` (int) - 玩家编号  
**返回值**: 无  
**调用次数**: 1次（罕用）

**用途**: 轮到指定玩家。

#### Role.Settle(roleId)

**地址**: 004fe2b0  
**参数**: `roleId` (int) - 玩家编号  
**返回值**: 无  
**调用次数**: 1次（罕用）

**用途**: 结算玩家的资产和税收。

#### Role.SetTrts(roleId, traitType)

**地址**: 004fe210  
**参数**:
- `roleId` (int) - 玩家编号
- `traitType` (int) - 特性类型

**返回值**: 无  
**调用次数**: 1次（罕用）

**用途**: 设置玩家特性/天赋。

### 3.10 游戏进度

#### Role.GameDate()

**地址**: 004ffbf0  
**参数**: 无  
**返回值**: **本局已进行的天数**（g_GameState+0x14，主代理已验证：存档中 201 天对应此字段）  
**调用次数**: 3次（罕用）

**用途**: 卡片 AI 用它判断游戏阶段，例如 `GameDate() > 20` 才〔string.bin#542〕。

#### Role.GameDay()

**地址**: 004ffc30  
**参数**: 无  
**返回值**: **当月日期 1~31**（g_GameState+0x0C，主代理已验证；游戏内日期取自真实日历）  
**调用次数**: 16次

**用途**: AI_xx.Access 据此判断月初（≤7）、月末（≥26）调整存款比例。

**示例调用**:
```python
# 来自 AI_01.py:28
d = Role.GameDay()
if d <= 7:
    r = r + 60
elif d >= 26:
    r = r + 20
```

#### Role.GameDffty()

**地址**: 004ffc70  
**参数**: 无  
**返回值**: 游戏难度 (int)  
**调用次数**: 3次（罕用）

**用途**: 获取当前游戏难度等级。

### 3.11 股票

#### Role.Stock(roleId, stockIndex)

**地址**: 004ffca0  
**参数**:
- `roleId` (int) - 玩家编号
- `stockIndex` (int) - 股票索引

**返回值**: 持股数量 (int)  
**调用次数**: 18次

**用途**: 获取玩家持有的指定股票数量。

#### Role.StockSum(roleId)

**地址**: 004ffdb0  
**参数**: `roleId` (int) - 玩家编号  
**返回值**: 股票总数或类型数 (int)  
**调用次数**: 20次

**用途**: 获取玩家持有的股票种类总数或总股数。

#### Role.StockPrice(roleId, stockIndex)

**地址**: 004ffe60  
**参数**:
- `roleId` (int) - 玩家编号
- `stockIndex` (int) - 股票索引

**返回值**: 该股票的〔string.bin#6101〕 (int)  
**调用次数**: 16次

**用途**: 获取玩家持有股票的价格（或平均价格）。

#### Role.StockValue(roleId, stockIndex)

**地址**: 004fff30  
**参数**:
- `roleId` (int) - 玩家编号
- `stockIndex` (int) - 股票索引

**返回值**: 持股总价值 (int)  
**调用次数**: 3次（罕用）

**用途**: 获取玩家持有的指定股票的总价值。

### 3.12 其他

#### Role.Enemy(roleId)

**地址**: 004ff680  
**参数**: `roleId` (int) - 玩家编号  
**返回值**: 敌人编号 (int)  
**调用次数**: 0次（未被调用）

**用途**: 未知。**潜在可用于mod的接口**。

#### Role.TimeBomb(roleId)

**地址**: 004fffe0  
**参数**: `roleId` (int) - 玩家编号  
**返回值**: 炸弹状态 (int)  
**调用次数**: 2次（罕用）

**用途**: 获取玩家身上的定时炸弹（时间炸弹卡）。

#### Role.TimeBombDis(roleId)

**地址**: 00500090  
**参数**: `roleId` (int) - 玩家编号  
**返回值**: 无  
**调用次数**: 1次（罕用）

**用途**: 引爆/解除玩家身上的定时炸弹。

---

## 4. Terra 模块（地块与物业管理）

Terra模块提供17个函数，管理〔string.bin#6203〕上的各个地块。1个函数未被调用（WonderFine）。

### 4.1 查询函数

#### Terra.Sum()

**地址**: 005005e0  
**参数**: 无  
**返回值**: 地块总数 (int)  
**调用次数**: 9次

**用途**: 获取〔string.bin#6203〕上的总地块数。

**示例调用**:
```python
# 来自 CardPy_01.py:43
sum = Terra.Sum()
for n in range(sum):
    tr = Terra.Terra(n)
```

#### Terra.TermSum(streetId, roleId)

**地址**: 00500610  
**参数**:
- `streetId` (int) - 街道ID
- `roleId` (int) - 玩家编号（可能用于筛选）

**返回值**: 该街道的地块数  
**调用次数**: 2次（罕用）

**用途**: 获取指定街道上的地块总数。

#### Terra.Terra(index)

**地址**: 00500640  
**参数**: `index` (int) - 地块索引  
**返回值**: 地块编号 (int)  
**调用次数**: 12次

**用途**: 通过索引获取地块编号。

### 4.2 地块属性

#### Terra.Owner(terraId)

**地址**: 005006a0  
**参数**: `terraId` (int) - 地块编号  
**返回值**: 拥有者玩家编号 (int)  
**调用次数**: 69次

**用途**: 获取地块的现有拥有者。返回-1表示无主。

**示例调用**:
```python
# 来自 CardPy_01.py:15
no = Terra.Owner(t)
if no >= 0 and no != curRole:
    Reaction(no, sb)
```

#### Terra.Type(terraId)

**地址**: 005006f0  
**参数**: `terraId` (int) - 地块编号  
**返回值**: 地块类型 (int)  
**调用次数**: 8次

**用途**: 获取地块的类型。

**已知类型值**:
- `1`: 普通地块或某种地块类型（在CardPy_01.py和CardPy_04.py中被检查）
- `2`: 另一种地块类型（在CardPy_01.py中被检查）

#### Terra.Level(terraId)

**地址**: 00500750  
**参数**: `terraId` (int) - 地块编号  
**返回值**: 建筑等级 (int)  
**调用次数**: 34次

**用途**: 获取地块上的建筑等级（0=未建, 1=平房, 2=楼房, 3=大厦, 4=城堡等）。

**示例调用**:
```python
# 来自 CardPy_01.py:33
level = Terra.Level(t)
if level > randint(1, 3):
    return 1
```

#### Terra.Street(terraId)

**地址**: 005007b0  
**参数**: `terraId` (int) - 地块编号  
**返回值**: 街道编号 (int)  
**调用次数**: 18次

**用途**: 获取地块所在的街道。

**示例调用**:
```python
# 来自 CardPy_01.py:42
s = Terra.Street(t)
sum = Terra.Sum()
for n in range(sum):
    tr = Terra.Terra(n)
    if Terra.Street(tr) == s and Terra.Owner(tr) == curRole:
        return 1
```

### 4.3 财务函数

#### Terra.Rent(terraId, roleId)

**地址**: 00500800  
**参数**:
- `terraId` (int) - 地块编号
- `roleId` (int) - 玩家编号（被访问者，推测）

**返回值**: 租金数额 (int)  
**调用次数**: 23次

**用途**: 计算地块的租金（根据建筑等级和所有者）。

**示例调用**:
```python
# 来自 AI_01.py:144
Terra.Rent(t, 1)
```

#### Terra.Charge(terraId)

**地址**: 00500e70  
**参数**: `terraId` (int) - 地块编号  
**返回值**: 购买或升级成本 (int)  
**调用次数**: 3次（罕用）

**用途**: 获取地块的购买或升级成本。

**示例调用**:
```python
# 来自 CardPy_01.py:34
p = Terra.Charge(t)
if cash > p:
```

### 4.4 地块操作

#### Terra.Buy(terraId, roleId)

**地址**: 00500a80  
**参数**:
- `terraId` (int) - 地块编号
- `roleId` (int) - 购买者玩家编号

**返回值**: 无  
**调用次数**: 1次（罕用）

**用途**: 让玩家购买地块。

**示例调用**:
```python
# 来自 CardPy_01.py:16
Terra.Buy(t, curRole)
```

#### Terra.Build(terraId, roleId)

**地址**: 00500860  
**参数**:
- `terraId` (int) - 地块编号
- `roleId` (int) - 建造者玩家编号

**返回值**: 无  
**调用次数**: 1次（罕用）

**用途**: 在地块上建造建筑。

**示例调用**:
```python
# 来自 CardPy_02.py:14
Terra.Build(t, curRole)
```

#### Terra.Break(terraId, roleId)

**地址**: 00500b30  
**参数**:
- `terraId` (int) - 地块编号
- `roleId` (int) - 拆除者玩家编号

**返回值**: 无  
**调用次数**: 1次（罕用）

**用途**: 拆除地块上的建筑。

#### Terra.Exchng(terraId, roleId)

**地址**: 00500be0  
**参数**:
- `terraId` (int) - 地块编号
- `roleId` (int) - 玩家编号

**返回值**: 无  
**调用次数**: 1次（罕用）

**用途**: 交换地块（交换卡的具体实现）。

#### Terra.Dragon(terraId, roleId)

**地址**: 00500ca0  
**参数**:
- `terraId` (int) - 地块编号
- `roleId` (int) - 玩家编号

**返回值**: 无  
**调用次数**: 1次（罕用）

**用途**: 在地块上放置龙（怪兽卡的具体实现）。

**示例调用**:
```python
# 来自 CardPy_06.py:15
Terra.Dragon(t, curRole)
```

#### Terra.Bomb(terraId, roleId, bombType)

**地址**: 00500d50  
**参数**:
- `terraId` (int) - 地块编号
- `roleId` (int) - 玩家编号
- `bombType` (int) - 炸弹类型（1=..., 2=...）

**返回值**: 无  
**调用次数**: 2次（罕用）

**用途**: 在地块上放置炸弹。

**示例调用**:
```python
# 来自 CardPy_18.py:13
Terra.Bomb(p, curRole, 1)
# 来自 CardPy_19.py:12
Terra.Bomb(p, curRole, 2)
```

### 4.5 其他

#### Terra.InScn(terraId)

**地址**: 00500e10  
**参数**: `terraId` (int) - 地块编号  
**返回值**: 布尔值 (0/1)  
**调用次数**: 2次（罕用）

**用途**: 检查地块是否在当前场景中显示。

#### Terra.WonderFine(terraId)

**地址**: 00500ee0  
**参数**: `terraId` (int) - 地块编号  
**返回值**: 未知  
**调用次数**: 0次（未被调用）

**用途**: 未知（可能与奇迹地块有关）。**潜在可用于mod的接口**。

---

## 5. Stock 模块（股票交易）

Stock模块提供6个函数，全部被脚本调用，管理股票交易系统。

#### Stock.Sum()

**地址**: 005015b0  
**参数**: 无  
**返回值**: 股票种类总数 (int)  
**调用次数**: 14次

**用途**: 获取游戏中有多少种股票。

#### Stock.FlowSum(stockIndex)

**地址**: 00501550  
**参数**: `stockIndex` (int) - 股票索引  
**返回值**: 该股票的交易量 (int)  
**调用次数**: 30次

**用途**: 获取指定股票的总交易量/流通量。

#### Stock.CurPrice(stockIndex)

**地址**: 005015e0  
**参数**: `stockIndex` (int) - 股票索引  
**返回值**: 当前股价 (int)  
**调用次数**: 46次

**用途**: 获取指定股票的〔string.bin#6101〕。

**示例调用**:
```python
# 来自 AI_01.py:81
for i in range(Stock.Sum()):
    cur = Stock.CurPrice(i)
```

#### Stock.AvrPrice(stockIndex)

**地址**: 00501650  
**参数**: `stockIndex` (int) - 股票索引  
**返回值**: 平均股价 (int)  
**调用次数**: 30次

**用途**: 获取指定股票的平均价格（用于判断涨跌）。

#### Stock.TradeEnable(stockIndex)

**地址**: 005016f0  
**参数**: `stockIndex` (int) - 股票索引  
**返回值**: 〔string.bin#6303〕或状态 (int)  
**调用次数**: 46次

**用途**: 获取指定股票当前是否可交易或剩余可交易数量。

**示例调用**:
```python
# 来自 AI_01.py:83
if Stock.TradeEnable(i) > 0:
```

#### Stock.Open()

**地址**: 00501760  
**参数**: 无  
**返回值**: 无  
**调用次数**: 3次（罕用）

**用途**: 打开/显示股票交易界面。

---

## 6. StPoint 模块（特殊地点）

StPoint模块提供8个函数，管理地图上的特殊地点（NPC地点、奇迹等）。1个函数未被调用（Wonder）。

#### StPoint.Sum()

**地址**: 00500f70  
**参数**: 无  
**返回值**: 特殊地点总数 (int)  
**调用次数**: 1次（罕用）

**用途**: 获取特殊地点总数。

#### StPoint.Point(pointId)

**地址**: 00500fa0  
**参数**: `pointId` (int) - 特殊地点ID  
**返回值**: 地块索引 (int)  
**调用次数**: 7次

**用途**: 获取特殊地点所在的地块。

**示例调用**:
```python
# 来自 CardPy_12.py:10
p = StPoint.Point(param)
```

#### StPoint.No(pointId)

**地址**: 005010a0  
**参数**: `pointId` (int) - 特殊地点ID  
**返回值**: 地点编号或类型 (int)  
**调用次数**: 8次

**用途**: 获取特殊地点的编号或类型。

#### StPoint.Npc(pointId)

**地址**: 00501160  
**参数**: `pointId` (int) - 特殊地点ID  
**返回值**: NPC编号 (int)  
**调用次数**: 26次

**用途**: 获取特殊地点关联的NPC。

**示例调用**:
```python
# 来自 AI_01.py:185
npc = StPoint.Npc(s)
```

#### StPoint.Terra(pointId)

**地址**: 00501050  
**参数**: `pointId` (int) - 特殊地点ID  
**返回值**: 关联的地块编号 (int)  
**调用次数**: 53次

**用途**: 获取特殊地点关联的地块。

#### StPoint.Event(pointId)

**地址**: 005011c0  
**参数**: `pointId` (int) - 特殊地点ID  
**返回值**: 事件类型或ID (int)  
**调用次数**: 1次（罕用）

**用途**: 获取特殊地点的事件类型。

#### StPoint.Wonder(pointId)

**地址**: 00501100  
**参数**: `pointId` (int) - 特殊地点ID  
**返回值**: 奇迹数据 (int)  
**调用次数**: 0次（未被调用）

**用途**: 获取特殊地点的奇迹数据。**潜在可用于mod的接口**。

#### StPoint.InScn(pointId)

**地址**: 00500ff0  
**参数**: `pointId` (int) - 特殊地点ID  
**返回值**: 布尔值 (0/1)  
**调用次数**: 4次（罕用）

**用途**: 检查特殊地点是否在当前场景中显示。

---

## 7. Npc 模块（NPC管理）

Npc模块提供7个函数，管理游戏中的NPC角色。2个函数未被调用（Disappear、StPoint）。

#### Npc.GetNpc(npcId)

**地址**: 005002f0  
**参数**: `npcId` (int) - NPC ID  
**返回值**: NPC对象或编号 (int)  
**调用次数**: 1次（罕用）

**用途**: 获取NPC对象。

#### Npc.Create(npcId, location, roleId)

**地址**: 00500160  
**参数**:
- `npcId` (int) - NPC类型ID
- `location` (int) - 地点ID或坐标
- `roleId` (int) - 关联玩家（通常是触发的玩家）

**返回值**: 创建的NPC编号 (int)  
**调用次数**: 4次（罕用）

**用途**: 临时创建一个NPC实例。

**示例调用**:
```python
# 来自 CardPy_12.py:13
pp = Npc.Create(12, p, curRole)
```

#### Npc.Disappear(npcId)

**地址**: 005003b0  
**参数**: `npcId` (int) - NPC编号  
**返回值**: 无  
**调用次数**: 0次（未被调用）

**用途**: 移除NPC。**潜在可用于mod的接口**。

#### Npc.ID(npcId)

**地址**: 00500340  
**参数**: `npcId` (int) - NPC编号  
**返回值**: NPC的ID (int)  
**调用次数**: 4次（罕用）

**用途**: 获取NPC的类型ID。

#### Npc.StPoint(npcId)

**地址**: 005004b0  
**参数**: `npcId` (int) - NPC编号  
**返回值**: 所在特殊地点 (int)  
**调用次数**: 0次（未被调用）

**用途**: 获取NPC所在的特殊地点。**潜在可用于mod的接口**。

#### Npc.Goodness(npcId)

**地址**: 00500520  
**参数**: `npcId` (int) - NPC编号  
**返回值**: 好感度 (int)  
**调用次数**: 19次

**用途**: 获取NPC与玩家的好感度。

**示例调用**:
```python
# 来自 AI_01.py:187
npc = StPoint.Npc(s)
Npc.Goodness(npc)
```

#### Npc.InScn(npcId)

**地址**: 00500430  
**参数**: `npcId` (int) - NPC编号  
**返回值**: 布尔值 (0/1)  
**调用次数**: 2次（罕用）

**用途**: 检查NPC是否在当前场景中。

---

## 8. Street 模块（街道操作）

Street模块提供4个函数，全部被脚本调用，管理街道级别的操作。

#### Street.Close(streetId, closeLevel, roleId)

**地址**: 00501260  
**参数**:
- `streetId` (int) - 街道ID
- `closeLevel` (int) - 封闭等级或模式（9表示查封）
- `roleId` (int) - 操作玩家

**返回值**: 无  
**调用次数**: 1次（罕用）

**用途**: 封闭街道（查封卡的具体实现）。

**示例调用**:
```python
# 来自 CardPy_05.py:15
Street.Close(s, 9, curRole)
```

#### Street.Flow(streetId, amount, roleId)

**地址**: 00501310  
**参数**:
- `streetId` (int) - 街道ID
- `amount` (int) - 流动税金额度
- `roleId` (int) - 操作玩家

**返回值**: 无  
**调用次数**: 1次（罕用）

**用途**: 设置街道的流动税（通过地点时的税金）。

**示例调用**:
```python
# 来自 CardPy_29.py:16
Street.Flow(s, 200, curRole)
```

#### Street.Build(streetId, roleId)

**地址**: 005013c0  
**参数**:
- `streetId` (int) - 街道ID
- `roleId` (int) - 建造者玩家

**返回值**: 无  
**调用次数**: 1次（罕用）

**用途**: 对街道进行建设升级。

**示例调用**:
```python
# 来自 CardPy_30.py:16
Street.Build(s, curRole)
```

#### Street.Buy(streetId, roleId)

**地址**: 00501470  
**参数**:
- `streetId` (int) - 街道ID
- `roleId` (int) - 购买者玩家

**返回值**: 无  
**调用次数**: 1次（罕用）

**用途**: 购买街道。

**示例调用**:
```python
# 来自 CardPy_48.py:16
Street.Buy(s, curRole)
```

---

## 9. 未被调用的函数（潜在MOD接口）

以下6个函数在脚本中从未被调用，可能是游戏设计中的预留接口，可用于MOD开发：

| 模块 | 函数 | 地址 | 推测用途 |
|------|------|------|---------|
| **Role** | Hospital | 004ff090 | 将玩家送入医院（对标Prison函数） |
| **Role** | Enemy | 004ff680 | 获取或设置玩家的敌人状态 |
| **Npc** | Disappear | 005003b0 | 移除/消灭NPC（对标Create函数） |
| **Npc** | StPoint | 005004b0 | 获取NPC所在的特殊地点 |
| **StPoint** | Wonder | 00501100 | 获取特殊地点的奇迹数据 |
| **Terra** | WonderFine | 00500ee0 | 与奇迹地块相关的操作（功能不明） |

---

## 10. 剧情脚本结构分析

### 10.1 时间线实现机制

剧情脚本（da*.py文件）使用Python 2.4的生成器特性实现基于帧的时间线系统。

**关键要素**:
- `from __future__ import generators` - 启用生成器支持
- `yield None` - 将控制权交回给引擎，等待下一帧
- `CTime.Wait(seconds)` - 暂停脚本执行指定时间

**基本模式**:
```python
def timeline():
    Movie.PlayMusic(45)
    CTime.Wait(1.0)
    yield None  # 引擎在此暂停1秒，然后继续
    
    Movie.RoleSpeak(roleHandle, 6700)
    CTime.Wait(5.0)
    yield None  # 等待5秒
    
    Movie.RoleStopSpeak(roleHandle)
    Movie.QuitMovie()
    yield None
    return
```

**执行流程**:
1. 脚本函数是一个生成器，返回迭代器
2. 每遇到 `yield None`，脚本暂停执行
3. 游戏引擎继续运行（处理输入、渲染、音频等）
4. CTime.Wait 指定的时间到达后，引擎调用迭代器的 `next()` 方法恢复脚本
5. 脚本继续执行直到下一个 `yield None`

这种设计使剧情脚本能够控制长时间的序列演出，同时不阻塞游戏主循环。

### 10.2 演出函数分类

**角色相关** (Role*/21函数):
- 创建/删除: RoleCreate, RoleDel
- 移动/旋转: RoleMove, RoleRotate, RoleFace, RoleSetPos, RoleSetSpeed
- 动画/表情: RoleSetAnim, RoleSetScale
- 对话: RoleSpeak, RoleStopSpeak

**物件相关** (Art*/20函数):
- 与Role完全平行的接口
- 用于道具、装饰、特效等非角色元素

**音效系统** (2函数):
- PlaySound: 音效
- PlayMusic: 背景音乐

**背景与视觉效果** (6函数):
- SetBackPic/DelBackPic: 背景图层
- SetFrontPic/DelFrontPic: 全屏特效（如闪光、暴雨）
- SetCameraPos: 摄像机位置与运动

**旁白与字幕** (4函数):
- SpeakAside/StopSpeakAside: 旁白（无口型同步）

**控制** (1函数):
- QuitMovie: 结束演出

### 10.3 常见演出模式

**对话演出**:
```python
Movie.RoleSpeak(roleHandle, speechId)  # 开始说话（含口型同步）
CTime.Wait(duration)  # 等待语音播放
yield None
Movie.RoleStopSpeak(roleHandle)  # 停止说话
```

**特效演出**:
```python
Movie.SetFrontPic('shine6', '\\demo_tex\\')  # 闪光效果
CTime.Wait(1.0)
yield None
Movie.DelFrontPic()  # 移除特效
```

**角色动画**:
```python
Movie.RoleSetAnim(roleHandle, animIndex, frame)  # 播放动画
CTime.Wait(animDuration)
yield None
```

---

## 11. 魔数与常量

### 11.1 Role.Shape() / Role.Become() 的取值（主代理已按代码核实）

`Shape` 是“附身神明/特殊形态”编号，与 `Role.Become(role, n)` 写入的值一一对应：

| 值 | 含义 | 依据 |
|----|------|------|
| 0 | 无附身（正常） | CardPy_26 送神卡：`Role.Become(curRole, 0)` 即送走神明 |
| 1 | 财神 | CardPy_36 財神卡 `Become(param, 1)` |
| 2 | 穷神 | CardPy_37 窮神卡 `Become(param, 2)` |
| 3 | 衰神 | CardPy_38 衰神卡 `Become(param, 3)`；AI_xx.TBuy/TBuild 在 s==3 时不买/不建，CardPy_01 中 s==3 时不触发对方反应 |
| 4 | 福神 | CardPy_39 `Become(param, 4)` |
| 5 | 土地公 | CardPy_40 土地卡 `Become(param, 5)`；AI.TBuy 在 s==5 时返回 0（推测：土地公附身时引擎自动占地，无需买） |
| 6 | 糊涂神 | CardPy_41 `Become(param, 6)` |
| 7 | 警察 | CardPy_42 `Become(param, 7)` |
| 8 | 小偷 | CardPy_43 `Become(param, 8)` |
| 9 | 野狗 | CardPy_44 `Become(param, 9)` |
| 16 | 路霸 | CardPy_34 路霸卡 `Become(param, 16)`；npcinfo 中 路霸 Id=16。Shape 值与 NPC 编号一致 |

AI 把 {1,4,5,7,8,9} 当作“好状态”（不会用神卡覆盖自己，会用穷神/衰神/糊涂卡去覆盖对手），{2,3} 当作“坏状态”（会用好神卡覆盖自己）。

### 11.2 Terra.Type() 的返回值

| 值 | 含义（推测） | 依据 |
|----|------|------|
| 1 | 住宅用地（属于街道，0~5 级房屋） | CardPy_04/05 只对 Type==1 统计街道；CardPy_06 AI 对 Type 1 用 `level >= randint(2,4)`；string.bin「住宅建地 / %d級住宅」 |
| 2 | 〔string.bin#14〕（连锁超市/研究所/饭店等） | CardPy_01 AI 对 Type 2 直接使用购地卡；string.bin「商業建地 / %d級連鎖超市…」 |

### 11.3 脚本中出现的卡片 / NPC 编号

| 编号 | 含义 | 依据 |
|----|------|------|
| Role.HasCard(cur, 11) | 11 = 轉向卡 | card.bin Id=11；AI.SSellAll 中若无转向卡则评估前方高额租金 |
| Role.HasCard(cur, 21) | 21 = 均富卡 | card.bin Id=21；AI.Access 持有均富卡时把现金全部存银行（返回 -现金，推测为“存入”）以便均富时少损失 |
| Npc.Create(12/13/14/15) | 12 路障、13 地雷、14 定时炸弹、15 机器娃娃 | CardPy_12/16/17/14 |
| Npc.ID / Npc.Create 编号 | 与 Config/npcinfo.bin 的 Id 一致：1 财神、2 穷神、3 衰神、4 福神、5 〔string.bin#549〕、6 糊涂神、7 警察、8 小偷、9 野狗、10 〔npcinfo.bin#Id_10〕、11 宝箱、12 路障、13 地雷、14 定时炸弹、15 机器娃娃、16 路霸、17 拼图 | npcinfo.bin；CardPy_14 AI 遇到 2/3/6/7/8/9/12/13/14 就放机器娃娃清障。封印卡给卡 `34+ID` 与此表差 1，疑似原版 bug |
| Role.OnEvent(cur, 94/95/96, 1) | 94 钻石卡、95 红卡、96 黑卡触发的事件（股市涨/跌，推测） | CardPy_47/23/22；AI 判断依据都是持股市值 |
| Role.Grab(a, b, kind, n) | kind=1 抢现金 n 元；kind=4 抢卡（n=1 抢一张，n=0 推测为全部） | CardPy_27 查税（现金 1/5）、CardPy_25 搶奪、CardPy_45 老千 |

### 11.4 常用音效ID

| ID范围 | 用途 | 示例 |
|--------|------|------|
| 5000+ | 特效音效 | 5402 (时间炸弹?) |
| 6000+ | 语音/旁白 | 6700-6865 (对话台词) |
| 6200+ | 游戏音效 | 6201-6209 (过场动画音效) |

**说明**: 具体ID的语义需要查看游戏的音效资源表。

### 11.5 角色模型ID

在Movie.RoleCreate中常见的ID:
- 1-16: 主要角色（8个玩家角色）

在Movie.ArtCreate中常见的ID范围:
- 600-699: 背景装饰
- 2200-2210: 特定场景的道具或NPC

---

## 12. 总结

### 12.1 API统计

- **总函数数**: 130个
- **被调用函数**: 124个 (95.4%)
- **未被调用函数**: 6个 (4.6%)

### 12.2 使用频率排名（TOP 10）

| 排名 | 函数 | 模块 | 调用次数 |
|------|------|------|---------|
| 1 | CTime.Wait | CTime | 660 |
| 2 | Movie.ArtCreate | Movie | 318 |
| 3 | Movie.ArtSetAnim | Movie | 307 |
| 4 | Movie.PlaySound | Movie | 368 |
| 5 | Role.CurRole | Role | 237 |
| 6 | Movie.RoleCreate | Movie | 56 |
| 7 | Movie.RoleSetAnim | Movie | 130 |
| 8 | Movie.ArtDel | Movie | 168 |
| 9 | Stock.CurPrice | Stock | 46 |
| 10 | Stock.TradeEnable | Stock | 46 |

### 12.3 模块功能概览

- **CTime**: 时间控制的唯一接口，被剧情脚本大量使用
- **Movie**: 演出系统，包括角色、物件、音效、视觉效果，功能最全面
- **Role**: 角色状态管理，涵盖属性、操作、特殊效果
- **Terra**: 地块和物业管理，与商业系统紧密相关
- **Stock**: 股票交易系统，相对独立完整
- **StPoint/Npc/Street**: 辅助系统，用于管理特殊位置、NPC和街道级操作

### 12.4 MOD开发建议

以下接口虽未被官方脚本调用，但功能清晰，可用于创意MOD:
- `Role.Hospital()`: 创建与医疗相关的机制
- `Npc.Disappear()`: 动态NPC管理
- `StPoint.Wonder()`: 奇迹系统扩展
- `Terra.WonderFine()`: 奇迹地块特殊效果

---

## 附录：文件来源

- 函数表来源: `<workspace>\out\py_api_tables.txt`
- 脚本源码来源: `<workspace>\out\pysrc\*.py` (137个文件，包括AI脚本、卡片脚本、剧情脚本等)
- 卡片配置来源: `<workspace>\out\config_utf8\card.bin.txt`
- 分析工具: Python 2.4+ 脚本解析，基于正则表达式匹配调用

---

**生成日期**: 2026年11月14日  
**分析工具**: Python API 调用统计器  
**准确度**: 基于反编译源码的静态分析，调用计数为精确值，魔数推测基于代码上下文
