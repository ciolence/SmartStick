/**
 * @file    drv_batt.h
 * @brief   电池电压监测（PA4 / ADC1_IN4 + 33kΩ/10kΩ 分压）
 * @note    换算：V_bat = ADC / 4095 × 3300mV × (batt_div_ratio_x1e4 / 10000)
 *          默认分压比 4.3（33k/10k）→ 满电 12.6V 对应约 2.93V（安全）。
 *          滤波：batt_avg_n 点滑动平均（默认 8）。
 * @version 0.1  (2026-09-24)
 */
#ifndef USER_DRV_BATT_H
#define USER_DRV_BATT_H

#include <stdint.h>
#include "err.h"

typedef enum {
  BATT_LEVEL_NORMAL = 0,
  BATT_LEVEL_LOW    = 1,     /* < batt_low_mv  */
  BATT_LEVEL_CRIT   = 2      /* < batt_crit_mv */
} batt_level_t;

typedef struct {
  uint16_t     mv;           /* 滤波后的电池电压（mV） */
  uint16_t     raw;          /* 最近一次 ADC 原始值（0~4095） */
  batt_level_t level;
  uint8_t      valid;
} batt_data_t;

err_t               batt_init(void);
err_t               batt_update(void);         /* 500ms 周期任务 */
const batt_data_t  *batt_data(void);           /* 只读缓存，永不返回 NULL */
err_t               batt_selftest(char *out, uint16_t n);
err_t               batt_shell_register(void);

#endif /* USER_DRV_BATT_H */
