/**
 * @file    drv_motor.h
 * @brief   L298N 双直流电机驱动（开环）：软启动斜坡 + 限速 + 换向保护
 * @note    唯一写 IN1~IN4 / ENA / ENB 的地方。上层只调用 motor_set_* 系列。
 *          安全设计（针对 12V + L298N）：
 *            1) 占空比硬上限 CFG motor_max_duty（默认 70%）
 *            2) 软启动斜坡（默认 40/10ms），避免启动电流冲击
 *            3) 换向先停 60ms，避免 H 桥瞬时直通
 *            4) 刹车用于急停（跌倒/故障），滑行用于常规停止
 * @version 0.1  (2026-09-24)
 */
#ifndef USER_DRV_MOTOR_H
#define USER_DRV_MOTOR_H

#include <stdint.h>
#include "err.h"

typedef enum {
  MOTOR_L = 0,          /* 左轮（ENA / IN1 / IN2） */
  MOTOR_R = 1           /* 右轮（ENB / IN3 / IN4） */
} motor_id_t;

typedef enum {
  MOTOR_DIR_FWD   = 0,  /* 正转（车前进方向） */
  MOTOR_DIR_REV   = 1,  /* 反转 */
  MOTOR_DIR_COAST = 2,  /* 滑行（IN 全低，不加电） */
  MOTOR_DIR_BRAKE = 3   /* 刹车（IN 同高，短接绕组） */
} motor_dir_t;

typedef enum {
  MOTOR_STOP_COAST = 0, /* 常规停止：滑行 */
  MOTOR_STOP_BRAKE = 1  /* 急停：刹车（跌倒/故障用） */
} motor_stop_mode_t;

/**
 * @brief 初始化：PWM 通道启动、占空比清零、IN 全低（滑行）
 * @note  在 MX_TIM3_Init / MX_GPIO_Init 之后调用
 */
err_t motor_init(void);

/* 10ms 周期任务：推进斜坡与换向保护 */
err_t motor_update(void);

/* 单轮设置：duty 0~1000（内部再受 motor_max_duty 与死区约束） */
err_t motor_set(motor_id_t id, motor_dir_t dir, int16_t duty);

/* 双轮一起设置（推荐：保证同一拍内生效） */
err_t motor_set_pair(motor_dir_t dir_l, int16_t duty_l,
                     motor_dir_t dir_r, int16_t duty_r);

/* 全部停止：COAST=滑行（柔和）/ BRAKE=刹车（急停） */
err_t motor_stop(motor_stop_mode_t mode);

/* 当前实际占空比（斜坡后的值，调试/显示用） */
int16_t motor_get_duty(motor_id_t id);
motor_dir_t motor_get_dir(motor_id_t id);

/* 自测：不转动电机，只检查配置与安全态 */
err_t motor_selftest(char *out, uint16_t n);

/* 把本模块的 shell 命令挂到命令行（app_init 调用） */
err_t motor_shell_register(void);

#endif /* USER_DRV_MOTOR_H */
