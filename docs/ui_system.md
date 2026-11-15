# 大富翁8 UI 系统说明

> 分析对象：`RichMan8.exe`（VA 地址，ImageBase 0x400000），`Config\UI\*.ui`（64 个），`Texture\tex000x.dat`，`Config\prt\uvlisttable.tb`，`out\pysrc_clean`。
> 全部为静态分析。把握分三级：**高**＝亲自读过反编译/反汇编并有数据交叉验证；**中**＝读过代码但语义靠上下文推断；**低**＝只有间接线索。
> 函数名以 `out\final\functions.tsv` 为准；本文发现的错误命名见 `out\ui_review\name_fixes.tsv`（152 条，未改动 out\final）。下文提到错名函数时写成“地址 新名（原名）”。

交付物与验收结果

| 交付物 | 结果 |
|---|---|
| `scripts\uifmt.py check` | 64 个文件 / 307,439 字节 / 24,800 行全部按游戏的行文法归类（节 1,442、键值 23,358、注释/空行/被忽略行 0），剩余 0 字节，加解密往返 64/64 逐字节一致；按游戏建树算法 1,378 个控件全部可达（不可达节 0）；62 种键全部能在 exe 的读取函数中找到（“exe 不读的键”0 个）；1,949 处贴图号全部能在 uvlisttable.tb 找到、对应的 260 张贴图全部在 tex0000/tex0001.dat 中；9 种音效号全部在 snd0000.dat 中；6 项反向自检（拼错键名、孤儿节、CRLF、类型不允许的键、大小写）全部能被发现。结论 PASS |
| `scripts\uifmt.py render` | 按游戏的坐标/Z 序/贴图规则把 .ui 画成 PNG；64 个界面的渲染图在 `out\ui_review\render\`（`_sheet0~3.png` 为总览），主菜单、银行 ATM、主界面 HUD 等与游戏外观一致，用来反证格式理解 |
| `out\ui_review\name_fixes.tsv` | 152 条 UI 相关命名修正（地址、旧名、旧置信度、新名、依据） |

---

## 1. 总体结构

```
          Windows 消息                     每帧
CApp::WndProc 0x403AA0 ─► 0x5ACA80 UiMgr_OnWndMsg ──(返回“UI 已消费”存 CApp+0x238)
                                │ 命中测试 0x5AC550 / 焦点 / 悬停 / 拖动
                                ▼
                 控件虚函数（OnClick/OnMouseUp...）─► 控件上挂的回调（按事件索引）
                                                         │
             ┌───────────────────────────────────────────┘
             ▼
   游戏侧回调（例：0x4E7700 UI_OnDialogEvent）─► 场景 0x5E00E8 vf12 ─► 当前事件任务 vf12 / 角色等待状态
CApp 渲染 0x403790：3D 场景 → 0x5AA410 UiMgr_DrawAll（整棵控件树） → 场景 vf2(1) → 文字层 → Present
```

三层（高）：

| 层 | 地址范围 | 说明 |
|---|---|---|
| UI 控件库 | 0x5A6000–0x5C4B40 | 开发商自研、与渲染器无关的控件库（调试字符串为 GBK 简体，例如 0x610818「创建控件失败」）。负责 .ui 解析、控件树、事件分发、绘制调度；所有画图/取图/放音都通过 UiMgr 上的 15 个钩子回调到游戏 |
| 游戏侧桥接 | 0x4EAEE0–0x4ECD00 | 钩子实现：按图号从 uvlisttable 取精灵、按文件名读 RW 纹理、GDI 字形纹理绘字、播放音效；以及界面缩放 0x4ECA70/0x4ECBC0 |
| 游戏界面类 | 0x429xxx–0x515xxx | 三个族：对话框单例（Dlg 族）、前端菜单（Menu 族）、事件任务（Task 族，银行/法院等） |

---

## 2. UI 控件库

### 2.1 全局管理器 UiMgr（`DAT_006570d8`，0x538 字节）

由 0x4EC6C0 UiMgr_Init（原名 UI_InitGraphics）在 CApp 初始化 0x402F50 中创建：`new(0x538)` → 0x5A6BF0 构造，传入 `"%s\texture\prt\"` 作为图片前缀、`DAT_00612c78`（0x402930 静态初始化逐字节写出的 `"rich8ui"`）作为 .ui 解密密钥，然后注册钩子、记下窗口句柄与客户区尺寸。紧接着 0x406C50 加载光标。（高）

| 偏移 | 含义 | 依据 |
|---|---|---|
| +0x00..+0x38 | 15 个后端钩子（见 2.2） | 0x5AC990..0x5ACA70 各写一个偏移 |
| +0x3C | 自定义关键字解析钩子（本游戏未设置） | 0x5A6EE0 中途调用 |
| +0x40 | 自定义控件工厂钩子（未设置） | 0x5A8320/0x5A83C0 |
| +0x58 | 解析 .ui 时的临时 INI 缓冲（加载完即释放） | 0x5AA460 |
| +0x4C0 | 顶层控件链表头（按 Index 降序） | 0x5A8320、0x5A93F0 |
| +0x4C4 | 最近按下左键的控件（激活控件，失活时调 vtable+0x28） | 0x5AD1D0、0x5ACA80 |
| +0x4C8 | 悬停控件 | 0x5ACA80 WM_MOUSEMOVE |
| +0x4CC | 按下中的控件（用于判定单击与按住连发） | 0x5ACA80、0x5AA2F0 |
| +0x4D0 | 正在显示悬停提示的控件 | 0x5AA2F0、0x5AA410 |
| +0x4D4 | 键盘焦点控件（TabIndex>0 才可获得） | 0x5AD1D0、0x5B5930 |
| +0x4D8 | 拖动中的控件 | 0x5ACA80 |
| +0x4E4 / +0x4E8 / +0x4EC | 窗口句柄 / 客户区宽 / 高（构造默认 800×600） | 0x4EC6C0、0x5A6BF0 |
| +0x4F0 | **输入层掩码**：与控件 EnableFlag 相与为 0 则不可命中。光标隐藏时置 0，显示时置 -1 | 0x5AC550；写入只在 0x407600 Cursor_SetVisible |
| +0x4F4 / +0x4FC | .ui 解密密钥 / 按文件名取图的路径前缀 | 0x5A7750 / 0x5A76B0 |
| +0x4F8 | ==1 为“设计模式”（忽略 Enabled/Visible 检查），游戏内无写入 | 多处判断，无写入（中） |
| +0x500 | 有待删除控件（下一帧清理） | 0x4E7850、0x5AA410 |
| +0x50C/+0x510 | 当前鼠标坐标；+0x51C 按键号（1 左、3 右、2 中）；+0x520/+0x524 按下/悬停起始 tick | 0x5ACA80 |

### 2.2 后端钩子（UI 库 → 游戏）

| 偏移 | 注册函数 | 游戏实现 | 作用 | 把握 |
|---|---|---|---|---|
| +0x00 | 0x5AC990 | 0x4EB810 UiHook_FillRect | (x0,y0,x1,y1,ARGB,mode)：mode 1 画实心矩形（RwIm2D 四顶点），否则画 4 条边 | 高 |
| +0x04 | 0x5AC9A0 | 0x4EB6C0 DrawLine | 画线（边框样式） | 高 |
| +0x08 | 0x5AC9B0 | 0x4EB040 UiHook_DrawPicById | 按贴图号取精灵（0x4EAF20）并绘制到控件矩形 | 高 |
| +0x0C | 0x5AC9C0 | 0x4EB340 UiHook_DrawPicByName | 按文件名读 RW 纹理（0x4EB100）绘制 | 高 |
| +0x10 | 0x5ACA20 | 0x4EB610 UiHook_GetPicSizeById | 取贴图像素尺寸（PicType=1 时控件取此尺寸） | 高 |
| +0x14 | 0x5ACA30 | 0x4EB670 UiHook_GetPicSizeByName | 同上，按文件名 | 高 |
| +0x18 / +0x1C | 0x5ACA50 / 0x5ACA60 | 0x4EC400 / 0x4EC4A0 | CheckType=pic 时按像素 alpha>0x32 判定命中 | 高 |
| +0x20 | 0x5ACA40 | 0x4EBC90 UiHook_TextLength | 计算显示长度，跳过 `~[...]` 控制标记 | 中 |
| +0x24 | 0x5AC9D0 | 0x4EBAF0 UiHook_DrawText | 画文字（GDI 字形纹理，见 4.3） | 高 |
| +0x28 | 0x5AC9F0 | 0x4EC4F0 | free | 高 |
| +0x2C | 0x5ACA00 | 0x4EC530 UiHook_ReleasePic | 换图时释放控件缓存的纹理 | 中 |
| +0x30 | 0x5ACA10 | 0x4EC680 UiHook_PlaySound | 播放音效（InSound/DownSound/ChangeSound 等，0x5AF…/0x5BE… 中以 `(*(UiMgr+0x30))(+0x268)` 调用） | 高 |
| +0x34 | 0x5AC9E0 | 0x4EBC80（空函数） | UseCtlText 控件的提示文字绘制（游戏未实现） | 高 |
| +0x38 | 0x5ACA70 | 0x4EBEC0 UiHook_ParseRichText | 把含 `~[` 标记的文字拆成分段链表 | 中 |

