# ARCHITECTURE.md — 智能盲杖 Demo v1 软件架构

> 阶段 2 产出物。与 `PINOUT.md`（硬件资源）、`CUBEMX_GUIDE.md`（CubeMX 配置）、`CODE_STANDARD.md`（编码规范）配套。
> 本文回答四个问题：**代码放哪**（第 2 节）、**怎么分层**（第 3~5 节）、**怎么跑起来**（第 6~7 节）、**行为怎么定义**（第 8~11 节）。

---

## 1. 设计约束与目标

| 约束 | 数值/说明 | 对架构的影响 |
|---|---|---|
| Flash | 64 KB（HAL 基线占 ~16KB） | 不上 RTOS、不上 DMP、不上 u8g2；驱动全部手写轻量版 |
| RAM | 20 KB | OLED 帧缓冲 1KB 可接受；禁止动态内存；环形缓冲小而够用 |
| 无 RTOS | 合作式调度（SysTick 1ms 节拍 + 任务表） | 所有驱动必须**非阻塞**：`update()` 只做一次采样/一步状态推进 |
| 无法实时调试 | 串口 shell 是主要排错手段 | 每个驱动必须能被 shell 单独驱动 + 自测 |
| CubeMX 会重新生成代码 | 我们的代码必须放在 USER CODE 区或独立目录 | 见第 2 节目录规划 + `CODE_STANDARD.md` 第 4 节 |
| 用户提供 OLED 驱动 | 我方不写显示底层 | 用 `drv_oled` 适配层隔离（第 5.1 节） |

---

## 2. `demo/SmartStick` 目录规划（我定，阶段 3/4 照此执行）

CubeMX 生成 `Core/Inc`、`Core/Src`、`MDK-ARM` 之后，我在里面加一棵 **`user/` 子树**（CubeMX 不认识它，**永不覆盖**）：

```
demo/SmartStick/
├── SmartStick.ioc                          # CubeMX 配置（用户维护）
├── Core/
│   ├── Inc/
│   │   ├── main.h  gpio.h  i2c.h  tim.h  usart.h  adc.h   # CubeMX 生成，只改 USER CODE 区
│   │   └── user/                           # ★ 我们的头文件（CubeMX 不管）
│   │       ├── api.h                       # 统一对外入口（唯一被 main.c include 的头）
│   │       ├── err.h                       # 错误码全集
│   │       ├── cfg.h                       # ★ 全部可调参数（阈值/标定值）
│   │       ├── bsp/  board.h  bsp_time.h  bsp_i2c.h  bsp_uart.h  bsp_gpio.h  bsp_adc.h
│   │       ├── drv/  drv_oled.h  drv_imu.h  drv_mag.h  drv_us.h
│   │       │         drv_motor.h  drv_alarm.h  drv_key.h  drv_batt.h
│   │       ├── svc/  svc_log.h  svc_err.h  svc_sched.h  svc_shell.h  svc_cfg.h
│   │       └── app/  app.h  app_fsm.h  app_guide.h  app_avoid.h
│   │                 app_fall.h  app_alarm.h  app_ui.h
│   └── Src/
│       ├── main.c  gpio.c  i2c.c  tim.c  usart.c  adc.c  stm32f1xx_it.c   # CubeMX 生成
│       └── user/                           # ★ 我们的源文件（与 Inc/user 同构）
│           ├── bsp/  board.c  bsp_time.c  bsp_i2c.c  bsp_uart.c  bsp_gpio.c  bsp_adc.c
│           ├── drv/  drv_oled.c  ... （与头文件一一对应）
│           ├── svc/  svc_log.c  svc_err.c  svc_sched.c  svc_shell.c  svc_cfg.c
│           └── app/  app.c  app_fsm.c  app_guide.c  app_avoid.c
│                     app_fall.c  app_alarm.c  app_ui.c
└── MDK-ARM/                                # Keil 工程（我方负责把 user/ 挂进工程分组）
```

**三条铁律：**

