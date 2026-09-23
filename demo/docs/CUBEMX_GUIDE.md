# CUBEMX_GUIDE.md — STM32CubeMX 逐项配置指南（智能盲杖 Demo v1）

> 阶段 1 产出物，与 `PINOUT.md` 配套。
> 目标：**照着点，全程不需要想**。所有数值已算好（时钟、分频、波特率、PWM 频率）并注明原因。
> 环境：STM32CubeMX 6.x（6.3 以上均可）、Keil MDK5、芯片 **STM32F103C8Tx**。
>
> **操作顺序**：第 1 节 → 第 10 节，一气呵成。**中途不要点 "GENERATE CODE"**，全部配完再生成。
> 工程生成位置：`f:/RM/smart_sti/demo/SmartStick`

---

## 0. 配置总览（最后对照检查用）

| 项 | 最终值 | 说明 |
|---|---|---|
| SYSCLK | **72 MHz** | HSE 8MHz × 9 |
| HCLK / APB1 / APB2 | 72 / 36 / 72 MHz | APB1 分频≠1，故 TIM 时钟仍 72MHz |
| ADC 时钟 | 12 MHz | 72 / 6（上限 14MHz） |
| I2C1 | 400 kHz Fast Mode | OLED + MPU6050 + QMC5883L |
| USART1 | **115200** 8N1 | 调试 shell（USB-TTL） |
| USART2 | 9600 8N1 | 超声波 UART 模式 |
| USART3 | 9600 8N1 | 蓝牙透传 |
| TIM3 CH3/CH4 | **18 kHz** PWM | PSC=3, ARR=999 → 占空比刻度 0~1000 |
| TIM4 | 1 MHz 计数（1µs） | PSC=71, ARR=65535（超声波/超时基准） |
| ADC1_IN4 | 12 bit，连续转换 | PA4 电池电压 |

---

## 1. 新建工程

1. `File → New Project`（首页 `ACCESS TO MCU SELECTOR` 同效）。
2. 搜索 **STM32F103C8**，列表选 **STM32F103C8Tx**（LQFP48），点 `Start Project`。
3. 若弹窗 "Initialize all peripherals with their default Mode?" → 点 **No**。
   （默认模式会占掉一堆引脚，不要用。）

---

## 2. System Core 配置

### 2.1 RCC（时钟源）

左侧 `System Core → RCC`：

| 参数 | 设置为 |
|---|---|
| High Speed Clock (HSE) | **Crystal/Ceramic Resonator** |
| Low Speed Clock (LSE) | Disable |

> 前提：Blue Pill 板上焊着 8MHz 晶振（标准板都有）。若你的板子没有晶振，见第 10 节兜底。

### 2.2 SYS（调试口 + 时基）

左侧 `System Core → SYS`：

| 参数 | 设置为 | 原因 |
|---|---|---|
| Debug | **Serial Wire** | 保留 SWD 给 ST-Link，同时**自动释放 PB3/PB4/PA15**（否则 PB4 按键不可用） |
| Timebase Source | **SysTick** | 不用 TIM4（TIM4 我们要作微秒基准） |

---

## 3. Clock Configuration（时钟树）

切到 `Clock Configuration` 标签页，**只改下面列出的项**：

1. 顶部 `Input frequency` 的 **HSE 填 `8`** MHz。
2. **PLL Source Mux** → **HSE**。
3. **PLL Mul** → **×9**。
4. **System Clock Mux** → **PLLCLK**。
5. **AHB Prescaler** → **/1**（HCLK = 72 MHz）。
6. **APB1 Prescaler** → **/2**（36 MHz）。
7. **APB2 Prescaler** → **/1**（72 MHz）。
8. **ADC Prescaler** → **/6**（12 MHz）。
9. **USB Prescaler** → 保持 `/1.5`（未用 USB）。

**检查点**：右下角显示 `SYSCLK = 72.000000 MHz`，页面无红色报错。

---

## 4. 外设配置（Pinout & Configuration）

### 4.1 I2C1（OLED + MPU6050 + QMC5883L）

`Connectivity → I2C1`，模式选 **I2C**（不是 SMBus）。
引脚应自动落到 **PB6 = I2C1_SCL、PB7 = I2C1_SDA**。

| Parameter Settings | 值 |
|---|---|
| I2C Speed Mode | **Fast Mode** |
| I2C Clock Speed (Max) | **400000** |
| Clock No Stretch Mode | Disabled |
| Primary Address Length selection | 7-bit |
| Dual Address Acknowledged | Disabled |
| Primary slave address | 0 |
| General Call address detection | Disabled |
| Analog Filter / Digital Filter | 保持默认（Enabled / 0） |

