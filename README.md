# ADSP-SC589 音频开发

在 ADSP-SC589 EZ-Board 上开发音频处理程序。日常在 Mac 上写代码、跑测试，通过 UART 启动板卡；
CCES 构建和需要单步调试的时候用 Windows 机器（见 [ADR 0001](docs/adr/0001-mac-board-testing-via-uart-boot.md)）。

## 目录

自己的代码：

| 目录 | 内容 |
|---|---|
| `firmware/audio_loopback/` | ADC → 处理 → DAC 回环程序：`adc_dac_playback.c` 是入口 |
| `firmware/audio_loopback/dsp/` | 信号处理，纯 C99，不依赖 CCES 和板卡：`audio_process.c` 按声道调用 `fir.c`、`biquad.c`；`lowpass_coeffs.*` 由 `coursework/E01-fir-iir/design.py` 生成 |
| `firmware/audio_loopback/dsp/test/` | `dsp/` 的主机端测试 |
| `firmware/audio_loopback/platform/` | 板卡、codec、SPORT/DMA 音频通路和串口控制台；`filter_report.c` 在启动时把滤波器的冲激响应和周期数打到控制台 |
| `firmware/audio_loopback/cces/SC589/sharc/` | 三个核的 CCES 工程，源码通过链接引用 `firmware/audio_loopback/` 下的文件 |
| `host/board/` | Mac 端的 UART 启动和日志工具（`uart_boot.py`、`uart_log.py`）及其测试 |
| `host/dsplab/` | Mac 端的滤波器设计与分析包（系数生成 C 表、频响、读板卡日志、作图），由根目录 `pyproject.toml` 用 uv 管理 |
| `host/build/` | 远端构建：Mac 上运行的 `remote_build.sh`，以及它在 Windows 上调用的 `remote_build.ps1`、`make_ldr.ps1` |
| `coursework/` | 课程实验：每次一个目录（要求清单、设计脚本、作图脚本、报告源文件），`common/` 放报告的共用样式 |
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

运行 `dsp/` 的主机端测试和 `host/board/` 的 Python 测试。

## 在板卡上运行

板卡 BMODE 拨到 UART 启动，然后：

```bash
make board
```

在 Windows 主机上用当前工作树（含未提交的改动）构建，取回 `.ldr` 到 `build/`，通过 UART 启动板卡，
按串口输出判定通过或失败，控制台输出存进 `build/board.log`。看到 `uart_boot.py` 开始等待时按复位。只构建不上板用 `host/build/remote_build.sh`。
其余启动方式见 [ADR 0001](docs/adr/0001-mac-board-testing-via-uart-boot.md) 的「验证步骤」。