1. `Core/Src/user/**` 与 `Core/Inc/user/**` 是**我们唯一的代码家**，CubeMX 重新生成时完全不动它。
2. CubeMX 生成的文件里，我们**只在 `/* USER CODE BEGIN ... */` 与 `/* USER CODE END ... */` 之间写字**（主要是 `main.c` 里的两个钩子）。
3. **只有 `board.c/board.h` 允许直接写 `IN1_GPIO_Port`、`htim3`、`huart2` 这类 CubeMX 符号**；其余所有文件通过 `board.h`/`bsp_*.h` 访问硬件。这样 CubeMX 改引脚只改一处。

**`main.c` 里我们只加两处钩子：**

```c
/* USER CODE BEGIN Includes */
#include "api.h"
/* USER CODE END Includes */

int main(void)
{
  HAL_Init();
  SystemClock_Config();
  MX_GPIO_Init();  MX_ADC1_Init();  MX_I2C1_Init();
  MX_TIM3_Init();  MX_TIM4_Init();
  MX_USART1_UART_Init(); MX_USART2_UART_Init(); MX_USART3_UART_Init();

  /* USER CODE BEGIN 2 */
  app_init();                       /* 我们的一切初始化 */
  /* USER CODE END 2 */

  while (1)
  {
    /* USER CODE BEGIN WHILE */
    app_poll();                     /* 我们的合作式调度主循环 */
    /* USER CODE END WHILE */
  }
}
```

---

## 3. 分层结构与依赖方向

```
        ┌──────────────────────────────────────────────────────────┐
 应用层  │ app_fsm   app_guide   app_avoid   app_fall   app_alarm  app_ui │  ← 业务策略/状态机
        └───────────────┬──────────────────────────────────────────┘
                        │ 只向下调用
        ┌───────────────▼──────────────────────────────────────────┐
 服务层  │ svc_sched   svc_log   svc_err   svc_cfg   svc_shell     │  ← 调度/日志/错误码/参数/命令行
        └───────────────┬──────────────────────────────────────────┘
                        │
        ┌───────────────▼──────────────────────────────────────────┐
 驱动层  │ drv_motor drv_us drv_imu drv_mag drv_oled                │  ← 面向"器件"的语义接口
        │ drv_alarm drv_key drv_batt                               │
        └───────────────┬──────────────────────────────────────────┘
                        │
        ┌───────────────▼──────────────────────────────────────────┐
 BSP层   │ board(引脚门面)  bsp_time  bsp_i2c  bsp_uart  bsp_gpio  bsp_adc │ ← 唯一碰 HAL 的层
        └───────────────┬──────────────────────────────────────────┘
                        ▼
        STM32 HAL + CubeMX 生成的 MX_xxx_Init()（只初始化，不含业务）
```

**依赖规则（违反即返工）：**

| 允许 | 禁止 |
|---|---|
| 应用层 → 服务层、驱动层 | 驱动层 → 应用层 |
| 服务层 → 驱动层、BSP | BSP → 驱动层 |
| 驱动层 → BSP、HAL 类型（仅 `HAL_StatusTypeDef` 转换处） | 应用层直接调 `HAL_GPIO_WritePin` / `htim3` 等 |
| `board.h` → CubeMX 头文件 | 其它任何文件 include `main.h` 之外的 CubeMX 头 |

> 例外：`drv_oled.c` 里对接用户提供的 OLED 驱动时可直接调其 API（见 5.1）。

---

## 4. 驱动层统一接口约定

每个驱动**四件套**：`init` / `update` / `data`（只读缓存）/ `selftest`。

```c
/* 以 MPU6050 为例，全部驱动同构 */
err_t              imu_init(void);                     /* 上电初始化：配置寄存器，校验 WHO_AM_I */
err_t              imu_update(void);                   /* 非阻塞：内部节流到 CFG_IMU_PERIOD_MS，读一帧 */
const imu_data_t  *imu_data(void);                     /* 返回内部缓存指针，永不返回 NULL；失败时 valid=0 */
err_t              imu_selftest(char *out, uint16_t n);/* 自测：返回 ERR_OK/错误码，并把人类可读结论写进 out */
```

