# CODE_STANDARD.md — 编码规范与工程约定

> 阶段 2 产出物。**阶段 4 起，所有代码以本文为准绳**；与本文冲突的写法一律视为缺陷。
> 目标不是"好看"，而是：**让一个无法在线调试的固件，出了问题能自己说清楚**。

---

## 1. 总则（五条最高优先级的规矩）

1. **不阻塞**：任何函数都不得用 `HAL_Delay()` / 忙等超过 2ms（`app_init()` 里的上电提示音除外）。一切"等待"都拆成 `update()` + 状态机。
2. **不吞错**：每个可能失败的操作都要检查返回值，记录错误码 + 一条日志；**禁止空 `if (ret != HAL_OK) { }`**。
3. **不碰硬件细节**：引脚/外设句柄只在 `board.c/h` 与 `bsp_*.c` 出现（`drv_oled` 对接用户驱动是唯一例外）。
4. **不留魔法数**：所有阈值、周期、标定值集中在 `cfg.h`，通过 shell 可读可改。
5. **不写会消失的代码**：我们的代码只在 `user/` 目录 + `USER CODE` 区（第 4 节）。

---

## 2. 文件与目录命名

| 层 | 前缀 | 示例 | 说明 |
|---|---|---|---|
| BSP | `bsp_` + `board` | `bsp_i2c.c/h`、`board.c/h` | 唯一接触 HAL 的层 |
| 驱动 | `drv_` | `drv_imu.c/h` | 一个器件一对文件 |
| 服务 | `svc_` | `svc_shell.c/h` | 跨模块的基础设施 |
| 应用 | `app_` | `app_avoid.c/h` | 业务策略 |

- 文件名：**全小写 + 下划线**，与模块名一致（`drv_motor.h` ↔ `drv_motor.c`）。
- 头文件必须有 **include guard**：`#ifndef USER_DRV_IMU_H`（统一 `USER_` 前缀，避免与 HAL 头冲突）。
- 每个 `.c` 文件顶部必须有模块头注释：

```c
/**
 * @file    drv_imu.c
 * @brief   MPU6050 六轴驱动（互补滤波输出姿态角）
 * @note    非阻塞：update() 内部按 CFG_IMU_PERIOD_MS 节流
 * @version 0.1  (2026-xx-xx)
 */
```

---

## 3. 命名规范

| 类别 | 规则 | 示例 |
|---|---|---|
| 类型 | 小写 + `_t` 后缀 | `imu_data_t`、`guide_cmd_t` |
| 函数 | `模块前缀_动作()`，全小写 | `us_update()`、`motor_set()`、`app_avoid_task()` |
| 局部/静态变量 | 小写 + 下划线 | `duty_l`、`sample_cnt` |
| 文件内静态变量 | `s_` 前缀 | `s_last_ms`、`s_rx_ring` |
| 全局变量 | `g_` 前缀（**原则上不新增**，用接口取值） | `g_cfg` |
| 宏/常量 | 全大写 + 分类前缀 | `CFG_*`（参数）、`ERR_*`（错误码）、`MOD_*`（模块 ID）、`US_*`（模块内常量） |
| 枚举类型 | 小写 + `_t` | `alarm_ev_t` |
| 枚举成员 | 大写 + 类型前缀 | `ALARM_EV_FALL_SOS`、`GUIDE_CMD_STOP` |
| 布尔 | `uint8_t` + `is_`/`has_` 前缀，**不用 `bool`/`bit`** | `is_valid`、`has_new_event` |
| 单位后缀 | 变量名带单位，杜绝歧义 | `duty`(0~1000)、`mm`、`ms`、`us`、`mv`、`deg10`(0.1°)、`mg`(0.001g) |

**单位与量程约定（全工程统一，禁止自创）：**

| 物理量 | 类型 | 单位 | 约定 |
|---|---|---|---|
| 距离 | `uint16_t` | **mm** | 无效值用 `US_DIST_INVALID = 0xFFFF` |
| 时间（长） | `uint32_t` | ms | 统一用 `bsp_time_ms()` |
| 时间（短） | `uint16_t` | µs | 统一用 `bsp_time_us()` |
| 电压 | `uint16_t` | mV | 不用浮点 |
| 占空比 | `int16_t` | 0~1000 | 1000 = 100%；负数 = 反转 |
| 角度 | `int16_t` | 0.1° | ±1800 = ±180° |
| 加速度 | `int16_t` | mg | 1000 = 1g |
| 角速度 | `int16_t` | 0.1°/s | 10 = 1°/s |
| 比例系数 | `uint16_t` | 千分比 0~1000 | 1000 = ×1.0 |
| 浮点 | — | — | **只允许出现在 `drv_imu`（互补滤波）与 `drv_mag`（校准）**，其余一律整数/定点 |

