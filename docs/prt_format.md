# 大富翁8 粒子配置文件格式（Config/prt）

解析器：`<workspace>\scripts\prtfmt.py`（按游戏加载函数的读取顺序逐字段走一遍，结束时位置必须正好等于文件长度，否则抛异常）。

- `PrtEffTable.tb`：9580 字节，26 条粒子效果记录 + 26 条索引，解析后剩余 0 字节
- `uvlisttable.tb`：131153 字节，1119 条 UV 动画列表 + 1119 条索引，解析后剩余 0 字节

说明：下面每个字段都标注了读取它的函数地址与反编译变量名。"游戏不读"的字段（索引项的偏移/大小/填充）也如实标出，解析器只做一致性校验。

---

## PrtEffTable.tb（加载函数 FUN_0046df60，由粒子系统初始化 FUN_00404400 调用）

### 更正（相对旧版文档）
旧版把 +4 的 26 当成"类型数"、+6 的 25 当成"记录数"，并认为有 25 条索引和 12 字节尾部。这是错的：
FUN_0046df60 的循环上界是 +4（`local_28`，=26 条记录），+6（`local_26`，=25）是最大 id（指针数组 `operator_new(local_26*4+4)`）。
索引区是 26 条 12 字节，正好填满到文件末尾（0x2434 + 26×12 = 0x256C = 9580），没有尾部填充。

### 布局

| 偏移 | 类型 | 含义 | 读取处（FUN_0046df60） |
|------|------|------|------|
| 0x00 | char[4] | 魔数 "PRT\0" | `memcpy(local_2c,file,0xc)`；`strcmp(local_2c, PTR_PTR_005e1524)` |
| 0x04 | u16 | record_count = 26 | `local_28`，循环 `local_48 < local_28` |
| 0x06 | u16 | max_id = 25 | `local_26`，存到 this+4，`operator_new(local_26*4+4)` |
| 0x08 | u32 | data_size = 9256 | `local_24`；索引起点 = 文件 + 0xC + data_size |
| 0x0C | 记录 × record_count | 顺序排列 | `local_20/local_40` 游标 |
| 0xC+data_size | 索引 × record_count | 每项 12 字节 | `local_1c` |

记录（顺序读取）：

| 字段 | 类型 | 读取方式 |
|------|------|------|
| kind | u32 | `memcpy(obj+4, p, 4)`；obj = `operator_new(0x1c)` |
| payload | kind==0：0x160 字节；kind==1：0xD8 字节 | `operator_new(0x160/0xd8)` + `memcpy(obj+8, p+4, n)`；下一条 = p+0x164 / p+0xDC |

本文件 26 条全部 kind=0，每条 4+0x160 = 356 字节，共 9256 = data_size。其他 kind 加载函数不拷贝 payload、只前进 4 字节（本文件不出现）。

索引项（12 字节）：

| 偏移 | 类型 | 含义 | 游戏是否读取 |
|------|------|------|------|
| +0 | u32 | 记录的文件绝对偏移（观察值） | 不读 |
| +4 | u32 | 记录字节数 = 356（观察值） | 不读 |
| +8 | u16 | **id**，`*(ushort*)(local_1c + 8 + i*0xc)`，obj+0 = id，表[id] = obj | 读（FUN_0046df60） |
| +10 | u16 | 0（观察值） | 不读 |

本文件 id = 0..25 依次对应第 i 条记录，偏移/大小与顺序遍历的结果完全一致（解析器校验 `matches_walk`）。

### payload 内部（0x160 字节）——加载函数只做 memcpy
加载函数不解释 payload 内部，字段含义由使用它的粒子代码决定，本次任务未追踪。解析器把它按 u32 数组（88 个）输出，不做字段命名。旧版文档里"水平速度/生命周期…"之类的字段名只是对样本数值的猜测，未经代码证实，不要当作结论。

---

## uvlisttable.tb（加载函数 FUN_00467650）

调用链：FUN_00404400（粒子系统初始化，拼路径 `%s\config\prt\uvlisttable.tb`）→ FUN_0046e2b0（包装）→ FUN_00467ac0 → FUN_00467650（读文件）。FUN_00467ac0 随后还读同目录的 `UVLInf.ini`（`[UVL Inf]` 节，`No%d`/`Inf%d`/`Sort%d`，由 FUN_0046ef20 → FUN_0046eb30 登记，仅是编辑器用的说明文字，与 .tb 二进制无关；该 ini 在本机不存在）。FUN_00467c00 是不带 ini 的另一个入口。

