/**
 * @file    drv_us.c
 * @brief   HC-SR04 超声波驱动实现
 * @version 0.1  (2026-09-24)
 */
#include <stdio.h>
#include <string.h>

#include "drv_us.h"
#include "svc_log.h"
#include "svc_shell.h"
#include "bsp_gpio.h"
#include "bsp_time.h"
#include "cfg.h"
#include "board.h"
#include "Delay.h"

#define LOG_TAG "US  "

#define US_ECHO_TIMEOUT_MS   40u      /* 4m 回波约 23ms，40ms 足够 */
#define US_FILTER_N          3u       /* 中值滤波点数 */

typedef struct {
  uint8_t  state;              /* 0 = 空闲，1 = 等待回波 */
  uint8_t  got_rise;           /* 已捕获上升沿 */
  uint8_t  done;               /* 一次测量已完成待消化 */
  uint16_t t_rise;
  uint16_t width_us;
  uint32_t trig_ms;            /* 触发时刻（毫秒，用于超时判定） */
  uint32_t last_trig_ms;       /* 上次触发时刻（用于触发间隔节流） */
  uint16_t raw_mm;             /* 本次换算结果 */
  uint16_t filt_mm;            /* 中值滤波后 */
  uint16_t buf[US_FILTER_N];   /* 滤波窗口 */
  uint8_t  buf_n;
  uint8_t  valid;
  uint32_t ok_cnt;
  uint32_t err_cnt;
  uint32_t ok_ms;
} us_ctx_t;

static us_ctx_t s_us;

/* ---------------- 内部：触发一次测距（阻塞 10µs，可接受） ---------------- */
static void us_trigger(void)
{
  uint16_t t0;

  s_us.got_rise = 0u;
  s_us.done     = 0u;

  bsp_us_trig(1u);
  t0 = bsp_time_us_raw();
  while (bsp_time_us_diff(bsp_time_us_raw(), t0) < US_TRIG_PULSE_US) {
    /* 忙等 10µs（TIM4 计数，回绕安全） */
  }
  bsp_us_trig(0u);

  s_us.trig_ms      = bsp_time_ms();
  s_us.last_trig_ms = s_us.trig_ms;
  s_us.state        = 1u;
}

/* ---------------- 内部：中值滤波 ---------------- */
static uint16_t us_median3(uint16_t a, uint16_t b, uint16_t c)
{
  uint16_t t;

  if (a > b) { t = a; a = b; b = t; }
  if (b > c) { t = b; b = c; c = t; }
  if (a > b) { t = a; a = b; b = t; }
  return b;
}

static void us_filter_push(uint16_t mm)
{
  if (s_us.buf_n < US_FILTER_N) {
    s_us.buf[s_us.buf_n++] = mm;
  } else {
    s_us.buf[0] = s_us.buf[1];
    s_us.buf[1] = s_us.buf[2];
    s_us.buf[2] = mm;
  }
  if (s_us.buf_n >= 2u) {
    uint8_t n = s_us.buf_n;

    s_us.filt_mm = us_median3(s_us.buf[n - 1u], s_us.buf[n - 2u], s_us.buf[n - 3u]);
  } else {
    s_us.filt_mm = s_us.buf[s_us.buf_n - 1u];
  }
}

/* ---------------- 内部：消化一次测量结果 ---------------- */
static void us_consume(void)
{
  uint32_t mm;

  s_us.done  = 0u;
  s_us.state = 0u;

  if (s_us.width_us == 0u) {
    s_us.valid = 0u;
    s_us.err_cnt++;
    err_record(MOD_US, ERR_US_NO_ECHO);
    LOG_D(LOG_TAG, "no echo (timeout) err=%lu", (unsigned long)s_us.err_cnt);
    return;
  }

  /* 距离(mm) = 回波宽度(µs) × 10 / 58  （先乘后除，避免精度损失） */
  mm = ((uint32_t)s_us.width_us * 10u) / US_US_PER_CM;

  if (mm < (uint32_t)US_MIN_MM) {
    s_us.valid = 0u;                 /* 过近：回波不可信，按"有障碍"处理 */
    s_us.err_cnt++;
    err_record(MOD_US, ERR_US_RANGE);
    LOG_D(LOG_TAG, "too close: %lumm (%uus)", (unsigned long)mm, (unsigned)s_us.width_us);
    return;
  }
  if (mm > (uint32_t)US_MAX_MM) {
    mm = (uint32_t)US_MAX_MM;        /* 超量程 → 饱和为"远"，视为无障碍 */
  }

  s_us.raw_mm = (uint16_t)mm;
  us_filter_push((uint16_t)mm);
  s_us.valid  = 1u;
  s_us.ok_cnt++;
  s_us.ok_ms  = bsp_time_ms();
}

/* ---------------- 对外接口 ---------------- */
err_t us_init(void)
{
  memset(&s_us, 0, sizeof(s_us));

#if (BOARD_US1_READY == 0)
  LOG_W(LOG_TAG, "TRIG/ECHO pins not configured in CubeMX -> %s", err_str(ERR_US_NOT_CFG));
  err_record(MOD_US, ERR_US_NOT_CFG);
  return ERR_US_NOT_CFG;
#else
  bsp_us_trig(0u);                     /* TRIG 拉低待命 */
  LOG_I(LOG_TAG, "init ok: TRIG=PA0 ECHO=PA1(EXTI1) trigger every %ldms",
        (long)g_cfg.us_period_ms);
  return ERR_OK;
#endif
}

