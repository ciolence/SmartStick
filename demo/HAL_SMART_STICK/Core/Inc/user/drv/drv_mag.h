/**
 * @file    drv_mag.h
 * @brief   QMC5883L 三轴磁力计驱动（P2：只做"能读数据 + 可校准"，不做航向业务）
 * @note    ⚠ QMC5883L ≠ HMC5883L：寄存器完全不同，地址 0x0D（HMC 是 0x1E）。
 *          配置：CTRL1(0x09) = 0x1D → 连续测量 / ODR 200Hz / 量程 8G / OSR 512
 *                SET-RESET(0x0B) = 0x01（官方推荐，抑制零点漂移）
 *          单位：8G 量程下 3000 LSB/G → 1 LSB ≈ 0.333（0.1µT）→ val = raw/3
 *          校准：硬磁（min/max 求圆心偏移）；软铁只留比例接口（默认 ×1.00）
 * @version 0.1  (2026-09-24)
 */
#ifndef USER_DRV_MAG_H
#define USER_DRV_MAG_H

#include <stdint.h>
#include "err.h"

typedef enum {
  MAG_CAL_IDLE = 0,
  MAG_CAL_RUNNING,
  MAG_CAL_DONE
} mag_cal_state_t;

typedef struct {
  int16_t  xyz_d1ut[3];       /* 校准后的磁场：0.1 µT */
  int16_t  raw[3];            /* 原始值（未校准） */
  uint8_t  valid;
  uint8_t  cal_state;
  int16_t  min_raw[3];        /* 校准过程中的极值 */
  int16_t  max_raw[3];
  uint32_t cnt;
} mag_data_t;

err_t              mag_init(void);
err_t              mag_update(void);            /* 100ms 周期任务 */
const mag_data_t  *mag_data(void);
err_t              mag_cal_start(void);         /* 开始采集（请原地缓慢转 360°） */
err_t              mag_cal_stop(void);          /* 结束并把偏移写进 g_cfg */
err_t              mag_selftest(char *out, uint16_t n);
err_t              mag_shell_register(void);

#endif /* USER_DRV_MAG_H */
