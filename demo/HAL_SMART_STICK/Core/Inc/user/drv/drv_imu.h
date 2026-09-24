/**
 * @file    drv_imu.h
 * @brief   MPU6050 六轴驱动：单位换算 + 姿态指标（**纯整数实现，不依赖浮点/三角函数库**）
 * @note    姿态指标定义（供跌倒检测使用）：
 *            acc_norm_mg    加速度矢量模长（1000 = 1g）
 *            gyro_norm_d10  角速度矢量模长（0.1°/s）
 *            tilt_sin_pct   竖直方向夹角的正弦 ×100（0 = 竖直，100 = 水平躺倒）
 *                           —— 等价于倾角阈值 sin(55°)=0.82 → 82%
 *            tilt_deg       由 tilt_sin_pct 查表插值得到的角度（0~90°，仅用于显示）
 *          说明：v1 不做互补滤波（省 Flash、无浮点）；跌倒判定用「模长 + 倾角 + 静止」
 *          三要素组合，见 app_fall。
 * @version 0.1  (2026-09-24)
 */
#ifndef USER_DRV_IMU_H
#define USER_DRV_IMU_H

#include <stdint.h>
#include "err.h"

typedef struct {
  int16_t  acc_mg[3];        /* 加速度：mg（1000 = 1g） */
  int16_t  gyro_d10[3];      /* 角速度：0.1°/s */
  uint16_t temp_d10;         /* 温度：0.1°C */
  uint16_t acc_norm_mg;      /* |a| */
  uint16_t gyro_norm_d10;    /* |ω| */
  uint16_t tilt_sin_pct;     /* 倾角正弦 ×100 */
  uint16_t tilt_deg;         /* 估算角度（0~90°，显示用） */
  uint8_t  valid;
  uint32_t cnt;
} imu_data_t;

err_t             imu_init(void);
err_t             imu_update(void);              /* 20ms 周期任务（50Hz） */
const imu_data_t *imu_data(void);                /* 只读缓存，永不返回 NULL */
err_t             imu_selftest(char *out, uint16_t n);
err_t             imu_shell_register(void);

#endif /* USER_DRV_IMU_H */
