# E03 实时 FFT 和 IFFT

在 SC589 的音频回环里实现 32 点 FFT 和 IFFT，并与 CCES 运行库（`<trans.h>` 的 `rfft32`、`cfft32`、`ifft32`）比较。

## 文件

| 文件 | 内容 |
|---|---|
| `design.py` | 旋转因子、比特倒序表、频点增益，写出 `dsp/fft_tables.{c,h}` |
| `figures.py` | 用 `dsplab.cdsp` 在 Mac 上调用 `dsp/fft.c` 与 `AudioProcess`，与 numpy 比较、作图，把报告引用的数字写成 `build/E03/results.tex`；有板卡日志时加上与运行库的比较和周期数 |
| `report.tex` | 报告正文，样式在 `../common/labreport.sty`；关键代码直接从 `dsp/fft.c` 引入 |

固件部分：

| 文件 | 内容 |
|---|---|
| `dsp/cplx.h` | 复数类型与加、减、乘、共轭、乘 j |
| `dsp/fft.{c,h}` | 基 2 时间抽取 FFT/IFFT；实序列 FFT/IFFT（打包成 16 点复数） |
| `dsp/audio_process.c` | 每声道 32 点帧：偶数声道 FFT→IFFT（输出为延迟 32 点的输入），奇数声道把 3 kHz 以上频点置零 |
| `platform/filter_report.c` | 只在板上：同一帧上跑自写变换与运行库，打出向量、回环误差、冲激响应和周期数 |

## 生成报告

```bash
make e03
```

产物在 `build/E03/report.pdf`。没有 `build/E03/board.log` 时，报告的“与 CCES 运行库比较”一节只写明尚未上板；
上板后（`make board EXPERIMENT=E03`）重新 `make e03` 即可补上。运行库的比例（是否除以 N、`rfft32` 是否填满全部频点）
由 `figures.py` 从打出的向量拟合，不预先假定。

## 要求

课程给出的要求：

- [x] 32 点实时 FFT 与 IFFT，接进音频回环（`audio_process.c`）
- [x] 旋转因子等公共变量预先初始化：`design.py` 生成 `const` 表，运行时不调用 sin/cos
- [x] 复数运算自行编写：`dsp/cplx.h`
- [x] 与 CCES 运行库 `rfftN`/`ifftN` 比较结果与效率：`filter_report.c` 在板上运行，结果进报告第 4.4 节
- [x] 报告：原理与框图、关键代码、结果分析

实现与验证：

- [x] `dsp/test/` 测表、复数与实 FFT/IFFT 对双精度 DFT、往返误差、实时链（偶数声道为延迟副本，奇数声道为逐帧低通）
- [x] 在 noah 上用 CCES 构建通过
- [x] 在板上运行（`make board EXPERIMENT=E03`），报告里出现运行库比较与周期数
- [ ] 在板上听：偶数声道与输入相同（延迟 0.67 ms），奇数声道只剩 3 kHz 以下