| 约定 | 说明 |
|---|---|
| **绝不阻塞** | `update()` 总耗时 < 2ms；I²C 用超时版 HAL 接口（超时 5ms），超时即返回错误码 |
| **缓存制** | 业务层读 `xxx_data()`，不自己去调 HAL；`valid` 字段标记数据是否新鲜 |
| **错误只记一次** | 同一错误连续出现只在首次记日志（去抖计数），避免刷屏 |
| **必带 selftest** | 自测要能单独说明"通/不通/读数是多少"，供 shell 与上电自检用 |
| **头文件不暴露 HAL** | 驱动头里不出现 `I2C_HandleTypeDef` 之类，硬件句柄隔离在 `board.h`/`bsp_*.h` |

### 4.1 各驱动对外接口一览（阶段 4 实现时按此写）

| 驱动 | 关键接口 | 数据结构要点 |
|---|---|---|
| `drv_motor` | `motor_init/update/motor_set(l_dir,l_duty,r_dir,r_duty)/motor_stop(mode)/motor_selftest` | `duty ∈ [0,1000]`；`mode = 滑行 / 刹车`；内部做软启动斜坡与限速 |
| `drv_us` | `us_init/update/us_data/us_selftest/us_set_mode` | `uint16_t mm`、`valid`、`last_err`；支持 `CFG_US_MODE = UART / TRIG_ECHO` |
| `drv_imu` | `imu_init/update/imu_data/imu_selftest` | `acc[3](mg)`、`gyro[3](0.1°/s)`、`angle_cf[2](0.1°，互补滤波输出)`、`valid`、`sample_ms` |
| `drv_mag` | `mag_init/update/mag_data/mag_selftest/mag_cal_start/mag_cal_status` | `xyz[3](0.1µT)`、`cal_offset[3]`、`cal_state`；**不做航向** |
| `drv_oled` | `oled_init/oled_clear/oled_flush/oled_text(x,y,str)/oled_num(x,y,val,unit)/oled_selftest` | 128×64 帧缓冲；适配层（见 5.1） |
| `drv_alarm` | `alarm_init/update/alarm_play(pattern)/alarm_stop_all/alarm_selftest` | 蜂鸣器电平极性可配（`CFG_BUZZER_ACTIVE_LOW`）；振动同拍 |
| `drv_key` | `key_init/update/key_event/pressed/key_selftest` | 去抖 20ms；事件：短按 / 长按 1s / 长按 3s；4 个键位（PA11/PA12/PB4/PB5） |
| `drv_batt` | `batt_init/update/batt_data/batt_selftest` | `mV`（已按分压比还原）、8 点滑动平均、`level` 分级 |

---

## 5. 两处需要用户参与的接口

### 5.1 OLED：适配层设计（用户提供驱动）

我方只写 **`drv_oled.c` 适配层**，它对外提供上面那套接口，对内调用**用户提供的驱动**。需要用户驱动提供的最小 API（四选一即可，能对上就够）：

| 我方期望的底层函数 | 用途 | 若你的驱动是别的形式 |
|---|---|---|
| `oled_hw_init(void)` | 初始化（发命令序列） | 名字不同 → 我方在适配层写一行 `#define` 映射 |
| `oled_write_cmd(uint8_t)` / `oled_write_data(uint8_t)` | 写命令 / 写数据 | 若是 `oled_write(cmd, data[], len)` 形式也可以 |
| 是否自带帧缓冲？ | 有则直接用；无则我方在适配层自建 1KB 帧缓冲 | 两种都支持 |

**开始阶段 4 时我会问你的 4 个问题**（现在不用答）：
1. 控制器是 **SSD1306** 还是 **SH1106**？（SH1106 需要列偏移 2，否则显示错位）
2. 分辨率 **128×64** 还是 128×32？
3. 驱动是**硬件 I²C** 还是**软件模拟 I²C**？（我们用硬件 I²C1 / PB6/PB7）
4. 驱动里 7 位地址用的 **0x3C** 还是 0x3D？

### 5.2 蓝牙：只做骨架

`drv_bt`（在 `bsp_uart` 之上，与 shell 共用解析器）：
- v1 提供：初始化 9600、收字节进环形缓冲、把 shell 输出同时镜像到蓝牙（`CFG_BT_MIRROR_SHELL` 默认 **关**，避免乱码刷屏）。
- v1 **不提供**任何手机业务协议；手机侧软件后期另做。
- 预留升级点：`bt_on_line()` 钩子 → 将来接"上报接口"（第 11 节）。

