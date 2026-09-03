# OpenWave —— 基于 openvela 的高精度采集与波形显示终端

OpenWave 是一个运行在 openvela（NuttX）上的端侧数据采集终端。它在
STM32H7A3 单颗 MCU 上完成 16 位 ADC 采样、双精度浮点实时信号处理
（均值 / RMS / 峰峰值 / 过零频率 / FFT），并通过 0.96 英寸 SSD1306
OLED 显示实时波形与参数；支持按键交互、LED 告警、RTC 时间戳与 Flash
快照记录。纯端侧应用，不联网、不依赖云端。

## 一、硬件平台

- 开发板：NUCLEO-H7A3ZI-Q（STM32H7A3ZIT6Q，Cortex-M7 @ 280 MHz）
- 2 MB Flash，1.4 MB SRAM，单/双精度 FPU，DSP 指令
- 16 位 ADC1（通道 5/10/12/13/15），TIM1 互补 PWM
- SPI3、I2C1/I2C2、USART3（ST-Link 虚拟串口 115200 8N1）
- RTC、看门狗、TRNG、内部 Flash MTD
- 0.96 英寸 SSD1306 OLED（128×64，SPI）

### 设备节点

| 外设 | 设备节点 |
| --- | --- |
| ADC1 | `/dev/adc0` |
| TIM1 PWM | `/dev/pwm0` |
| SSD1306 OLED | `/dev/lcd0` |
| USER 按键 | `/dev/buttons` |
| 用户 LED | `/dev/userleds` |
| RTC | `/dev/rtc0` |
| 内部 Flash MTD | `/dev/progmem0` |
| 串口控制台 | `/dev/console`（ttyS0） |

## 二、接线

> 注意：以下引脚大部分位于板边 **morpho 排针 CN11/CN12** 上，需要先
> 焊接排针后才能用杜邦线可靠连接。

### OLED（SSD1306，128×64，SPI，7 引脚）

| OLED 引脚 | 接到板子 |
| --- | --- |
| VCC | 3V3 |
| GND | GND |
| SCK | PB3（SPI3 SCK） |
| MOSI | PB5（SPI3 MOSI） |
| CS | PE11 |
| DC | PE12 |
| RES | PE13 |

### PWM 自闭环（测试信号回采）

用一根杜邦线把 **PE9（TIM1_CH1）** 直接连到 **PB1（ADC 通道 5）**。

## 三、编译

> 板级适配依赖 nuttx/vendor 的补丁（logs/patch_nuttx.diff、
> logs/patch_vendor_st.diff）。若从干净源码构建，请先应用：
>
> ```bash
> bash ~/openvela/contest2026_406_mengxiangchengweigaoshou/apply_patches.sh
> ```

```bash
cd ~/openvela
./build.sh $(pwd)/vendor/st/boards/stm32h7a3/nucleo-h7a3zi-q/configs/m2-bsp -j$(nproc)
```

产物：`~/openvela/nuttx/nuttx.bin`

## 四、烧录

```bash
cd ~/openvela
openocd -f interface/stlink.cfg -f target/stm32h7x.cfg \
  -c "program nuttx/nuttx.bin 0x08000000 verify reset exit"
```

看到 `** Verified OK **` 即成功。烧录后**拔掉 USB 线等 2 秒再插回**
（PWR_CR3 供电配置只能在上电瞬间写入一次）。

## 五、运行

串口终端（115200 8N1）连接 ST-Link 虚拟串口：

```bash
minicom -D /dev/ttyACM0 -b 115200
```

进入 NSH 后：

```bash
openwave              # 分析模式，默认 64 个窗口后退出
openwave -n 0         # 无限运行
openwave -n 20        # 运行 20 个统计窗口
openwave -T           # 接线自检（OLED + PWM/ADC 回环）
openwave -f 1000 -n 5 # 开启 1 kHz PWM 测试信号并分析
openwave -w 0 -n 5    # 原始采样模式（打印每通道 raw/mV）
```

主要选项：

| 选项 | 说明 |
| --- | --- |
| `-n <窗口数>` | 统计窗口数，0 = 无限 |
| `-w <窗口大小>` | 每窗口采样数（默认 256，0 = 原始模式） |
| `-c <通道>` | 分析通道（默认 5） |
| `-f <频率>` | PWM 测试信号频率（0 = 关闭） |
| `-D <占空比>` | PWM 占空比百分比 |
| `-t` | 每次读取前发软件触发（默认已开启） |

