# 版本参考（references）：怎么增加一个新版本

每个受支持的《大富翁8》版本一对文件：

* `<id>.json`：该版本的“指纹”：每个核心文件（导出脚本读取的 + 要复制的）的相对路径、大小、SHA-256，以及 `role`
  （`core` 必须一致 / `video` 转影片时需要 / `info` 只作识别信息，如 exe / DLL / `mutable` 用户设置）。
  字段 `status`：`primary`（优先支持，目前是 Steam 版 `sc`）/ `supported` / `legacy`（暂时保持可用，目前是开发机上的旧版 `tc`）。
  `raw_copy` 是要复制到输出 `game\` 的文件（写成 app 期望的大小写）。
* `<id>_outputs.json`：在“导出脚本的某一版”下，该版本提取结果的行数 / 引用统计，用来在提取后做校验；
  记录了 `exporters_digest`，导出脚本变了就自动不再比较（不会误报）。

提取器会对所有参考打分（所有小文件的哈希 + 大文件的大小），选最接近的一份做完整校验；分数相同时 `primary` 优先。

## 增加 / 更换一个版本（例如以后换成干净的旧版）

1. 准备好该版本的安装文件夹（只读使用）。
2. 在 `tools\refresh_references.py` 的 `NAMES` 里加一行（id、名称、exe 说明、status），并在 `main()` 里加对应的 `--<id>` 参数；
   换掉旧版 `tc` 时直接改 `tc` 那一行即可，旧的指纹会被覆盖。
3. 运行：`python -I tools\refresh_references.py --tc <旧版> --sc <Steam 版> --work <空文件夹>`。它对每个安装先做一次 `--no-reference`
   的提取，再用 `make_reference.py` 记录指纹和输出统计。（新版本的密钥 / 编码如果与 `scripts\rm8_edition.py` 里的两种不同，
   先在 `EDITIONS` 里加一项；其它文件格式不同则需要先让导出脚本能读它。）
4. 检查 `git diff data\references`、跑 `tools\selftest.py`，再重新打包。
5. 每次导出脚本变化（`tools\sync_exporters.py`）后也要重跑第 3 步，让 `_outputs.json` 跟上。
