# E02 多速率处理

在 SC589 的音频回环上做降采样与多速率处理。

## 文件

| 文件 | 内容 |
|---|---|
| `figures.py` | 用 `dsplab.cdsp` 在 Mac 上跑固件的 `dsp/` 代码，作图并把报告引用的数字写成 `build/E02/results.tex` |
| `explore_chain.py` | 按 `# %%` 分格的探索脚本：C 处理链的响应、scipy 的抽取与内插参考 |
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

- [ ] 抄录课程给出的作业要求
- [ ] `design.py`：抽取与内插滤波器设计，生成 `dsp/` 下的系数
- [ ] 多速率处理链进 `audio_process.c`
- [ ] `dsp/test/` 加分块不变性与阻带抑制的测试
- [ ] 在板上运行，记录周期数