---

## 6. 任务与调度（合作式，无 RTOS）

### 6.1 时间基准

| 计时源 | 实现 | 用途 |
|---|---|---|
| 毫秒 | SysTick（HAL `HAL_GetTick()`） | 调度、去抖、模式时序 |
| 微秒 | **TIM4** 自由运行的 1MHz 计数（`__HAL_TIM_GET_COUNTER(&htim4)`，64 位回绕累加） | 超声波 Echo 脉宽、超时判定 |

### 6.2 中断分工（ISR 只做"搬字节"，绝不运算）

| 中断 | 处理 | 预算 |
|---|---|---|
| SysTick | HAL 1ms 节拍 | 已有 |
| USART1 RX | 收 1 字节 → shell 环形缓冲 → 立即重新武装接收 | < 2µs |
| USART3 RX | 收 1 字节 → 蓝牙环形缓冲 → 重新武装 | < 2µs |
| （预留）TIM2_CH2 / EXTI | 第二路超声波 Echo 边沿打时间戳 | 阶段 4 加 |

**实现方式**：`bsp_uart.c` 里调 `HAL_UART_Receive_IT()`，并实现 `HAL_UART_RxCpltCallback()`（HAL 弱函数覆盖）。
→ **好处：完全不用改 `stm32f1xx_it.c`**，CubeMX 重新生成也不影响我们。

### 6.3 任务表（`svc_sched.c` 里的一张静态表）

| 任务 | 周期 | 归属 | 做什么 |
|---|---|---|---|
| `shell_poll` | 10 ms | svc | 从环形缓冲取一行 → 解析 → 执行 |
| `key_scan` | 20 ms | drv_key | 去抖、产生短按/长按事件 |
| `imu_update` | 20 ms | drv_imu | 读 14 字节 → 单位换算 → 互补滤波（50Hz） |
| `us_update` | 50 ms | drv_us | 触发一次测距 → 读回波（20Hz） |
| `batt_update` | 500 ms | drv_batt | ADC 采样 + 滑动平均 |
| `fall_task` | 20 ms | app_fall | 跌倒状态机推进 |
| `avoid_task` | 20 ms | app_avoid | 避障分级判定，输出速度系数 |
| `alarm_task` | 10 ms | app_alarm | 报警 pattern 时序推进（非阻塞） |
| `guide_task` | 50 ms | app_guide | 计算 L/R 占空比 → `motor_set()` |
| `ui_task` | 200 ms | app_ui | OLED 刷新（内容变化时提前刷新） |
| `hb_task` | 500 ms | app | 心跳灯翻转（1Hz，证明固件在跑） |
| `mag_update` | 100 ms | drv_mag | 读磁场（P2，仅数据可用） |

> 调度器实现：`for each task: if (now - last >= period) { last += period; fn(); }` —— 无阻塞、无优先级反转、可读性最好。单轮总耗时估算 < 3ms。

### 6.4 上电顺序（`app_init()` 内）

```
1. bsp_time_init()      启动 TIM4 微秒基准 + 记录 tick
2. svc_log_init()       初始化日志（USART1 直接输出，不依赖调度）
3. svc_err_init()       清错误表
4. svc_cfg_init()       载入默认参数（见 CODE_STANDARD 第 7 节）
5. bsp_gpio_init()      心跳灯亮、报警输出安全态（蜂鸣器静音、马达停）
6. drv_motor_init()     电机停止（IN 全低、PWM=0）★ 必须在最前，防止上电乱动
7. svc_shell_init()     注册命令表，打印 banner
8. drv_alarm_init()     蜂鸣器短鸣 2 声 = 上电就绪
9. drv_oled_init()      ★ 失败不阻断（无屏也能跑），只记错
10. drv_imu_init()      ★ 失败则记错，跌倒检测标记不可用（降级）
11. drv_us_init()       ★ 失败则记错，避障标记不可用
12. drv_batt_init()     失败只记错
13. drv_key_init() / drv_mag_init() / drv_bt_init()
14. app_fsm_init()      进入 ST_SELFTEST → ST_IDLE
```