this 对象（FUN_00467650 的 `in_ECX`）：+0 u16 max_id，+2 u16 record_count，+4 指针数组（`max_id+1` 项，按 id 索引到记录对象）。释放见 FUN_00467c30 / FUN_00467550。

### 文件布局

| 偏移 | 类型 | 含义 | 读取处（FUN_00467650） |
|------|------|------|------|
| 0x00 | char[4] | 魔数 "UVL\0" | `memcpy(local_24,file,0x10)`；`strcmp(local_24,"UVL")` |
| 0x04 | u16 | record_count = 1119 | `local_20` → this+2；循环 `local_48 < in_ECX[1]` |
| 0x06 | u16 | max_id = 1118 | `local_1e` → this+0；`operator_new(max_id*4+4)` |
| 0x08 | char[4] | 版本 "1.01" | `local_1c..local_19` 逐字节与 `sprintf("%s",DAT_005e147c)`（"1.01"）比较，不等则失败 |
| 0x0C | u32 | data_size = 117709 | `local_18`；索引起点 = 文件 + 0x10 + data_size = 0x1CBDD |
| 0x10 | 记录 × 1119 | 顺序排列 | 游标 `local_44` |
| 0x1CBDD | 索引 × 1119 | 每项 12 字节，到文件末尾（0x1CBDD + 13428 = 131153） | `local_10` |

旧版文档里"约 82 条索引、从 0x18 开始"是错误的；0x10 开始就是第一条记录，索引在文件**末尾**。

### 记录（运行时对象 `operator_new(0x24)`，存入 表[id]）

**记录头（0x18 字节，`memcpy(&local_60, p, 0x18)`）**

| 偏移 | 类型 | 含义 | 对象字段 / 使用处 |
|------|------|------|------|
| +0x00 | u32 | loop_start_frame：播放到最后一帧后重新开始的帧号 | `local_60` → obj[2]；FUN_00468090 `local_20 = obj[2]` |
| +0x04 | u32 | loop_count：0 = 无限循环，N = 播放 N 遍 | `local_5c` → obj[3]；FUN_00468090 `obj[3]-1`/`obj[3]==0` 判断 |
| +0x08 | f32 | width（像素） | `local_58` → obj[0]；FUN_00467d40 `param_2[10] = obj[0]` |
| +0x0C | f32 | height（像素） | `local_54` → obj[1]；FUN_00467d40 `param_2[0xb] = obj[1]` |
| +0x10 | u32 | frame_count | `local_50` → obj[4] |
| +0x14 | u32 | texture_count | `local_4c` → obj[6] |

**帧 × frame_count（每帧 0x18 字节，`memcpy(obj[5], p, frame_count*0x18)`）**

| 偏移 | 类型 | 含义 | 使用处 |
|------|------|------|------|
| +0x00 | f32 | u0 | FUN_00467d40 / FUN_00468090 把前 4 个 dword 拷到精灵状态 `param_2[0..3]` |
| +0x04 | f32 | u1 | 同上 |
| +0x08 | f32 | v0 | 同上 |
| +0x0C | f32 | v1 | 同上 |
| +0x10 | u32 | duration_ms：本帧持续毫秒 | FUN_00468090：`frame[+0x10] < (timeGetTime() - 帧起始时刻)` 则切下一帧（timeGetTime 由 FUN_00466c10 传入） |
| +0x14 | u32 | texture_index：本帧使用的贴图序号 | FUN_00467d40 校验 `< obj[6]`；取 `obj[7][texture_index*0xC]` 为本帧贴图 |

u 的两个值在前、v 的两个值在后（例：4×4 图集记录 1 的第 0、1 帧为 (0,0.25,0,0.25)、(0.25,0.5,0,0.25)）。

**贴图项 × texture_count（`obj[7] = operator_new(texture_count*0xC)`，运行时项 +0 存贴图句柄，文件里没有）**

| 字段 | 类型 | 含义 | 读取 / 使用 |
|------|------|------|------|
| use_mask_name | u32 | 0：`FUN_0051bf10(path, 0)`；非 0：`FUN_0051bf10(path, path)`（第二个参数为 mask 名，与路径相同） | `memcpy(obj[7]+4+i*0xC, p, 4)`；使用于 FUN_00467d40 |
| path_len | u32 | 路径字节数（含结尾 NUL） | `local_6c` |
| path | char[path_len] | 贴图路径，如 `prt01.tga`、`UI\mini game\common\Gpoint.tga` | `operator_new(local_6c)` + `memcpy` |

