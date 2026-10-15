# E01 数字滤波器设计与实现

在 SC589 上实现截止频率 2 kHz（fs = 48 kHz）的 31 阶 FIR 与 8 阶 IIR 低通，
通过输入输出比较单位冲激响应、幅频、相频、群时延与计算量。
报告用宋体 + Times New Roman、小四、1.5 倍行距，含框图、关键代码与结果分析。

## 文件

| 文件 | 内容 |
|---|---|
| `design.py` | 设计两组系数，生成 `firmware/audio_loopback/dsp/lowpass_coeffs.{c,h}` |
| `figures.py` | 由设计和 `build/board.log` 作图，并把报告引用的数字写成 `build/E01/results.tex` |
| `report.tex` | 报告正文，样式在 `../common/labreport.sty` |

## 生成报告

```bash
make board
```

```bash
make e01
```

`make board` 让板卡打印冲激响应和周期数到 `build/board.log`；`make e01` 作图并编译出 `build/E01/report.pdf`。
改了 `design.py` 之后先运行 `uv run coursework/E01-fir-iir/design.py` 重新生成系数，再 `make board`。

## 要求

- [x] 31 阶 FIR、8 阶 IIR 低通，fc = 2 kHz，在板上运行
- [x] 单位冲激响应、幅频、相频、群时延、计算量的比较
- [x] 报告格式、框图、关键代码、结果分析
- [ ] 用信号源经 ADC → 滤波 → DAC 实测幅频响应（现有数据只验证板上浮点运算与设计一致，不含模拟通路）
- [ ] 开编译优化后的周期数（现有数据来自 Debug 配置）