err_t us_update(void)
{
#if (BOARD_US1_READY == 0)
  return ERR_US_NOT_CFG;
#else
  /* 1) 有结果就先消化 */
  if (s_us.done != 0u) {
    us_consume();
  }

  /* 2) 等待中的超时判定 */
  if ((s_us.state != 0u) && (s_us.done == 0u)) {
    if (bsp_time_expired(s_us.trig_ms, US_ECHO_TIMEOUT_MS) != 0u) {
      s_us.width_us = 0u;          /* 标记为无回波 */
      s_us.done     = 1u;
    }
  }

  /* 3) 空闲且距离上次触发超过 us_period_ms → 触发新一次 */
  if ((s_us.state == 0u) && (s_us.done == 0u)) {
    if ((s_us.last_trig_ms == 0u) ||
        (bsp_time_expired(s_us.last_trig_ms, (uint32_t)g_cfg.us_period_ms) != 0u)) {
      us_trigger();
    }
  }
  return ERR_OK;
#endif
}

uint16_t us_get_mm(void)
{
  return (s_us.valid != 0u) ? s_us.filt_mm : US_DIST_INVALID;
}

uint8_t us_is_valid(void)
{
  return s_us.valid;
}

uint16_t us_get_raw_mm(void)
{
  return s_us.raw_mm;
}

uint32_t us_get_ok_count(void)
{
  return s_us.ok_cnt;
}

uint32_t us_get_err_count(void)
{
  return s_us.err_cnt;
}

uint32_t us_get_last_ms(void)
{
  return s_us.ok_ms;
}

err_t us_selftest(char *out, uint16_t n)
{
  if ((out == NULL) || (n == 0u)) {
    return ERR_PARAM;
  }
#if (BOARD_US1_READY == 0)
  (void)snprintf(out, n, "FAIL: TRIG/ECHO not configured (see CUBEMX_GUIDE 4.10)");
  return ERR_US_NOT_CFG;
#else
  if (s_us.ok_cnt == 0u) {
    (void)snprintf(out, n, "FAIL: no valid echo yet (err=%lu) - check 5V, divider 2k2/3k3, TRIG/ECHO wiring",
                   (unsigned long)s_us.err_cnt);
    return ERR_US_NO_ECHO;
  }
  (void)snprintf(out, n, "OK: %umm raw=%umm ok=%lu err=%lu",
                 (unsigned)us_get_mm(), (unsigned)s_us.raw_mm,
                 (unsigned long)s_us.ok_cnt, (unsigned long)s_us.err_cnt);
  return ERR_OK;
#endif
}

/* ==================== shell 命令 ==================== */
/* us           读当前值
 * us mon [n]   连续打印 n 次（默认 5，最多 20），间隔 150ms —— 便于移动目标观察
 */
static int cmd_us(int argc, char **argv)
{
  if ((argc >= 2) && (strcmp(argv[1], "mon") == 0)) {
    int32_t cnt = 5;
    int32_t i;

    if ((argc >= 3) && (svc_shell_parse_i32(argv[2], &cnt) == 0u)) {
      return ERR_SHELL_ARG;
    }
    if (cnt < 1) { cnt = 1; }
    if (cnt > 20) { cnt = 20; }

    for (i = 0; i < cnt; i++) {
      uint16_t d = us_get_mm();

      if (d == US_DIST_INVALID) {
        LOG_I(LOG_TAG, "[%ld] invalid (no echo/too close)", (long)i);
      } else {
        LOG_I(LOG_TAG, "[%ld] %u mm  (raw %u mm)", (long)i, (unsigned)d,
              (unsigned)us_get_raw_mm());
      }
      Delay_ms(150u);
    }
    return ERR_OK;
  }

  {
    uint16_t d = us_get_mm();

    if (d == US_DIST_INVALID) {
      LOG_W(LOG_TAG, "invalid: no echo / too close  (ok=%lu err=%lu)",
            (unsigned long)s_us.ok_cnt, (unsigned long)s_us.err_cnt);
    } else {
      LOG_I(LOG_TAG, "%u mm  raw=%u mm  valid=%u  ok=%lu err=%lu  last=%lums ago",
            (unsigned)d, (unsigned)s_us.raw_mm, (unsigned)s_us.valid,
            (unsigned long)s_us.ok_cnt, (unsigned long)s_us.err_cnt,
            (unsigned long)(bsp_time_ms() - s_us.ok_ms));
    }
  }
  return ERR_OK;
}

static const shell_cmd_t s_us_cmds[] = {
  { "us", "us | us mon [n]         - read distance (mm) / monitor n samples", cmd_us }
};

err_t us_shell_register(void)
{
  return svc_shell_register_table(s_us_cmds,
                                  (uint16_t)(sizeof(s_us_cmds) / sizeof(s_us_cmds[0])));
}

/* ==================== EXTI 回调（ISR 上下文：只存时间戳） ==================== */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
#if (BOARD_US1_READY == 1)
  if (GPIO_Pin == US1_ECHO_PIN) {
    uint16_t t = bsp_time_us_raw();

    if (bsp_us_echo() != 0u) {           /* 上升沿 */
      s_us.t_rise   = t;
      s_us.got_rise = 1u;
    } else {                             /* 下降沿 */
      if (s_us.got_rise != 0u) {
        s_us.width_us = bsp_time_us_diff(t, s_us.t_rise);
        s_us.got_rise = 0u;
        s_us.done     = 1u;
      }
    }
  }
#else
  (void)GPIO_Pin;
#endif
}