---

## 4. USER CODE 区规则（CubeMX 共存，最重要的一节）

### 4.1 我们只在这三个地方写代码

| 位置 | 我们写什么 | 备注 |
|---|---|---|
| `Core/Src/user/**`、`Core/Inc/user/**` | **99% 的代码** | CubeMX 完全不认识这个目录，永不覆盖 |
| `main.c` 的 `/* USER CODE BEGIN Includes */` | `#include "api.h"` | 一行 |
| `main.c` 的 `/* USER CODE BEGIN 2 */` | `app_init();` | 一行 |
| `main.c` 的 `/* USER CODE BEGIN WHILE */` | `app_poll();` | 一行 |

**除此之外的 CubeMX 生成文件（`gpio.c` / `i2c.c` / `tim.c` / `usart.c` / `adc.c` / `stm32f1xx_it.c` / `main.h`）一律不改其 USER CODE 区之外的内容。**

> 中断处理**不需要**改 `stm32f1xx_it.c`：`bsp_uart.c` 用 `HAL_UART_Receive_IT()` + 覆盖 `HAL_UART_RxCpltCallback()` 实现（见 `ARCHITECTURE.md` 6.2）。

### 4.2 CubeMX 重新生成后的自查流程（每次必做）

```
1. Keil 里先关闭工程（否则文件被锁）
2. CubeMX 里改配置 → GENERATE CODE
3. 检查 Core/Src/user/ 与 Core/Inc/user/ 是否还在（应该在）
4. Keil 打开 → F7 编译 → 若报 "cannot open source input file"：
   → 说明 user/ 文件没挂进工程分组，告诉我，我重新注入（也可以手动 Add Files）
5. 若 CubeMX 改了引脚名/外设名 → 只改 board.c/h 一处，其它文件不动
```

### 4.3 禁止事项（会导致代码丢失）

- ❌ 在 `main.c` 的非 USER CODE 区写任何东西
- ❌ 把我们的 `.c` 文件放在 `Core/Src/` 根下（会被误认为 CubeMX 管理文件）
- ❌ 在 CubeMX 里点 "Delete previously generated files"（会把我们的东西当孤儿删掉）
- ❌ 关掉 "Keep User Code when re-generating"

---

## 5. 错误码体系（`err.h`）

```c
typedef int32_t err_t;          /* 0 = 成功，负数 = 错误 */
#define ERR_OK                  0
```

**命名：`ERR_<模块>_<原因>`，值为负数，按模块分段（便于人肉识别，也便于 shell 显示）。**

