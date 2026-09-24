/**
 * @file    cfg.h
 * @brief   全部可调参数的集中定义（编译期开关 + 运行期参数 g_cfg）
 * @note    规则：代码里禁止出现裸数字；阈值/周期/标定值一律从这里取。
 *          运行期参数统一用 int32_t 存放（便于表驱动的 shell 读写与范围校验）。
 * @version 0.1  (2026-09-24)
 */
#ifndef USER_CFG_H
#define USER_CFG_H

#include <stdint.h>
#include "err.h"

/* ==================== 编译期开关（改了要重新编译） ==================== */

/* 运行期默认日志级别：0=E 1=W 2=I 3=D 4=T */
#define CFG_LOG_LEVEL          2
/* 编译期日志上限：TRACE 默认不编入（省 Flash）；需要逐帧排查时临时改 4 */
#define CFG_LOG_COMPILE_LEVEL  3

#define CFG_USE_FLOAT_PRINT    0        /* 恒为 0：禁止 printf 浮点 */
#define CFG_ALARM_ENABLE       1        /* 0 = 全局静音（调试用） */
#define CFG_FALL_ENABLE        1        /* 0 = 关闭跌倒检测 */
#define CFG_ADV_GUIDE_EN       0        /* ★预留：高级牵引（SPIN/ARC/TURN_TO）默认关 */
#define CFG_US_MODE            1        /* 0 = UART(US-100)  1 = TRIG_ECHO(HC-SR04) ← 当前型号 */
#define CFG_US_SECOND_EN       0        /* ★预留：第二路超声波（PA2/PA3） */
#define CFG_MAG_ENABLE         1        /* 磁力计读数据（不做航向业务） */
#define CFG_BT_MIRROR_SHELL    0        /* 日志镜像到蓝牙 */
#define CFG_ENABLE_NV_SAVE     0        /* ★预留：参数写 Flash */
#define CFG_BUZZER_ACTIVE_LOW  1        /* 1 = 低电平触发（与 CubeMX 初值 HIGH 对应） */
#define CFG_KEY_COUNT          2        /* v1 = SOS + MODE；补配 PB4/PB5 后改 4 */
#define CFG_OLED_ENABLE        1        /* 0 = 完全不碰 OLED（无屏调试） */
#define CFG_OLED_PROBE_MS      5        /* OLED 在线探测超时 */

/* 固件版本（banner / ver 命令） */
#define CFG_FW_VERSION         "v0.1.0"

/* 电机换向保护：前进<->后退 切换时先滑行这么久再上电（H 桥保护） */
#define CFG_MOTOR_REV_DELAY_MS 60u

/* ==================== 运行期参数 ==================== */
typedef struct {
  /* 任务周期（ms） */
  int32_t imu_period_ms;         /* 20    50Hz 姿态采样 */
  int32_t us_period_ms;          /* 80    HC-SR04 要求 ≥60ms */
  int32_t batt_period_ms;        /* 500 */
  int32_t ui_period_ms;          /* 200   OLED 刷新 */

  /* 避障（mm / ms） */
  int32_t avoid_slow_mm;         /* 1200  进入减速 */
  int32_t avoid_stop_mm;         /* 400   停车 */
  int32_t avoid_hyst_mm;         /* 200   回滞（停车后退出需 > stop+hyst） */
  int32_t avoid_invalid_ms;      /* 300   无效数据超时 → 按停车处理 */
  int32_t avoid_beep_ms;         /* 700   减速提示间隔 */

  /* 牵引（占空比刻度 0~1000） */
  int32_t guide_base_duty;       /* 600   直行基础速度 */
  int32_t motor_max_duty;        /* 700   ★限速上限（保护 L298N） */
  int32_t motor_min_duty;        /* 120   死区 */
  int32_t motor_ramp_per10ms;    /* 40    软启动斜率 */
  int32_t motor_trim_l;          /* 0     左轮补偿（直行偏航标定，±200） */
  int32_t motor_trim_r;          /* 0     右轮补偿 */
  int32_t motor_l_invert;        /* 0     左电机接线极性反了改 1 */
  int32_t motor_r_invert;        /* 0     右电机 */

  /* 跌倒（mg / 0.1° / ms） */
  int32_t fall_freefall_mg;      /* 400   (0.40g) */
  int32_t fall_freefall_ms;      /* 30 */
  int32_t fall_impact_mg;        /* 2200  (2.20g) */
  int32_t fall_tilt_deg10;       /* 550   (55.0°) */
  int32_t fall_confirm_ms;       /* 2000 */
  int32_t fall_cooldown_ms;      /* 10000 */

  /* 电池（mV / 分压比 ×10000） */
  int32_t batt_div_ratio_x1e4;   /* 43000 (=4.3000) */
  int32_t batt_low_mv;           /* 10500 (3S) */
  int32_t batt_crit_mv;          /* 9900  (3S) */
  int32_t batt_avg_n;            /* 8     滑动平均点数 */

  /* 按键（ms） */
  int32_t key_debounce_ms;       /* 20 */
  int32_t key_long_ms;           /* 1000 */
  int32_t key_vlong_ms;          /* 3000 */

  /* 报警 */
  int32_t alarm_dedup_ms;        /* 5000  同级事件去重窗口 */

  /* 磁力计校准（偏移 ×100，比例 ×100） */
  int32_t mag_off_x_x100;
  int32_t mag_off_y_x100;
  int32_t mag_off_z_x100;
  int32_t mag_scale_x_x100;      /* 默认 100 */
  int32_t mag_scale_y_x100;
  int32_t mag_scale_z_x100;
} cfg_t;

extern cfg_t g_cfg;

/* ==================== 接口 ==================== */
void  cfg_load_defaults(void);                       /* 上电调用：写入默认值 */

void  cfg_init(void);
err_t cfg_set(const char *key, int32_t value);       /* 带范围校验 */
err_t cfg_get(const char *key, int32_t *out);
void  cfg_list(void);                                /* 打印全部 key=value（带单位） */
uint16_t cfg_count(void);
const char *cfg_key_at(uint16_t idx);
err_t cfg_reset(void);                               /* 恢复默认值 */
err_t cfg_save(void);                                /* ★预留：NV 未启用时返回 ERR_UNSUPPORTED */

#endif /* USER_CFG_H */