**降级原则**：任何"非安全关键"外设初始化失败，**都不允许卡死启动**；记录 `last_err`，OLED 上显示错误码，shell 可查（`err` 命令）。只有电机子系统初始化失败才进入 `ST_FAULT`（禁止一切运动）。

---

## 7. 数据流总图

```
   ┌── ISR ───────────────────────────────────────────────┐
   │ USART1 RX ─► ring(128B) ─┐                           │
   │ USART3 RX ─► ring(128B) ─┴─► svc_shell ─► 命令       │
   └──────────────────────────────────────────────────────┘
                                   │（测试命令可直调驱动，绕过业务）
                                   ▼
  ┌── 定期任务 ────────────────────────────────────────────┐
  │ drv_imu ─► 缓存 ─► app_fall ──┐                        │
  │ drv_us  ─► 缓存 ─► app_avoid ─┤                        │
  │ drv_key ─► 事件 ──────────────┼─► app_fsm（全局状态）  │
  │ drv_batt ─► 缓存 ─────────────┤                        │
  │ drv_mag ─► 缓存（P2，仅记录） │                        │
  └───────────────────────────────┴────────────────────────┘
                                   │
        ┌──────────────────────────┼─────────────────────────┐
        ▼                          ▼                         ▼
   app_guide                 app_alarm                  app_ui
        │                          │                         │
        ▼                          ▼                         ▼
   drv_motor                 drv_alarm(buzz+vib)          drv_oled
```

**单向数据流**：传感器 → 业务判断 → 执行器；**没有任何模块反向修改传感器缓存**。

---

## 8. 状态机设计

### 8.1 全局状态机（`app_fsm`）

| 状态 | 含义 | 电机 | 报警 | 进入条件 | 离开条件 |
|---|---|---|---|---|---|
| `ST_BOOT` | 上电，HAL/时钟就绪 | 停 | — | 复位 | `app_init()` 完成 |
| `ST_SELFTEST` | 逐模块自检（不阻塞，逐项过一遍） | 停 | 结果提示音 | 初始化后 | 自检结束（无论成败） |
| `ST_IDLE` | 待机（可测传感器） | 停 | 静音 | 自检完成 / `guide stop` / SOS 清除 | SOS 键短按 或 `guide start` |
| `ST_GUIDE` | 手扶牵引中 | 按 `app_guide` 输出 | 避障/低电按需 | 来自 IDLE | `guide stop` / SOS 短按 / 跌倒 / 严重错误 |
| `ST_ALARM_SOS` | 跌倒报警中 | **停**（滑行） | `FALL_SOS` 循环 | 跌倒事件 | 长按 SOS 3s 或 `alarm clear` |
| `ST_DEGRADED` | 降级运行（关键传感器失效） | 限速 30% | `SYS_DEGRADED` 提示 | 超声波持续无效 > 3s 且处于 GUIDE / I²C 全部失效 | 错误消失 + `sys clear` |
| `ST_FAULT` | 电机子系统故障 | 停（刹车） | 长鸣 1 次 | `drv_motor` 初始化失败 | 仅复位 |

> 状态切换全部记录日志（`[I][FSM] IDLE -> GUIDE`），OLED 第 1 行显示当前状态，方便现场判断。

### 8.2 避障分级（`app_avoid`）

以**正前方**单路超声波为输入（v1）：

| 级别 | 条件（默认阈值，全部可在 cfg/shell 改） | 速度系数 | 提示 |
|---|---|---|---|
| `L0 通畅` | `d > 1200 mm` | ×1.00 | 无 |
| `L1 减速` | `400 < d ≤ 1200 mm` | 线性：`0.25 + 0.75×(d−400)/800` | 每 700ms 短鸣 + 振 1 次 |
| `L2 停车` | `d ≤ 400 mm` | ×0 | 每 500ms 双短鸣 + 振动；OLED 显示 `OBSTACLE` |
| `L3 失效` | 连续 300ms 无有效回波 | ×0（**安全优先**） | 每 1s 长鸣 1 次；OLED 显示 `E:US_NO_ECHO`；记 `ERR_US_NO_ECHO` |

实现要点：
- 距离做 **3 点中值滤波**，滤掉单次野值；
- 分级加 **0.2m 回滞**（停车后要退到 >600mm 才允许重新前进），避免边界抖动导致电机抽搐；
- L3 是 **fail-safe**：测不到 ≠ 前方没障碍，一律按"有障碍"处理。