```c
/* 通用 0x00xx */
#define ERR_NOT_INIT        (-0x0001)   /* 模块未初始化 */
#define ERR_PARAM           (-0x0002)   /* 入参非法 */
#define ERR_TIMEOUT         (-0x0003)   /* 超时 */
#define ERR_BUSY            (-0x0004)
#define ERR_UNSUPPORTED     (-0x0006)   /* 功能未启用（如预留牵引指令） */
#define ERR_OVERFLOW        (-0x0008)
#define ERR_NO_DATA         (-0x0009)

/* 硬件抽象 0x01xx */
#define ERR_HW_INIT         (-0x0101)
#define ERR_HW_TXFER        (-0x0102)
#define ERR_HW_NOT_READY    (-0x0103)

/* I²C 0x02xx */
#define ERR_I2C_NACK        (-0x0201)   /* 从机无应答（地址错误/未接线） */
#define ERR_I2C_BUS         (-0x0202)   /* 总线被拉死 */

/* 电机 0x10xx */
#define ERR_MOTOR_CFG       (-0x1001)
#define ERR_MOTOR_INIT      (-0x1002)

/* 超声波 0x11xx */
#define ERR_US_NO_ECHO      (-0x1101)   /* 无回波（超量程/软质反射/未接线） */
#define ERR_US_RANGE        (-0x1102)   /* 超量程 */
#define ERR_US_UART         (-0x1103)   /* UART 收发失败 */
#define ERR_US_MODE         (-0x1104)

/* MPU6050 0x12xx */
#define ERR_IMU_WHOAMI      (-0x1201)   /* 读回不是 0x68 */
#define ERR_IMU_CFG         (-0x1202)
#define ERR_IMU_READ        (-0x1203)

/* QMC5883L 0x13xx */
#define ERR_MAG_ID          (-0x1301)   /* chip ID 不是 0xFF */
#define ERR_MAG_CFG         (-0x1302)
#define ERR_MAG_READ        (-0x1303)
#define ERR_MAG_CAL         (-0x1304)

/* OLED 0x14xx */
#define ERR_OLED_INIT       (-0x1401)
#define ERR_OLED_BUS        (-0x1402)

/* 电池 0x15xx */
#define ERR_BATT_ADC        (-0x1501)
#define ERR_BATT_RANGE      (-0x1502)   /* 读数离谱（分压比配错/未接线） */

/* 按键 0x16xx */
#define ERR_KEY_INIT        (-0x1601)

/* 蓝牙 0x17xx */
#define ERR_BT_UART         (-0x1701)

/* 控制台 0x18xx */
#define ERR_SHELL_CMD       (-0x1801)   /* 未知命令 */
#define ERR_SHELL_ARG       (-0x1802)   /* 参数个数/格式错 */
#define ERR_SHELL_FULL      (-0x1803)   /* 行缓冲溢出（单行 > 96 字符） */

/* 应用 0x20xx */
#define ERR_APP_STATE       (-0x2001)   /* 状态机上不允许的操作 */
#define ERR_APP_NO_SENSOR   (-0x2002)   /* 依赖的传感器不可用 */

/* 配置 0x30xx */
#define ERR_CFG_KEY         (-0x3001)
#define ERR_CFG_RANGE       (-0x3002)
#define ERR_CFG_SAVE        (-0x3003)
```

**配套设施（`svc_err`）：**

```c
const char *err_str(err_t e);                 /* 错误码 → "US_NO_ECHO"；未知返回 "UNKNOWN" */
void        err_record(uint8_t mod, err_t e); /* 记录到统计表（按模块计数 + 存最近一次） */
err_t       err_last(void);                   /* 最近一次错误 */
uint16_t    err_count(uint8_t mod);           /* 某模块错误累计次数 */
uint32_t    err_count_total(void);
```

**使用模板（所有初始化/通信代码照抄）：**

```c
err_t e = imu_read_whoami(&id);
if (e != ERR_OK) {
    err_record(MOD_IMU, e);
    LOG_E("WHO_AM_I read fail: %s", err_str(e));
    return e;                     /* 向上传递；调用者决定是否降级 */
}
```

**分级处理原则：**

| 模块 | 失败后的行为 |
|---|---|
| 电机 | **致命**：进 `ST_FAULT`，禁止一切运动 |
| 超声波 | 降级：避障按"有障碍"处理（fail-safe），提示 + 记错 |
| IMU | 降级：跌倒检测关闭（`CFG_FALL_ENABLE=0`），OLED 提示 |
| OLED | 忽略：仅记错，不影响任何业务（无屏也能跑） |
| 电池 ADC | 忽略：仅记错，不报警 |
| 磁力计 / 蓝牙 | 忽略：P2 功能，仅记错 |

---

## 6. 日志规范（`svc_log`）

**五级，编译期 + 运行期双重控制：**

| 级别 | 宏 | 值 | 用途 | 现场默认 |
|---|---|---|---|---|
| ERROR | `LOG_E` | 0 | 功能失败、硬件异常 | ✅ 开 |
| WARN | `LOG_W` | 1 | 可恢复异常、数据无效、超时 | ✅ 开 |
| INFO | `LOG_I` | 2 | 状态切换、上电自检结果、命令执行结果 | ✅ 开 |
| DEBUG | `LOG_D` | 3 | 周期性数据、参数变更 | 默认关（`log 3` 开） |
| TRACE | `LOG_T` | 4 | 逐字节/逐帧流水（排查 I²C/串口用） | 默认关 |

**输出格式（一行一条，便于人眼扫 + 便于你复制给我）：**

```
[t=12345][E][US  ] no echo over 300ms (err=-0x1101)
[t=12346][I][FSM ] IDLE -> GUIDE
[t=12350][D][MOT ] duty L=600 R=600 (ramp)
```