- **NVIC Settings**：全部不勾（轮询 + HAL 超时，简单可靠）。
- **GPIO Settings（自动）核对**：PB6/PB7 = `AF Open Drain`、`No pull-up and no pull-down`、Output speed `Low`。
  > 上拉由模块自带（每个模块 4.7kΩ），**CubeMX 不要开内部上拉**。

### 4.2 USART1（调试 shell，115200）

`Connectivity → USART1`，Mode = **Asynchronous**。

| Parameter Settings | 值 |
|---|---|
| Baud Rate | **115200** Bits/s |
| Word Length | 8 Bits (including Parity) |
| Parity | None |
| Stop Bits | 1 |
| Data Direction | Receive and Transmit |
| Over Sampling | 16 Samples |
| 其余（Hardware Flow Control 等） | 全部 **Disable** |

- **引脚**：PA9 = USART1_TX、PA10 = USART1_RX。
- **NVIC Settings**：☑ `USART1 global interrupt` → Preemption Priority = **5**。

### 4.3 USART2（超声波 UART 模式，9600）

`Connectivity → USART2`，Mode = **Asynchronous**。

| Parameter Settings | 值 |
|---|---|
| Baud Rate | **9600** Bits/s |
| Word Length | 8 Bits |
| Parity | None |
| Stop Bits | 1 |
| Data Direction | Receive and Transmit |
| Over Sampling | 16 Samples |

- **引脚**：PA2 = USART2_TX、PA3 = USART2_RX。
- **NVIC Settings**：**全部不勾**（超声波是"我们发问、它回答"，轮询 + 超时足够）。

### 4.4 USART3（蓝牙，9600）

`Connectivity → USART3`，Mode = **Asynchronous**，参数与 USART2 **完全相同**（9600 / 8 / None / 1 / 16 Samples）。

- **引脚**：PB10 = USART3_TX、PB11 = USART3_RX。
- **NVIC Settings**：☑ `USART3 global interrupt` → Preemption Priority = **5**。
  > 蓝牙数据随时可能来，必须中断接收。

### 4.5 TIM3（双路 PWM → L298N 调速，18kHz）

`Timers → TIM3`：先把 **Channel3 选 `PWM Generation CH3`**、**Channel4 选 `PWM Generation CH4`**，再改参数。

| Parameter Settings | 值 | 原因 |
|---|---|---|
| Prescaler (PSC - 16 bits value) | **3** | 72MHz / (3+1) = 18MHz |
| Counter Mode | Up | 默认 |
| Counter Period (AutoReload Register) | **999** | 18MHz / 1000 = **18 kHz** |
| Internal Clock Division (CKD) | No Division | 默认 |
| auto-reload preload | Enable | 默认 |
| Trigger Output (TRGO) Parameter | Reset | 默认 |

**Channel3 / Channel4（两个通道改成完全一样）：**

| 参数 | 值 |
|---|---|
| Mode | PWM mode 1 |
| Pulse (16 bits value) | **0**（0% 占空比，上电不转） |
| Output compare preload | Enable |
| Fast Mode | Disable |
| CH Polarity | High |

- **GPIO Settings**：PB0 / PB1 = `AF Push Pull`、`No pull-up and no pull-down`、Maximum output speed = **High**。
- **NVIC Settings**：全部不勾。

> **写代码时的换算**：`占空比刻度 duty ∈ [0,1000]` → `__HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_3, duty)`。

### 4.6 TIM4（微秒计时基准，不占引脚）

`Timers → TIM4`，勾选 **Clock Source = Internal Clock**（即激活），只改两个参数：

| Parameter Settings | 值 | 原因 |
|---|---|---|
| Prescaler | **71** | 72MHz / 72 = **1MHz → 1 计数 = 1µs** |
| Counter Period | **65535** | 最大量程；溢出周期 65.536ms，驱动层处理回绕 |
| 其余（Mode/CKD/preload） | 保持默认 | — |

- **NVIC Settings**：全部不勾（直接读 `__HAL_TIM_GET_COUNTER`）。

### 4.7 ADC1（电池电压，PA4）

`Analog → ADC1`，勾选 **IN4**（PA4 变绿）。

| 分组 | 参数 | 值 |
|---|---|---|
| ADCs_Common_Settings | Mode | Independent mode |
| ADC_Settings | Clock Prescaler | **Synchronous clock mode divided by 6** |
| | Resolution | **12 bits** |
| | Data Alignment | Right alignment |
| | Scan Conversion Mode | Disabled |
| | Continuous Conversion Mode | **Enabled** |
| | Discontinuous Conversion Mode | Disabled |
| | DMA Continuous Requests | Disabled |
| | End Of Conversion Selection | EOC flag at the end of single channel conversion |
| ADC_Regular_ConversionMode | Enable Regular Conversions | Enable |
| | Number Of Conversion | **1** |
| | External Trigger Conversion Source | **Regular Conversion launched by software** |
| | Rank 1 → Channel | **Channel 4** |
| | Rank 1 → Sampling Time | **55.5 Cycles** |
| | Rank 1 → Offset | 0 |
| ADC_Injected_ConversionMode | 不用 | 保持默认 |