### 2.3 控件类型

类型名由 0x5A6EE0 UiLib_ParseKeyword（原名 UiLib_ControlType_Parse）转成类型号，0x5A8470 按类型号 `new` 对应类。0x5A6EE0 其实是**通用关键字表**，Enabled/Visible/ALignH/CheckType/Kind 等键也用它：`top=3, true=1, false=0, left=0, mid=1, right=2, rect=0, bottom=4, pic=1, HScroll=1, VScroll=0, null=0`，之后才是类型名；不认识的字符串返回 0。（高）

| 类型号 | Type= | vtable | 对象大小 | LoadFromIni（槽 +0x78） | SaveToIni（槽 +0x70） | 文件中数量 |
|---|---|---|---|---|---|---|
| — | （基类 CUiWidget） | 0x5F5580 | — | 0x5AA7D0 | 0x5AB7F0 | — |
| 0 | plane | 0x5F5D60 | 0x1D8 | 0x5AA7D0 | 0x5C3960 | 467 |
| 1 | label | 0x5F5778 | 0x1F0 | 0x5B6EE0 | 0x5B70C0 | 536 |
| 2 | title | 0x5F5C10 | 0x1F0 | 0x5B6EE0 | 0x5C3040 | 7 |
| 3 | button | 0x5F5CB8 | 0x1FC | 0x5C3710（先调 0x5B6EE0） | 0x5C37D0 | 348 |
| 4 | edit | 0x5F56D0 | 0x240 | 0x5B5930 | 0x5B5C70 | 2 |
| 5 | radiobutton | 0x5F5AC0 | 0x1F0 | 0x5C1E40（Ghidra 未建函数） | 0x5C21D0 | 0 |
| 6 | checkbox | 0x5F5B68 | 0x1EC | 0x5C2B00 | 0x5C2CF0 | 0 |
| 7 | groupbox | 0x5F5A18 | 0x1DC | 0x5C1850（未建函数） | 0x5C18C0 | 0 |
| 8 | scrollbar | 0x5F5970 | 0x344 | 0x5BEFD0 | 0x5C01C0 | 1 |
| 9 | progress | 0x5F58C8 | 0x1EC | 0x5BB8A0 | 0x5BB9E0 | 0 |
| 10 | listbox | 0x5F5820 | 0x274 | 0x5BA3C0 | 0x5BAD20 | 6 |
| 11 | slider | 0x5F5628 | 0x278 | 0x5B00A0 | 0x5B07F0 | 11 |

依据：0x5A8470 的 switch（类型号→`operator_new` 大小→构造函数→构造函数写的 vtable），以及各类 vtable 第 28/30 槽（槽 30 由 0x5AA680 以 `vtable+0x78` 调用来读键；槽 28 的函数都以写 `Type=xxx` 开头，是保存）。（高）
注意：现有命名里 0x5AE0F0–0x5B07F0 一带的 `u_UiLib_Scrollbar_*` 实际属于 **slider**（类型 11，vtable 0x5F5628），而名为 `UiLib_Slider_*` 的 0x5BDEA0–0x5BEFD0 属于 **scrollbar**（类型 8，vtable 0x5F5970），两者在旧命名中互换了。

### 2.4 控件结构体（基类部分，高/中）

| 偏移 | 含义 | 来源 |
|---|---|---|
| +0x00 | vtable | 构造 0x5A71A0 |
| +0x04 | ID（键 `ID`） | 0x5AA7D0；0x5A89D0 按它查找 |
| +0x08 | 名字（= .ui 节名） | 0x5A7810；0x5A8940 按它查找（不区分大小写） |
| +0x0C | 提示文字 HintText；+0x20 提示字色；+0x34 提示底色；+0x48 提示字号 | 0x5AA7D0、0x5A9F40 |
| +0x94 | BorderType（5=贴图模式） | 0x5A7A10 |
| +0xA0 | 当前底色（BColor 四态之一） | 0x5AA190 |
| +0xB4 / +0xB8 / +0xBC | 当前贴图号 / 第二套贴图号 / 当前贴图文件名 | 0x5A7EC0/0x5A7F10/0x5A7AD0；0x5AA190 绘制 |
| +0xC0..+0xEC | 四态贴图号（Normal/MouseOn/MouseDown/Disable 及 *2）与四态文件名 | 0x5A7F40 等 |
| +0xF0 | PicType | 0x5A9510 |
| +0xF8 | **控件类型号**（如 button 构造 0x5C3140 写 3） | 0x5ADE00 按它批量挂回调 |
| +0xFC/+0x100/+0x104/+0x108 | Left/Top（相对父）/Width/Height | 0x5A8F00/0x5A9060/0x5A91F0/0x5A92F0 |
| +0x10C/+0x110 | 绝对屏幕坐标 | 0x5A8E50 |
| +0x114 | Index（Z 序） | 0x5A93F0 |
| +0x118 | TabIndex | 0x5AA7D0 |
| +0x11C | CheckType（0 矩形，1 按贴图 alpha） | 0x5A94E0、0x5AC550 |
| +0x124 | EnableFlag（输入层位掩码，文件中全部 -1） | 0x5AC550 |
| +0x128 / +0x129 / +0x12A / +0x12B / +0x12C | Enabled / Visible / Ignore（鼠标穿透） / MoveOut / CanDrag | 0x5A9780/0x5A9A70/0x5AA7D0 |
| +0x12F | 待删除标记 | 0x4E7850、0x5ADD00 |
| +0x138 / +0x13C / +0x140 | 父 / 第一个子 / 下一个兄弟 | 0x5A83C0、0x5A89D0 |
| +0x158 / +0x15C / +0x168 | MousePressTime / MousePressSpaceTime / MouseStopTime（ms） | 0x5AA7D0、0x5AA2F0 |
| +0x174..+0x1D4 | 25 个事件回调指针（见 2.6） | 0x5AD830/0x5AD990 |

### 2.5 控件虚表（每类 42 槽，0xA8 字节；高）

槽语义由 0x5ACA80（输入分发）、0x5AA2F0（绘制循环）、0x5A9780/0x5A9A70 中的调用位置确定；基类默认实现都只是“检查 Enabled/Visible 后调用对应回调”。

