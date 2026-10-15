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

| 目录 | 内容 |
|---|---|
| `firmware/audio_loopback/` | ADC → 处理 → DAC 回环程序，入口 `adc_dac_playback.c` 不在仓库里（见下一节） |
| `firmware/audio_loopback/dsp/` | 信号处理，纯 C99，不依赖 CCES 和板卡：`audio_process.c` 按声道调用 `multirate.c`（降采样、升采样）、`biquad.c`、`tone.c`；`multirate_coeffs.*` 由 `coursework/E02-multirate/design.py` 生成，`lowpass_coeffs.*`（E01，`fir.c` 的测试也用它）由 `coursework/E01-fir-iir/design.py` 生成 |
| `firmware/audio_loopback/dsp/test/` | `dsp/` 的主机端测试 |
| `firmware/audio_loopback/platform/` | 串口控制台（`uart_console.c`）；`filter_report.c` 在启动时把滤波器的冲激响应和周期数打到控制台 |
| `firmware/audio_loopback/cces/SC589/sharc/` | 三个核的 CCES 工程，源码通过链接引用 `firmware/audio_loopback/` 下的文件 |
| `host/board/` | Mac 端的 UART 启动和日志工具（`uart_boot.py`、`uart_log.py`）及其测试 |
| `host/dsplab/` | Mac 端的滤波器设计与分析包（系数生成 C 表、频响、读板卡日志、作图，以及在 Mac 上跑 `dsp/` 的 C 代码：`cdsp`、`wavrun`），由根目录 `pyproject.toml` 用 uv 管理 |
| `host/build/` | 远端构建：Mac 上运行的 `remote_build.sh`，以及它在 Windows 上调用的 `remote_build.ps1`、`make_ldr.ps1` |
| `coursework/` | 课程实验：每次一个目录（要求清单、设计脚本、作图脚本、报告源文件），`common/` 放报告的共用样式 |
| `docs/adr/` | 架构决策记录 |

## 不在仓库里的 ADI 代码

回环程序是从 ADI 的 ADAU1979 示例（`drivers/adc/adau1979` 下的 `adc_dac_playback`）改出来的。这部分文件带着 ADI「proprietary and confidential」的版权声明，不能公开，所以不在这个仓库里：

- `firmware/audio_loopback/adc_dac_playback.{c,h}`：入口、ping-pong 缓冲和回调
- `firmware/audio_loopback/platform/` 下的 `board_setup`、`adau1979_setup`、`adau1962a_setup`、`audio_path`：
  板卡、两颗 codec 和 SPORT/DMA 音频通路
- `firmware/audio_loopback/cces/SC589/` 下的 `SoftConfig_SC589.c`，以及 CCES 生成的 `system.svc` 和 `system/adi_initialize.*`

我自己构建时，它们放在另一个私有仓库里，`host/build/remote_build.sh` 在发给 Windows 之前按原路径叠加进去。
所以直接 clone 这个仓库是编译不出固件的；如果你手上也有这块板，可以从自己的 CCES / BSP 安装里拿到原始示例，
`dsp/`、`host/` 和 `coursework/` 则不依赖这些文件，在 Mac 上就能跑。

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

先列出 `coursework/` 下的实验让你选（直接回车选最新的一个），然后在 Windows 主机上构建它的固件，取回 `.ldr`，
通过 UART 启动板卡，按串口输出判定通过或失败，控制台输出存进 `build/<实验>/board.log`。看到 `uart_boot.py` 开始等待时按复位。
有 tag（如 `e01`）的实验构建 tag 上的固件，其余用当前工作树（含未提交的改动）；不想被问时写 `make board EXPERIMENT=E01`。

构建过的固件按 `firmware/` 与 `host/build/` 的内容缓存在 `build/ldr/`，这两处没变就直接取缓存，不连 Windows 主机。
`.ldr` 同时留在 `build/<实验>/`，之后只想启动、不构建，用 `make boot`（同样会问，或带 `EXPERIMENT=`）。
新增一个实验只需建 `coursework/E<nn>-<名字>/` 目录，菜单里就有它。
只构建不上板用 `host/build/remote_build.sh`（可带一个 commit，如 `e01`）。
其余启动方式见 [ADR 0001](docs/adr/0001-mac-board-testing-via-uart-boot.md) 的「验证步骤」。

## 许可

[Apache License 2.0](LICENSE)。可以自由使用、修改和商用；再分发时请保留 `LICENSE` 和 [`NOTICE`](NOTICE)（里面写着出处），
改过的文件注明改动。上面列出的 ADI 文件不在仓库里，也不在这个许可之内。