### 8.3 跌倒检测（`app_fall`，MPU6050 阈值 + 互补滤波）

```
F_IDLE ──|a| < 0.40 g 且持续 ≥ 30 ms──► F_FREEFALL
F_FREEFALL ──|a| > 2.20 g（500ms 内）──► F_IMPACT
F_IMPACT ──500ms 内姿态异常（|pitch| 或 |roll| > 55°）──► F_CONFIRM_PENDING
F_CONFIRM_PENDING ──持续 2 s 姿态仍异常──► 触发 FALL 事件 → app_fsm(ST_ALARM_SOS) + app_alarm(FALL_SOS)
任一阶段条件不满足 → 回 F_IDLE；触发后 10 s 冷却期内不重复触发
```

- 姿态角用**互补滤波**：`a = 0.98×(a + gyro×dt) + 0.02×acc_angle`（不依赖 DMP，省 Flash/RAM）。
- 阈值全部集中在 `cfg.h`（`CFG_FALL_FREEFALL_G`、`CFG_FALL_IMPACT_G`、`CFG_FALL_TILT_DEG`、`CFG_FALL_CONFIRM_MS`），现场可调。
- 局限（写进 VERIFY）：**跌倒检测是启发式**，剧烈晃动可能误报、缓慢躺倒可能漏报；v1 目标"能演示、可调参、不吓人"。

### 8.4 报警联动（`app_alarm`）

| 优先级 | 事件 | 蜂鸣器 pattern | 振动 | 说明 |
|---|---|---|---|---|
| 5 | `FALL_SOS` | 长鸣 800ms × 3 组，循环 | 与鸣叫同拍 | **不可被抢占**，直到人工清除 |
| 4 | `BATT_CRIT` | 长鸣 300ms × 2 | 否 | < 9.9V，10s 重复 |
| 3 | `OBSTACLE_STOP` | 双短鸣（80ms×2）每 500ms | 是 | 避障 L2 |
| 3 | `SYS_DEGRADED` | 短鸣 1 声每 3s | 否 | 降级提示 |
| 2 | `LOW_BATT` | 短鸣 1 声每 60s | 否 | < 10.5V |
| 1 | `OBSTACLE_WARN` | 短鸣 1 声每 700ms | 是 | 避障 L1 |
| 0 | `BOOT_OK` / `INFO` | 短鸣 2 声（上电）/ 1 声 | 否 | 上电就绪 |

规则：
1. **高优先级立即抢占**当前 pattern（记录被抢占者，10s 冷却内不恢复）。
2. 同级事件 5s 内**去重**（避免刷屏式鸣叫）。
3. `alarm_task` 是**非阻塞状态机**（10ms 步进），绝不 `HAL_Delay()`。
4. `CFG_ALARM_ENABLE=0` 时全部静音放行（调试模式，shell 可切）。

### 8.5 牵引控制（`app_guide`，v1 = 直行 + 分级调速）

**接口（对外稳定，为 v2 扩展预留）：**

```c
typedef enum {
    GUIDE_CMD_STOP = 0,        /* 停车（滑行） */
    GUIDE_CMD_FWD,             /* 直行，速度 = base × 避障系数 */
    GUIDE_CMD_SPIN,            /* ★预留：原地转（a=±速度，正=左转） */
    GUIDE_CMD_ARC,             /* ★预留：圆弧（a=内外差速比，b=速度） */
    GUIDE_CMD_DIFF,            /* ★预留：直接给左右占空比（调车用） */
    GUIDE_CMD_TURN_TO          /* ★预留：转向指定航向（依赖磁力计） */
} guide_cmd_t;

err_t guide_cmd(guide_cmd_t cmd, int16_t a, int16_t b);   /* ★预留指令在 CFG_ADV_GUIDE_EN=0 时返回 ERR_UNSUPPORTED */
```

**v1 实际启用的只有 `STOP` / `FWD`**，其余**代码写好、编译在固件里、默认被参数开关关掉**（满足 O7："先写好但不启用"）。

**直行输出计算（50ms 周期）：**