| 槽(偏移) | 含义 | 基类实现 → 回调字段（事件索引） |
|---|---|---|
| 0~2 | 空 | 0x546700 |
| 3 (+0x0C) | 重置缓存 | 0x5ADCD0 |
| 4 (+0x10) / 5 (+0x14) | 被隐藏 / 被显示（SetVisible 状态变化时） | 0x5ADA80→+0x174(0x0F) / 0x5ADA90→+0x178(0x10) |
| 6 (+0x18) | 销毁通知 | 0x5ADAA0→+0x17C(0x11) |
| 7 (+0x1C) | 绘制自身 | 0x5AA190 UiWidget_DrawDefault |
| 8 (+0x20) | — | 0x5ADAB0→+0x188(0x14) |
| 9 (+0x24) / 10 (+0x28) | 被左键激活 / 失去激活 | 0x5ADAC0→+0x18C(0x17) / 0x5ADAD0→+0x190(0x18) |
| 11 (+0x2C) / 12 (+0x30) | 获得 / 失去键盘焦点 | 0x5ADAE0→+0x194(0x15) / 0x5ADAF0→+0x198(0x16) |
| 13 (+0x34) | 鼠标按下(按键号) | 0x5AD1D0→+0x19C(0) |
| 14 (+0x38) | 鼠标抬起 | 0x5AD320→+0x1A0(1) |
| 15 (+0x3C) | **单击**（按下与抬起是同一控件） | 0x5AD380→+0x1C4(10) |
| 16 (+0x40) | 按住连发 | 0x5AD400→+0x1A4(2) |
| 17 (+0x44) | 双击 | 0x5AD3C0→+0x1A8(3) |
| 18 (+0x48) | 鼠标移动 | 0x5AD440→+0x1AC(4) |
| 19 (+0x4C) / 20 (+0x50) | 鼠标进入 / 离开 | 0x5AD460→+0x1B0(5) / 0x5AD490→+0x1B4(6) |
| 21 (+0x54) / 22 (+0x58) | 拖动 / 拖动结束 | 0x5AD4C0→+0x1B8(7) / 0x5AD690→+0x1BC(8) |
| 23 (+0x5C) | 悬停计时（每帧） | 0x5AD6D0→+0x1C0(9) |
| 24..27 (+0x60..+0x6C) | KeyDown / KeyUp / Char / 按键连发 | →+0x1CC(11) / +0x1D0(12) / +0x1C8(14) / +0x1D4(13)；KeyDown 中 Tab 键切换焦点 0x5ADB00 |
| 28 (+0x70) | SaveToIni | 见 2.3 |
| 29 (+0x74) | 删除自身 | 0x5A91C0 |
| 30 (+0x78) | LoadFromIni(节名) | 见 2.3 |
| 31..41 | 取当前贴图、SetText(+0x8C)、SetFontSize(+0x90)、复制布局(+0xA4) 等 | — |

旧命名把多个槽的名字搞错了（例如 0x5AD380 名为 OnMouseEnter 实为单击，0x5AD6D0 名为 OnMouseWheel 实为悬停计时；鼠标滚轮 0x20A 在 0x5ACA80 中根本不处理），已列入 name_fixes.tsv。

### 2.6 回调注册（游戏挂事件的方式；高）

* 0x5AD830 UiWidget_SetHandler（原 RW_Callback_SetByIndex）：索引 4..9、0x0F..0x18。
* 0x5AD990 UiWidget_SetHandler2（原 RW_Callback_SetRenderByIndex）：索引 0..3、10..14。
* 0x5ADEA0 / 0x5ADF10（`(索引, 函数, 类型号, 根控件)`）：给根控件下**所有某类型**的控件挂同一个回调（比较的是 +0xF8 类型号，不是 ID）。例：银行 0x4C1590 `u_RW_Callback_Register2(10,Bank_OnButton,3,root)` = 给所有 button 挂单击。

| 索引 | 事件 | 索引 | 事件 |
|---|---|---|---|
| 0 | 按下 | 10 | **单击** |
| 1 | 抬起（对话框通用处理 0x4E7700 挂在这里） | 11 / 12 | KeyDown / KeyUp |
| 2 | 按住连发 | 13 | 按键连发 |
| 3 | 双击 | 14 | 字符输入 |
| 4 | 鼠标移动 | 0x0F / 0x10 | 被隐藏 / 被显示 |
| 5 / 6 | 进入 / 离开（大量界面用来做悬停高亮） | 0x11 | 销毁 |
| 7 / 8 | 拖动 / 拖动结束 | 0x12 / 0x13 | **绘制前 / 绘制后自定义绘制**（在控件里画 3D 角色、卡片，例如银行 NPC 0x4C1590 挂 0x13） |
| 9 | 悬停计时 | 0x15 / 0x16 | 获得 / 失去焦点 |

回调签名：`bool fn(CUiWidget* w, int button)`（以 0x4E7700 为例：`param_1` 为控件，`param_2==1` 为左键）。

### 2.7 创建与销毁流程（高）

1. 界面类拼路径 `%s\config\UI\<名>.ui`，调用 0x5AA460 UiMgr_LoadUiFile(path, 1)。
2. 0x5C4730 逐字节解密读入 INI 缓冲（行文法见 3.2）；取 `[Head] head=` 为根节名，用根节的 Type/Left/Top/Width/Height 经 0x5A8320 创建顶层控件（插入 UiMgr+0x4C0 链表），调用其 `vtable+0x78` 读其余键。
3. 0x5AA680 UiMgr_BuildChildrenFromIni：从最后一节向前扫描，`Parent` 等于当前父名（不区分大小写）的节就用 0x5A83C0 创建子控件、读键、再递归；计数器为“节数−2”，减到 0 停止。没有被任何父节匹配的节**不会被创建**（本游戏 0 个）。
4. 每个控件读键时 `Index` 键调用 0x5A93F0 把自己按 Index 插回兄弟链表（表头 Index 最大，相等时后插入者在前）。
5. 返回根控件后，界面类按分辨率缩放（3.6），再用 FindByName/FindById 取控件、挂回调、设置文字。
6. 销毁：0x4E7850 UI_DestroyDialog 给根控件置 +0x12F 并置 UiMgr+0x500，随后 0x5ADD00 递归删除所有被标记的控件（`vtable+0x74`）。游戏退出时 0x4E7450 UI_DestroyAllDialogs（原名 UI_InitializeAllDialogs）逐个卸载对话框单例。

### 2.8 每帧绘制（高）

CApp 帧循环 0x4035E0（约 15 ms 限帧）→ 0x403790：开始相机 → 当前场景 `vtable+8(0)` → 3D 场景 → **0x5AA410 UiMgr_DrawAll** → 场景 `vtable+8(1)` → 0x40DD90 文字层 → Present。

0x5AA410：若有待删控件先清理；从 UiMgr+0x4C0 调 0x5AA2F0 画整棵树；最后若有悬停提示控件，调 0x5A9F40 画提示框（HintBColor 底、HintFColor 字、黑边，位置在控件下方并夹在屏幕内）。

0x5AA2F0 UiWidget_DrawTree 的顺序：先递归画“后面的兄弟”，再画自己，再进入子链表。因为兄弟链表按 Index 降序，结果是**Index 小的先画、大的后画（在上面）**，子控件总画在父控件之上。不可见（Visible=0）的控件连同子树跳过。同一循环里还处理两件事：悬停超过 MouseStopTime 毫秒则把该控件设为提示控件；按住超过 MousePressTime 后每隔 MousePressSpaceTime 调一次“按住连发”。每个控件绘制前后各调用一次回调 0x12/0x13。

控件默认绘制 0x5AA190：BorderType<5 时用 FillRect 填 BColor，BorderType 1~4 再画线框；BorderType=5 时按当前贴图号调 DrawPicById，图号<0 但有文件名则调 DrawPicByName。label/button 的文字由各类自己的绘制槽另行绘制（中，未逐一阅读）。

### 2.9 输入分发（高）

CApp WndProc 0x403AA0 在处理任何消息前先调 0x5ACA80 UiMgr_OnWndMsg，返回值（UI 是否消费）存入 CApp+0x238（`in_ECX[0x8e]`），游戏自己的鼠标处理据此决定是否响应。

* WM_KEYDOWN/KEYUP/CHAR：只发给键盘焦点控件（+0x4D4），带按键连发。
* WM_MOUSEMOVE：0x5AC550 命中测试（从顶层链表头开始、Index 大者优先、递归子树；跳过不可见、Ignore=true、EnableFlag 与 UiMgr+0x4F0 相与为 0 的控件；CheckType=pic 时再按贴图像素 alpha 判定），悬停控件变化时依次调旧控件“离开”、新控件“进入”；有拖动控件时调“拖动”。
* 按下（左/右/中 = 1/3/2）：SetCapture，记录按下控件，调“按下”；左键按下还会把控件及其所有祖先按 Index 重新插入链表（被点的窗口提到同级最前），并处理焦点。
* 抬起：ReleaseCapture，调悬停控件的“抬起”，若与按下控件相同再调“单击”；结束拖动。
* 双击：调“双击”。
* 光标隐藏（0x407600 Cursor_SetVisible(0)）时 UiMgr+0x4F0=0，所有控件都不可命中——这就是游戏在动画/AI 行动期间锁住界面的方式。（高）

