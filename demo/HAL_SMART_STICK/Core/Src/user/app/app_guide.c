/**
 * @file    app_guide.c
 * @brief   牵引控制实现：直行分级调速 + ★绕行状态机（默认关闭，可在线启用/调参）
 * @note    绕行思路（单路前向超声波下的可行方案）：
 *            停车持续 avoid_stuck_ms → 原地转向 detour_spin_ms → 直行试探 detour_fwd_ms
 *            → 重新测距：已通（≥ detour_recheck_mm）则回到直行；未通则换方向重试；
 *            连续 detour_retry 次仍不通 → 停车 + 冷却（并由报警层提示）。
 *          现场调参建议：先 `guide spin L 700 400` 摸清转向速度，再开 `guide detour 1`，
 *            然后按实际表现调 detour_spin_ms / detour_fwd_ms / detour_recheck_mm。
 *          ★v2（需第二路超声波/磁力计）：按左右净空选择转向方向（GUIDE_ARC_*）。
 * @version 0.1  (2026-09-24)
 */
#include <stdio.h>
#include <string.h>

#include "app_guide.h"
#include "app_avoid.h"
#include "app_fsm.h"
#include "app_alarm.h"
#include "drv_motor.h"
#include "svc_log.h"
#include "svc_shell.h"
#include "bsp_time.h"
#include "cfg.h"

#define LOG_TAG "GUI "

#define PROBE_SPEED_PCT    60      /* 试探速度 = guide_base_duty 的 60% */
#define DEGRADED_LIMIT     300     /* 降级状态下的速度上限（千分比） */

typedef enum {
  DT_IDLE = 0,
  DT_SPIN,
  DT_PROBE
} dt_st_t;

static guide_cmd_t s_cmd = GUIDE_STOP;
static dt_st_t     s_dt;
static uint32_t    s_dt_ms;
static uint8_t     s_dir;              /* 0 = 左，1 = 右 */
static uint8_t     s_attempt;
static uint32_t    s_stop_since;
static uint32_t    s_cooldown_until;
static uint32_t    s_dt_count;

/* 手动覆盖（shell 调参用，仅转向） */
static guide_cmd_t s_manual_cmd = GUIDE_STOP;
static uint16_t    s_manual_duty;
static uint32_t    s_manual_until;

const char *app_guide_state_str(void)
{
  static const char *const n[6] = { "STOP", "FWD", "SPIN_L", "SPIN_R", "ARC_L", "ARC_R" };

  return n[(uint8_t)s_cmd];
}

const char *app_guide_detour_state_str(void)
{
  static const char *const n[3] = { "idle", "spin", "probe" };

  return n[(uint8_t)s_dt];
}

guide_cmd_t app_guide_get(void)
{
  return s_cmd;
}

uint8_t app_guide_is_moving(void)
{
  return (s_cmd != GUIDE_STOP) ? 1u : 0u;
}

uint8_t app_guide_is_detouring(void)
{
  return (s_dt != DT_IDLE) ? 1u : 0u;
}

err_t app_guide_set(guide_cmd_t cmd)
{
  /* 只允许外部强制"停车"：前进/转向必须由 app_guide_task 依据避障结果决策，防止绕过安全逻辑 */
  if (cmd == GUIDE_STOP) {
    s_dt     = DT_IDLE;
    s_cmd    = GUIDE_STOP;
    s_manual_cmd = GUIDE_STOP;
    (void)motor_stop(MOTOR_STOP_COAST);
    return ERR_OK;
  }
  return ERR_UNSUPPORTED;
}

/* ---------------- 内部：输出直行 ---------------- */
static void guide_drive_straight(uint16_t scale, uint16_t limit)
{
  int32_t base  = ((int32_t)g_cfg.guide_base_duty * (int32_t)scale / 1000);
  int32_t duty_l;
  int32_t duty_r;

  base   = base * (int32_t)limit / 1000;
  duty_l = base + g_cfg.motor_trim_l;
  duty_r = base + g_cfg.motor_trim_r;
  if (duty_l < 0) { duty_l = 0; }
  if (duty_r < 0) { duty_r = 0; }

  if ((duty_l == 0) && (duty_r == 0)) {
    (void)motor_stop(MOTOR_STOP_COAST);
    s_cmd = GUIDE_STOP;
    return;
  }
  (void)motor_set_pair(MOTOR_DIR_FWD, (int16_t)duty_l, MOTOR_DIR_FWD, (int16_t)duty_r);
  s_cmd = GUIDE_FORWARD;
}

/* ---------------- 内部：原地转向 ---------------- */
static void guide_spin(uint8_t dir, uint16_t duty)
{
  if (dir == 0u) {
    (void)motor_set_pair(MOTOR_DIR_REV, (int16_t)duty, MOTOR_DIR_FWD, (int16_t)duty);
    s_cmd = GUIDE_SPIN_LEFT;
  } else {
    (void)motor_set_pair(MOTOR_DIR_FWD, (int16_t)duty, MOTOR_DIR_REV, (int16_t)duty);
    s_cmd = GUIDE_SPIN_RIGHT;
  }
}