- `t` = 上电以来的 ms（`bsp_time_ms()`），不是时钟时间（无 RTC）。
- `[模块]` 用 4 字符 ASCII 标签（`FSM `/`MOT `/`US  `/`IMU `/`MAG `/`OLED`/`BATT`/`KEY `/`SHELL`/`CFG `/`ALM `/`BT  `），由 `cfg.h` 里的 `LOG_TAG` 宏提供。
- **禁止**在日志里放浮点（Flash 代价高）：角度/电压都按整数单位打印（`%d` + 单位写在文案里）。

**输出通道：** USART1（115200，阻塞式直接发，单条 < 100 字节）。`CFG_BT_MIRROR_SHELL=1` 时可镜像到蓝牙（默认关）。

**纪律：**

1. 日志**不得**在中断里调用（ISR 只搬字节）。
2. 周期性日志（DEBUG/TRACE）必须节流，禁止每 20ms 刷屏。
3. 同一错误**连续**出现只在首次打 `LOG_E`；恢复时打一条 `LOG_I("...recovered")`。
4. 启动 banner 必须包含：固件版本、编译日期、`SYSCLK`、各模块 selftest 结论。

---

## 7. 参数集中配置（`svc_cfg` + `cfg.h`）

### 7.1 规则

1. **所有可调数值只在 `cfg.h` 定义默认值**，代码里**禁止**出现裸数字（`if (d < 400)` ✗ → `if (d < g_cfg.avoid_stop_mm)` ✓）。
2. 参数分两类：
   - **编译期开关**（`#define`，改了要重新编译）：`CFG_*_ENABLE` 类；
   - **运行期可调值**（存在 `g_cfg` 结构体里）：阈值、标定值、周期。
3. 运行期参数通过 shell 读写：`cfg list` / `cfg get <key>` / `cfg set <key> <value>`。
4. **v1 参数掉电丢失**（不写 Flash）；`cfg save` 预留（`CFG_ENABLE_NV_SAVE` 默认 0，标定完成后再开，用 Flash 最后一页 0x0800FC00）。

### 7.2 参数表（`cfg.h` 骨架，阶段 4 照此实现）

```c
/* ---------- 编译期开关 ---------- */
#define CFG_LOG_LEVEL          1    /* 0=E 1=W 2=I 3=D 4=T */
#define CFG_USE_FLOAT_PRINT    0    /* 恒为 0：禁止 printf 浮点 */
#define CFG_ALARM_ENABLE       1    /* 0 = 全局静音（调试用） */
#define CFG_FALL_ENABLE        1    /* 0 = 关闭跌倒检测 */
#define CFG_ADV_GUIDE_EN       0    /* ★预留：高级牵引（SPIN/ARC/TURN_TO）默认关 */
#define CFG_US_MODE            0    /* 0 = UART(US-100)  1 = TRIG_ECHO(HC-SR04) */
#define CFG_US_SECOND_EN       0    /* ★预留：第二路超声波 */
#define CFG_MAG_ENABLE         1    /* 磁力计读数据（不做航向业务） */
#define CFG_BT_MIRROR_SHELL    0    /* 日志镜像到蓝牙 */
#define CFG_ENABLE_NV_SAVE     0    /* ★预留：参数写 Flash */
#define CFG_BUZZER_ACTIVE_LOW  1    /* 1 = 低电平触发（与 CubeMX 初值 HIGH 对应） */
#define CFG_KEY_COUNT          2    /* v1 = SOS + MODE；补配 PB4/PB5 后改 4 */
#define CFG_OLED_ENABLE        1    /* 0 = 完全不碰 OLED（无屏调试） */

/* ---------- 运行期参数（g_cfg） ---------- */
typedef struct {
  /* 周期（ms） */
  uint16_t imu_period_ms, us_period_ms, batt_period_ms, ui_period_ms;

  /* 避障（mm / ms） */
  uint16_t avoid_slow_mm;      /* 默认 1200  进入减速 */
  uint16_t avoid_stop_mm;      /* 默认  400  停车 */
  uint16_t avoid_hyst_mm;      /* 默认  200  回滞（停车后退出需 > stop+hyst） */
  uint16_t avoid_invalid_ms;   /* 默认  300  无效数据超时 → 按停车处理 */
  uint16_t avoid_beep_ms;      /* 默认  700  减速提示间隔 */

  /* 牵引（占空比刻度 0~1000） */
  uint16_t guide_base_duty;    /* 默认  600  直行基础速度 */
  uint16_t motor_max_duty;     /* 默认  700  ★限速上限（保护 L298N） */
  uint16_t motor_min_duty;     /* 默认  120  死区（低于此值不转，避免嗡鸣） */
  uint16_t motor_ramp_per10ms; /* 默认   40  软启动斜率 */
  int16_t  motor_trim_l;       /* 默认    0  左轮补偿（直行偏航标定） */
  int16_t  motor_trim_r;       /* 默认    0  右轮补偿 */
  uint8_t  motor_l_invert;     /* 默认    0  左电机接线极性反了改 1 */
  uint8_t  motor_r_invert;     /* 默认    0  右电机 */

  /* 跌倒（mg / 0.1° / ms） */
  uint16_t fall_freefall_mg;   /* 默认  400  (0.40g) */
  uint16_t fall_freefall_ms;   /* 默认   30 */
  uint16_t fall_impact_mg;     /* 默认 2200  (2.20g) */
  uint16_t fall_tilt_deg10;    /* 默认  550  (55.0°) */
  uint16_t fall_confirm_ms;    /* 默认 2000 */
  uint16_t fall_cooldown_ms;   /* 默认 10000 */

  /* 电池（mV / 分压比 ×10000） */
  uint32_t batt_div_ratio_x1e4;/* 默认 43000 (=4.3000) */
  uint16_t batt_low_mv;        /* 默认 10500 */
  uint16_t batt_crit_mv;       /* 默认  9900 */
  uint8_t  batt_avg_n;         /* 默认     8 */

  /* 按键（ms） */
  uint16_t key_debounce_ms;    /* 默认   20 */
  uint16_t key_long_ms;        /* 默认 1000 */
  uint16_t key_vlong_ms;       /* 默认 3000 */

  /* 报警 */
  uint16_t alarm_dedup_ms;     /* 默认 5000 */

  /* 磁力计校准（×100，整数避免浮点） */
  int16_t  mag_off_x100[3];
  uint16_t mag_scale_x100[3];
} cfg_t;

extern cfg_t g_cfg;

void    cfg_load_defaults(void);
err_t   cfg_set(const char *key, int32_t value);   /* shell 用；带范围校验 */
err_t   cfg_get(const char *key, int32_t *out);
void    cfg_list(void);                            /* 打印所有 key=value */
err_t   cfg_save(void);                            /* ★预留（NV 未开时返回 ERR_UNSUPPORTED） */
```

