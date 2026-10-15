# ADI 的代码不进这个仓库

这个仓库是公开的。带 Analog Devices 版权声明（「proprietary and confidential」或「Analog Devices Inc., All Rights Reserved」）
的文件、ADI 示例和驱动源码的拷贝，都放在私有仓库 `soundadam/sc589-adi`（本机 `../sc589-adi`）里，不提交到这里。

- 改这些文件：在 `../sc589-adi` 里改、在那里提交；新增一个要参与构建的 ADI 文件，同时写进它的 `firmware-files.txt`，
  `host/build/remote_build.sh` 按这份清单把文件叠加进构建快照。
- 只作参考的 ADI 示例和驱动源码在 `../sc589-adi/vendor/adi/`，原样保存，不改。
- 提交前看一眼新增文件的头部注释；从 ADI 示例或 CCES 生成的文件拷来的，归 `sc589-adi`。
- `refs/remote-build/snapshot` 含有这些文件，只在本机和 Windows 构建机之间传，不 push。

**原因：** ADI 的许可不允许公开再分发这些源码；进过公开历史就撤不回来了。