/* ---------------- 内部：绕行一步 ---------------- */
static void guide_detour_step(const avoid_out_t *av, uint16_t limit)
{
  uint32_t now  = bsp_time_ms();
  int16_t  spin = (int16_t)((int32_t)g_cfg.detour_spin_duty * (int32_t)limit / 1000);

  /* 安全：传感器失效 → 立刻放弃绕行 */
  if (av->level == AVOID_L3) {
    LOG_W(LOG_TAG, "detour aborted: sensor invalid");
    s_dt = DT_IDLE;
    s_attempt = 0u;
    (void)motor_stop(MOTOR_STOP_COAST);
    return;
  }

  switch (s_dt) {
    case DT_SPIN:
      guide_spin(s_dir, (uint16_t)spin);
      if (bsp_time_expired(s_dt_ms, (uint32_t)g_cfg.detour_spin_ms) != 0u) {
        s_dt    = DT_PROBE;
        s_dt_ms = now;
      }
      break;

    case DT_PROBE: {
      /* 试探前进：速度 = 基础速度 × 60% */
      guide_drive_straight((uint16_t)PROBE_SPEED_PCT, limit);

      /* 安全：试探过程中前方又贴近 → 立刻判定失败 */
      if (av->level == AVOID_L2) {
        LOG_I(LOG_TAG, "probe blocked at %umm -> retry", (unsigned)av->mm);
      } else if (bsp_time_expired(s_dt_ms, (uint32_t)g_cfg.detour_fwd_ms) == 0u) {
        break;                                    /* 继续试探 */
      }

      /* 判定：通了就回到直行，否则换方向重试 */
      if ((av->valid != 0u) && (av->mm >= (uint16_t)g_cfg.detour_recheck_mm)) {
        LOG_I(LOG_TAG, "detour OK: %umm (>= %umm), attempts=%u",
              (unsigned)av->mm, (unsigned)g_cfg.detour_recheck_mm, (unsigned)s_attempt);
        s_attempt = 0u;
        s_dt      = DT_IDLE;
      } else {
        s_attempt++;
        s_dir   = (uint8_t)((s_dir == 0u) ? 1u : 0u);
        s_dt    = DT_SPIN;
        s_dt_ms = now;
        LOG_I(LOG_TAG, "detour retry %u, switch to %s", (unsigned)s_attempt,
              (s_dir == 0u) ? "left" : "right");
      }
      break;
    }

    default:
      s_dt = DT_IDLE;
      break;
  }
}

/* ---------------- 周期任务 ---------------- */
void app_guide_task(void)
{
  const avoid_out_t *av;
  uint32_t           now = bsp_time_ms();
  uint16_t           limit;

  /* 0) 非牵引状态：确保停止 */
  if (app_fsm_guide_allowed() == 0u) {
    if (s_cmd != GUIDE_STOP) {
      (void)motor_stop(MOTOR_STOP_COAST);
      s_cmd = GUIDE_STOP;
    }
    s_dt         = DT_IDLE;
    s_attempt    = 0u;
    s_stop_since = 0u;
    s_manual_cmd = GUIDE_STOP;
    return;
  }

  limit = (app_fsm_state() == ST_DEGRADED) ? (uint16_t)DEGRADED_LIMIT : 1000u;

  /* 1) 手动覆盖（shell 调参：定时转向） */
  if (s_manual_cmd != GUIDE_STOP) {
    if ((int32_t)(now - s_manual_until) < 0) {
      guide_spin((s_manual_cmd == GUIDE_SPIN_LEFT) ? 0u : 1u,
                 (uint16_t)((int32_t)s_manual_duty * (int32_t)limit / 1000));
      return;
    }
    s_manual_cmd = GUIDE_STOP;
    (void)motor_stop(MOTOR_STOP_BRAKE);
    LOG_I(LOG_TAG, "manual spin finished");
  }

  av = app_avoid_out();

  /* 2) 绕行推进中 */
  if (s_dt != DT_IDLE) {
    guide_detour_step(av, limit);
    return;
  }

  /* 3) 遇障停车：可选触发绕行 */
  if ((av->level == AVOID_L2) || (av->level == AVOID_L3)) {
    if (s_stop_since == 0u) {
      s_stop_since = now;
    }
    (void)motor_stop(MOTOR_STOP_COAST);
    s_cmd = GUIDE_STOP;

    if ((g_cfg.detour_enable != 0) && (av->level == AVOID_L2) &&
        (bsp_time_expired(s_stop_since, (uint32_t)g_cfg.avoid_stuck_ms) != 0u) &&
        ((int32_t)(now - s_cooldown_until) >= 0)) {
      if (s_attempt < (uint8_t)g_cfg.detour_retry) {
        s_dt       = DT_SPIN;
        s_dt_ms    = now;
        s_dt_count++;
        LOG_I(LOG_TAG, "blocked %ldms -> detour #%lu, spin %s %ldms",
              (long)g_cfg.avoid_stuck_ms, (unsigned long)s_dt_count,
              (s_dir == 0u) ? "left" : "right", (long)g_cfg.detour_spin_ms);
      } else {
        LOG_W(LOG_TAG, "detour failed after %u tries -> stop + cooldown %ldms",
              (unsigned)s_attempt, (long)g_cfg.detour_cooldown_ms);
        s_attempt        = 0u;
        s_cooldown_until = now + (uint32_t)g_cfg.detour_cooldown_ms;
      }
    }
    return;
  }

  s_stop_since = 0u;

  /* 4) 常规直行（按避障系数分级调速） */
  guide_drive_straight(av->scale, limit);
}

