# 法律声明 / Legal Notice

## 简体中文

### 1. 非官方项目

本项目是《大富翁8》（RichMan 8）的**非官方**、非营利爱好者原生移植，与游戏的权利人没有任何关系，也未获其授权、赞助或认可。
“大富翁”“RichMan”、游戏名称、角色、美术、音乐、语音、文字、地图、影片及其他游戏内容的著作权和商标权，均属原权利人所有。

### 2. 本仓库不含游戏文件

本仓库只含我们自己写的源码、工具、测试和文档：**没有**资源包、图片、模型、贴图、音乐、语音、影片、执行档、存档，也没有原版字体；
也**没有**由这些文件转换出来的数据（地图场景、界面布局、文字表、转换后的影片等）。

运行移植版需要你**自己合法拥有《大富翁8》**（推荐在 Steam 购买：https://store.steampowered.com/app/2185870/），并在你自己的电脑上运行随附的提取器 `rm8_extract`，
由它把你自己的游戏文件整理成移植版读取的数据（技术分层和逐文件清单见 [docs/assets_legal.md](docs/assets_legal.md)）。
提取器只读取你指定的游戏文件夹，不联网获取游戏文件（唯一的联网动作是在找不到 ffmpeg 时下载转换影片用的 ffmpeg，见 THIRD_PARTY_NOTICES.md）。
本项目不提供、也不协助取得任何游戏文件；请购买正版，不要下载或传播盗版。

关于文字：为了让界面与原版一致，移植版运行时从**你提取出的**文字表读取原版文字，源码里只留我们自己写的提示语。
如果你在源码或文档里发现原版文字的引文，请告诉我们，我们会改成引用编号或转述；这些文字的权利仍属原权利人，不在下面第 4 节的开源许可范围内。

### 3. 不得散布含有游戏内容的数据与构建

提取器生成的输出文件夹（`game\`、`scenes\`、`manifest.json`）和在本机生成的、内含你自己游戏数据的个人 APK（`rm8_extract --apk`，或 `build_apk.py --with-data`；由你自己的密钥签名），
**只供拥有正版游戏的你本人在自己的设备上使用**：

* 不得上传、分享、出售或公开散布这些文件夹或安装包；
* 不得上架任何应用商店；
* 不得用于商业用途。

我们发布的移植版 exe / APK 不含任何游戏数据。

### 4. 我们代码的许可证

我们自己写的代码（`port/` 下的 app、安卓外壳、构建脚本，`extractor/` 提取器和导出脚本，以及文档）依
**GNU General Public License v3.0 或更新版本（GPL-3.0-or-later）** 授权，全文见 [LICENSE](LICENSE)。
GPL 只涵盖本项目自己的代码，**不涵盖、也不授予**任何《大富翁8》游戏内容的权利。
`port/extern/` 里的第三方源码保留各自的许可证（见 THIRD_PARTY_NOTICES.md 和各目录里的许可证文件）。

### 5. 第三方组件

见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。

### 6. 无担保

本软件按“现状”提供，不附带任何明示或默示担保，详见 GPL 第 15、16 条。提取和存档功能已尽力验证，但请自行备份重要文件。

### 7. 权利人联系

如果你是权利人并认为本项目有任何不妥，请在**本仓库的 Issues 页面**提出，我们会尽快处理（包括下架相关内容）。

---

## English (summary)

This is an **unofficial**, non-commercial fan port of *RichMan 8* (大富翁8); it is not affiliated with, authorised or endorsed by the rights holder, who owns all game content.
The repository contains only our own source code, tools and documentation: **no** game files and **no** data converted from them.
You must legally own the game (buy it on Steam: https://store.steampowered.com/app/2185870/); the bundled extractor `rm8_extract` converts *your own* installation on *your own* machine
(see `docs/assets_legal.md`). Extracted output folders and any APK built with `--with-data` are for your personal use only: do not share, sell or
upload them. Our code is GPL-3.0-or-later (`LICENSE`) and covers our code only. Third-party components keep their own licenses
(`THIRD_PARTY_NOTICES.md`). No warranty. Rights holders: please open an issue on the repository's Issues page; we will respond promptly, including by removing material.
