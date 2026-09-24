/**
 * @file    app_fsm.c
 * @brief   全局状态机实现（按键事件 + 传感器事件 + 迁移）
 * @version 0.1  (2026-09-24)
 */
#include <stdio.h>
#include <string.h>

#include "app_fsm.h"
#include "app_avoid.h"
#include "app_fall.h"
#include "app_guide.h"
#include "app_alarm.h"
#include "drv_alarm.h"
#include "drv_motor.h"
#include "drv_key.h"
#include "drv_us.h"
#include "svc_log.h"
#include "svc_shell.h"
#include "bsp_time.h"
#include "cfg.h"

#define LOG_TAG "FSM "

#define DEGRADE_AFTER_MS   3000u      /* 超声波失效持续多久 → 进入降级 */

static app_state_t s_state = ST_BOOT;
static uint32_t    s_l3_start;        /* L3（失效）起始时刻 */
static uint32_t    s_state_ms;

static const char *state_name(app_state_t st)
{
  switch (st) {
    case ST_IDLE:      return "IDLE";
    case ST_GUIDE:     return "GUIDE";
    case ST_ALARM_SOS: return "SOS";
    case ST_DEGRADED:  return "DEGR";
    case ST_FAULT:     return "FAULT";
    case ST_BOOT:
    default:           return "BOOT";
  }
}

const char *app_fsm_state_str(void)
{
  return state_name(s_state);
}

app_state_t app_fsm_state(void)
{
  return s_state;
}

uint8_t app_fsm_guide_allowed(void)
{
  return ((s_state == ST_GUIDE) || (s_state == ST_DEGRADED)) ? 1u : 0u;
}

static void fsm_enter(app_state_t st)
{
  if (st == s_state) {
    return;
  }
  LOG_I(LOG_TAG, "%s -> %s (after %lums)", state_name(s_state), state_name(st),
        (unsigned long)(bsp_time_ms() - s_state_ms));
  s_state    = st;
  s_state_ms = bsp_time_ms();
}

void app_fsm_init(void)
{
  s_state    = ST_IDLE;
  s_state_ms = bsp_time_ms();
  s_l3_start = 0u;
  LOG_I(LOG_TAG, "init ok: %s (SOS short=toggle guide, SOS 3s=clear alarm, MODE short=mute)",
        state_name(s_state));
}

void app_fsm_task(void)
{
  key_ev_t ev;

  /* ---------- 1. 按键 ---------- */
  ev = key_take_event(KEY_ID_SOS);
  if (ev != KEY_EV_NONE) {
    if (ev == KEY_EV_VLONG) {
      if (s_state == ST_ALARM_SOS) {
        LOG_I(LOG_TAG, "SOS 3s -> clear alarm");
        app_alarm_clear();
        fsm_enter(ST_IDLE);
      } else {
        LOG_I(LOG_TAG, "SOS 3s ignored in %s", state_name(s_state));
      }
    } else if (ev == KEY_EV_SHORT) {
      if (s_state == ST_IDLE) {
        LOG_I(LOG_TAG, "SOS short -> start guide");
        fsm_enter(ST_GUIDE);
      } else if ((s_state == ST_GUIDE) || (s_state == ST_DEGRADED)) {
        LOG_I(LOG_TAG, "SOS short -> stop guide");
        fsm_enter(ST_IDLE);
      }
    }
  }

  ev = key_take_event(KEY_ID_MODE);
  if (ev == KEY_EV_SHORT) {
    alarm_set_enable((alarm_get_enable() != 0u) ? 0u : 1u);
  }

  /* ---------- 2. 跌倒事件（最高优先级） ---------- */
  if (app_fall_take_event() != 0u) {
    LOG_E(LOG_TAG, "FALL detected -> SOS state");
    (void)motor_stop(MOTOR_STOP_BRAKE);
    app_alarm_fall_start();
    fsm_enter(ST_ALARM_SOS);
  }

  /* ---------- 3. 超声波失效 → 降级 ---------- */
  if ((s_state == ST_GUIDE) || (s_state == ST_DEGRADED)) {
    const avoid_out_t *av = app_avoid_out();

    if (av->level == AVOID_L3) {
      if (s_l3_start == 0u) {
        s_l3_start = bsp_time_ms();
      }
      if ((s_state == ST_GUIDE) &&
          (bsp_time_expired(s_l3_start, DEGRADE_AFTER_MS) != 0u)) {
        LOG_W(LOG_TAG, "blind(us) for %ums -> DEGRADED", (unsigned)DEGRADE_AFTER_MS);
        fsm_enter(ST_DEGRADED);
      }
    } else {
      s_l3_start = 0u;
    }
  } else {
    s_l3_start = 0u;
  }
}

err_t app_fsm_cmd(const char *cmd)
{
  if (cmd == NULL) {
    return ERR_PARAM;
  }
  if (strcmp(cmd, "guide start") == 0) {
    if (s_state == ST_FAULT) {
      return ERR_APP_STATE;
    }
    fsm_enter(ST_GUIDE);
    return ERR_OK;
  }
  if (strcmp(cmd, "guide stop") == 0) {
    if (s_state == ST_ALARM_SOS) {
      return ERR_APP_STATE;
    }
    fsm_enter(ST_IDLE);
    return ERR_OK;
  }
  if (strcmp(cmd, "alarm clear") == 0) {
    app_alarm_clear();
    if (s_state == ST_ALARM_SOS) {
      fsm_enter(ST_IDLE);
    }
    return ERR_OK;
  }
  if (strcmp(cmd, "sys clear") == 0) {
    if (s_state == ST_DEGRADED) {
      fsm_enter(ST_IDLE);
    }
    return ERR_OK;
  }
  if (strcmp(cmd, "fault") == 0) {
    (void)motor_stop(MOTOR_STOP_BRAKE);
    fsm_enter(ST_FAULT);
    return ERR_OK;
  }
  return ERR_SHELL_ARG;
}

/* ==================== shell 命令 ==================== */
static int cmd_sys(int argc, char **argv)
{
  if (argc < 2) {
    LOG_I(LOG_TAG, "usage: sys state | sys clear | sys fault");
    return ERR_OK;
  }
  if (strcmp(argv[1], "state") == 0) {
    LOG_I(LOG_TAG, "state=%s guide_allowed=%u", app_fsm_state_str(),
          (unsigned)app_fsm_guide_allowed());
    return ERR_OK;
  }
  if (strcmp(argv[1], "clear") == 0) {
    err_t e = app_fsm_cmd("sys clear");

    LOG_I(LOG_TAG, "sys clear -> %s (state=%s)", err_str(e), app_fsm_state_str());
    return ERR_OK;
  }
  if (strcmp(argv[1], "fault") == 0) {
    (void)app_fsm_cmd("fault");
    LOG_W(LOG_TAG, "entered FAULT (reset to recover)");
    return ERR_OK;
  }
  return ERR_SHELL_ARG;
}

static const shell_cmd_t s_sys_cmds[] = {
  { "sys", "sys state|clear|fault    - FSM state / clear degraded / force fault", cmd_sys }
};

err_t app_fsm_shell_register(void)
{
  return svc_shell_register_table(s_sys_cmds,
                                  (uint16_t)(sizeof(s_sys_cmds) / sizeof(s_sys_cmds[0])));
}