---

## 3. .ui 文件格式

解析器：`scripts\uifmt.py`（`check` 全量验收，`dump` 打印树，`render` 画图，`decode/encode` 加解密）。

### 3.1 加密与编码（高）

* 逐字节 `明文 = (密文 − key[i mod 7]) & 0xFF`，`key = "rich8ui"`，i 从文件第 0 字节计（0x5C4730；密钥来自 0x402930）。与 `scripts\rm8crypt.py` 一致。
* 明文按字节处理，exe 不做任何代码页转换；文字显示时才经 `MultiByteToWideChar(CP_ACP)`（见 4.3）。
* 64 个文件全是 LF 换行、无 Tab、无注释、无空行（最后一行也以 LF 结束）。
* 只有 9 行含非 ASCII 字节，全是设计时留下的占位 CapText，**编码是 GBK**（如 rob.ui 的「核子飞弹」、MagicHouse.ui 的「魔法1大大大…」），按 Big5 解码会变成乱码（`out\config_utf8\UI` 里看到的「瞄赽滄粟」等）。这些文字运行时都会被代码覆盖。uifmt 按字节往返，不受影响。

### 3.2 行文法（0x5C4730 / 0x5C4440，高）

1. 逐字节读，`'\t'` 字节直接丢弃；遇 `'\n'` 或 EOF 结束一行（`'\r'` 不会去掉，会成为节名/键/值的一部分——CRLF 文件会让节头失效）。
2. 行首 `;` → 注释。
3. 行首 `[` 且行末 `]` → 节头，节名为中间部分；同名节（不区分大小写）合并。
4. 空行：若已 EOF 则结束，否则跳过。
5. 其他行找第一个 `=`：左边为键（不去空格，不能为空），右边为值（可为空）；无 `=` 的行忽略。键按 `__stricmp` 不区分大小写，同节重复键**后者覆盖前者**（0x5C3CB0）。
6. 第一个节头之前的键值无效（节名为空时 0x5C4390 不写入）。

### 3.3 结构

* `[Head]` 节只有 `head=<根控件节名>`（本游戏 59 个为 `Plane`、3 个 `PLANE`、各 1 个 `Plane2`/`cardShop`）。
* 其余每节一个控件，节名即控件名（代码用它 FindByName）。树结构完全由各节的 `Parent=` 决定，根节写 `Parent=null`（null 只是不匹配任何节名）。最大嵌套深度 4。
* 创建顺序：倒序扫描节（见 2.7），所以**同 Index 的兄弟中，文件里靠前的节画在上面**。

### 3.4 字段表

公共字段（所有类型，0x5AA460/0x5AA680 建树时读前 6 个，其余由 0x5AA7D0 读）：

| 键 | 类型 | 含义 | 文件中出现次数 |
|---|---|---|---|
| Type | 关键字 | 控件类型（2.3） | 1378 |
| Left / Top / Width / Height | 整数(_atol) | 相对父控件的位置与尺寸，基准 1024×768 | 1378 |
| Parent | 字符串 | 父节名，根为 null | 1378 |
| ID | 整数 | 控件 ID，代码 FindById 用；对话框基类 vf2~vf9 都以 ID 指定控件 | 1378 |
| EnableFlag | 整数 | 输入层位掩码（与 UiMgr+0x4F0 相与）；全部为 -1 | 1378 |
| BorderType | 整数 | 0 无边框；1~4 线框样式（0x5A9CE0）；5 贴图模式 | 1378（0:256, 1:5, 2:4, 3:70, 5:1043） |
| PicType | 整数 | 1＝控件宽高改用贴图实际尺寸（经钩子 +0x10/+0x14 查询，此时 Width/Height 被忽略，SetWidth 也不生效）；0＝用 Width/Height | 1378 |
| NormalPic / MouseOnPic / MouseDownPic / DisablePic | 贴图号 | 常态/悬停/按下/禁用贴图，= uvlisttable.tb 的 id（4.1）；-1 为无 | 671/574/570/102 |
| NormalPic2 … DisablePic2 | 贴图号 | 第二套四态贴图（选中态等） | 0 |
| NormalPicName … DisablePicName | 字符串 | 四态贴图文件名（相对 texture\prt\） | 0 |
| HintText / HintFontSize / HintBColor / HintFColor | 字符串/整数/颜色/颜色 | 悬停提示（文字/字号默认 12/底色/字色） | 0/0/1378/1378 |
| CapText | 字符串 | 标题文字（经 vtable+0x8C SetText）；空则取空串 | 292 |
| CapFontSize / CapFontType | 整数 | 字号（16/14/32）/字体类型 | 491/0 |
| CapFontFColor 及 Normal/MouseIn/MouseDown/Disable 前缀 | 颜色 | 四态文字颜色（无前缀者一次设置四态） | 0/5/0/6/33 |
| CapFontBColor 及四态前缀 | 颜色 | 四态文字底色 | 0/1/0/0/0 |
| BColor 及 NormalBColor/MouseInBColor/MouseDownBColor/DisableBColor | 颜色 | 四态底色（BorderType<5 时填充） | 412/0/0/0/0 |
| Enabled | 布尔 | 默认 true；false 时用 DisablePic，没有 DisablePic 就不画贴图，且不响应鼠标 | 448（全为 false） |
| Visible | 布尔 | 默认 true；false 时不画、不可命中，等代码显示 | 73（全为 false） |
| Ignore | 布尔 | 鼠标穿透 | 75 |
| Index | 整数 | 同级 Z 序（负数按 0） | 928 |
| TabIndex | 整数 | >0 可获得键盘焦点；edit 加载时若祖先都可见可用则直接获得焦点 | 3 |
| MoveOut | 布尔 | true 允许超出父控件；false 时 SetLeft/SetTop 把控件夹在父控件内 | 31 |
| CheckType | 关键字 | rect / pic（按贴图 alpha 命中） | 429（全为 pic） |
| CanDrag / Attract / EdgeSize | 布尔/布尔/整数 | 可拖动 / 拖动吸附 / 吸附距离 | 1/0/0 |
| MousePressTime / MousePressSpaceTime / MouseStopTime | 整数(ms) | 按住连发首延迟 / 间隔 / 悬停提示延迟 | 0 |
| UseCtlText | 布尔 | 文字长度用钩子 +0x20 计算（处理 `~[` 标记） | 0 |

类型专属字段（只列在本游戏出现的类型；完整表在 uifmt.py 的 SCHEMA）：

| 类型 | 读取函数 | 键（出现次数） |
|---|---|---|
| label / title / button | 0x5B6EE0 | ALignH（left/mid/right，431）、ALignV（top/mid/bottom，461）、MultiLine（48）、Space 行距（1）、AutoSize（0） |
| button | 0x5C3710 | DownSound（257）、InSound（262）、OutSound（0）：音效号，snd0000.dat 的 ID，经钩子 +0x30 播放 |
| edit | 0x5B5930 | ALignH、MaxRow、MaxCol（2）、ShowTime、HideTime（光标闪烁）、PassWordChar、TextType（2，=1）、CheckColor（2）、Paste、ImeType、ReadOnly；CapText 作为初始文字 |
| listbox | 0x5BA3C0 | NorBColor/InBColor/DownBColor（条目三态底色，各 6）、Nor/In/Down/DisFontColor、*BPic/*BPic2/*BPicName、MaxRow、MultiSelect、ShowPic、Space、Down/In/OutSound |
| slider | 0x5B00A0 | Kind（HScroll/VScroll，9）、MidSize（滑块尺寸，11）、LineWidth、Max（10）、Min（4）、SmallChange（12）、LargeChange（10）、Btn* 滑块四态贴图与 BtnBorderType/BtnPicType（各 11）、InSound（9）、DownSound/OutSound/ChangeSound |
| scrollbar | 0x5BEFD0 | Kind、MidSize、SideSize、Max、Min、Value、PageSize（1）、SmallChange、LargeChange、Btn0*/Btn1*/Btn2*（两端箭头与滑块），以及 exe 里的笔误键 `Btn2MouseDownPic22` |

值类型：整数为 MSVC `_atol`；颜色为 0x5A6DE0：以小写 `0x` 开头按十六进制（忽略非十六进制字符），否则 `atof` 取整，ARGB；布尔与关键字经 0x5A6EE0（>0 为真）。

