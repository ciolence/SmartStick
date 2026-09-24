/**
 * @file    app_alarm.c
 * @brief   报警策略实现（把状态/传感器翻译成报警事件；节奏由 drv_alarm 的去重窗口控制）
 * @version 0.1  (2026-09-24)
 */
#include "app_alarm.h"
#include "app_avoid.h"
#include "app_fsm.h"
#include "drv_alarm.h"
#include "drv_batt.h"
#include "svc_log.h"
#include "bsp_time.h"
#include "cfg.h"

#define LOG_TAG "PALM"

static uint8_t s_sos;

void app_alarm_fall_start(void)
{
  s_sos = 1u;
  LOG_E(LOG_TAG, "FALL SOS: loop alarm until cleared (SOS 3s / 'alarm clear')");
  (void)alarm_play(ALARM_EV_FALL_SOS);
}

void app_alarm_clear(void)
{
  s_sos = 0u;
  alarm_stop();
  LOG_I(LOG_TAG, "alarm cleared");
}

uint8_t app_alarm_is_sos(void)
{
  return s_sos;
}

void app_alarm_task(void)
{
  const avoid_out_t  *av;
  const batt_data_t  *bat;

  if (alarm_get_enable() == 0u) {
    return;                                     /* 已静音：不产生任何提示 */
  }

  av  = app_avoid_out();
  bat = batt_data();

  /* ---------- 电量提示（与状态无关，始终有效） ---------- */
  if (bat->valid != 0u) {
    if (bat->level == BATT_LEVEL_CRIT) {
      (void)alarm_play(ALARM_EV_BATT_CRIT);     /* pattern 自带去重窗口 9s */
    } else if (bat->level == BATT_LEVEL_LOW) {
      (void)alarm_play(ALARM_EV_LOW_BATT);      /* 55s 去重 */
    }
  }

  /* ---------- 跌倒 SOS 期间不再叠加其它提示 ---------- */
  if (s_sos != 0u) {
    return;
  }

  /* ---------- 避障提示（只在牵引状态提示，待机时保持安静） ---------- */
  if (app_fsm_guide_allowed() == 0u) {
    return;
  }
  switch (av->level) {
    case AVOID_L1:
      (void)alarm_play(ALARM_EV_OBSTACLE_WARN); /* 700ms 一次 */
      break;
    case AVOID_L2:
      (void)alarm_play(ALARM_EV_OBSTACLE_STOP); /* 500ms 一次 */
      break;
    case AVOID_L3:
      (void)alarm_play(ALARM_EV_SYS_DEGRADED);  /* 2.5s 一次 */
      break;
    case AVOID_L0:
    default:
      break;
  }
}
