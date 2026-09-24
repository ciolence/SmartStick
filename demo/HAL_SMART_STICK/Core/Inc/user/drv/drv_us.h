/**
 * @file    drv_us.h
 * @brief   超声波测距驱动（HC-SR04，TRIG/ECHO 模式）
 * @note    工作方式：
 *            TRIG 发 10µs 高脉冲 → ECHO 变高 → ECHO 变低
 *            ECHO 双边沿由 EXTI（PA1 → EXTI1）中断打 TIM4(1MHz) 时间戳，
 *            ISR 里只存时间戳；换算（µs/58 → mm）、量程判定、中值滤波都在任务里做。
 *          非阻塞：us_update() 以 20ms 周期调用，内部按 g_cfg.us_period_ms 节流触发
 *            （HC-SR04 数据手册要求两次测距间隔 ≥60ms，80ms 留余量）。
 *          失败处理遵循 fail-safe：无回波/超量程 → valid=0，上层按"有障碍"处理。
 * @version 0.1  (2026-09-24)
 */
#ifndef USER_DRV_US_H
#define USER_DRV_US_H

#include <stdint.h>
#include "err.h"

err_t    us_init(void);
err_t    us_update(void);              /* 20ms 周期任务 */
uint16_t us_get_mm(void);              /* 当前距离；无效返回 US_DIST_INVALID */
uint8_t  us_is_valid(void);            /* 1 = 最近一次测量有效 */
uint16_t us_get_raw_mm(void);          /* 未经滤波的原始值（调试用） */
uint32_t us_get_ok_count(void);
uint32_t us_get_err_count(void);
uint32_t us_get_last_ms(void);         /* 最近一次有效测量时刻 */
err_t    us_selftest(char *out, uint16_t n);
err_t    us_shell_register(void);

#endif /* USER_DRV_US_H */