## 六、功能说明

### 采集（M2）

- 从 `/dev/adc0` 读取 16 位采样，5 通道（5/10/12/13/15）
- 每次读取前发 `ANIOC_TRIGGER` 软件触发（H7A3 板载 ADC 需要）
- 每通道约 200 Hz 采样率

### PWM 自闭环（M3）

- 通过 `/dev/pwm0`（TIM1）输出可调频率 / 占空比方波
- 杜邦线 PE9→PB1 接回 ADC，形成自闭环验证

### 信号处理（M4）

- 均值、RMS、峰峰值、过零频率
- 256 点 radix-2 FFT 辅助测频

### OLED 显示（M5）

- 128×64 单色屏实时绘制波形曲线
- 顶部显示通道与采样率，底部显示 RMS / Vpp / 频率

### 交互与持久化（M6）

- **短按 B1**：切换分析通道（5→10→12→13→15→5）
- **长按 B1**：保存快照（RTC 时间戳 + 当前统计），三个 LED 闪烁确认
- **阈值告警**：Vpp 超过 2500 mV 时 LED0 点亮
- 快照打印到串口；Flash 写入尝试后自动降级为串口确认

## 七、工程结构

```text
app/openwave/
├── Kconfig             # 应用配置项
├── Makefile / CMakeLists.txt
├── openwave_main.c     # 入口 + 事件循环（M6 交互）
├── wave_core.c/h       # ADC 采样、环形缓冲
├── wave_dsp.c/h        # 均值/RMS/Vpp/过零/FFT
├── wave_gen.c/h        # PWM 测试信号
├── wave_ui.c/h         # SSD1306 波形/文本绘制
├── wave_input.c/h      # 按键短按/长按识别
└── wave_store.c/h      # 快照（RTC + Flash/串口）
```

## 八、里程碑完成情况

| 里程碑 | 内容 | 状态 |
| --- | --- | --- |
| M1 | 工程骨架可编译，NSH 可运行 openwave | ✅ |
| M2 | ADC 采集（raw/mV 输出） | ✅ |
| M3 | PWM 测试信号 + 回环 | ✅（固件，回环待接线） |
| M4 | 信号处理（均值/RMS/Vpp/频率/FFT） | ✅ |
| M5 | SSD1306 波形与参数显示 | ✅（固件，屏幕待接线） |
| M6 | 按键/LED/RTC/Flash 快照 | ✅ |

## 九、已知限制

1. **Flash 快照降级**：板载 Flash 驱动为 H743 兼容驱动，H7A3 上仅第一
   个 32 字节页可编程，后续写入返回 EIO；快照按任务书以**串口确认**
   验收。如需真正的 Flash 持久化，需要补齐 H7A3 原生 Flash 驱动。
2. **OLED/PWM 物理验证**：morpho 排针需焊接后才能接线验证屏幕显示与
   PWM 回环；固件侧驱动均已就绪（`/dev/lcd0`、`/dev/pwm0` 存在）。
3. **NSH `help` 小问题**：`help` 列出内置应用时可能触发一次 panic，
   不影响 openwave 运行。
4. **RTC 默认时间**：未设置时 RTC 读回 2000-01-01，可用 NSH `date`
   命令设置。
5. **采样率**：软件触发模式下每通道约 200 Hz；如需更高采样率应配置
   定时器触发（如 TIM2_ADC）。

## 十、演示流程（视频脚本）

1. 打开串口，展示 NSH 启动；
2. `openwave -n 0` 无限运行，展示实时统计（50 Hz 工频干扰可被正确检测）；
3. 短按 B1 依次切换通道 5→10→12→13→15；
4. 长按 B1 保存快照，展示时间戳与统计、LED 闪烁；
5. 若已接线 OLED，展示屏幕上的实时波形；
6. `openwave -T` 展示接线自检。

## 十一、AI 协作说明

本项目的应用代码在 Windows 侧开发，通过临时文件服务同步到 Ubuntu
虚拟机，在虚拟机内编译、烧录、验证。板级适配期间修复了多个厂商
适配遗留问题（SMPS 供电配置、H7A3 内存尺寸、按键/LED 驱动注册等），
详见 `logs/` 目录的 AI 编码日志。
