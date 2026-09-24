/**
 * @file    drv_motor.c
 * @brief   L298N 双电机驱动实现（开环）
 * @note    安全策略见 drv_motor.h；本文件是唯一写 IN1~IN4 / ENA / ENB 的地方
 * @version 0.1  (2026-09-24)
 */
#include <stdio.h>
#include <string.h>

#include "drv_motor.h"
#include "svc_log.h"
#include "svc_shell.h"
#include "bsp_time.h"
#include "cfg.h"
#include "board.h"

#define LOG_TAG "MOT "

#define MOTOR_COUNT   2u

typedef struct {
  GPIO_TypeDef  *in1_port;
  uint16_t       in1_pin;
  GPIO_TypeDef  *in2_port;
  uint16_t       in2_pin;
  uint32_t       pwm_ch;
  uint8_t        invert;          /* 1 = 电机线接反了（改参数即可，不用拆线） */
  motor_dir_t    cur_dir;         /* 逻辑方向（已写到 IN 引脚） */
  motor_dir_t    tgt_dir;         /* 目标逻辑方向 */
  int16_t        cur_duty;        /* 斜坡后的当前占空比（0~1000） */
  int16_t        tgt_duty;        /* 目标占空比 */
  uint32_t       hold_start_ms;   /* 换向保护计时起点 */
  uint8_t        dir_pending;     /* 1 = 有待生效的换向 */
} motor_t;

static motor_t s_m[MOTOR_COUNT];

/* ---------------- 内部：写 IN 引脚 / PWM ---------------- */
static void motor_write_dir(motor_t *m, motor_dir_t dir)
{
  GPIO_PinState a;
  GPIO_PinState b;
  motor_dir_t   pin_dir = dir;

  if ((m->invert != 0u) && ((dir == MOTOR_DIR_FWD) || (dir == MOTOR_DIR_REV))) {
    pin_dir = (dir == MOTOR_DIR_FWD) ? MOTOR_DIR_REV : MOTOR_DIR_FWD;
  }

  switch (pin_dir) {
    case MOTOR_DIR_FWD:   a = GPIO_PIN_SET;   b = GPIO_PIN_RESET; break;   /* 正转 */
    case MOTOR_DIR_REV:   a = GPIO_PIN_RESET; b = GPIO_PIN_SET;   break;   /* 反转 */
    case MOTOR_DIR_BRAKE: a = GPIO_PIN_SET;   b = GPIO_PIN_SET;   break;   /* 刹车 */
    case MOTOR_DIR_COAST:
    default:              a = GPIO_PIN_RESET; b = GPIO_PIN_RESET; break;   /* 滑行 */
  }
  HAL_GPIO_WritePin(m->in1_port, m->in1_pin, a);
  HAL_GPIO_WritePin(m->in2_port, m->in2_pin, b);

  m->cur_dir = dir;               /* 记录逻辑方向（不受 invert 影响） */
}

static void motor_write_pwm(motor_t *m, int16_t duty)
{
  if ((duty <= 0) ||
      ((m->cur_dir != MOTOR_DIR_FWD) && (m->cur_dir != MOTOR_DIR_REV))) {
    duty = 0;
  }
  __HAL_TIM_SET_COMPARE(BOARD_TIM_PWM, m->pwm_ch, (uint32_t)duty);
}

/* ---------------- 初始化 ---------------- */
err_t motor_init(void)
{
  /* 先清零占空比再启动 PWM 通道：避免上电"窜一下" */
  __HAL_TIM_SET_COMPARE(BOARD_TIM_PWM, BOARD_PWM_CH_L, 0u);
  __HAL_TIM_SET_COMPARE(BOARD_TIM_PWM, BOARD_PWM_CH_R, 0u);

  if (HAL_TIM_PWM_Start(BOARD_TIM_PWM, BOARD_PWM_CH_L) != HAL_OK) {
    return ERR_MOTOR_INIT;
  }
  if (HAL_TIM_PWM_Start(BOARD_TIM_PWM, BOARD_PWM_CH_R) != HAL_OK) {
    return ERR_MOTOR_INIT;
  }

  memset(s_m, 0, sizeof(s_m));

  s_m[MOTOR_L].in1_port = MOT_L_IN1_PORT;
  s_m[MOTOR_L].in1_pin  = MOT_L_IN1_PIN;
  s_m[MOTOR_L].in2_port = MOT_L_IN2_PORT;
  s_m[MOTOR_L].in2_pin  = MOT_L_IN2_PIN;
  s_m[MOTOR_L].pwm_ch   = BOARD_PWM_CH_L;
  s_m[MOTOR_L].invert   = (uint8_t)((g_cfg.motor_l_invert != 0) ? 1u : 0u);

  s_m[MOTOR_R].in1_port = MOT_R_IN3_PORT;
  s_m[MOTOR_R].in1_pin  = MOT_R_IN3_PIN;
  s_m[MOTOR_R].in2_port = MOT_R_IN4_PORT;
  s_m[MOTOR_R].in2_pin  = MOT_R_IN4_PIN;
  s_m[MOTOR_R].pwm_ch   = BOARD_PWM_CH_R;
  s_m[MOTOR_R].invert   = (uint8_t)((g_cfg.motor_r_invert != 0) ? 1u : 0u);

  motor_write_dir(&s_m[MOTOR_L], MOTOR_DIR_COAST);
  motor_write_dir(&s_m[MOTOR_R], MOTOR_DIR_COAST);
  motor_write_pwm(&s_m[MOTOR_L], 0);
  motor_write_pwm(&s_m[MOTOR_R], 0);

  if (g_cfg.motor_max_duty > (int32_t)BOARD_PWM_PERIOD) {
    return ERR_MOTOR_CFG;                 /* 参数不合法：上层会记录并拒绝运动 */
  }

  LOG_I(LOG_TAG, "init ok: max=%ld min=%ld ramp=%ld/10ms rev_hold=%ums",
        (long)g_cfg.motor_max_duty, (long)g_cfg.motor_min_duty,
        (long)g_cfg.motor_ramp_per10ms, (unsigned)CFG_MOTOR_REV_DELAY_MS);
  return ERR_OK;
}

