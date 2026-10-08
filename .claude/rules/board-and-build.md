# 只做 SC589，在 noah 上构建

手头只有 ADSP-SC589 EZ-Board，不为 SC573 / SC584 写或改代码。只要还没有第二块板卡，这条规则就成立。

板卡通过 P8 接在 Mac 上（FT232R，`/dev/cu.usbserial-*`），UART 启动时 BMODE 为 7；
需要复位时请用户来按。

CCES 2.9.3 的构建在 `noah.tail`（Windows）上进行，用 `tools/remote_build.sh`（见脚本头部）。
它把当前工作树同步到 noah 的 `workspaces\adi-cces-examples`，构建完成后把 `.ldr` 取回本地。

- noah 上那份 clone 是 Mac 工作树的镜像，每次构建都会被覆盖：**不要在 noah 上改代码**，
  用 ICE 单步调试时也只看不改。
- 不要只为了让 noah 拿到代码就 push：`remote_build.sh` 不需要 push。
- 在 ssh 上执行的命令里，路径带空格时写成 `.ps1` 文件，用 `powershell -File` 运行，不要用内联的 `-Command`。

**原因：** 板卡和这台构建主机是验证固件改动的唯一途径；只有一份镜像，Mac 上的代码和板上跑的才对得上。
