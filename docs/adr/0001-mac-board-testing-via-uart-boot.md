# ADR 0001: 在 Mac 上不经调试器运行和测试板卡（UART 启动）

- 状态：提议（待上板验证）
- 日期：2026-09-24
- 适用：`apps/audio_loopback`，ADSP-SC589 / ADSP-SC573 EZ-Kit

## 背景

日常开发在 Mac（Apple Silicon, arm64）上进行，但：

- CCES 只有 Windows 和 Linux x86_64 版本。Linux 版的二进制是 ELF/glibc/x86_64，
  macOS 无法直接运行；ICE-1000/ICE-2000 的主机端驱动和调试服务也没有 macOS 版本。
- 因此 Mac 无法通过 ICE 把 `.dxe` 下载到板卡。编译产物（`.dxe` / `.ldr`）本身与主机无关，可以拷贝。

目标：在 Mac 上完成"加载程序 → 运行 → 看结果"，用于日常听效果和跑回归；需要单步/断点时再回到 Windows + ICE。

## 考虑过的方案

| 方案 | 说明 | 结论 |
|---|---|---|
| A. Mac 远程触发 Windows | 板卡和 ICE 固定接在 Windows 上，Mac 通过 ssh 调 CCES 命令行工具编译、加载，串口日志回传 | 可靠，保留作为调试路径；但日常依赖 Windows 在线 |
| **B. UART 启动（本 ADR 选择）** | Windows 生成 `.ldr`，Mac 通过板载 USB 转串口按 Boot ROM 协议发送 `.ldr`，并直接读取串口输出 | 不需要调试器；只能跑，不能单步 |
| C. ADI OpenOCD 分支 | 自行在 macOS 编译，走 ICE | SHARC+ 核支持不确定，维护成本高，不采用 |
| D. Mac 上跑 Windows 虚拟机 | Apple Silicon 只能跑 ARM 版 Windows，ICE 的 x86 内核驱动大概率无法安装 | 不可行 |

## 决定

采用方案 B，分阶段落地：B1（从 Flash 启动，Mac 只看结果）作为保底，B2（Mac 直接 UART 启动）作为目标。编译仍在 Windows（或 Mac 上的 x86_64 Linux 虚拟机 + CCES Linux 版）上完成。

### 仓库中已有的依据

- EZ-Kit 板载 USB 转串口接 UART0：SC589 为 P8，SC573 为 P7
  （见 `services/stdio/stdio_char_echo/Readme_stdio_char_echo.html`）。
- ADI 官方流程已支持"生成 ldr → 写 Flash → BMODE=1 启动 → 输出到 UART"
  （见 `Power_On_Self_Test/Readme/Readme_Power_On_Self_Test_SC589.html`）。
- 脱离调试器后需要初始化代码来配置 CGU/DDR（调试会话中由 preload 完成），可直接使用
  `Power_On_Self_Test/Custom_Init_Preload_Code/prebuilt_exe/Post_Init_SC589_Core0`（SC573 同目录有对应文件）。
- printf 重定向到 UART 的参考实现：`services/stdio/stdio_char_echo/char_echo.c`
  （`adi_stdio_RegisterUART` / `adi_stdio_OpenDevice` / `adi_stdio_RedirectStream`）。
- `apps/audio_loopback/SC589/SoftConfig_SC589.c` 中 `~UART0_EN`、`~UART0_FLOW_EN` 已设为 0（启用 UART0 和流控）。
- Core0（ARM）通过 `adi_core_enable` 启动两个 SHARC 核，多核可打包成一个 ldr。

### 需要的改动

1. **串口输出**：Core1 的 `main` 在软开关配置之后调用 `UartConsoleInit()`，把 stdio 重定向到 UART0
   （`apps/audio_loopback/uart_console.h`）。没有调试器时 JTAG 控制台看不到 `printf`。
2. **生成 ldr**：`tools/make_ldr.ps1 -Board SC589`（Windows，先编译三个核的 Debug）用 elfloader 把
   Core0/1/2 和 Post_Init 打包成 SPI master（B1）与 UART slave（B2）两份 binary ldr，不改工程配置。
   三个核也可以用 CCES headless builder 在命令行编译，用法写在脚本头部。
3. **Mac 端脚本**（pyserial，`pip install -r tools/requirements.txt`）：
   - `tools/uart_log.py`：读取串口、保存日志，按 `All done` / `Error` 给出通过或失败的退出码（B1 用）。
   - [ ] `tools/uart_boot.py`（B2）：自动波特率握手（发送检测字节，打印 Boot ROM 回应便于排查），
     在 CTS 流控下发送 `.ldr`，发完交给 `uart_log.py` 的读取逻辑。
     握手字节和流控要求以硬件参考手册（HRM）Boot ROM 章节为准，实现前先核对。

## 验证步骤（按风险从低到高）

1. **串口通路**：P8/P7 接到 Mac，确认出现 `/dev/cu.usbserial-*`。在 Windows 上用 ICE 运行 stdio_char_echo，
   在 Mac 上执行 `screen /dev/cu.usbserial-XXX 57600` 确认能收发。
2. **B1：程序脱离调试器能跑**：Windows 上用 `tools/make_ldr.ps1` 生成 ldr，用 CLDP 写入 Flash
   （`cldp -proc ADSP-SC589 -core 1 -emu 1000 -driver Device_Programmer/sc589/sharc/sc589_w25q128fv_dpia_Core1/sc589_w25q128fv_dpia_Core1.dxe -cmd prog -erase affected -format bin -file adc_dac_playback_SC589_spimaster.ldr`），
   BMODE=1，先在 Mac 上运行 `python3 tools/uart_log.py --log run.log`，再复位；看串口输出并试听音频。验证初始化代码、时钟、多核启动和串口输出。
3. **B2：Mac 直接 UART 启动**：BMODE 切到 UART 从机启动，按复位后运行 `python tools/uart_boot.py <app>.ldr`。

## 待确认事项 / 风险

- UART 从机启动对应的 BMODE 取值（SC589 预计为 3，SC573 需查手册）。
- 开关由软件在运行时配置；芯片还在 Boot ROM 阶段时，UART0 和流控在 EZ-Board 上电默认状态下是否已接通，需对照 EZ-Board 手册确认。若默认未接通，B2 不可行，退回 B1。
- Boot ROM 的握手字节和对 CTS/RTS 的要求，以及板载 USB 转串口芯片在 macOS 上的驱动情况。
- 在没有调试器时，重定向之前调用的 `printf` 行为不确定，所以重定向要放在最前面。
- 每次运行需要手动按复位键（除非后续引出复位控制）。

## 影响

- 正面：日常加载、运行、听效果、看日志都可以在 Mac 上完成，便于脚本化回归。
- 负面：B 方案不能单步和设断点；编译仍依赖 Windows 或 x86 Linux 环境；B1 每次改代码都要在 Windows 上重新烧写 Flash。
- 方案 A 仍保留，作为需要调试时的路径。
