/**
 * @file    bsp_gpio.c
 * @brief   GPIO 语义封装实现
 * @version 0.1  (2026-09-24)
 */
#include "bsp_gpio.h"
#include "board.h"
#include "cfg.h"

err_t bsp_gpio_init(void)
{
  /* 上电安全态：先确认执行器不动作（CubeMX 已给初值，这里再显式写一次防呆） */
  bsp_buzz(0u);
  bsp_vib(0u);
  bsp_led_hb_set(0u);          /* 熄灭 */

#if (BOARD_US1_READY == 0)
  return ERR_US_NOT_CFG;        /* 提示需要补配 PA0/PA1，但不阻断启动 */
#else
  bsp_us_trig(0u);
  return ERR_OK;
#endif
}

void bsp_led_hb_set(uint8_t on)
{
  HAL_GPIO_WritePin(LED_HB_PORT, LED_HB_PIN, (on != 0u) ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

void bsp_led_hb_toggle(void)
{
  HAL_GPIO_TogglePin(LED_HB_PORT, LED_HB_PIN);
}

void bsp_buzz(uint8_t on)
{
#if (CFG_ALARM_ENABLE == 0)
  on = 0u;                                           /* 全局静音（调试用） */
#endif
#if (CFG_BUZZER_ACTIVE_LOW != 0)
  HAL_GPIO_WritePin(ALM_BUZZ_PORT, ALM_BUZZ_PIN, (on != 0u) ? GPIO_PIN_RESET : GPIO_PIN_SET);
#else
  HAL_GPIO_WritePin(ALM_BUZZ_PORT, ALM_BUZZ_PIN, (on != 0u) ? GPIO_PIN_SET : GPIO_PIN_RESET);
#endif
}

void bsp_vib(uint8_t on)
{
#if (CFG_ALARM_ENABLE == 0)
  on = 0u;
#endif
  HAL_GPIO_WritePin(ALM_VIB_PORT, ALM_VIB_PIN, (on != 0u) ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

uint8_t bsp_key_raw(uint8_t idx)
{
  GPIO_PinState st;

  switch (idx) {
    case 0u: st = HAL_GPIO_ReadPin(KEY_SOS_PORT, KEY_SOS_PIN);   break;
    case 1u: st = HAL_GPIO_ReadPin(KEY_MODE_PORT, KEY_MODE_PIN); break;
    default: return 0u;        /* KEY3/KEY4 未配置（见 PINOUT 2.2 #9） */
  }
  return (st == GPIO_PIN_RESET) ? 1u : 0u;    /* 内部上拉，按下接地 → 低电平 */
}

void bsp_us_trig(uint8_t level)
{
#if (BOARD_US1_READY == 1)
  HAL_GPIO_WritePin(US1_TRIG_PORT, US1_TRIG_PIN,
                    (level != 0u) ? GPIO_PIN_SET : GPIO_PIN_RESET);
#else
  (void)level;
#endif
}

uint8_t bsp_us_echo(void)
{
#if (BOARD_US1_READY == 1)
  return (HAL_GPIO_ReadPin(US1_ECHO_PORT, US1_ECHO_PIN) == GPIO_PIN_SET) ? 1u : 0u;
#else
  return 0u;
#endif
}
