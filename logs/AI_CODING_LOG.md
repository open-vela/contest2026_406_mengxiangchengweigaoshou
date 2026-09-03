# OpenWave AI 编码日志

> 记录 OpenWave 应用开发与板级 bring-up 的关键过程、问题与修复。
> 时间跨度：2026-08-24 ~ 2026-08-30。

## 一、总体进度

- M1 工程骨架：✅（NSH 可运行 openwave）
- M2 ADC 采集：✅（软件触发，5 通道）
- M3 PWM 自闭环：✅ 固件（回环待接线）
- M4 信号处理：✅（均值/RMS/Vpp/过零/FFT）
- M5 OLED 显示：✅ 固件（屏幕待接线）
- M6 交互与持久化：✅（短按/长按/LED/RTC/快照）
- M7 README/视频：README 已完成，演示视频待录制

## 二、板级 bring-up 关键修复（厂商适配遗留问题）

### 1. 上电卡死在时钟配置（PWR_CR3 供电模式）

**现象**：固件烧录后无任何串口输出，OpenOCD 显示 PC 停在
`stm32_stdclockconfig`（VOSRDY 等待循环）。

**根因**：NUCLEO-H7A3ZI-Q 默认以**内部 SMPS 直供**内核（原理图
Supply Config 2：SMPS ON、LDO OFF），而 NuttX 启动代码把 PWR_CR3
写成 LDO 模式（0x06，非法组合），VOSRDY 永不置位。

**修复**：在 `stm32h7x3xx_rcc.c` 中强制 H7A3ZI 写 `PWR_CR3 =
0x04`（SMPSEN=1、LDO 关）。注意 PWR_CR3 只在 POR 后可写一次，
烧录后必须拔电重启。

### 2. 堆初始化总线错误（H7A3 内存尺寸错误）

**现象**：供电修复后，系统在 `arm_addregion` 建立堆时触发不精确
总线错误。

**根因**：厂商用 H743 内存尺寸适配 H7A3：AHB SRAM 被当成 288 KB
（实际 128 KB）、SRD SRAM 64 KB（实际 32 KB），堆初始化写入不存在
的内存。

**修复**：
- `chip.h` 中 H7X3XX 分支内存尺寸改为 H7A3 真实值（AXI 1 MB、
  SRAM123 128 KB、SRAM4 32 KB）；
- defconfig：`RAM_START=0x20000000`、`RAM_SIZE=131072`、
  `MM_REGIONS=3`、`STM32H7_DTCMEXCLUDE=y`（主堆用满 128 KB DTCM，
  避免 DTCM 被重复加入堆）。

### 3. 芯片型号适配说明

厂商的 H7A3 适配尚未完成（Kconfig 无 `ARCH_CHIP_STM32H7A3ZI`），
本工程沿用其做法：以 `CONFIG_ARCH_CHIP_STM32H743ZI` 顶替编译，
依靠 H7A3 与 H743 约 90% 共享的外设布局。该做法已通过实测。

### 4. OLED 驱动未注册（条件编译错误）

板级 `stm32_bringup.c` 用 `#ifdef CONFIG_LCD_DRIVER` 包住
`board_lcd_initialize`/`lcddev_register`，但 openvela 并不存在
`CONFIG_LCD_DRIVER` 符号（实际为 `CONFIG_LCD_DEV`），导致
`/dev/lcd0` 永远不出现。修复：改为 `#ifdef CONFIG_LCD_DEV`。

### 5. 按键/LED 设备未注册

`/dev/buttons`、`/dev/userleds` 缺失。根因：
- `INPUT_BUTTONS_LOWER` 依赖 `ARCH_BUTTONS && ARCH_IRQBUTTONS`，
  二者又依赖隐藏的 `ARCH_HAVE_BUTTONS/ARCH_HAVE_IRQBUTTONS`；
- `USERLED_LOWER` 依赖隐藏的 `ARCH_HAVE_LEDS`；
- 本板无板级 Kconfig，无人 select 这些能力开关。

修复：在 `arch/arm/src/stm32h7/Kconfig` 的 H743ZI 入口补
`select ARCH_HAVE_LEDS/BUTTONS/IRQBUTTONS`，defconfig 打开
`ARCH_BUTTONS`、`ARCH_IRQBUTTONS`、`INPUT_BUTTONS_LOWER`、
`USERLED_LOWER`。

### 6. ADC 默认无触发

H7A3 板载 ADC 需要触发才开始转换。应用默认在每次读取前发送
`ANIOC_TRIGGER`（`swtrig=true`），实现约 200 Hz/通道采样。

## 三、应用层开发要点（M2-M6）

- **wave_core**：`/dev/adc0` 批量读取，环形缓冲，mV 换算。
- **wave_dsp**：均值/RMS/Vpp/过零频率 + 256 点 radix-2 FFT。
- **wave_ui**：128×64 影子帧缓冲，5×7 点阵字体，波形/文本绘制，
  经 `/dev/lcd0` 的 `LCDDEVIO_PUTAREA` 刷新。
- **wave_input**：`/dev/buttons` 轮询 + 边沿检测，按按压时长区分
  短按（<500 ms）与长按（≥500 ms）。
- **wave_store**：快照 = RTC 时间戳 + 统计；写入最后一个 Flash 扇区
  （0x081E0000，远离固件）的 32 字节槽位；写入失败自动降级为串口确认。

### 短按不响应的修复

初版主循环每约 1.2 秒（一个统计窗口）才轮询一次按键，短按被完全
错过。改为**分片采集**：每读一批 ADC 数据就轮询按键，事件暂存、
窗口统计完成后处理，短按即时响应。

## 四、实测记录

- `openwave -n 5`：5 通道 raw/mV 正常输出；
- 分析模式：`[stats]` 行正常，浮空引脚可检测到约 50 Hz 工频干扰
  （过零 49.4 Hz / FFT 50.0 Hz 均正确）；
- 短按 B1：通道 5→10→12→13→15→5 循环切换；
- 长按 B1：快照打印（RTC 时间戳），LED 闪烁；
- `/dev` 全部设备节点就绪（adc0/pwm0/lcd0/buttons/userleds/rtc0/
  progmem0/oneshot/watchdog0/ttyS0/ttyS1…）。

## 五、已知限制

1. Flash 快照降级为串口确认（H743 闪存驱动与 H7A3 不完全兼容，
   第二个 32 字节页编程 EIO）；
2. OLED/PWM 物理验证需先焊接 morpho 排针；
3. NSH `help` 列内置应用时偶发 panic（不影响 openwave）。

## 六、协作方式

应用代码在 Windows 侧编写，通过临时文件服务（uguu.se）同步到
Ubuntu 虚拟机，虚拟机内编译/烧录/验证；板级与内核补丁均在文档中
记录，便于复核与回滚。