/* ---------------- 占空比约束 ---------------- */
static int16_t motor_clamp_duty(int16_t duty)
{
  int16_t d = duty;

  if (d < 0) {
    d = 0;
  }
  if (d > (int16_t)g_cfg.motor_max_duty) {
    d = (int16_t)g_cfg.motor_max_duty;
  }
  if ((d > 0) && (d < (int16_t)g_cfg.motor_min_duty)) {
    d = (int16_t)g_cfg.motor_min_duty;     /* 死区补偿：否则"嗡嗡不转" */
  }
  return d;
}

/* ---------------- 设置 ---------------- */
err_t motor_set(motor_id_t id, motor_dir_t dir, int16_t duty)
{
  motor_t *m;

  if ((uint8_t)id >= MOTOR_COUNT) {
    return ERR_PARAM;
  }
  m = &s_m[id];

  /* 停止类方向：立即生效（滑行/刹车） */
  if ((dir == MOTOR_DIR_COAST) || (dir == MOTOR_DIR_BRAKE)) {
    m->tgt_duty    = 0;
    m->cur_duty    = 0;
    m->tgt_dir     = dir;
    m->dir_pending = 0u;
    motor_write_pwm(m, 0);
    motor_write_dir(m, dir);
    return ERR_OK;
  }

  if (m->cur_dir == dir) {
    m->tgt_dir     = dir;
    m->dir_pending = 0u;
    m->tgt_duty    = motor_clamp_duty(duty);
    return ERR_OK;
  }

  /* 换向：先滑行 CFG_MOTOR_REV_DELAY_MS，时间到再由 update() 上电（H 桥保护） */
  m->tgt_dir      = dir;
  m->dir_pending  = 1u;
  m->hold_start_ms = bsp_time_ms();
  m->tgt_duty     = motor_clamp_duty(duty);
  m->cur_duty     = 0;
  motor_write_pwm(m, 0);
  motor_write_dir(m, MOTOR_DIR_COAST);
  LOG_D(LOG_TAG, "dir -> %d, hold %ums", (int)dir, (unsigned)CFG_MOTOR_REV_DELAY_MS);
  return ERR_OK;
}

err_t motor_set_pair(motor_dir_t dir_l, int16_t duty_l,
                     motor_dir_t dir_r, int16_t duty_r)
{
  err_t e1 = motor_set(MOTOR_L, dir_l, duty_l);
  err_t e2 = motor_set(MOTOR_R, dir_r, duty_r);

  return (e1 != ERR_OK) ? e1 : e2;
}

err_t motor_stop(motor_stop_mode_t mode)
{
  motor_dir_t d = (mode == MOTOR_STOP_BRAKE) ? MOTOR_DIR_BRAKE : MOTOR_DIR_COAST;

  (void)motor_set(MOTOR_L, d, 0);
  (void)motor_set(MOTOR_R, d, 0);
  LOG_I(LOG_TAG, "stop (%s)", (mode == MOTOR_STOP_BRAKE) ? "brake" : "coast");
  return ERR_OK;
}