```
base   = CFG_GUIDE_BASE_DUTY (=600，即 60%，同时受 CFG_MOTOR_MAX_DUTY=700 上限约束)
scale  = app_avoid 给出的速度系数 (0.0 ~ 1.0)
trim_l = CFG_MOTOR_TRIM_L (=+0)，trim_r = CFG_MOTOR_TRIM_R (=+0)   ← 直行偏航标定用
duty_l = clamp(base × scale + trim_l, 0, CFG_MOTOR_MAX_DUTY)
duty_r = clamp(base × scale + trim_r, 0, CFG_MOTOR_MAX_DUTY)
motor_set(MOTOR_L, DIR_FWD, duty_l);  motor_set(MOTOR_R, DIR_FWD, duty_r);
```

**机械/电气保护（重要，针对 12V + L298N）：**
1. **软启动**：占空比从 0 斜坡升到目标值，斜率 `CFG_MOTOR_RAMP_PER_10MS=40`（即 400/100ms），避免启动电流冲击与轮子猛冲。
2. **限速**：`CFG_MOTOR_MAX_DUTY=700`（70%）——低于满速可显著降低 L298N 发热与堵转电流（风险 R1）。
3. **急停用刹车**：`GUIDE_CMD_STOP` 默认**滑行**（省电、柔和）；跌倒/故障用**刹车**（IN1=IN2 电平相同），保证立即停住。
4. **方向切换先停**：前进↔后退切换时，先输出 duty=0 保持 60ms 再反向，避免 H 桥瞬时直通。
5. 全部指令经 `motor_set()` 单点出口，**没有第二处写 `IN1~IN4`/PWM 的地方**。

**★ 留给 v2 的复杂牵引（本次只写框架，不启用）**：
- `GUIDE_CMD_SPIN`：左右轮反向 → 原地掉头；
- `GUIDE_CMD_ARC`：左右轮差速 → 绕行弧线；
- `GUIDE_CMD_TURN_TO`：需 QMC5883L 校准后的航向闭环；
- 差速转向的**判定输入**（第二路超声波左右测距、磁力计航向）在 v1 均不存在，故一律 `ERR_UNSUPPORTED`，避免"看起来能用其实乱转"。
- 具体转向策略（何时转、转多少度、如何回正）**到阶段 4 后半段与你确认后再实现**（O7 约定）。

---

## 9. 调试设施（一等公民）

| 设施 | 内容 | 归属 |
|---|---|---|
| **串口 shell** | 10ms 轮询解析，命令表见第 9.1 节 | `svc_shell` |
| **模块自测** | 每个驱动 `xxx_selftest()`；`diag` 命令一键全测 | 全部驱动 |
| **上电自检** | `app_init()` 末尾逐项 selftest，结果进 OLED + 日志 | `app_fsm` |
| **心跳灯** | PC13 1Hz 翻转——"固件活着"最简单证据 | `app` |
| **错误码** | 全局限定：`err_last()` / `err_count(mod)`，`err` 命令可查 | `svc_err` |
| **日志分级** | E/W/I/D/T 五级，`CFG_LOG_LEVEL` 编译期 + `log <n>` 运行期 | `svc_log` |
| **在线调参** | `cfg set <key> <value>` 立即生效（掉电丢失，见 CODE_STANDARD 第 7 节） | `svc_cfg` |

### 9.1 shell 命令表（阶段 4 按此实现）

