---
paths:
  - "vendor/**"
---

# 不改 ADI 导入的代码

`vendor/adi/` 下是 ADI 官方示例和驱动源码，只作参考。要基于某个示例做开发时，先把它移到 `apps/` 下再改
（`audio_loopback` 就是这样从 `vendor/adi/examples/drivers/adc/adau1979` 移出来的）。

**原因：** 保持与初始导入（`1721959`）和 CCES 安装目录里的版本一致，读寄存器行为或对比上游时
diff 才干净。