### 3.5 分辨率缩放（高）

.ui 坐标以 1024×768 为基准，加载后由界面类缩放，系数都是 `init.txt 宽度 / 1024`，**X、Y 用同一个系数**：

* 0x4ECBC0 UI_ScaleDialogPosition（普通对话框 0x4E7760、银行等任务）：只移动根控件，尺寸不变，使中心按比例移动：`x = Left·s − (W − W·s)/2`，`y = Top·s − (W − W·s)/2`（y 公式也用宽度 W，原版笔误）。
* 0x4ECA70 UI_ScaleWidgetTree（0x4E7F50：gmaindlg、loading、playerinfo、balance；SpeakAside 用 (s, 1.0)）：递归把每个控件的 Left/Top/Width/Height 乘系数并四舍五入（0x46E940）。

非 4:3 分辨率下 Y 方向会错位，这正是 RichMan8Fix 挂钩 0x4ECA70、0x46E940、0x5A8F00/0x5A9060/0x5A91F0/0x5A92F0（SetLeft/SetTop/SetWidth/SetHeight）、0x5A9510（SetPicType）的原因（与 exe_internals.md 第 5 节的挂钩地址一一对应；中——挂钩目的为推断）。

### 3.6 验收方法

`python -I scripts\uifmt.py check` 的检查项：文件逐行归类后无未归类字节；逐行拼回再加密与原文件逐字节相同；按 3.2/2.7 复刻建树后所有节可达；每个键都在“建树键 + 0x5AA7D0 + 该类型 LoadFromIni”读取的键集合中（键集合是从这些函数的字符串引用整理的，不是从文件里归纳的）；贴图号/音效号与资源包交叉核对；最后用 6 个构造的坏样本做反向测试。数字见文首。

---

## 4. 资源来源

### 4.1 贴图（高）