- **GPIO Settings（自动）**：PA4 = `ADC1_IN4`、模式 `Analog`、`No pull-up and no pull-down`。
- **NVIC Settings**：不勾。**DMA Settings**：不用。

### 4.8 GPIO（电机方向 / 报警 / 按键 / 心跳灯）

在 `System Core → GPIO` 或直接在芯片图上点引脚：输出选 `GPIO_Output`、输入选 `GPIO_Input`，
然后**右键引脚 → Enter User Label**，填下表的 User Label（**必须一字不差**，代码会用到）。

**输出：**

| 引脚 | User Label | GPIO output level | GPIO mode | Pull-up/Pull-down | Output speed |
|---|---|---|---|---|---|
| PB12 | `IN1` | **Low** | Output Push Pull | No pull-up and no pull-down | Low |
| PB13 | `IN2` | **Low** | Output Push Pull | No pull-up and no pull-down | Low |
| PB14 | `IN3` | **Low** | Output Push Pull | No pull-up and no pull-down | Low |
| PB15 | `IN4` | **Low** | Output Push Pull | No pull-up and no pull-down | Low |
| PB9 | `BUZZ` | **High** | Output Push Pull | No pull-up and no pull-down | Low |
| PB8 | `VIB` | **Low** | Output Push Pull | No pull-up and no pull-down | Low |
| PC13 | `LED_HB` | **High** | Output Push Pull | No pull-up and no pull-down | Low |

> - `IN1~IN4` 初值全 **Low** → 上电电机不转（L298N 真值表：IN1=IN2=0 为停止）。
> - `BUZZ` 初值 **High** → 按**低电平触发**的蜂鸣器模块，上电静音。若实测你的模块是高电平触发，代码改一个参数即可。
> - `VIB` 初值 Low → 不振动。
> - `PC13` 板载 LED **低电平点亮**，初值 High = 熄灭，作心跳灯。

**输入（按键）：**

| 引脚 | User Label | GPIO mode | Pull-up/Pull-down |
|---|---|---|---|
| PA11 | `SOS_KEY` | Input mode | **Pull-up** |
| PA12 | `MODE_KEY` | Input mode | **Pull-up** |
| PB4 | `KEY3` | Input mode | **Pull-up** |
| PB5 | `KEY4` | Input mode | **Pull-up** |

> 按键一端接引脚、另一端接 **GND**，按下读到低电平；内部上拉足够，不用外接电阻。
> PB4 能当输入的前提是第 2.2 节已设 `Debug = Serial Wire`。

### 4.9 不要碰的引脚

- **PA13 / PA14** → SWDIO / SWCLK，CubeMX 自动配置，别改。
- **PD0 / PD1** → 板载 8MHz 晶振（OSC_IN/OSC_OUT），保持 RCC 配置即可。
- **PA0 / PA1 / PA5 / PA6 / PA7 / PA15 / PB3** → **保持未配置**（预留）。会由第 9 节的选项自动设为模拟输入，属正常。

---

## 5. NVIC 中断优先级（统一设置）

`System Core → NVIC`：

1. 右上角 **Priority Group** 选 **4 bits for pre-emption priority / 0 bits for subpriority**。
   （若该下拉不可见，去 `NVIC` 页签勾选 "Force 4 bits..." 或在代码里保持 HAL 默认分组即可。）
2. 按下表填 **Preemption Priority**：

| 中断 | Preemption Priority | 说明 |
|---|---|---|
| `SysTick timer` | **15**（最低） | HAL 的 1ms 节拍，绝不能被业务抢占 |
| `USART1 global interrupt` | **5** | 调试 shell 收命令 |
| `USART3 global interrupt` | **5** | 蓝牙收数据 |
| 其余（I2C/TIM/ADC） | — | 不使能 |

> 本项目**不做中断嵌套**，全部是"收字节进环形缓冲"级别的轻活，优先级只保证"业务中断 > SysTick"即可。

---

## 6. Project Manager

### 6.1 Project

| 项 | 值 |
|---|---|
| Project Name | **SmartStick** |
| Project Location | `f:/RM/smart_sti/demo`（生成后为 `demo/SmartStick`） |
| Application Structure | **Advanced** |
| Toolchain / IDE | **MDK-ARM**，Min Version 选 **V5** |
| Firmware Package | 用推荐版本即可（`STM32Cube FW_F1 V1.8.x`）；提示下载就点 Download |