/* ==================== shell 命令 ==================== */
static int cmd_guide(int argc, char **argv)
{
  if (argc < 2) {
    LOG_I(LOG_TAG, "usage: guide start|stop|status|detour 0|1|spin L|R [ms] [duty]");
    return ERR_OK;
  }

  if (strcmp(argv[1], "start") == 0) {
    LOG_I(LOG_TAG, "guide start -> %s", err_str(app_fsm_cmd("guide start")));
    return ERR_OK;
  }
  if (strcmp(argv[1], "stop") == 0) {
    (void)app_guide_set(GUIDE_STOP);
    LOG_I(LOG_TAG, "guide stop -> %s", err_str(app_fsm_cmd("guide stop")));
    return ERR_OK;
  }
  if (strcmp(argv[1], "status") == 0) {
    const avoid_out_t *av = app_avoid_out();

    LOG_I(LOG_TAG, "fsm=%s guide=%s detour=%s attempts=%u  avoid=%s %umm scale=%u",
          app_fsm_state_str(), app_guide_state_str(), app_guide_detour_state_str(),
          (unsigned)s_attempt, app_avoid_level_str(av->level), (unsigned)av->mm,
          (unsigned)av->scale);
    LOG_I(LOG_TAG, "cfg: enable=%ld spin=%ldms/%ld fwd=%ldms retry=%ld recheck=%ldmm base=%ld max=%ld",
          (long)g_cfg.detour_enable, (long)g_cfg.detour_spin_ms, (long)g_cfg.detour_spin_duty,
          (long)g_cfg.detour_fwd_ms, (long)g_cfg.detour_retry,
          (long)g_cfg.detour_recheck_mm, (long)g_cfg.guide_base_duty, (long)g_cfg.motor_max_duty);
    return ERR_OK;
  }
  if ((strcmp(argv[1], "detour") == 0) && (argc >= 3)) {
    int32_t v = 0;

    if (svc_shell_parse_i32(argv[2], &v) == 0u) {
      return ERR_SHELL_ARG;
    }
    {
      err_t e = cfg_set("detour_enable", (v != 0) ? 1 : 0);

      s_dt      = DT_IDLE;
      s_attempt = 0u;
      LOG_I(LOG_TAG, "detour = %ld (%s)", (long)g_cfg.detour_enable, err_str(e));
    }
    return ERR_OK;
  }
  if ((strcmp(argv[1], "spin") == 0) && (argc >= 3)) {
    int32_t ms = 700;
    int32_t duty = (int32_t)g_cfg.detour_spin_duty;

    if ((argv[2][0] != 'L') && (argv[2][0] != 'l') &&
        (argv[2][0] != 'R') && (argv[2][0] != 'r')) {
      return ERR_SHELL_ARG;
    }
    if ((argc >= 4) && (svc_shell_parse_i32(argv[3], &ms) == 0u)) {
      return ERR_SHELL_ARG;
    }
    if ((argc >= 5) && (svc_shell_parse_i32(argv[4], &duty) == 0u)) {
      return ERR_SHELL_ARG;
    }
    if ((ms < 100) || (ms > 5000) || (duty < 0) || (duty > 1000)) {
      return ERR_SHELL_ARG;
    }
    s_manual_cmd  = ((argv[2][0] == 'L') || (argv[2][0] == 'l')) ? GUIDE_SPIN_LEFT
                                                                : GUIDE_SPIN_RIGHT;
    s_manual_duty = (uint16_t)duty;
    s_manual_until = bsp_time_ms() + (uint32_t)ms;
    LOG_I(LOG_TAG, "manual spin %s %ldms duty=%ld (clamped by motor_max_duty)",
          (s_manual_cmd == GUIDE_SPIN_LEFT) ? "L" : "R", (long)ms, (long)duty);
    return ERR_OK;
  }
  return ERR_SHELL_ARG;
}

static const shell_cmd_t s_guide_cmds[] = {
  { "guide", "guide start|stop|status|detour 0|1|spin L|R [ms] [duty]", cmd_guide }
};

err_t app_guide_shell_register(void)
{
  return svc_shell_register_table(s_guide_cmds,
                                  (uint16_t)(sizeof(s_guide_cmds) / sizeof(s_guide_cmds[0])));
}
