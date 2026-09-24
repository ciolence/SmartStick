/**
 * @file    bsp_time.c
 * @brief   时间基准实现：TIM4（1MHz）提供微秒，SysTick（HAL）提供毫秒
 * @note    TIM4 由 CubeMX 配为 PSC=71 / ARR=65535 → 1 计数 = 1µs，65.536ms 回绕一次
 * @version 0.1  (2026-09-24)
 */
#include "bsp_time.h"
#include "board.h"

err_t bsp_time_init(void)
{
  if (HAL_TIM_Base_Start(BOARD_TIM_US) != HAL_OK) {
    return ERR_HW_INIT;
  }
  __HAL_TIM_SET_COUNTER(BOARD_TIM_US, 0u);
  return ERR_OK;
}

uint32_t bsp_time_ms(void)
{
  return HAL_GetTick();
}

uint16_t bsp_time_us_raw(void)
{
  return (uint16_t)__HAL_TIM_GET_COUNTER(BOARD_TIM_US);
}

uint16_t bsp_time_us_diff(uint16_t t_end, uint16_t t_start)
{
  return (uint16_t)(t_end - t_start);      /* 无符号减法天然处理回绕 */
}

uint8_t bsp_time_reached(uint32_t *last_ms, uint32_t period_ms)
{
  uint32_t now = HAL_GetTick();

  if ((uint32_t)(now - *last_ms) >= period_ms) {
    *last_ms = now;
    return 1u;
  }
  return 0u;
}

uint8_t bsp_time_expired(uint32_t start_ms, uint32_t timeout_ms)
{
  return ((uint32_t)(HAL_GetTick() - start_ms) >= timeout_ms) ? 1u : 0u;
}