### 6.2 Code Generator

| 选项 | 设置 |
|---|---|
| `Copy only the necessary library files` | ☑ 勾 |
| `Generate peripheral initialization as a pair of .c/.h files per peripheral` | ☑ 勾（得到 gpio.c / i2c.c / tim.c / usart.c / adc.c） |
| `Keep User Code when re-generating` | ☑ 勾（**必勾**） |
| `Delete previously generated files when not re-generated` | ☐ 不勾 |
| `Set all free pins as analog (to optimize the power consumption)` | ☑ 勾（空闲脚变模拟输入，减少噪声） |
| `Backup previously generated files when re-generating` | ☐ 不勾（避免产生一堆 .bak） |
| `Enable Full Assert` | ☐ 不勾（省 Flash） |

### 6.3 Advanced Settings

- 每个外设的驱动选择保持 **HAL**（不要 LL）。
- `GPIO` 一栏不用改。
- **User Constants** 不用填。

---

## 7. 生成工程

点右上角 **`GENERATE CODE`** → 若提示 "The Firmware Package ... is missing" 就点 Download → 生成完成后点 `Open Project`（用 Keil 打开）。

---

## 8. 生成后自检清单（把结果告诉我）

打开 `demo/SmartStick/Core/Src/main.c`，逐条核对：

| # | 检查项 | 期望 |
|---|---|---|
| 1 | `SystemClock_Config()` 里 `PLLMUL = RCC_PLL_MUL9`、`FLASH_LATENCY_2` | ✅ |
| 2 | `main.c` 顶部有 `#include "i2c.h" / "tim.h" / "usart.h" / "adc.h" / "gpio.h"` | ✅ |
| 3 | `MX_GPIO_Init()` 在最前，`MX_ADC1_Init()` 等依次调用 | ✅ |
| 4 | `tim.c` 里 `htim3.Init.Prescaler = 3; htim3.Init.Period = 999;` | ✅ |
| 5 | `tim.c` 里 `htim4.Init.Prescaler = 71;` | ✅ |
| 6 | `usart.c` 里 115200 / 9600 / 9600 三个波特率正确 | ✅ |
| 7 | `adc.c` 里 `ADC_CHANNEL_4`、`SamplingTime = ADC_SAMPLETIME_55CYCLES_5` | ✅ |
| 8 | `main.h` 里能看到 `IN1_Pin`、`BUZZ_Pin`、`SOS_KEY_Pin` 等宏 | ✅ |
| 9 | 编译（Keil 里 F7）：**0 Error / 0 Warning** | ✅ |
| 10 | 把 `demo/SmartStick` 整个目录告诉我，我会做一次引脚/时钟/优先级核对 | — |

---

## 9. Keil 侧一次性设置（下载器）

`Options for Target → Debug`：

| 项 | 值 |
|---|---|
| Use | **ST-Link Debugger** |
| Settings → Debug 页 | Port = **SW**，Max Clock 默认 |
| Settings → Flash Download 页 | ☑ `Reset and Run`（烧完自动跑） |
| Utilities → Settings | 确认勾选 `Reset and Run` |

> 烧录顺序：先接好 SWD 4 线（3.3V / GND / SWDIO / SWCLK），Keil 里 F8 下载。

---

## 10. 常见坑 / 兜底方案

| 现象 | 原因 | 处理 |
|---|---|---|
| 板子没有 8MHz 晶振，程序卡在 `SystemClock_Config` | HSE 起不来 | RCC 里把 HSE 改 **Disable**，Clock Configuration 里 System Clock Mux 选 **HSI**，PLL Mul 选 **×16**、PLL Source 选 HSI/2 → 得到 64MHz（其余配置不变，波特率仍准确） |
| PB4 按键读不到 | Debug 没设成 Serial Wire | 回 `SYS` 改成 Serial Wire，重新生成 |
| PWM 调速无效、电机永远全速 | L298N 的 **ENA/ENB 跳线帽没拆** | 拆掉跳线帽（硬件问题，不是代码问题） |
| Keil 编译报 `stm32f1xx_hal_xxx.h` 找不到 | 重新生成后没重新打开工程 | 关掉 Keil → 重新 Generate → 再打开 |
| CubeMX 重新生成后我们的代码没了 | 没勾 `Keep User Code` | 重新生成前确认该选项已勾；我们的代码**只写在 USER CODE 区内 + user/ 独立目录**（见 `CODE_STANDARD.md`） |
| 生成时提示 `LL` 驱动 | 老版本默认 | 保持 HAL，改回 HAL 再生成 |
| I²C 一个设备都没有 | 供电/接线 | 先跑 I²C 扫描（`VERIFY.md` 第 3 步），确认 0x3C/0x68/0x0D |
