# E02 多速率处理

在 SC589 的音频回环里把 48 kHz 降到 8 kHz，在 8 kHz 上处理，再升回 48 kHz。

## 文件

| 文件 | 内容 |
|---|---|
| `design.py` | 原型低通、8 kHz 上的 IIR 与单频表的设计，写出 `dsp/multirate_coeffs.{c,h}` |
| `figures.py` | 用 `dsplab.cdsp` 在 Mac 上跑固件的 `dsp/` 代码，作图并把报告引用的数字写成 `build/E02/results.tex` |
| `explore_chain.py` | 按 `# %%` 分格的探索脚本：C 处理链的响应与分块不变性、scipy 的抽取与内插参考 |
| `report.tex` | 报告正文，样式在 `../common/labreport.sty` |

## 迭代

| 回路 | 命令 | 看什么 |
|---|---|---|
| 设计与 C 实现 | 在编辑器的交互窗口里逐格运行 `explore_chain.py` | 改了 `dsp/*.c` 后重跑一格，`cdsp` 会自动重新编译 |
| 听 | `uv run python -m dsplab.wavrun in.wav build/E02/out.wav --play` | 混叠和镜像，不经板卡 |
| 板卡 | `make board` | 周期数、模拟通路 |

能支撑报告结论的探索留在 `explore_*.py` 里并入库；走不通的放在仓库根目录的 `scratch/`（不入库）。

## 生成报告

```bash
make e02
```

产物在 `build/E02/report.pdf`，署名的规则与 E01 相同。

## 要求

课程给出的要求：

- [x] 输入信号 48 kHz → 8 kHz（降采样 6:1）
- [x] 输出信号 8 kHz → 48 kHz（升采样 1:6）
- [x] 自行设计低通滤波器：`design.py` 的原型低通，降采样与升采样共用
- [x] 验证 8 kHz 信号有效：截止频率 2 kHz 的 IIR 低通（偶数声道）
- [x] 验证 8 kHz 信号有效：生成单频信号（奇数声道）

实现与验证：

- [x] 多相降采样与升采样进 `dsp/multirate.c`，接进 `audio_process.c`
- [x] `dsp/test/` 测分块不变性、链路频响与混叠、单频电平与镜像
- [x] 在 noah 上用 CCES 构建通过
- [x] 在板上运行（`make board`），报告里出现冲激响应对比与周期数
- [ ] 在板上听：偶数声道是低通后的输入，奇数声道是 1 kHz 单频
