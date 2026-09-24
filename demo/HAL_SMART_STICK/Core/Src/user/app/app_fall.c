/**
 * @file    app_fall.c
 * @brief   跌倒检测实现（自由落体 → 冲击 → 躺倒确认 三要素状态机）
 * @version 0.1  (2026-09-24)
 */
#include <stdio.h>

#include "app_fall.h"
#include "svc_log.h"
#include "bsp_time.h"
#include "drv_imu.h"
#include "cfg.h"

#define LOG_TAG "FALL"

typedef enum {
  F_IDLE = 0,
  F_FREEFALL,
  F_IMPACT,
  F_CONFIRM
} fall_st_t;

static fall_st_t s_st;
static uint32_t  s_st_ms;
static uint8_t   s_ev;
static uint32_t  s_cooldown_until;
static uint32_t  s_count;

/* 角度(0.1°) → sin×100 的查表插值（与 drv_imu 的反查互为正逆） */
static uint16_t sin_pct_from_deg10(uint16_t deg10)
{
  static const uint16_t deg_tbl[7] = { 0u, 150u, 300u, 450u, 600u, 750u, 900u };
  static const uint16_t sin_tbl[7] = { 0u, 26u, 50u, 71u, 87u, 97u, 100u };
  uint8_t i;

  if (deg10 >= 900u) {
    return 100u;
  }
  for (i = 0u; i < 6u; i++) {
    if (deg10 <= deg_tbl[i + 1u]) {
      uint16_t d0 = deg_tbl[i];
      uint16_t d1 = deg_tbl[i + 1u];
      uint16_t s0 = sin_tbl[i];
      uint16_t s1 = sin_tbl[i + 1u];

      return (uint16_t)(s0 + ((uint32_t)(s1 - s0) * (deg10 - d0)) / (d1 - d0));
    }
  }
  return 100u;
}

const char *app_fall_state_str(void)
{
  static const char *const n[4] = { "IDLE", "FREEFALL", "IMPACT", "CONFIRM" };

  return n[(uint8_t)s_st];
}

void app_fall_clear(void)
{
  s_st             = F_IDLE;
  s_ev             = 0u;
  s_cooldown_until = 0u;
}

void app_fall_task(void)
{
  const imu_data_t *imu;
  uint32_t          now;
  uint16_t          sin_th;

#if (CFG_FALL_ENABLE == 0)
  return;
#endif

  imu = imu_data();
  if (imu->valid == 0u) {
    return;
  }

  now = bsp_time_ms();
  if ((int32_t)(now - s_cooldown_until) < 0) {
    return;                                   /* 冷却中：不重复触发 */
  }

  sin_th = sin_pct_from_deg10((uint16_t)g_cfg.fall_tilt_deg10);

  switch (s_st) {
    case F_IDLE:
      if (imu->acc_norm_mg < (uint16_t)g_cfg.fall_freefall_mg) {
        s_st    = F_FREEFALL;
        s_st_ms = now;
      }
      break;

    case F_FREEFALL:
      if (imu->acc_norm_mg > (uint16_t)g_cfg.fall_impact_mg) {
        s_st    = F_IMPACT;                   /* 撞击：进入冲击确认 */
        s_st_ms = now;
        LOG_D(LOG_TAG, "freefall -> impact (|a|=%umg)", (unsigned)imu->acc_norm_mg);
      } else if (imu->acc_norm_mg > ((uint16_t)g_cfg.fall_freefall_mg + 200u)) {
        s_st = F_IDLE;                        /* 只是轻晃：复位 */
      } else if (bsp_time_expired(s_st_ms, (uint32_t)g_cfg.fall_freefall_ms + 300u) != 0u) {
        s_st = F_IDLE;                        /* 自由落体后没有撞击（例如缓慢放下） */
      }
      break;

    case F_IMPACT:
      if (imu->tilt_sin_pct >= sin_th) {
        s_st    = F_CONFIRM;                  /* 撞击后姿态异常：进入躺倒确认 */
        s_st_ms = now;
        LOG_D(LOG_TAG, "impact -> confirm (tilt=%u%% >= %u%%)",
              (unsigned)imu->tilt_sin_pct, (unsigned)sin_th);
      } else if (bsp_time_expired(s_st_ms, 500u) != 0u) {
        s_st = F_IDLE;
      }
      break;

    case F_CONFIRM:
      if (imu->tilt_sin_pct < sin_th) {
        s_st = F_IDLE;                        /* 姿态恢复正常：误报，放弃 */
        LOG_I(LOG_TAG, "confirm aborted (posture recovered)");
      } else if (bsp_time_expired(s_st_ms, (uint32_t)g_cfg.fall_confirm_ms) != 0u) {
        s_ev             = 1u;
        s_count++;
        s_cooldown_until = now + (uint32_t)g_cfg.fall_cooldown_ms;
        s_st             = F_IDLE;
        LOG_E(LOG_TAG, "FALL confirmed #%lu: |a|=%umg tilt=%u%% (%u >= %u)",
              (unsigned long)s_count, (unsigned)imu->acc_norm_mg,
              (unsigned)imu->tilt_sin_pct, (unsigned)imu->tilt_deg,
              (unsigned)((uint16_t)g_cfg.fall_tilt_deg10 / 10u));
      }
      break;

    default:
      s_st = F_IDLE;
      break;
  }
}

uint8_t app_fall_take_event(void)
{
  uint8_t e = s_ev;

  s_ev = 0u;
  return e;
}
