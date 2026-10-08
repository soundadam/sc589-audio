# ADSP-SC589 音频开发

在 ADSP-SC589 EZ-Board 上开发音频处理程序。日常在 Mac 上写代码、跑测试，通过 UART 启动板卡；
CCES 构建和需要单步调试的时候用 Windows 机器（见 [ADR 0001](docs/adr/0001-mac-board-testing-via-uart-boot.md)）。

## 目录

自己的代码：

| 目录 | 内容 |
|---|---|
| `apps/audio_loopback/` | ADC → 处理 → DAC 回环程序。`audio_process.c` 是信号处理部分，其余是板卡、codec、SPORT 和串口控制台 |
| `apps/audio_loopback/host_test/` | `audio_process.c` 的主机端测试，不需要 CCES 和板卡 |
| `apps/audio_loopback/SC589/sharc/` | 三个核的 CCES 工程，源码通过链接引用上一级目录 |
| `tools/` | Mac 端的 UART 启动和日志工具（`uart_boot.py`、`uart_log.py`）、Windows 端的 ldr 打包脚本（`make_ldr.ps1`）；用法写在各脚本头部 |
| `docs/adr/` | 架构决策记录 |

ADI 官方代码，只读参考：

| 目录 | 内容 |
|---|---|
| `drivers/` `services/` `Device_Programmer/` `Power_On_Self_Test/` | 导入的 CCES 示例，与初始导入的提交（`1721959`）保持一致 |
| `adi_src/` | 示例链接的驱动源码，见 [adi_src/README.md](adi_src/README.md) |

## 本地检查

```bash
make check
```

运行 `audio_process` 的主机端测试和 `tools/` 的 Python 测试。

## 在板卡上运行

步骤见 [ADR 0001](docs/adr/0001-mac-board-testing-via-uart-boot.md) 的「验证步骤」。
Mac 端工具依赖 pyserial：`pip install -r tools/requirements.txt`。
