# extern/：第三方依赖（脚本下载，不入库）

这个目录里**只有** `fetch_deps.py` 和本说明是仓库的内容；`SDL2-2.32.10/`、`glfw-3.5.1/`、`librw/`、`cpython24/`、`dr_libs/`、`pl_mpeg/`、`stb/`、`opencc_dict/`
都由脚本从各自的官方来源下载到这里（被 `.gitignore` 忽略）。

```
python extern/fetch_deps.py            # 下载缺少的 / 不符合固定值的依赖（缓存在 extern/.cache）
python extern/fetch_deps.py --verify   # 只检查已有内容是否与固定值一致（不联网）
python extern/fetch_deps.py --force    # 全部重新下载
python extern/fetch_deps.py --offline  # 只用缓存
```

* 只用 Python 标准库（3.8+）。每个依赖固定到版本 / 提交，下载后核对 SHA-256（官方发布的文件）或解包后整棵目录的哈希（GitHub 自动生成的源码包不是字节稳定的，所以按提交固定、按目录哈希校验）；不符就删除并停止。
* librw 下载后自动运行 `patches/apply_librw_android.py`（我们的补丁：骨骼上限、GLES 3.0、`readTGAMemory`…）。
* CPython 2.4.6 只解出 `deps_local/cpython24/files.txt` 列出的 208 个文件（解释器核心），再把**我们自己写的**
  `deps_local/cpython24/{rm8_config.c, rm8_getpath.c, pyconfig.h}`（已入库）复制进去。
* 各依赖的许可证见仓库根目录的 `THIRD_PARTY_NOTICES.md`；升级版本时先改 `fetch_deps.py` 里的 URL，运行 `--record` 得到新的固定值。
