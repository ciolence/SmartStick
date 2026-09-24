/**
 * @file    app_avoid.c
 * @brief   避障分级策略实现（含回滞与 fail-safe）
 * @version 0.1  (2026-09-24)
 */
#include <stdio.h>

#include "app_avoid.h"
#include "svc_log.h"
#include "bsp_time.h"
#include "drv_us.h"
#include "cfg.h"
#include "board.h"

#define LOG_TAG "AVD "

static avoid_out_t s_out;
static uint32_t    s_invalid_start;     /* 首次"无效"时刻（0 = 之前有效） */
static uint8_t     s_stopped;           /* 已进入停车状态 → 启用回滞 */

void app_avoid_task(void)
{
  uint16_t d     = us_get_mm();
  uint8_t  valid = us_is_valid();
  uint32_t now   = bsp_time_ms();

  /* ---------- 无效数据：fail-safe ---------- */
  if ((valid == 0u) || (d == US_DIST_INVALID)) {
    if (s_invalid_start == 0u) {
      s_invalid_start = now;
    }
    if (bsp_time_expired(s_invalid_start, (uint32_t)g_cfg.avoid_invalid_ms) != 0u) {
      if (s_out.level != AVOID_L3) {
        LOG_W(LOG_TAG, "L3 sensor fail: no valid echo for %ldms -> treat as obstacle",
              (long)g_cfg.avoid_invalid_ms);
      }
      s_out.level = AVOID_L3;
      s_out.scale = 0u;
      s_out.mm    = US_DIST_INVALID;
      s_out.valid = 0u;
      s_stopped   = 1u;
    }
    /* 未超时：维持上一次等级（单次丢回波不打断牵引） */
    return;
  }
  s_invalid_start = 0u;
  s_out.valid     = 1u;
  s_out.mm        = d;

  /* ---------- 回滞：停车后必须真正退出危险区才恢复 ---------- */
  if (s_stopped != 0u) {
    uint16_t clear_mm = (uint16_t)((uint16_t)g_cfg.avoid_stop_mm +
                                   (uint16_t)g_cfg.avoid_hyst_mm);

    if (d > clear_mm) {
      s_stopped = 0u;
      LOG_I(LOG_TAG, "obstacle cleared: %umm > %umm", (unsigned)d, (unsigned)clear_mm);
    } else {
      s_out.level = AVOID_L2;
      s_out.scale = 0u;
      return;
    }
  }

  /* ---------- 三级判定 ---------- */
  if (d > (uint16_t)g_cfg.avoid_slow_mm) {
    s_out.level = AVOID_L0;
    s_out.scale = 1000u;
  } else if (d > (uint16_t)g_cfg.avoid_stop_mm) {
    /* 线性减速：stop_mm → avoid_min_scale，slow_mm → 1000 */
    uint32_t span  = (uint32_t)((uint16_t)g_cfg.avoid_slow_mm - (uint16_t)g_cfg.avoid_stop_mm);
    uint32_t over  = (uint32_t)(d - (uint16_t)g_cfg.avoid_stop_mm);
    uint32_t smin  = (uint32_t)g_cfg.avoid_min_scale;
    uint32_t scale = (span == 0u) ? 1000u : (smin + ((1000u - smin) * over) / span);

    s_out.level = AVOID_L1;
    s_out.scale = (uint16_t)((scale > 1000u) ? 1000u : scale);
  } else {
    s_out.level = AVOID_L2;
    s_out.scale = 0u;
    s_stopped   = 1u;
  }
}

const avoid_out_t *app_avoid_out(void)
{
  return &s_out;
}

const char *app_avoid_level_str(avoid_level_t lv)
{
  static const char *const n[4] = { "L0 clear", "L1 slow", "L2 stop", "L3 fail" };

  return ((uint8_t)lv < 4u) ? n[lv] : "?";
}