* `*Pic=N` 的 N 是 **`Config\prt\uvlisttable.tb` 的记录 id**：钩子 0x4EB040/0x4EB610 → 0x4EAF20 以图号为键缓存精灵 → `Sprite_Init(…, id)` 0x461E70 → 0x467D40 以 `ECX=0x63E838`（0x46E2B0 读 uvlisttable.tb 的同一对象）取记录。记录给出贴图文件、UV 帧和像素宽高（格式见 prt_format.md）。
* 贴图文件路径相对 `texture\prt\`（UiMgr 前缀），实际在 `Texture\tex0000.dat` 的 `prt\ui\inside\`、`prt\ui\outside\` 等目录；`tex0001.dat` 是补丁包，覆盖其中 2 张（`j205_1_1.tga`、`j238.tga`）。本游戏 .ui 共引用 532 个图号、260 张贴图，全部存在。
* `*PicName` 按文件名直接读 RW 纹理（0x4EB100），本游戏 .ui 未使用，但代码运行时会用（如 0x4E7910 SetPicName，选角色头像 `npc\%s_12`）。
* 很多“文字”其实是图片：例如 exit/supply/cover 三个确认框的问题文字分别是 `j232.tga`「是否退出遊戲？」、`j231.tga`「是否托管？」、`j238.tga`「是否覆蓋存檔？」。

### 4.2 光标与音效（高）

* 光标：0x406C50 循环 0..63 用 `LoadImageA(…, LR_LOADFROMFILE)` 读相对路径 `cursor\s_%03d_0.ani` / `_1.ani`（游戏目录下的 `Cursor\` 文件夹，35 个；tex0000.dat 的 `cursor\` 是同名副本）。WndProc 在 WM_MOUSEMOVE 时 SetCursor(光标表[光标号][套号])；左键按下时把 1 号光标切成 2 号、抬起再切回（0x403AA0）。
* 音效：控件的 InSound/DownSound 等经钩子 0x4EC680 → CAudioMgr_PlaySample，ID 即 `Sound\snd0000.dat` 的条目号（本游戏用到 11、111、112、114、117、118、136、146、147）。

### 4.3 文字渲染与编码（高）

* 不是位图字体文件，而是**运行时用 GDI 把字形画进纹理**：0x4EBAF0 取字体名 = string.bin 第 22 条「新細明體」，`Font_GetOrCreate(名, 字号)` 0x417950 按 (名, 字号) 缓存字体对象；0x418370 建 24 位 DIBSection，0x418D90 `CreateFontIndirectA`（`lfCharSet = 0x88 CHINESEBIG5_CHARSET`、weight 100、PROOF_QUALITY），另建一个 "SYSTEM" 字体专门画 A–Z/a–z；0x418B40 每个字符 `TextOutW` 到 DIB，再把像素复制进 256×256 的 RW 光栅页（字形缓存 0x4186C0 按字符码分页），最后以纹理四边形绘制。
* 编码：游戏文本（string.bin、.ui）都是 Big5 字节串，绘制前由 0x4EBAF0 调 `MultiByteToWideChar(CP_ACP, …)` 转 UTF-16。也就是说原版依赖系统 ANSI 代码页为 950，在简体系统上会乱码——RichMan8Fix 通过 IAT 挂钩 `MultiByteToWideChar` 和 `CreateFontIndirectA`（exe_internals.md 第 5 节）解决的就是这个问题。（挂钩目的为推断，中）
* `~[...]` 是文字里的控制标记（0x4EBC90 计算长度时跳过，`~[[` 表示字面量 `~[`，含 `P` 参数的项占 2 个字宽）；具体渲染效果未逐一核对（中）。

---

## 5. 游戏侧界面框架

### 5.1 对话框族（Dlg，函数静态单例；高）

每个界面类有一个 `GetInstance`（首次调用时写 vtable 并 `atexit` 注册析构），对象 +4 保存根控件。基类虚表（以 ginfo 0x5E2138 为例）：

| 槽 | 地址 | 作用 |
|---|---|---|
| vf0 | 0x502930 | 析构 |
| vf1 | 0x4E7760 CUiDialog_Load / 0x4E7F50 CUiDialog_LoadScaled / 0x509260（菜单版） | 用 vf11 的名字加载 `%s\config\UI\%s.ui`，缩放（3.5），调 vf10 |
| vf2(text,id) | 0x4E78B0 | 显示对话框并设置某控件文字 |
| vf3(name,id) / vf4(pic,id) | 0x4E7910 / 0x4E7A10 | 设置控件贴图（文件名 / 图号） |
| vf5(id) | 0x4E7AE0 | FindById |
| vf6(id) | 0x4E7BE0 **Show**：未加载则先 vf1；id=-1 显示整个对话框 |
| vf7(id) | 0x4E7B60 **Hide** |
| vf8(id,b) / vf9(id,b) | 0x4E7C80 / 0x4E7CD0 | SetEnabled / SetIgnore |
| vf10 | 各类自己（基类 0x49F230 空） | 加载后绑定回调 |
| vf11 | 各类自己 | 返回界面名（= .ui 文件名） |

没有通用的“界面栈”：对话框是全局单例，`Show` 只是把根控件设为可见；同时可见的多个对话框按顶层链表 Index 叠放，最近被点击的提到前面（2.9）。

### 5.2 模态对话框与回合流程（高）

* 0x4883C0 Role_OpenModalUi(kind, param, cursor)：0x80 okcancel、0x81 business、0x82 dice、0x83 rob（列出 param 角色的 8 张卡）、0x84/0x85 chsplayer（列出可选对手头像）、0x86 exit、0x87 supply、0x88 cover。打开后记录 `角色+0x21C=kind, +0x220=param, +0x224=0`，把角色状态 `+0x1B8` 设为 **7（等待界面）**，显示光标。
* 这些对话框在 vf10 里把按钮的“抬起”（索引 1）挂到 0x4E7700 UI_OnDialogEvent：左键抬起且仍在该控件上 → 调用 `g_GameState`（场景对象，vtable 0x5E00E8）的 vf12(控件ID, 1)。
* 0x4A2E10 场景 vf12：若有事件任务（场景+0x34）则转给任务的 vf12；否则找到状态为 7 的角色，写 `角色+0x228 = 控件ID, +0x224 = 值`，角色状态机下一帧读取结果继续流程。
* 场景每帧 0x4A17B0 调当前事件任务的 vf1（Update）；返回值最高位为 1 表示任务结束，进入 0x4A48D0 Game_EndTurn。鼠标/键盘也由场景 vf6~vf9 转发给任务。

### 5.3 事件任务族（Task；高）

走到地图格子或触发事件时调用 0x4A4160 Game_StartEventTask（原名 Game_UseCard）→ 0x4B1BC0 CreateGameObject(事件号)，结果挂在场景 +0x34。任务基类 vtable 0x5DFF88（16 槽），带界面的任务 17 槽，vf16 加载 .ui。下表由脚本 `out\ui_review\factory_table.py` 从 switch 自动生成（构造函数 → 写入的 vtable → vf16 → 加载的 .ui）：

| 事件号 | 构造函数 | vtable | vf1 Update | vf16 | 界面 |
|---|---|---|---|---|---|
| 1 卡片商店 | 0x4B3C60 | 0x5E4A10 | 0x4E6240 | 0x4E6180 | cardShop.ui |
| 2 问答点 | 0x4B2AE0 | 0x5E46F8 | 0x4BD610 | 0x4BD5F0→0x4BD1F0 | AskPoint.ui |
| 3 卡片点 / 4 公园 | 内联 | 0x5E46B0 / 0x5E4668 | — | 0x4B1750 / 0x4B1890 | 无 .ui |
| 5 银行（99：构造参数 1，禁用 loan 控件，中） | 0x4B2C60 | 0x5E4740 | 0x4C1C40 | 0x4C1590 | bank.ui |
| 6 乐透 | 0x4B2DE0 | 0x5E4788 | 0x4C7790 | 0x4C71A0 | letou.ui |
| 10/11/12 点券点 50/30/10 | 内联（+4 写 0x32/0x1E/10） | 0x5E4620 | — | 0x4BC990 | 无 |
| 0x0D 魔法屋 | 0x4B2F60 | 0x5E47D0 | 0x4CB580 | 0x4CB400→0x4CAAF0 | magicHouse.ui |
| 0x0E 法院 | 0x4B3260 | 0x5E4860 | 0x4D5A60 | 0x4D4DE0 | court.ui |
| 0x0F 电力公司 | 0x4B33E0 | 0x5E48A8 | 0x4D9180 | 0x4D8720 | powerCo.ui |
| 0x10 点券中心 | 0x4B3610 | 0x5E48F0 | 0x4DE640 | 0x4DDA70 | ticketCenter.ui（内含 go 类 0x5E2538 的掷骰） |
| 0x11 | 0x493820 | 0x5E20A8 | 0x4B4690 | 0x4B4170 | 无 .ui 类文件的任务；新闻 TV 场景用 news.ui（docs/news.md，已实现） |
| 0x12~0x17 六个小游戏 | 0x50DD20 | 0x5E88E0 | 0x50ECD0 | 0x50DDD0 | 由小游戏 DLL 加载 `lg\lg_intro.ui` 等 |
| 0x50 / 0x51 拍卖（推测为普通 / 破产拍卖，string 4309「破產拍賣地產」，中） | 0x4B3910 | 0x5E4980 | 0x4E4020 | 0x4E2FC0 | vendue.ui |
| 0x5A / 0x5B / 0x5C 三种奇迹 | 0x4B3790 | 0x5E4938 | 0x4E09C0 | 0x4E05D0 | wonder.ui |
| 0x5E~0x61 股市 | 内联 | 0x5E45D8 | 0x4DBE60 | 0x4DBCC0 | stockBoard.ui（子界面 singleStock.ui 0x4C6830、tradeStock.ui 0x4DD150） |
| 0x62 乐透开奖 | 0x4B30E0 | 0x5E4818 | 0x4C9420 | 0x4C8BB0 | letouErnie.ui |
| 0x65 | 内联 | 0x5E4590 | 0x429E90 | 0x4298C0 | minimap.ui |
| 0x66 / 0x67 / 0x68 / 0x69 / 0x6A,0x6B | 内联 | 0x5E4548 / 0x5E4500 / 0x5E44B8 / 0x5E4470 / 0x5E4428 | — | 0x4AA9E0 / 0x4ACAB0 / 0x4AA690 / 0x4AC840 / 0x4AC190 | playerinfo / wincondition / gsys / setting / saveload（游戏中系统菜单的各项） |

注意：0x4B2F60、0x4B3260、0x4B33E0、0x4B3790、0x4B3910、0x4B30E0、0x4B3C60 这 7 个构造函数的旧名都和实际构造的类错位了（例如名为 Court_InitContainer 的实际构造魔法屋），已列入 name_fixes.tsv。

### 5.4 前端菜单（高/中）

CApp_SwitchToMenuScene 0x405650 建菜单场景（vtable 0x5E0048），0x509FE0 → 0x509910 按编号创建菜单状态对象，其 vf16 显示对应菜单单例：

| 编号 | 状态 vtable | vf16 | 菜单 |
|---|---|---|---|
| 1 | 0x5E8190 | 0x5089D0 | mainmenu 主菜单 |
| 2 | 0x5E8140 | 0x505940 | chsRoleMenu 选角色 |
| 3 / 4 | 0x5E81E0（参数 1 / 0xB） | 0x503100 | chsMapMenu 选地图/模式 |
| 5 | 0x5E80F0 | 0x50AA20 | optionMenu 选项 |
| 6 | 0x5E80A0 | 0x508240 | ltgameMenu 小游戏 |
| 7 | 0x5E8050 | 0x506FC0 | finalMenu |
| 8 | 0x5E8000 | 0x50D9E0 | single / mult（两个菜单共用一个状态） |
| 9 | 0x5E7FB0 | 0x50CF20 | slmenu 读档 |
| 10 | 0x5E7F60 | 0x50AD50 | picmenu |
| 0xB | 0x5E7F10 | 0x501E60 | boxmenu |

菜单类 vf1 = 0x509260（同样按名字加载），vf12~vf15 是菜单基类 0x5E7668 的图片项管理（0x5093D0/0x509440/0x509460/0x509490），chsRoleMenu/finalMenu 另有 vf16~vf18（基类 0x5E7890）。0x508C60 关闭所有菜单。

### 5.5 剧情（Python）界面

剧情任务 vtable 0x5E91C8 的 vf16 0x5141B0：加载 SpeakAside.ui（找 asideWord/asideWord2），初始化 Python 的 Movie/CTime 模块，导入剧情脚本，调用其生成器函数 `rolemove()` 并保存 `.next`；每帧（vf1 0x514A60）推进一步。（高）

---

## 6. 界面清单（64 个文件）

“打开方式”中的地址是创建/显示该界面的函数。用途的依据为：渲染图（`out\ui_review\render\`）、代码里用到的 string.bin 文字、调用者。

| .ui | 类 / vtable | 关键函数 | 打开方式 | 用途 | 把握 |
|---|---|---|---|---|---|
| AskPoint | 问答任务 0x5E46F8 | 加载 0x4BD1F0 | 事件 2 | 问答点：幕布 + NPC 出题（string 4501「我來問你來答」） | 高 |
| balance | CUi_balance 0x5E43A8 | LoadScaled；0x4AF870 | Balance_* 0x4AF650~0x4B0DF0 | 结算/资产表 | 中 |
| bank | 银行任务 0x5E4740 | vf16 0x4C1590，vf1 0x4C1C40 | 事件 5/99 | 银行 ATM：存款、取款、申请/偿还贷款、数字键盘（4601~4603、5420） | 高 |
| boxmenu | CUi_boxmenu 0x5E7628 | vf10 0x501A70 | 菜单 0xB | 两个十字形格子间搬移卡片/地产，显示现金、点券（7201/7202） | 中 |
| business | CUi_business 0x5E22D0 | vf10 0x4E8E10 | Role_OpenModalUi 0x81；py_Terra_Build 0x500860 | 商业地选择建筑类型（三个建筑按钮） | 中 |
| cardShop | 卡片商店任务 0x5E4A10 | vf16 0x4E6180 | 事件 1 | 用点券买卡片（5202、5206/5207 卡片奇迹折扣） | 高 |
| chsMapMenu | 0x5E7738 | vf10 0x5029B0 | 菜单 3/4 | 选择地图（世界地图）与模式 | 高 |
| chsplayer | 0x5E24B8 | vf10 0x4EAC60 | Role_OpenModalUi 0x84/0x85 | 选择目标玩家（头像 `npc\%s_12`），飞弹/陷害等卡使用 | 高 |
| chsRoleMenu | 0x5E7840 | vf10 0x505050 | 菜单 2 | 选择角色（16 人）及 1~4P | 高 |
| court | 法院任务 0x5E4860 | vf16 0x4D4DE0 | 事件 0x0E | 选择被告与罪名（〔string.bin#4030〕/〔string.bin#4031〕/〔string.bin#4032〕，4001~4032） | 高 |
| cover | 0x5E2294 | — | Role_OpenModalUi 0x88；存档 0x4AC2A0 | 「是否覆蓋存檔？」 | 高 |
| dice | 0x5E23B0 | vf10 0x4E82F0 | Role_OpenModalUi 0x82 | 遥控骰子选点数（string 561「請選擇您想要的點數」） | 高 |
| exit | 0x5E221C | — | Role_OpenModalUi 0x86；0x4927B0 ← 系统菜单 0x4AA790 | 「是否退出遊戲？」 | 高 |
| finalMenu | 0x5E79A8 | vf10 0x506F20 | 菜单 7 | 电视画面 + 16 角色头像（结局/影片欣赏） | 中 |
| getcard | 0x5E2378 | — | 0x4AE790、0x488E80 | 〔string.bin#5303〕时展示 | 高 |
| ginfo | 0x5E2138 | — | 0x488AE0、0x4A4800、0x4F6C80 | 单行信息提示框 | 中 |
| gmaindlg | 0x5E230C | LoadScaled；vf10 0x4E9550 | 0x4A14A0 Game_InitUI 等 | 游戏主界面 HUD：日期、现金/存款/点券、4 个玩家色块、当前角色头像、底栏按钮 | 高 |
| go | 0x5E23E8；0x5E2538（同名两类） | vf10 0x4E8680 / 0x4E8B80 | 0x4813C0 掷骰前；后者用于点券中心 0x4DDA70、饭店 0x4F7610 | 掷骰按钮（骰子图标） | 中 |
| graduate | 0x5E6268 | vf10 0x4E7200 | 0x4E7060；研究所建筑 0x5E6D28 | 研究所界面（0x4F6A20 显示卡片图） | 中 |
| gsys | 0x5E3F70 | vf10 0x4AA630；命令 0x4AA700 | 任务 0x68 | 游戏中系统菜单（右侧 6 个按钮，ID 1 托管→supply、2 存档、3 读档、4 设置、5 关闭、6 退出→exit） | 高 |
| gtinfo | 0x5E2170 | — | 0x488AE0 | 另一种信息提示框 | 低 |
| hotel | 0x5E6380 | vf10 0x4E7370 | 饭店建筑 0x4F62F0 | 饭店（含“色子”占位，入住掷骰） | 中 |
| intro | 0x5E3C70 | — | 0x4A4840 | 说明文字框 | 中 |
| Letou | 乐透任务 0x5E4788 | vf16 0x4C71A0 | 事件 6 | 选号买乐透（32 个号码，4801~4803） | 高 |
| letouErnie | 0x5E4818 | vf16 0x4C8BB0 | 事件 0x62 | 乐透开奖（4805~4810） | 高 |
| lg\lg_intro | 小游戏 DLL | ltlgame_18~21、23 | 小游戏开始 | 小游戏说明/开始界面 | 高（引用）/中（用途） |
| lg\lg_blastomere | 小游戏 DLL | ltlgame_22 | — | 分裂球界面 | 高 |
| lg_intro、lg_blastomere（UI 根目录） | — | — | **未被引用**（DLL 只读 lg\ 下的，且内容不同） | 残留 | 高 |
| loading | 0x5E3CE8 | LoadScaled | 0x4A1090、0x4A31F0、0x4A78F0 | 加载画面（进度条） | 高 |
| ltgameMenu | 0x5E7AF0 | vf10 0x507BF0 | 菜单 6 | 小游戏菜单（6 图标、HI-SCORE） | 高 |
| MagicHouse | 魔法屋任务 0x5E47D0 | vf16 0x4CB400 | 事件 0x0D | 选择施法对象（3902~3904） | 高 |
| mainmenu | 0x5E7C38 | vf10 0x508440 | 菜单 1 | 主菜单 | 高 |
| maintest | — | — | **未被引用** | 主菜单测试版 | 高 |
| market | 0x5E6318 | vf10 0x4E7370 | 0x4F6280（建筑 0x5E6C48） | 连锁超市 | 中低 |
| minimap | 0x5E07C4 | vf10 0x42A390 | 任务 0x65 | 地图总览框 | 中 |
| MovieFrame / MovieLine | — | — | **未被引用**（exe/DLL/pyc 中都没有该名字） | 电视边框/黑框残留 | 高 |
| movieWord | 剧情角色子对象 vtable 0x5E9060 | vf0 0x514030，vf1 0x514190 | Movie.RoleSpeak/ArtSpeak | 剧情角色台词字幕（word/word2） | 高 |
| mult | 0x5E7E88 | vf10 0x50D5B0 | 菜单 8 | 多人游戏设置 | 中 |
| news | 0x5E4C00 | — | News_* 0x4B41B0~0x4B7F80 | 新闻播报（电视机） | 高 |
| okcancel | 0x5E21A8 | vf12 0x4E81B0 | Role_OpenModalUi 0x80；住院/入狱/保释/是否继续 | 通用是/否对话框（文字由 vf2 设置） | 高 |
| optionMenu | 0x5E7D20 | vf10 0x50A840 | 菜单 5 | 选项（6 个滑块） | 高 |
| picmenu | 0x5E7D70 | vf10 0x50ACB0 | 菜单 10 | 角色拼图/图鉴（0x50AF60） | 中 |
| playerinfo | 0x5E402C | LoadScaled；vf10 0x4AAC00 | 任务 0x66 | 玩家资料/资产表 | 中 |
| PowerCo | 电力公司任务 0x5E48A8 | vf16 0x4D8720 | 事件 0x0F | 缴电费、买股（4401~4407） | 高 |
| rob | 0x5E241C | vf10 0x4EAC00 | Role_OpenModalUi 0x83；py_Role_Grab 0x4FED70 | 抢夺：列出对方 8 张卡选择 | 高 |
| saveload | 0x5E40A8 | vf10 0x4AC130 | 任务 0x6A/0x6B | 游戏中存/读档（10 槽，`save\%s%02d.sav`） | 高 |
| scrbar / sscrbar | 0x5E37B8 / 0x5E37F0 | — | 0x49F810（←0x49BEE0/0x49C0B0）；模式 0 用 4 格，否则 6 格 | 转轮数字条 | 中低 |
| setting | 0x5E40F8 | vf10 0x4AC7C0 | 任务 0x69 | 游戏中设置（音乐/音效/速度） | 高 |
| single | 0x5E7E00 | vf10 0x50D5B0 | 菜单 8 | 单人游戏设置 | 中 |
| SingleStock | — | 0x4C6830 | 0x4DC1C0 | 个股详情（走势图） | 中 |
| slmenu | 0x5E7DB8 | vf10 0x50CE10 | 菜单 9 | 前端读档（10 槽） | 高 |
| SpeakAside | 剧情任务 0x5E91C8 | vf16 0x5141B0 | 剧情脚本 | 旁白字幕（Movie.SpeakAside） | 高 |
| speech | 0x5E2480 | — | 0x48CCB0/0x48CDB0（左/右）、0x48C700 | 角色对话框（左右头像） | 高 |
| StockBoard | 股市任务 0x5E45D8 | vf16 0x4DBCC0 | 事件 0x5E~0x61 | 股票行情表 | 高 |
| supply | 0x5E2258 | — | Role_OpenModalUi 0x87；系统菜单 0x4AA810 | 「是否托管？」 | 高 |
| throwcard | 0x5E425C | vf10 0x4AE9E0 | 0x4AEA80 ← 0x4E9340（选中手牌） | 手牌上的两项小菜单 | 中 |
| ticketCenter | 点券中心任务 0x5E48F0 | vf16 0x4DDA70 | 事件 0x10 | 选择消费的点券、买股（4201~4209） | 高 |
| tradeStock | — | 0x4DD150 | 0x4C69C0 | 买卖股数输入（数字键盘 + Edit，6312~6314） | 高 |
| vendue | 拍卖任务 0x5E4980 | vf16 0x4E2FC0 | 事件 0x50/0x51 | 地产拍卖（加价 100/500/1000、PASS，4301~4310） | 高 |
| wincondition | 0x5E4144 | — | 任务 0x67 | 胜利条件说明 | 中 |
| wonder | 奇迹任务 0x5E4938 | vf16 0x4E05D0 | 事件 0x5A~0x5C | 奇迹（金钱/地产/卡片）建成与效果说明（201~209） | 高 |

完整的自动提取数据（每类的虚表槽、单例函数、全部使用者、挂的回调、FindByName 的控件名、用到的 string.bin 编号）见 `out\ui_review\screens_auto.txt`（由 `build_inventory.py` 生成）。

---

## 7. Python 脚本接口与界面

剧情脚本（`out\pysrc_clean\da*.py`，80 个）都是 `rolemove()` 生成器，在 Movie/CTime 调用之间 `yield None`，由剧情任务每帧推进（5.5）。与界面相关的接口：

| Python 接口 | exe 实现 | 界面效果 | 脚本中调用次数（文件数） | 把握 |
|---|---|---|---|---|
| Movie.SpeakAside(id) | 0x5135D0 → 0x515200 | 把 string.bin[id] 设为 SpeakAside.ui 的 asideWord 与 asideWord2 两个标签（两层，推测为阴影+正文）并显示 | 84（31） | 高 |
| Movie.StopSpeakAside(id) | 0x513620 | 隐藏旁白 | 72（29） | 中 |
| Movie.RoleSpeak(h, id) | 0x512B70 | 调剧情角色 +0x3C 子对象 vf0 0x514030：首次加载 movieWord.ui，把 string.bin[id] 设给 word/word2 并显示 | 33（10） | 高 |
| Movie.RoleStopSpeak(h) | 0x512BE0 | 子对象 vf1 0x514190：隐藏 | 32（10） | 高 |
| Movie.ArtSpeak / ArtStopSpeak | 0x513190 / 0x5131F0 | 对物件句柄调其首个虚函数（推测同为 movieWord 字幕） | 60（14）/ 58（14） | 中 |
| Movie.SetBackPic / SetFrontPic(name, dir) | 0x513490 / 0x513500 → 0x514FE0 | 从 `texture\<dir>` 读 RW 纹理作为全屏背景/前景图（不经 UI 库） | 23（21）/ 36（24） | 中 |
| Movie.DelBackPic / DelFrontPic | 0x513570 / 0x5135A0 | 删除上述图层 | 1 / 35 | 中 |
| Role.Speak(role, id) | 0x4FE3D0 → 0x484890 | 往角色消息队列压入消息，由角色状态机显示对话（speech 等） | 3（2） | 低 |
| Movie.PlaySound / CTime.Wait / Movie.QuitMovie | 0x5133C0 / 0x5160B0 / 0x5136D0 | 无界面效果 | — | 高 |

除此之外，AI 与卡片脚本不直接操作界面：卡片效果里需要玩家选择的步骤（选目标、选卡、遥控骰子）都由 exe 的 Role_OpenModalUi 打开对话框完成（5.2）。

---

## 8. 命名修正

`out\ui_review\name_fixes.tsv` 共 152 条，主要几类：

1. UI 库 setter：0x5A7F40 等 30 个函数原名 `u_RW_Color_SetPrimary`、`u_UiLib_Widget_SetAlignH`、`u_RW_ScrollBar_SetXPosition` 等，实为 SetNormalPic、SetMouseInBColor、SetLeft 等（依据：0x5AA7D0 中键名与调用一一对应）。
2. 控件虚函数默认实现 9 个（槽语义见 2.5）。
3. 控件类加载/保存函数 14 个（slider/scrollbar 互换、LoadFromIni/SaveToIni 互换等）。
4. 钩子注册与游戏侧实现 30 个（`u_RW_VirtualTable_SetN`、`Unk_DrawQuad_eb040` 等）。
5. 对话框基类 vf1~vf9、缩放函数、`UI_InitializeAllDialogs`（实为销毁）、`Game_UseCard`（实为启动事件任务）。
6. 事件任务构造/析构函数 10 个（名字与所建类错位）。
7. 6 个返回界面名的 vf11（news/gsys/exit/dice/go/rob）。
8. 游戏中系统菜单 gsys 的命令处理与三个确认框的打开函数 10 个（原名 NPC_SendActOnPlayer 等与 NPC 无关）。

建议类名：0x5F5580 CUiWidget、0x5F5D60 CUiPlane、0x5F5778 CUiLabel、0x5F5C10 CUiTitle、0x5F5CB8 CUiButton、0x5F56D0 CUiEdit、0x5F5AC0 CUiRadioButton、0x5F5B68 CUiCheckBox、0x5F5A18 CUiGroupBox、0x5F5970 CUiScrollBar、0x5F58C8 CUiProgress、0x5F5820 CUiListBox、0x5F5628 CUiSlider；Cls_5E4C00 CUi_news、Cls_5E3F70 CUi_gsys、Cls_5E7E88 CUi_mult、Cls_5E221C CUi_exit、Cls_5E23B0 CUi_dice、Cls_5E23E8/Cls_5E2538 CUi_go、Cls_5E241C CUi_rob、Cls_5E47D0 CMagicHouseTask、Cls_5E4788 CLetouTask、Cls_5E4938 CWonderTask、Cls_5E4A10 CCardShopTask、Cls_5E46F8 CAskPointTask。

---

## 9. 仍不确定的点

* radiobutton/groupbox 的 LoadFromIni（0x5C1E40/0x5C1850）Ghidra 没建函数，本文的键表取自对应的 SaveToIni；本游戏没有这两类控件，不影响解析。
* BorderType 1~4 的具体线框样式（0x5A9CE0）、`~[...]` 文字标记的完整语法、CapFontType 的取值，未逐一核对。
* EnableFlag 作为输入层掩码时，除光标隐藏（全 0）/显示（全 1）外没有发现其他取值，按位分层的设计在本游戏中未使用。
* 部分界面用途为中/低把握：gtinfo、graduate、market、hotel、scrbar/sscrbar、boxmenu、finalMenu、single/mult、事件 0x11 任务。需要进游戏对照。
* 3.5 中 y 方向缩放误用宽度属于静态推断（反汇编里两处都读 +0x104），未在非 4:3 分辨率下实测。
* RichMan8Fix 挂钩这些地址的目的（分辨率、代码页）是根据地址对应关系推断的，未反汇编其补丁内容。
* 全部结论来自静态分析，未动态调试。

---

## 10. 工作记录（子代理使用与验收）

* haiku 子代理 5 个：4 个分组整理界面清单（`out\ui_review\screens_A~D.tsv`），1 个整理 Python 接口（`python_ui.tsv`）。要求每条结论附“地址|原样代码行”。
* 机器验收（`out\ui_review\verify_agents.py`）：虚表槽地址 0 错；界面清单的证据行能在所引函数中逐字找到 84/118（71%，其余多为把单例里 `_DAT_006403e4` 写成 `_DAT_006403e8` 之类的偏移错误）；Python 表证据行 30/30 逐字命中。
* 命名修正的盲审：从 name_fixes.tsv 随机抽 25 条（`blind_sample.tsv`），交给 sonnet 子代理独立读代码判定，结果 25 条全部 correct（`blind_review.tsv`）；它另外确认了 supply 对话框的“确定”会触发托管（消息 0x79 → Role_EnableAutoPlay）。
* 抽查发现的问题：子代理把 `Register1/2` 的第三个参数当成控件 ID（实为类型号，是我给的背景写错，已在本文更正）；“打开方式”里把 `UI_InitializeAllDialogs`（实为销毁）当成显示者；不少用途过于笼统或猜错（如 boxmenu、graduate、gsys）。因此第 6 节的清单改由脚本 `build_inventory.py`、`factory_table.py` 从代码确定性提取结构数据，用途由主代理结合渲染图、string.bin 文字和调用者重新判定；子代理表格仅作参考，不作为结论来源。