> `CFG_*` 全大写宏 + `cfg_t` 运行期参数 = 上面第 7.1 节的"两类参数"。

---

## 8. 自测函数约定（每个驱动必备）

```c
/**
 * @brief  模块自测：不改变持久状态、不产生危险动作
 * @param  out  人类可读结论缓冲（≥48 字节）
 * @param  n    缓冲长度
 * @return ERR_OK 通过；否则具体错误码
 * @note   阻塞上限 200ms
 */
err_t us_selftest(char *out, uint16_t n);
```

| 规则 | 说明 |
|---|---|
| 输出格式 | 通过：`"OK: dist=1234mm"`；失败：`"FAIL: no echo (err=-0x1101)"` |
| 危险性 | **禁止**在 selftest 里转电机、长时间鸣叫。电机类测试只做"参数合法性 + 输出安全态"，实际转动必须由显式 shell 命令触发 |
| 幂等 | 可反复调用，不改变参数、不改变工作模式 |
| 失败也要有信息 | 必须说明"卡在哪一步"，而不是只返回错误码 |
| 汇总入口 | `diag` 依次调用全部 selftest，逐行打印，最后打印 `PASS x/y` |

**上电自检**（`app_init()` 末尾）调用同一套函数，结论写日志 + OLED 第 1 行；任何非致命模块失败都不阻断启动（见 `ARCHITECTURE.md` 6.4）。

---

## 9. 代码风格