/* ---------------- 10ms 周期任务 ---------------- */
err_t motor_update(void)
{
  uint8_t i;

  for (i = 0u; i < MOTOR_COUNT; i++) {
    motor_t *m = &s_m[i];

    /* 1) 换向保护期结束 → 真正换向 */
    if (m->dir_pending != 0u) {
      if (bsp_time_expired(m->hold_start_ms, CFG_MOTOR_REV_DELAY_MS) == 0u) {
        continue;                                  /* 保护期内保持滑行 */
      }
      motor_write_dir(m, m->tgt_dir);
      m->dir_pending = 0u;
    }

    /* 2) 斜坡推进 */
    if (m->cur_duty != m->tgt_duty) {
      int16_t step = (int16_t)g_cfg.motor_ramp_per10ms;

      if (step < 1) {
        step = 1;
      }
      if (m->cur_duty < m->tgt_duty) {
        m->cur_duty = (int16_t)(m->cur_duty + step);
        if (m->cur_duty > m->tgt_duty) {
          m->cur_duty = m->tgt_duty;
        }
      } else {
        m->cur_duty = (int16_t)(m->cur_duty - step);
        if (m->cur_duty < m->tgt_duty) {
          m->cur_duty = m->tgt_duty;
        }
      }
      motor_write_pwm(m, m->cur_duty);
    }
  }
  return ERR_OK;
}

int16_t motor_get_duty(motor_id_t id)
{
  return ((uint8_t)id < MOTOR_COUNT) ? s_m[id].cur_duty : 0;
}

motor_dir_t motor_get_dir(motor_id_t id)
{
  return ((uint8_t)id < MOTOR_COUNT) ? s_m[id].cur_dir : MOTOR_DIR_COAST;
}

err_t motor_selftest(char *out, uint16_t n)
{
  if ((out == NULL) || (n == 0u)) {
    return ERR_PARAM;
  }
  /* 自测不做任何危险动作：只核对参数合法性与当前是否处于安全态 */
  if (g_cfg.motor_max_duty > (int32_t)BOARD_PWM_PERIOD) {
    (void)snprintf(out, n, "FAIL: max_duty(%ld) > PWM period(%u)",
                   (long)g_cfg.motor_max_duty, (unsigned)BOARD_PWM_PERIOD);
    return ERR_MOTOR_CFG;
  }
  (void)snprintf(out, n, "OK: L dir=%d duty=%d | R dir=%d duty=%d | max_duty=%ld",
                 (int)s_m[MOTOR_L].cur_dir, (int)motor_get_duty(MOTOR_L),
                 (int)s_m[MOTOR_R].cur_dir, (int)motor_get_duty(MOTOR_R),
                 (long)g_cfg.motor_max_duty);
  return ERR_OK;
}

/* ==================== shell 命令 ==================== */
/* motor stop | motor brake | motor <l> <r>   (duty -1000~1000，负=反转，0=滑行) */
static int cmd_motor(int argc, char **argv)
{
  int32_t l = 0;
  int32_t r = 0;

  if (argc < 2) {
    LOG_I(LOG_TAG, "usage: motor stop|brake|<l> <r>  (duty -1000~1000)");
    LOG_I(LOG_TAG, "now: L duty=%d dir=%d | R duty=%d dir=%d",
          (int)motor_get_duty(MOTOR_L), (int)motor_get_dir(MOTOR_L),
          (int)motor_get_duty(MOTOR_R), (int)motor_get_dir(MOTOR_R));
    return ERR_OK;
  }

  if (strcmp(argv[1], "stop") == 0) {
    (void)motor_stop(MOTOR_STOP_COAST);
    return ERR_OK;
  }
  if (strcmp(argv[1], "brake") == 0) {
    (void)motor_stop(MOTOR_STOP_BRAKE);
    return ERR_OK;
  }
  if (argc < 3) {
    return ERR_SHELL_ARG;
  }
  if ((svc_shell_parse_i32(argv[1], &l) == 0u) || (svc_shell_parse_i32(argv[2], &r) == 0u)) {
    return ERR_SHELL_ARG;
  }
  if ((l < -1000) || (l > 1000) || (r < -1000) || (r > 1000)) {
    return ERR_SHELL_ARG;
  }

  (void)motor_set(MOTOR_L, (l >= 0) ? MOTOR_DIR_FWD : MOTOR_DIR_REV,
                  (int16_t)((l >= 0) ? l : -l));
  (void)motor_set(MOTOR_R, (r >= 0) ? MOTOR_DIR_FWD : MOTOR_DIR_REV,
                  (int16_t)((r >= 0) ? r : -r));

  LOG_I(LOG_TAG, "cmd L=%ld R=%ld  (ramp to max_duty=%ld, watch the wheels!)",
        (long)l, (long)r, (long)g_cfg.motor_max_duty);
  return ERR_OK;
}

static const shell_cmd_t s_motor_cmds[] = {
  { "motor", "motor stop|brake|<l> <r>  - drive motors, duty -1000~1000", cmd_motor }
};

err_t motor_shell_register(void)
{
  return svc_shell_register_table(s_motor_cmds,
                                  (uint16_t)(sizeof(s_motor_cmds) / sizeof(s_motor_cmds[0])));
}
