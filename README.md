# ADSP-SC589 音频实验

这是我做数字信号处理实验课用的仓库。板卡是 ADI 的 **ADSP-SC589 EZ-Board**（一颗 ARM Cortex-A5 加两颗 SHARC+ 核），
课上的作业都在它的音频回环上做：音频从 ADC 进来，SHARC 上跑滤波器，再从 DAC 送出去。

麻烦在于开发环境。ADI 的 IDE 和编译器 CrossCore Embedded Studio（CCES）只有 Windows 和 Linux x86 版本，
macOS 上不能编译；调试器 ICE 的驱动也没有 Mac 版。我手边只有一台 Mac，没有 Windows 笔记本，所以分成两半：

- **编译在 Windows 上做。** 一台装了 CCES 的 Windows 机器只负责编译：Mac 上一个脚本把当前工作树传过去，
  在那边用命令行编译好，再把生成的 `.ldr` 启动文件拿回来。
- **运行在 Mac 上做。** 板卡的 USB 转串口接到 Mac，把板子拨到 UART 启动，Mac 按 Boot ROM 的协议
  把 `.ldr` 通过串口发给板子，程序跑起来以后还是从这个串口读输出。这样不用调试器，代价是不能单步。

为什么这样选、试过哪些别的办法，写在 [ADR 0001](docs/adr/0001-mac-board-testing-via-uart-boot.md)。
滤波器本身是纯 C，不依赖板卡，在 Mac 上就能编译和测试；画图和写报告用 Python 和 LaTeX，也都在 Mac 上。

课程作业在 [`coursework/`](coursework/)，每次实验一个目录，里面有要求清单和报告源文件。

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
