/**
 * @file    bsp_time.h
 * @brief   时间基准：毫秒（SysTick / HAL 节拍）与微秒（TIM4 自由运行计数）
 * @note    全工程禁止直接调用 HAL_GetTick()，一律走本模块（便于将来替换时间源）
 * @version 0.1  (2026-09-24)
 */
#ifndef USER_BSP_TIME_H
#define USER_BSP_TIME_H

#include <stdint.h>
#include "err.h"

/**
 * @brief 启动 TIM4 微秒计数器（1MHz）。必须在其它微秒接口使用前调用一次。
 * @return ERR_OK / ERR_HW_INIT
 */
err_t bsp_time_init(void);

/* 上电以来的毫秒数（HAL 1ms 节拍） */
uint32_t bsp_time_ms(void);

/* TIM4 原始计数：1 计数 = 1µs，0~65535 循环（回绕安全用 bsp_time_us_diff） */
uint16_t bsp_time_us_raw(void);

/* 回绕安全的微秒差值：t_end 在 t_start 之后（间隔 < 65.536ms 时恒正确） */
uint16_t bsp_time_us_diff(uint16_t t_end, uint16_t t_start);

/**
 * @brief 周期判定辅助（非阻塞）
 * @param last_ms  上次执行时刻（调用者持有，首次置 0）
 * @param period_ms 周期
 * @return 1 = 到点（并已刷新 *last_ms）；0 = 未到
 */
uint8_t bsp_time_reached(uint32_t *last_ms, uint32_t period_ms);

/**
 * @brief 毫秒级超时判定辅助（非阻塞）
 * @param start_ms 起始时刻
 * @param timeout_ms 超时时间
 * @return 1 = 已超时；0 = 未超时
 */
uint8_t bsp_time_expired(uint32_t start_ms, uint32_t timeout_ms);

#endif /* USER_BSP_TIME_H */