obj[8]（+0x20）加载时置 0，文件里没有；FUN_00467ff0 对它做递减/清零判断，并在 =1 时释放各贴图（FUN_0051bce0），推测为引用计数。

### 索引项（12 字节）

| 偏移 | 类型 | 含义 | 游戏是否读取 |
|------|------|------|------|
| +0 | u32 | 记录文件绝对偏移（观察值） | 不读 |
| +4 | u32 | 记录字节数（观察值） | 不读 |
| +8 | u16 | **id**：`*(ushort*)(local_10 + 8 + i*0xc)`，第 i 条记录 → 表[id] | 读（FUN_00467650） |
| +10 | u16 | 0（观察值） | 不读 |

第 i 条记录与第 i 条索引按位置配对。本文件 id 恰为 0..1118 各出现一次，索引中的偏移/大小与顺序遍历完全一致。`UVListNo`（FUN_00451ad0 读 ini 的字段）即这里的 id；FUN_00467d40 用 `表[id]` 取记录，id 对应项为空则失败。

### 统计（本文件）
- 1119 条记录，共 2260 帧、1172 个贴图项；贴图项 use_mask_name：0 出现 1122 次，1 出现 50 次；路径全部以单个 NUL 结尾。
- 绝大多数记录 texture_count = 1，最多 7；loop_count 取 0（1052 条）/1（61 条）/10（5 条）；loop_start_frame 除 id 0 外全为 0。
- 帧时长常见 10、77~80、100、150 ms。
- **id 0 是异常记录**：头为 (loop_start=16, loop_count=82, w=h=0, frames=426, textures=0)，426 个"帧"的内容是整数而不是 UV（看起来像编辑器残留的索引数据）。加载函数照常读入；FUN_00467d40 绑定它时会因 `texture_index >= texture_count` 失败，所以游戏不会真正使用 id 0。

---

## 解析器使用

```bash
python prtfmt.py <文件路径>          # 摘要 JSON
python prtfmt.py <文件路径> --full   # 含全部记录
```

验收（已通过）：
- PrtEffTable.tb：remaining_bytes = 0
- uvlisttable.tb：remaining_bytes = 0
- 顺序遍历终点等于头部 data_size 指出的索引起点；索引的偏移/大小与遍历结果一致；id 无重复且 ≤ max_id；均为硬检查，不满足抛 ValueError。
- 剩余字节 = 文件长度 − 遍历终点位置，不由文件内存放的大小推算。

## 关键函数

| 地址 | 用途 |
|------|------|
| 0x00404400 | 粒子系统初始化，依次加载两个 .tb |
| 0x0046df60 | 读取 PrtEffTable.tb |
| 0x0046e2b0 | uvlisttable.tb 加载入口（转调 FUN_00467ac0） |
| 0x00467ac0 | 调 FUN_00467650 读 .tb，再读 UVLInf.ini |
| 0x00467650 | 实际解析 uvlisttable.tb |
| 0x00467c00 | 仅读 .tb 的入口 |
| 0x00467c30 / 0x00467550 | 释放整表 / 释放单条记录 |
| 0x00467cb0 | 清除所有记录的运行时贴图句柄 |
| 0x00467d40 | 把 UV 列表绑定到精灵：加载贴图、校验 texture_index、取首帧（或随机帧） |
| 0x00467ff0 | 引用计数递减并释放贴图 |
| 0x00468090 | 帧推进（时长、循环、随机起始帧） |
| 0x00466c10 | 调 FUN_00468090，传入 timeGetTime() |
| 0x0046ef20 / 0x0046eb30 | 读 UVLInf.ini 并登记说明项（与二进制格式无关） |
| 0x00451ad0 | 读角色/精灵 ini，字段 `UVListNo` = uvlisttable 的 id |

## 未完成
- ~~PrtEffTable 每条记录的 0x160 字节 payload 内部字段含义~~：已完成，见 `docs/particles.md`（解析与模拟器在 `scripts/prteff.py`）。粒子系统只把每个 uvlist 当作"按粒子寿命比例步进的 UV 表"来用，不读 loop_start_frame/loop_count。
- uvlisttable 头部 loop_start_frame/loop_count 与随机起始帧标志（FUN_00467d40 参数 `param_4`）在 UI 精灵中的上层调用语义。
