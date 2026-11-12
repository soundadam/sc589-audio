# E04 基于重叠保留法的实时滤波

在 SC589 的音频回环里把 48 kHz 降到 16 kHz，在 16 kHz 上用重叠保留法做 FIR 滤波，再升回 48 kHz。
偶数声道是 255 阶 FIR（普通重叠保留），奇数声道是 1023 阶 FIR，拆成 4 段、每段 256 个抽头（分段滤波）。
两者都是 512 点实数 FFT、帧移 256，比逐点 FIR 多 256 个样本（16 ms）的延迟。

## 文件

| 文件 | 内容 |
|---|---|
| `design.py` | 降采样与升采样的原型低通、16 kHz 上的 255 阶与 1023 阶 FIR，写出 `dsp/ols_coeffs.{c,h}` |
| `figures.py` | 用 `dsplab.cdsp` 在 Mac 上跑固件的 `dsp/` 代码并与 scipy 比较，作图并把报告引用的数字写成 `build/E04/results.tex`；有 `build/E04/board.log` 时加上板上的冲激响应与周期数 |
| `test_chain.py` | 整个处理链对 `scipy.signal.lfilter` 的逐样本比较，`make check` 运行 |
| `report.tex` | 报告正文，样式在 `../common/labreport.sty` |

C 代码在 `firmware/audio_loopback/dsp/`：

| 文件 | 内容 |
|---|---|
| `rfft.c` | 自写的实数 FFT 与反变换（N/2 点基 2 复数 FFT 加分裂步骤） |
| `ols.c` | 重叠保留，`P` 段均匀分区（`P = 1` 即普通重叠保留）；`OlsStagger` 把各声道的帧边界错开 |
| `chain_e04.c` | E04 的处理链：`multirate.c` 降采样，`ols.c` 滤波，`multirate.c` 升采样 |
| `chain_e02.c` | E02 的处理链，从原来的 `audio_process.c` 拆出，行为不变 |
| `audio_process.c` | 按 `AudioProcessSelect()` 选的实验分派到 `chain_e0n.c`，默认 E04 |
| `test/test_ols.c` | FFT 对直接 DFT、重叠保留与分段对直接卷积、分块与错开不改变输出 |

板上启动时 `platform/filter_report.c` 打出 255 阶与 1023 阶在 16 kHz 上的冲激响应，以及逐点 FIR、
重叠保留、分段滤波、自写 FFT/IFFT、CCES `rfft512` 和整个 `AudioProcess` 的周期数。

## 迭代

| 回路 | 命令 | 看什么 |
|---|---|---|
| C 实现 | `make check` | `test_ols` 与 `test_chain.py` |
| 听 | `uv run python -m dsplab.wavrun in.wav build/E04/out.wav --experiment 4 --play` | 两个低通的差别，不经板卡 |
| 板卡 | `make board EXPERIMENT=E04` | 冲激响应、周期数、模拟通路 |

## 生成报告

```bash
make e04
```

产物在 `build/E04/report.pdf`，署名的规则与 E01 相同。没有 `build/E04/board.log` 时，报告的“板上运行”一节是待测的占位。

## 要求

课程给出的要求：

- [x] 255 阶 FIR，以 16 kHz 处理（48 kHz 3:1 降采样后）
- [x] 用重叠保留法实现：帧长 512 = 2 × 帧移 256
- [x] 比较重叠保留与逐点时域 FIR 的运算效率：报告里的乘法计数与板上周期数（255 阶快 1.3 倍，1023 阶分段快 5.0 倍）
- [x] 提高：分段滤波，1023 阶拆成 4 个 255 阶、每段 256
- [x] 系数倒序：课件写成点积时需要；本实现的环形延时线与频域卷积都不需要，报告里说明

实现与验证：

- [x] 纯 C99 的 FFT、重叠保留、分段滤波进 `dsp/`，接进 `audio_process.c`；降采样与升采样复用 `multirate.c`
- [x] `dsp/test/` 与 `test_chain.py` 在 Mac 上对 scipy 逐样本比较，`make check` 运行
- [x] E02 的测试与结果不变
- [x] `filter_report.c` 用 cc21k（CCES 2.9.3）编译通过
- [x] 在 noah 上完整构建（`remote_build.sh`；在 worktree 里要设 `ADI_DIR` 为 `sc589-adi` 的绝对路径）
- [x] 在板上运行（`make board EXPERIMENT=E04`）：冲激响应与设计差 1.2e-8 / 2.7e-8；周期数进了报告；CCES `rfft512` 与自写 FFT 几乎相同（36,315 对 36,144）
- [ ] 在板上听