| 项 | 规定 |
|---|---|
| 缩进 | **2 空格**，禁止 Tab（与用户 Keil5 编辑器设置一致：`Edit → Configuration → Editor → Tab size = 2`，并勾选 Insert spaces 或直接不用 Tab） |
| 行宽 | ≤ 100 字符 |
| 大括号 | K&R（控制语句与函数左括号**不换行**）；即使只有一条语句也加 `{}` |
| 空格 | `if (a == b)`、`for (i = 0; i < n; i++)`、`f(a, b)`、`p->x` |
| 指针声明 | `const imu_data_t *p;`（星号靠变量） |
| 类型 | 一律 `stdint.h` 定宽类型；禁止裸 `int`/`long`（循环计数可用 `int`） |
| 常量 | `#define` 或 `enum`；禁止魔法数（第 7 节） |
| 转换 | 显式强制转换，禁止依赖隐式截断（`(uint16_t)(x & 0xFFFF)`） |
| 返回值 | 所有可能失败的函数返回 `err_t`；`void` 只用于明确不会失败者 |
| 局部变量 | 函数开头声明（C89 风格，Keil ARMCC 兼容性最稳） |
| 注释 | 模块头 + 每个函数一段 `@brief/@param/@return`；算法关键处写"为什么"；禁止"废话注释"（`i++; /* i 加一 */`） |
| 禁止清单 | `malloc/free`、递归、`goto`（错误清理除外）、可变参数自造、位域、C++ 特性、`HAL_Delay()`（`app_init` 除外）、`printf("%f")`、大数组局部变量（>64B 用 `static` 或全局） |

---

## 10. 并发与中断规则（无 RTOS 版）

| 规则 | 说明 |
|---|---|
| ISR 只搬字节 | 中断里只做 `ring_push()` + 重新武装接收，**不做运算、不调日志、不加长延时** |
| 共享数据 | ISR 与主循环共享的环形缓冲用 `volatile` 索引；**单生产者单消费者**，不加锁 |
| 关中断 | 禁止长时间关中断；如需临界区用 `__disable_irq()/__enable_irq()` 包裹**不超过 10µs** 的操作 |
| 标志传递 | ISR → 主循环只传"有新数据"标志；解析在任务里做 |
| 缓存一致性 | 驱动缓存结构只由对应任务写、其它模块只读；用 `valid` 字段而非"猜数据新不新" |
| 时间源 | 只用 `bsp_time_ms()` / `bsp_time_us()`，禁止直接读 `HAL_GetTick()`（便于将来替换） |

---

## 11. 资源红线

| 项 | 红线 | 超了怎么办 |
|---|---|---|
| 总 Flash | ≤ 50 KB / 64 KB | 降 `CFG_LOG_LEVEL`、去掉 DEBUG 字符串、OLED 字库裁成只用到的字符 |
| 我们代码 Flash | ≤ 30 KB | 合并小函数、表格化 |
| RAM | ≤ 10 KB / 20 KB | 砍环形缓冲、OLED 改无帧缓冲直写 |
| 栈 | ≥ 1 KB（`startup_stm32f103xb.s` 里 `Stack_Size` 确认） | — |
| 堆 | **0**（不用 `malloc`） | — |
| 单个 `update()` | < 2 ms | 拆成多步状态机 |

**编译设置（2026-09-24 按实际工程核对）**：入口工程 `demo/HAL_SMART_STICK/MDK-ARM/HAL_OLED.uvprojx`

| 项 | 实际值 | 说明 |
|---|---|---|
| Optimization | **Level 3 (-O3)**，未开 `-Otime` | CubeMX 生成值，暂保持；若后期 Flash 吃紧或需单步调试，降到 `-O1` 更稳 |
| C99 | ✅ 已勾 | — |
| One ELF Section per Function | ✅ 已勾 | 未用到的函数不进 Flash |
| 宏定义 | `USE_HAL_DRIVER, STM32F103xB` | 不要改 |
| Stack / Heap | 0x400 (1KB) / 0x200 (0.5KB) | 我们不用 `malloc`，Heap 可留可清零 |
| 基线体积（实测） | **Flash 9.57 KB / RAM 2.09 KB** | 含 HAL + OLED + Delay + Key；预算见第 11 节 |

---

## 12. 提交前检查清单（我交付每个模块时逐条自查）

- [ ] 编译 **0 Error / 0 Warning**
- [ ] 新增文件在 `Core/{Inc,Src}/user/` 下，且已挂进 Keil 工程分组
- [ ] 未修改任何 CubeMX 文件的非 USER CODE 区（`git diff` 或肉眼核对）
- [ ] 无魔法数：所有阈值来自 `g_cfg` 或 `CFG_*`
- [ ] 无阻塞：无 `HAL_Delay`、无忙等 > 2ms
- [ ] 错误路径：返回值有检查、`err_record()` + `LOG_E()` 到位
- [ ] 日志：格式符合第 6 节，周期性日志已节流
- [ ] 有 `selftest` + 对应 shell 命令，并已在文档里写"怎么验证"
- [ ] 单位/类型符合第 3 节约定
- [ ] 资源：Flash/RAM 增量在预算内（记录在交付说明里）
