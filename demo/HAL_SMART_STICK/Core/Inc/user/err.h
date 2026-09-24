/**
 * @file    err.h
 * @brief   全局错误码定义与错误统计接口（跨层共享的"词汇表"）
 * @note    本文件是命名前缀规则（CODE_STANDARD 第 2 节）的唯一例外：
 *          错误码需要被 BSP / 驱动 / 服务 / 应用四层同时引用，故不加层前缀。
 * @version 0.1  (2026-09-24)
 */
#ifndef USER_ERR_H
#define USER_ERR_H

#include <stdint.h>

/* ---------------- 基本类型 ---------------- */
typedef int32_t err_t;              /* 0 = 成功；负数 = 错误码 */
#define ERR_OK                   0

/* ---------------- 通用 0x00xx ---------------- */
#define ERR_NOT_INIT        (-0x0001)   /* 模块未初始化 */
#define ERR_PARAM           (-0x0002)   /* 入参非法 */
#define ERR_TIMEOUT         (-0x0003)   /* 超时 */
#define ERR_BUSY            (-0x0004)   /* 忙 */
#define ERR_UNSUPPORTED     (-0x0006)   /* 功能未启用（如预留牵引指令） */
#define ERR_OVERFLOW        (-0x0008)   /* 缓冲溢出 */
#define ERR_NO_DATA         (-0x0009)   /* 暂无有效数据 */

/* ---------------- 硬件抽象 0x01xx ---------------- */
#define ERR_HW_INIT         (-0x0101)
#define ERR_HW_TXFER        (-0x0102)
#define ERR_HW_NOT_READY    (-0x0103)

/* ---------------- I2C 0x02xx ---------------- */
#define ERR_I2C_NACK        (-0x0201)   /* 从机无应答（地址错 / 未接线 / 未供电） */
#define ERR_I2C_BUS         (-0x0202)   /* 总线被拉死 */

/* ---------------- 电机 0x10xx ---------------- */
#define ERR_MOTOR_CFG       (-0x1001)
#define ERR_MOTOR_INIT      (-0x1002)

/* ---------------- 超声波 0x11xx ---------------- */
#define ERR_US_NO_ECHO      (-0x1101)   /* 无回波（超量程 / 软质反射 / 未接线 / 未分压） */
#define ERR_US_RANGE        (-0x1102)   /* 超量程或过近 */
#define ERR_US_UART         (-0x1103)   /* UART 模式收发失败 */
#define ERR_US_MODE         (-0x1104)   /* 模式配置不支持 */
#define ERR_US_NOT_CFG      (-0x1105)   /* TRIG/ECHO 引脚未在 CubeMX 中配置 */

/* ---------------- MPU6050 0x12xx ---------------- */
#define ERR_IMU_WHOAMI      (-0x1201)
#define ERR_IMU_CFG         (-0x1202)
#define ERR_IMU_READ        (-0x1203)

/* ---------------- QMC5883L 0x13xx ---------------- */
#define ERR_MAG_ID          (-0x1301)
#define ERR_MAG_CFG         (-0x1302)
#define ERR_MAG_READ        (-0x1303)
#define ERR_MAG_CAL         (-0x1304)

/* ---------------- OLED 0x14xx ---------------- */
#define ERR_OLED_INIT       (-0x1401)
#define ERR_OLED_BUS        (-0x1402)

/* ---------------- 电池 0x15xx ---------------- */
#define ERR_BATT_ADC        (-0x1501)
#define ERR_BATT_RANGE      (-0x1502)   /* 读数离谱（分压比配错 / 未接线） */

/* ---------------- 按键 0x16xx ---------------- */
#define ERR_KEY_INIT        (-0x1601)

/* ---------------- 蓝牙 0x17xx ---------------- */
#define ERR_BT_UART         (-0x1701)

/* ---------------- 控制台 0x18xx ---------------- */
#define ERR_SHELL_CMD       (-0x1801)   /* 未知命令 */
#define ERR_SHELL_ARG       (-0x1802)   /* 参数个数 / 格式错 */
#define ERR_SHELL_FULL      (-0x1803)   /* 行缓冲溢出 */

/* ---------------- 应用 0x20xx ---------------- */
#define ERR_APP_STATE       (-0x2001)   /* 当前状态不允许该操作 */
#define ERR_APP_NO_SENSOR   (-0x2002)   /* 依赖的传感器不可用 */

/* ---------------- 配置 0x30xx ---------------- */
#define ERR_CFG_KEY         (-0x3001)   /* 参数名不存在 */
#define ERR_CFG_RANGE       (-0x3002)   /* 参数超范围 */
#define ERR_CFG_SAVE        (-0x3003)   /* 保存失败（NV 未启用时返回 ERR_UNSUPPORTED） */

/* ---------------- 模块 ID（错误统计用） ---------------- */
typedef enum {
  MOD_SYS = 0,
  MOD_CFG,
  MOD_HW,
  MOD_MOTOR,
  MOD_US,
  MOD_IMU,
  MOD_MAG,
  MOD_OLED,
  MOD_BATT,
  MOD_KEY,
  MOD_BT,
  MOD_SHELL,
  MOD_FSM,
  MOD_ALM,
  MOD_COUNT                     /* 必须放在最后 */
} mod_t;

/* ---------------- 接口 ---------------- */
const char *err_str(err_t e);           /* 错误码 → 短名（"US_NO_ECHO"）；未知 → "UNKNOWN"；永不返回 NULL */
const char *err_mod_str(uint8_t mod);   /* 模块 ID → 4 字符标签（"US  "）；越界 → "?   " */
void        err_record(uint8_t mod, err_t e);
err_t       err_last(void);
uint16_t    err_count(uint8_t mod);
uint32_t    err_total(void);
void        err_clear(void);

#endif /* USER_ERR_H */
