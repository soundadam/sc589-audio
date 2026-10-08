# ADSP-SC589 音频开发

在 ADSP-SC589 EZ-Board 上开发音频处理程序。日常在 Mac 上写代码、跑测试，通过 UART 启动板卡；
CCES 构建和需要单步调试的时候用 Windows 机器（见 [ADR 0001](docs/adr/0001-mac-board-testing-via-uart-boot.md)）。

## 目录

自己的代码：

| 目录 | 内容 |
|---|---|
| `apps/audio_loopback/` | ADC → 处理 → DAC 回环程序：`adc_dac_playback.c` 是入口，`tone_check.c` 是模拟回环自检 |
| `apps/audio_loopback/dsp/` | 信号处理（`audio_process.c`），纯 C99，不依赖 CCES 和板卡 |
| `apps/audio_loopback/dsp/test/` | `dsp/` 的主机端测试 |
| `apps/audio_loopback/platform/` | 板卡、codec、SPORT/DMA 音频通路和串口控制台 |
| `apps/audio_loopback/SC589/sharc/` | 三个核的 CCES 工程，源码通过链接引用上一级目录 |
| `tools/` | Mac 端的 UART 启动和日志工具（`uart_boot.py`、`uart_log.py`）、远端构建（`remote_build.sh` 及它在 Windows 上调用的 `remote_build.ps1`、`make_ldr.ps1`）；用法写在各脚本头部 |
| `docs/adr/` | 架构决策记录 |

ADI 官方代码，只读参考：

| 目录 | 内容 |
|---|---|
| `vendor/adi/examples/` | 导入的 CCES 示例（`drivers/` `services/` `Device_Programmer/` `Power_On_Self_Test/`），与初始导入的提交（`1721959`）内容一致 |
| `vendor/adi/sources/` | 示例链接的驱动源码，见 [vendor/adi/sources/README.md](vendor/adi/sources/README.md) |

## 本地检查

```bash
make check
```

运行 `audio_process` 的主机端测试和 `tools/` 的 Python 测试。

## 在板卡上运行

板卡 BMODE 拨到 UART 启动，然后：

```bash
make board
```

在 Windows 主机上用当前工作树（含未提交的改动）构建，取回 `.ldr` 到 `build/`，通过 UART 启动板卡，
按串口输出判定通过或失败。看到 `uart_boot.py` 开始等待时按复位。只构建不上板用 `tools/remote_build.sh`。
其余启动方式见 [ADR 0001](docs/adr/0001-mac-board-testing-via-uart-boot.md) 的「验证步骤」。
