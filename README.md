# SmartStick — 智能导盲杖（Demo v1）

> 基于 **STM32F103C8T6** 的低成本导盲杖电控平台：超声避障 + 跌倒检测 + 本地声/震报警 +
> OLED 状态显示 + 手扶牵引（双电机差速）。自带**串口 shell**，所有阈值与标定值都能在
> 现场用命令读写，不依赖在线调试器。

## 功能

| 功能 | 硬件 | 状态 |
|---|---|---|
| 正前方避障（分级减速 / 停车 / 提示） | HC-SR04（TRIG/ECHO + EXTI） | ✅ 含遇障绕行（默认关闭，可在线调参） |
| 姿态与跌倒检测 | MPU6050 | ✅ 三要素状态机，阈值在线可调 |
| 本地报警（声音 + 振动） | 有源蜂鸣器 + 振动马达 | ✅ 7 类事件 + 优先级抢占 + 去重 |
| 状态显示 | 0.96" OLED（SSD1306） | ✅ 4 行实时仪表盘 + 开机屏 |
| 手扶牵引（双电机差速） | L298N + GA25-370 DC12V ×2 | ✅ 软启动 / 限速 / 换向保护 / 直行偏航补偿 |
| 电池电压监测 | ADC + 33kΩ/10kΩ 分压 | ✅ 低电分级报警（参数可换电池） |
| 按键 | SOS / MODE（PA11 / PA12） | ✅ 短按 / 长按 / 超长按事件 |
| 蓝牙、磁力计、第二路超声波 | USART3 / QMC5883L / PA2-PA3 | 🟡 骨架与预留（P2，接口已就位） |

## 目录结构

```
smart_sti/
├── WORKFLOW.md            # 开发流程总纲、决策记录、进度
├── HARDWARE.md / HAVE.md  # 选型依据与模块手册（前期资料）
└── demo/
    ├── docs/              # PINOUT / CUBEMX_GUIDE / ARCHITECTURE / CODE_STANDARD / VERIFY
    ├── tools/             # sync_keil_project.ps1（CubeMX 重生成后一键恢复 Keil 分组）
    └── HAL_SMART_STICK/   # CubeMX + Keil 工程（工程名 HAL_OLED）
        └── Core/{Inc,Src}/user/   # ★ 全部电控代码（BSP / 驱动 / 服务 / 应用四层）
```

## 快速开始

1. **编译**：Keil MDK5 打开 `demo/HAL_SMART_STICK/MDK-ARM/HAL_OLED.uvprojx`，F7（实测 0 Error / 0 Warning）。
2. **烧录**：ST-Link 接 SWD 四线（3.3V / GND / SWDIO / SWCLK），F8 下载（建议勾选 Reset and Run）。
3. **看结果**：USB-TTL 接 PA9→模块 RX、PA10→模块 TX、GND 共地，串口 **115200 8N1**。
   出现 banner + **PC13 板载灯 1Hz 闪烁** + 两声短鸣 = 固件在跑。

> 详细接线与逐项验证见 `demo/docs/VERIFY.md`（16 步按单执行，每步有预期现象与排查指引）。

## 常用 shell 命令

| 命令 | 作用 |
|---|---|
| `help` / `status` / `ver` | 命令列表 / 状态摘要 / 版本 |
| `i2c` | I²C 扫描（期望 0x3C / 0x68 / 0x0D） |
| `us` / `us mon 10` | 读距离（mm）/ 连续观测 |
| `imu` / `imu mon 10` | 读姿态（加速度、倾角）/ 连续观测 |
| `batt` / `key` / `mag` | 电池电压 / 按键状态 / 磁场 |
| `cfg list`、`cfg set <key> <value>` | 查看与在线修改全部参数（阈值、标定值） |
| `motor 300 300`、`motor stop\|brake` | 直接驱动电机（调试用，先让轮子离地） |
| `buzz 500` / `vib 500` / `alarm fall` | 单测蜂鸣器 / 振动 / 演练报警 |
| `guide start\|stop\|status` | 开始/停止牵引、查看避障等级 |
| `guide detour 1`、`guide spin L 700 400` | 启用绕行、手动转向调参 |
| `err` / `tasks` / `log 3` | 错误统计 / 任务耗时 / 打开调试日志 |

## 硬件要点（易翻车的三件事）

1. **L298N 两个跳线帽都要拆**：ENA/ENB 上的（否则 PWM 无效、永远全速）+ 5V 上的（满电 12.6V 会烧板载稳压）。
2. **HC-SR04 的 ECHO 必须分压**：ECHO →[2.2kΩ]→ PA1，PA1 →[3.3kΩ]→ GND（5V 输出直连会打坏引脚）。
3. **电机消噪必须加**：每个电机端子并 100nF、电源入口并 100µF，否则电刷噪声会让 MCU 莫名复位。

主电源 12V（3S 锂电 11.1V 标称），电机、控制分别供电、星形共地。完整引脚表见 `demo/docs/PINOUT.md`。

## 文档索引

| 文件 | 内容 |
|---|---|
| `WORKFLOW.md` | 开发流程、开放项决策、Git 约定、进度 |
| `demo/docs/PINOUT.md` | 引脚分配总表、电源树、复用约束、风险清单 |
| `demo/docs/CUBEMX_GUIDE.md` | CubeMX 逐项配置（含 HC-SR04/EXTI 配置） |
| `demo/docs/ARCHITECTURE.md` | 分层与目录规划、状态机、任务表、绕行策略 |
| `demo/docs/CODE_STANDARD.md` | 命名/错误码/日志规范、USER CODE 共存规则、踩坑记录 |
| `demo/docs/VERIFY.md` | **上电验证 SOP** + 参数标定记录表 + 已知局限 |

## 说明

本项目为学习与科研用途的硬件原型，暂未指定开源许可协议。
远程仓库：<https://github.com/ciolence/SmartStick>
