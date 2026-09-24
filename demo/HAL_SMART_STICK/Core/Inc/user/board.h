/**
 * @file    board.h
 * @brief   硬件门面：全工程唯一允许直接引用 CubeMX 生成符号（引脚宏 / 外设句柄）的地方
 * @note    改引脚只改本文件。其它文件一律通过 board.h 或 bsp_*.h 访问硬件。
 *          对应 STM32F103C8T6 + HAL_OLED 工程（见 demo/docs/PINOUT.md）
 * @version 0.1  (2026-09-24)
 */
#ifndef USER_BOARD_H
#define USER_BOARD_H

#include "main.h"        /* CubeMX：引脚宏（IN1_Pin 等）+ stm32f1xx_hal.h */
#include "gpio.h"
#include "i2c.h"
#include "tim.h"
#include "usart.h"
#include "adc.h"

/* ==================== 外设句柄 ==================== */
#define BOARD_I2C          (&hi2c1)    /* PB6/PB7  400kHz：OLED(0x3C) + MPU6050(0x68) + QMC5883L(0x0D) */
#define BOARD_UART_SHELL   (&huart1)   /* PA9/PA10  115200：调试 shell（USB-TTL） */
#define BOARD_UART_BT      (&huart3)   /* PB10/PB11 9600：蓝牙透传（P2 预留） */
#define BOARD_UART_US      (&huart2)   /* PA2/PA3   9600：UART 型超声波（v1 备用，已预留给超声波 #2） */
#define BOARD_TIM_PWM      (&htim3)    /* PB0/PB1  18kHz：双电机调速 */
#define BOARD_TIM_US       (&htim4)    /* 1MHz 自由计数：微秒基准（不占引脚） */
#define BOARD_ADC          (&hadc1)    /* PA4：电池电压 */

/* PWM：duty 刻度 0~1000 线性对应 0~100% */
#define BOARD_PWM_CH_L     TIM_CHANNEL_3   /* ENA -> 左轮 */
#define BOARD_PWM_CH_R     TIM_CHANNEL_4   /* ENB -> 右轮 */
#define BOARD_PWM_PERIOD   1000u           /* = TIM3 Period(999) + 1 */

/* ADC：电池电压通道 */
#define BOARD_ADC_CH_BATT  ADC_CHANNEL_4

/* 统一超时（ms） */
#define BOARD_I2C_TIMEOUT        5u
#define BOARD_I2C_PROBE_TIMEOUT  5u
#define BOARD_UART_TX_TIMEOUT    20u

/* ==================== 电机（L298N） ==================== */
#define MOT_L_IN1_PORT     IN1_GPIO_Port
#define MOT_L_IN1_PIN      IN1_Pin
#define MOT_L_IN2_PORT     IN2_GPIO_Port
#define MOT_L_IN2_PIN      IN2_Pin
#define MOT_R_IN3_PORT     IN3_GPIO_Port
#define MOT_R_IN3_PIN      IN3_Pin
#define MOT_R_IN4_PORT     IN4_GPIO_Port
#define MOT_R_IN4_PIN      IN4_Pin

/* ==================== 报警（蜂鸣器 + 振动马达） ==================== */
#define ALM_BUZZ_PORT      BUZZ_GPIO_Port
#define ALM_BUZZ_PIN       BUZZ_Pin
#define ALM_VIB_PORT       VIB_GPIO_Port
#define ALM_VIB_PIN        VIB_Pin

/* ==================== 按键 ==================== */
#define KEY_SOS_PORT       SOS_KEY_GPIO_Port
#define KEY_SOS_PIN        SOS_KEY_Pin
#define KEY_MODE_PORT      MODE_KEY_GPIO_Port
#define KEY_MODE_PIN       MODE_KEY_Pin

/* ==================== 心跳灯（PC13，低电平点亮） ==================== */
#define LED_HB_PORT        LED_HB_GPIO_Port
#define LED_HB_PIN         LED_HB_Pin
#define LED_HB_ON()        HAL_GPIO_WritePin(LED_HB_PORT, LED_HB_PIN, GPIO_PIN_RESET)
#define LED_HB_OFF()       HAL_GPIO_WritePin(LED_HB_PORT, LED_HB_PIN, GPIO_PIN_SET)
#define LED_HB_TOGGLE()    HAL_GPIO_TogglePin(LED_HB_PORT, LED_HB_PIN)

/* ==================== HC-SR04 超声波（TRIG / ECHO） ====================
 * 需要用户在 CubeMX 中补配（CUBEMX_GUIDE.md 4.10）：
 *   PA0 = US1_TRIG（GPIO_Output, 初值 Low）
 *   PA1 = US1_ECHO（GPIO_Input, EXTI 双边沿, Pull-down）
 * 未配置时 BOARD_US1_READY = 0：drv_us 返回 ERR_US_NOT_CFG，而不是编译报错。
 */
#if defined(US1_TRIG_Pin) && defined(US1_ECHO_Pin)
  #define BOARD_US1_READY  1
  #define US1_TRIG_PORT    US1_TRIG_GPIO_Port
  #define US1_TRIG_PIN     US1_TRIG_Pin
  #define US1_ECHO_PORT    US1_ECHO_GPIO_Port
  #define US1_ECHO_PIN     US1_ECHO_Pin
#else
  #define BOARD_US1_READY  0
#endif

#if defined(US2_TRIG_Pin) && defined(US2_ECHO_Pin)
  #define BOARD_US2_READY  1
  #define US2_TRIG_PORT    US2_TRIG_GPIO_Port
  #define US2_TRIG_PIN     US2_TRIG_Pin
  #define US2_ECHO_PORT    US2_ECHO_GPIO_Port
  #define US2_ECHO_PIN     US2_ECHO_Pin
#else
  #define BOARD_US2_READY  0
#endif

/* HC-SR04 电气/量程常量 */
#define US_TRIG_PULSE_US   10u        /* TRIG 高电平宽度（数据手册要求 10µs） */
#define US_US_PER_CM       58u        /* ECHO 高电平 58µs = 1cm */
#define US_MIN_MM          20u        /* 有效下限（再近无回波） */
#define US_MAX_MM          4000u      /* 有效上限（保守取 4m，标称 4.5m） */
#define US_DIST_INVALID    0xFFFFu    /* 无效距离标志 */

#endif /* USER_BOARD_H */
