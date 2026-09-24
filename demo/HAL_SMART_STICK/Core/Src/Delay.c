/**
 * @file    Delay.c
 * @brief   阻塞式延时（改造版）
 * @note    ★ 2026-09-24 改造：原实现直接改写 SysTick（并在结尾停掉它），会导致
 *          HAL_GetTick() / HAL_Delay() 失效、HAL 超时机制与调度器全部失灵。
 *          现改为：微秒用 TIM4（1MHz 自由计数）差值忙等，毫秒用 HAL 1ms 节拍轮询。
 *          函数签名与 Delay.h 保持不变，用户既有代码无需改动。
 * @warning 延时是阻塞操作，业务代码请勿使用（见 CODE_STANDARD 第 1 节）；
 *          仅允许在初始化、上电提示音等场景使用。
 */
#include "stm32f1xx_hal.h"

#include "Delay.h"
#include "board.h"

#define DELAY_US_MAX   65535u     /* TIM4 为 16 位计数器，单次微秒延时上限 65.535ms */

/**
  * @brief  微秒级延时（TIM4 忙等）
  * @param  xus 延时时长，范围：0 ~ 65535
  * @retval 无
  */
void Delay_us(uint32_t xus)
{
  uint16_t start;

  if (xus == 0u) {
    return;
  }
  if (xus > DELAY_US_MAX) {
    xus = DELAY_US_MAX;                        /* 超长请改用 Delay_ms，避免语义歧义 */
  }

  HAL_TIM_Base_Start(BOARD_TIM_US);            /* 幂等：即便 bsp_time_init 已启动也无害 */
  start = (uint16_t)__HAL_TIM_GET_COUNTER(BOARD_TIM_US);
  while ((uint32_t)((uint16_t)__HAL_TIM_GET_COUNTER(BOARD_TIM_US) - start) < xus) {
    /* 忙等：差值天然处理 65.536ms 回绕 */
  }
}

/**
  * @brief  毫秒级延时（HAL 1ms 节拍轮询）
  * @param  xms 延时时长，范围：0 ~ 4294967295
  * @retval 无
  */
void Delay_ms(uint32_t xms)
{
  uint32_t start = HAL_GetTick();

  while ((uint32_t)(HAL_GetTick() - start) < xms) {
    /* 忙等（无符号差值可正确处理 49.7 天回绕） */
  }
}

/**
  * @brief  秒级延时
  * @param  xs 延时时长，范围：0 ~ 4294967295
  * @retval 无
  */
void Delay_s(uint32_t xs)
{
  while (xs-- != 0u) {
    Delay_ms(1000u);
  }
}