| 命令 | 作用 | 示例 |
|---|---|---|
| `help` | 命令列表 | `help` |
| `ver` | 固件版本/编译时间 | `ver` |
| `status` | 一行总览：状态/距离/姿态/电压/错误数 | `status` |
| `diag` | 跑全部 selftest | `diag` |
| `i2c` | I²C 扫描（应见 0x3C/0x68/0x0D） | `i2c` |
| `us` | 读超声波距离（连续 10 次） | `us` |
| `imu` | 读加速度/姿态角 | `imu` |
| `mag` | 读磁场原始值 | `mag` |
| `magcal start` / `magcal status` | 磁力计硬磁校准（转 360°） | `magcal start` |
| `batt` | 读电池 mV 与分级 | `batt` |
| `key` | 打印 4 个按键状态（按住看变化） | `key` |
| `motor <l> <r>` | 直接给左右占空比（-1000~1000，负=后退） | `motor 300 300` |
| `motor stop` | 立即停车 | `motor stop` |
| `buzz <ms>` / `vib <ms>` | 单独测试蜂鸣器/振动马达 | `buzz 200` |
| `alarm <ev>` | 触发某个报警 pattern（fall/stop/warn/lowbatt） | `alarm fall` |
| `guide start` / `guide stop` | 进入/退出牵引 | `guide start` |
| `oled test` | OLED 显示测试图案 | `oled test` |
| `cfg list` / `cfg get <k>` / `cfg set <k> <v>` | 参数查看/修改 | `cfg set CFG_AVOID_STOP_MM 500` |
| `log <0-4>` | 切换日志级别 | `log 3` |
| `err` | 显示最近错误与各模块错误计数 | `err` |
| `bt` | 蓝牙收发测试（回显） | `bt echo on` |
| `reset` | 软复位 | `reset` |

---

## 10. 资源预算（红线）

| 项 | 预算 | 控制手段 |
|---|---|---|
| Flash 总量 | **≤ 50 KB**（留 14KB） | 不引 printf 浮点（`CFG_USE_FLOAT_PRINT=0`）、手写 OLED 驱动、不做 DMP |
| 我们代码的 Flash | ≤ 30 KB | 单一职责小函数、表格化 pattern、避免字库（ASCII 8×16 点阵 ~1.5KB 可接受） |
| RAM 总量 | **≤ 10 KB / 20 KB** | 无动态分配、环形缓冲各 128B、OLED 帧缓冲 1KB、栈 1KB |
| 单个 `update()` 耗时 | < 2 ms | I²C/ADC 超时 5ms，超声波超时 30ms（在 50ms 周期任务内可接受，但会记录超时） |
| ISR 时长 | < 5 µs | ISR 只搬字节 |
| 栈 | ≥ 1 KB（Keil 默认 0x400，我们在 `startup_stm32f103xb.s` 里确认） | 禁止递归、禁止大数组局部变量 |

---

## 11. 扩展点（v1 留好门，不实现）

| 扩展 | 预留位置 | 启用方式 |
|---|---|---|
| 第二路超声波（左右判定） | `drv_us` 支持双实例；`app_avoid` 预留左右字段 | `CFG_US_SECOND_EN=1`，PA0/PA1 接线 |
| 差速转向 / 绕行 | `guide_cmd()` 的 `SPIN/ARC/TURN_TO` | `CFG_ADV_GUIDE_EN=1`（策略需先与用户确认） |
| 航向闭环 | `drv_mag` 校准后的 `heading` 字段 | 阶段 4 已备；`app_guide` 里 `TURN_TO` 直接用 |
| 手机上报（蓝牙业务） | `drv_bt` 的 `bt_on_line()` 钩子 | v2 定义协议后接 |
| 参数掉电保存 | `svc_cfg` 的 `cfg_save()/cfg_load()`（使用 Flash 最后一页 0x0800FC00） | 编译开关 `CFG_ENABLE_NV_SAVE`（**v1 默认关**，避免误写 Flash；标定完成后再开） |
| 4G/WiFi、GPS、语音 | 无 UART 余量（见 PINOUT 风险 R7） | v2 重新规划引脚 |
| 舵机云台扫描 | PA8 软件 PWM（或 PA5 硬件 PWM） | v2 |

---

## 12. 待用户确认（阶段 4 之前）

| # | 事项 | 影响的模块 |
|---|---|---|
| A1 | OLED 控制器型号 / 分辨率 / 地址 / 驱动形式（见 5.1） | `drv_oled` |
| A2 | 到货超声波的具体型号（UART 还是 Trig/Echo） | `drv_us` 模式选择 |
| A3 | 12V 电源形态（3S 锂电 / 铅酸 / 适配器），决定低电阈值 | `drv_batt`、`cfg.h` |
| A4 | 是否装第二路超声波、是否要转向绕行（决定 `CFG_ADV_GUIDE_EN`） | `app_avoid`、`app_guide` |
| A5 | 蜂鸣器模块触发极性（低电平还是高电平触发） | `drv_alarm` 的参数默认值 |
