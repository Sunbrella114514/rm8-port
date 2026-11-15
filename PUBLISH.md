# 发布内容清单（what gets published）

开发机上的 `<workspace>` 是**私有工作区**（含逆向产物），不是整个发布出去。发布的是下面这份白名单；根目录的 `.gitignore` 也按白名单写
（默认全部忽略，只放行下面的路径），防止有人直接在工作区里 `git init` 时把私有内容带进去。自动构建并审计这份清单：

```
python -I extractor\tools\make_publish_tree.py --out <workspace>\out\publish     # 复制白名单 + 文档去原文 + 审计，有问题就失败
```

## 发布

| 路径 | 说明 |
|---|---|
| `LICENSE`、`NOTICE.md`、`THIRD_PARTY_NOTICES.md`、`README.md`、`PUBLISH.md`、`.gitignore` | 声明与许可证（`port/` 下另有一份相同的 LICENSE / NOTICE / THIRD_PARTY_NOTICES） |
| `port/`：`app/`、`android/`（`AndroidManifest.xml`、`src/`、`build_apk.py`、`deploy.py`、`data_files.py`）、`cmake/`、`patches/`、`viewer/`、`loadtest/`、`mgtest/`、`CMakeLists.txt`、`README.md`、`tools/make_t2s.py`、`deps_local/`（我们自己的 CPython 配置文件）、`extern/fetch_deps.py` + `extern/README.md` | 移植版源码；**第三方源码不发布**，由 `fetch_deps.py` 从官方来源下载固定版本并校验 SHA-256 |
| `extractor/`：`rm8_extract.py`、`rm8x/`、`exporters/`、`data/`（版本参考、`missions.json`、OpenCC 词典）、`tools/`、`android/`、`README.md` | 提取器与导出脚本；`rm8_extract.exe` 作为发布附件另放（`build/dist/`，不入库） |
| 发布附件（不入库）：**源码包 `rm8-port-src-<tag>.zip`**（`make_publish_tree.py --zip` 生成，`rm8_extract --source` 直接接受；SHA-256 填进 `extractor/data/source_release.json` 后，不带 `--source` 时提取器会自己下载它）、`rm8_extract.exe` + `SHA256SUMS.txt`、**数据-free 的基础 APK**（`build_apk.py` 的默认产物，改名 `rm8_base.apk`，放在 exe 旁边供 `--apk` 使用）、移植版 exe | 构建产物，不含任何游戏数据 |
| `docs_public/`（构建时写到发布树的 `docs/`） | 技术文档的**去原文版**：原版文字已替换成编号或转述；不含只服务于反编译的文档（见下） |

## 不发布（只保存在本机）

| 路径 | 为什么 |
|---|---|
| `mod/`、`out/`、`ghidra_proj/`、`ghidra_scripts/` | 逆向产物：解包的资源、反编译 C / Python、Ghidra 工程、游戏画面截图、测试用的带数据 APK |
| `scripts/`（除了 `extractor/exporters/` 里的快照） | 逆向分析脚本，含开发机路径和少量原版文字 |
| `tools/`、`ref/`、`tmp_*`、`work_*` | 游戏自带 DLL 的副本、测试文件、转换影片、存档转储 |
| `port/scenes/`、`port/results/`、`port/build-*`、`port/*.apk`、`port/rm8_*.bin`、`port/rm8_settings.txt`、`port/android/debug.keystore` | 派生数据、截图、构建产物、用户进度、调试密钥 |
| `docs/` 里只服务于反编译的文档：`code_map.md`、`naming_review_v2.md`、`exe_internals.md`、`re_context.md`、`workflow.md`；以及所有文档的**原文版** | 记录函数地址 / 命名过程 / 修改原版的工作流；原文版里有大量原版文字（发布的是去原文版） |
| 任何提取器输出（`game\`、`scenes\`、`manifest.json`、迁移存档用的 `saves\`、`%APPDATA%\RM8` 里的存档）、个人 APK（`rm8_personal_*.apk`、`--with-data` 的 APK）、用户的签名密钥（`%APPDATA%\RM8\apk_key`） | 含游戏内容 / 私钥，只供用户本人使用 |

## 发布前检查

1. `extractor\tools\sync_exporters.py --check`（过期就先 `sync_exporters.py`，再 `refresh_references.py`，让 `data\references\*.json` 跟上导出脚本读的文件和输出统计）。
2. `extractor\tools\make_publish_tree.py --out <空文件夹> --zip <源码包.zip> [--corpus <新提取的 scenes> ...]`：构建发布树并审计（与两份安装逐字节比对、认派生文件、查原版文字、没有私有路径和 `__pycache__`、`THIRD_PARTY_NOTICES.md` 与代码里的下载清单一致）；必须全部为 0 / OK。`--corpus` 要给**刚提取的**输出的 `scenes`（原文有更新时，文档里的引文才能被换成编号）。
3. `extractor\tools\check_app_data.py --out <提取输出>`：提取结果与 `port\android\data_files.py` 一致（对 Steam 版和旧版各跑一次）。
4. `extractor\tools\selftest.py --game <Steam 安装> --work <空文件夹> [--exe ...]`：错误路径、更新、存档、双击流程（模拟输入）、断网、磁盘空间不足、版本不符。
5. `extractor\tools\verify_apk.py <apk> --data-dir <输出> --build-tools <...> --jdk <...>`：个人 APK 的结构、数据、zipalign、apksigner（只检查文件，不安装到手机）。
6. `extractor\tools\list_source_quotes.py`：源码里还有多少引用原文（目标 0；清单见 `out\app_text_edits.md`，私有）。
7. `rm8_viewer.apk` 默认不含数据（`build_apk.py` 不加 `--with-data`）；发布前换掉调试签名。
8. `python -I port\extern\fetch_deps.py --verify`：发布树里 `fetch_deps.py` 的固定值要和当时 `port\extern` 的内容一致（开发机上的 `librw` 是 CRLF 检出，逐字节哈希不同，行尾归一后逐文件相同，见 `docs\assets_legal.md` 第 13、16 节）。
9. 发布附件：`rm8_extract.exe`（`extractor\tools\build_exe.py`）+ `rm8-port-src-<tag>.zip` + `SHA256SUMS.txt`（两个文件的 SHA-256）。Windows 程序**不签名**（项目的决定），README 里已写明 SmartScreen 提示怎么点。
