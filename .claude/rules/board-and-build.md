# 只做 SC589，在 noah 上构建

手头只有 ADSP-SC589 EZ-Board，不为 SC573 / SC584 写或改代码。只要还没有第二块板卡，这条规则就成立。

板卡通过 P8 接在 Mac 上（FT232R，`/dev/cu.usbserial-*`），UART 启动时 BMODE 为 7；
需要复位时请用户来按。

CCES 2.9.3 的构建通过 `ssh noah.tail`（Windows）完成，在
`C:\Users\tianchi.liu\workspaces\adi-cces-uart` 这份 clone 里执行：用 scp 把源码传过去，在那边构建，
再用 scp 把 `.ldr` 取回来。

- **不要改动 noah 上的 `workspaces\adi-cces-examples`**：里面有用户还没提交的 IDE 改动。
- 路径里带空格时，写成 `.ps1` 文件，用 `powershell -File` 运行，不要用内联的 `-Command`。

**原因：** 板卡和这台构建主机是验证固件改动的唯一途径，而那份 examples clone 是用户自己正在用的工作区。
