/**
 * @file    bsp_gpio.h
 * @brief   GPIO 语义封装：心跳灯 / 蜂鸣器 / 振动马达 / 按键 / 超声波引脚
 * @note    报警输出的"安全态"（蜂鸣器静音、马达停）在 bsp_gpio_init() 里强制设置
 * @version 0.1  (2026-09-24)
 */
#ifndef USER_BSP_GPIO_H
#define USER_BSP_GPIO_H

#include <stdint.h>
#include "err.h"

/**
 * @brief 把报警输出置于安全态（蜂鸣器静音、马达停）、心跳灯点亮，并检查超声波引脚配置
 * @return ERR_OK；若超声波引脚未在 CubeMX 中配置，返回 ERR_US_NOT_CFG（不阻断启动）
 */
err_t   bsp_gpio_init(void);

/* 心跳灯（PC13，低电平点亮） */
void    bsp_led_hb_toggle(void);
void    bsp_led_hb_set(uint8_t on);

/* 蜂鸣器：on=1 发声（极性由 CFG_BUZZER_ACTIVE_LOW 决定；CFG_ALARM_ENABLE=0 时强制静音） */
void    bsp_buzz(uint8_t on);

/* 振动马达：on=1 振动 */
void    bsp_vib(uint8_t on);

/* 按键原始电平：1 = 按下（idx：0=SOS, 1=MODE；未配置的键恒返回 0） */
uint8_t bsp_key_raw(uint8_t idx);

/* 超声波 #1 引脚（BOARD_US1_READY=0 时为空实现） */
void    bsp_us_trig(uint8_t level);
uint8_t bsp_us_echo(void);

#endif /* USER_BSP_GPIO_H */
