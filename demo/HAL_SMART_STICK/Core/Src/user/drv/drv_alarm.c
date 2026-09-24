/**
 * @file    drv_alarm.c
 * @brief   报警驱动实现（蜂鸣器 + 振动马达，非阻塞 pattern 状态机）
 * @version 0.1  (2026-09-24)
 */
#include <stdio.h>
#include <string.h>

#include "drv_alarm.h"
#include "svc_log.h"
#include "svc_shell.h"
#include "bsp_gpio.h"
#include "bsp_time.h"
#include "cfg.h"
#include "Delay.h"

#define LOG_TAG "ALM "

/* ---------------- pattern 定义 ---------------- */
typedef struct {
  uint8_t  prio;          /* 越大越优先 */
  uint16_t beep_on_ms;    /* 单次鸣叫时长 */
  uint16_t beep_gap_ms;   /* 鸣叫间隔 */
  uint8_t  beep_rep;      /* 鸣叫次数（0 = 不鸣） */
  uint16_t vib_on_ms;
  uint16_t vib_gap_ms;
  uint8_t  vib_rep;
  uint8_t  loop;          /* 1 = 循环播放（需 alarm_stop 才停） */
  uint16_t loop_gap_ms;
  uint16_t dedup_ms;      /* 0 = 用全局 g_cfg.alarm_dedup_ms */
} alarm_pat_t;

static const alarm_pat_t s_pat[ALARM_EV_COUNT] = {
  /* BOOT_OK          */ { 0u, 100u,  80u, 2u,   0u,   0u, 0u, 0u,    0u,    0u },
  /* OBSTACLE_WARN    */ { 1u,  70u, 630u, 1u, 100u, 600u, 1u, 0u,    0u,  600u },
  /* LOW_BATT         */ { 2u, 250u,   0u, 1u,   0u,   0u, 0u, 0u,    0u, 55000u },
  /* OBSTACLE_STOP    */ { 3u,  80u, 120u, 2u, 200u, 300u, 2u, 0u,    0u,  500u },
  /* SYS_DEGRADED     */ { 3u, 300u,   0u, 1u,   0u,   0u, 0u, 0u,    0u, 2500u },
  /* BATT_CRIT        */ { 4u, 300u, 200u, 2u,   0u,   0u, 0u, 0u,    0u, 9000u },
  /* FALL_SOS         */ { 5u, 800u, 400u, 3u, 800u, 400u, 3u, 1u, 1200u,    0u }
};

/* ---------------- 状态 ---------------- */
typedef struct {
  uint8_t     idle;
  alarm_ev_t  ev;
  const alarm_pat_t *pat;
  uint8_t     buzz_on;        /* 当前是否在响 */
  uint8_t     buzz_rep;       /* 已完成的鸣叫段数 */
  uint32_t    buzz_t;
  uint8_t     vib_on;
  uint8_t     vib_rep;
  uint32_t    vib_t;
  uint8_t     loop_wait;
  uint32_t    loop_t;
} alarm_state_t;

static alarm_state_t s_al;
static uint8_t       s_enable = 1u;
static uint32_t      s_last_ms[ALARM_EV_COUNT];

/* ---------------- 内部 ---------------- */
static void alarm_out_off(void)
{
  bsp_buzz(0u);
  bsp_vib(0u);
  s_al.buzz_on = 0u;
  s_al.vib_on  = 0u;
}

static void alarm_start(alarm_ev_t ev)
{
  const alarm_pat_t *p = &s_pat[ev];

  s_al.idle      = 0u;
  s_al.ev        = ev;
  s_al.pat       = p;
  s_al.loop_wait = 0u;

  s_al.buzz_rep = (p->beep_rep > 0u) ? 1u : 0u;
  s_al.buzz_on  = (p->beep_rep > 0u) ? 1u : 0u;
  s_al.buzz_t   = bsp_time_ms();

  s_al.vib_rep  = (p->vib_rep > 0u) ? 1u : 0u;
  s_al.vib_on   = (p->vib_rep > 0u) ? 1u : 0u;
  s_al.vib_t    = bsp_time_ms();

  bsp_buzz(s_al.buzz_on);
  bsp_vib(s_al.vib_on);

  LOG_I(LOG_TAG, "play %s (prio=%u%s)", alarm_ev_name(ev), (unsigned)p->prio,
        (p->loop != 0u) ? ", loop" : "");
}

err_t alarm_init(void)
{
  memset(&s_al, 0, sizeof(s_al));
  memset(s_last_ms, 0, sizeof(s_last_ms));
  s_al.idle = 1u;
  s_al.ev   = ALARM_EV_COUNT;      /* 无效值 = 空闲 */
  alarm_out_off();
  s_enable = 1u;
  LOG_I(LOG_TAG, "init ok (buzz %s active, vib ok)",
        (CFG_BUZZER_ACTIVE_LOW != 0) ? "low-level" : "high-level");
  return ERR_OK;
}

err_t alarm_play(alarm_ev_t ev)
{
  const alarm_pat_t *p;
  uint32_t dedup;

  if (ev >= ALARM_EV_COUNT) {
    return ERR_PARAM;
  }
  if (s_enable == 0u) {
    return ERR_UNSUPPORTED;                       /* 已静音：明确告知调用方 */
  }

  p     = &s_pat[ev];
  dedup = (p->dedup_ms != 0u) ? (uint32_t)p->dedup_ms : (uint32_t)g_cfg.alarm_dedup_ms;

  /* 去重窗口内不再重复触发（避免刷屏式鸣叫） */
  if ((s_last_ms[ev] != 0u) && (bsp_time_expired(s_last_ms[ev], dedup) == 0u)) {
    LOG_D(LOG_TAG, "suppress %s (dedup %ums)", alarm_ev_name(ev), (unsigned)dedup);
    return ERR_BUSY;
  }

  /* 优先级仲裁：低优先级不打断正在播放的高优先级/同级事件 */
  if ((s_al.idle == 0u) && (ev != s_al.ev)) {
    if (p->prio <= s_pat[s_al.ev].prio) {
      LOG_D(LOG_TAG, "busy on %s, drop %s", alarm_ev_name(s_al.ev), alarm_ev_name(ev));
      return ERR_BUSY;
    }
    LOG_I(LOG_TAG, "preempt %s by %s", alarm_ev_name(s_al.ev), alarm_ev_name(ev));
  }

  s_last_ms[ev] = bsp_time_ms();
  alarm_start(ev);
  return ERR_OK;
}

void alarm_stop(void)
{
  if (s_al.idle == 0u) {
    LOG_I(LOG_TAG, "stop %s", alarm_ev_name(s_al.ev));
  }
  alarm_out_off();
  s_al.idle = 1u;
  s_al.ev   = ALARM_EV_COUNT;
  s_al.pat  = NULL;
}

alarm_ev_t alarm_active(void)
{
  return (s_al.idle != 0u) ? ALARM_EV_COUNT : s_al.ev;
}

void alarm_set_enable(uint8_t on)
{
  s_enable = (on != 0u) ? 1u : 0u;
  if (s_enable == 0u) {
    alarm_stop();
  }
  LOG_I(LOG_TAG, "alarm %s", (s_enable != 0u) ? "enabled" : "muted");
}

uint8_t alarm_get_enable(void)
{
  return s_enable;
}

const char *alarm_ev_name(alarm_ev_t ev)
{
  static const char *const names[ALARM_EV_COUNT] = {
    "boot", "warn", "lowbatt", "stop", "degraded", "crit", "fall"
  };

  return (ev < ALARM_EV_COUNT) ? names[ev] : "?";
}

/* ---------------- 10ms 步进状态机 ---------------- */
err_t alarm_update(void)
{
  const alarm_pat_t *p = s_al.pat;

  if (s_al.idle != 0u) {
    return ERR_OK;
  }

  /* --- 蜂鸣器相位 --- */
  if (p->beep_rep > 0u) {
    if (s_al.buzz_on != 0u) {
      if (bsp_time_expired(s_al.buzz_t, p->beep_on_ms) != 0u) {
        bsp_buzz(0u);
        s_al.buzz_on = 0u;
        s_al.buzz_t  = bsp_time_ms();
      }
    } else {
      if (s_al.buzz_rep < p->beep_rep) {
        if (bsp_time_expired(s_al.buzz_t, p->beep_gap_ms) != 0u) {
          bsp_buzz(1u);
          s_al.buzz_on = 1u;
          s_al.buzz_rep++;
          s_al.buzz_t = bsp_time_ms();
        }
      }
    }
  }

  /* --- 振动相位 --- */
  if (p->vib_rep > 0u) {
    if (s_al.vib_on != 0u) {
      if (bsp_time_expired(s_al.vib_t, p->vib_on_ms) != 0u) {
        bsp_vib(0u);
        s_al.vib_on = 0u;
        s_al.vib_t  = bsp_time_ms();
      }
    } else {
      if (s_al.vib_rep < p->vib_rep) {
        if (bsp_time_expired(s_al.vib_t, p->vib_gap_ms) != 0u) {
          bsp_vib(1u);
          s_al.vib_on = 1u;
          s_al.vib_rep++;
          s_al.vib_t = bsp_time_ms();
        }
      }
    }
  }

  /* --- 是否完成一整轮 --- */
  {
    uint8_t buzz_done = (p->beep_rep == 0u) ||
                        ((s_al.buzz_on == 0u) && (s_al.buzz_rep >= p->beep_rep));
    uint8_t vib_done  = (p->vib_rep == 0u) ||
                        ((s_al.vib_on == 0u) && (s_al.vib_rep >= p->vib_rep));

    if ((buzz_done != 0u) && (vib_done != 0u)) {
      if (p->loop != 0u) {
        if (s_al.loop_wait == 0u) {
          s_al.loop_wait = 1u;
          s_al.loop_t    = bsp_time_ms();
        } else if (bsp_time_expired(s_al.loop_t, p->loop_gap_ms) != 0u) {
          alarm_start(s_al.ev);                 /* 循环重放（FALL_SOS 一直响到人工清除） */
        }
      } else {
        s_al.idle = 1u;
        s_al.ev   = ALARM_EV_COUNT;
        s_al.pat  = NULL;
        alarm_out_off();
      }
    }
  }
  return ERR_OK;
}

err_t alarm_selftest(char *out, uint16_t n)
{
  if ((out == NULL) || (n == 0u)) {
    return ERR_PARAM;
  }
  /* 自测：短促响一声 + 振一下（各 50ms），用于确认硬件通路 */
  bsp_vib(1u);
  Delay_ms(50u);
  bsp_vib(0u);
  bsp_buzz(1u);
  Delay_ms(50u);
  bsp_buzz(0u);

  (void)snprintf(out, n, "OK: beep+vib pulsed 50ms, enable=%u, active=%s",
                 (unsigned)s_enable, alarm_ev_name(alarm_active()));
  return ERR_OK;
}

/* ==================== shell 命令 ==================== */
static alarm_ev_t alarm_parse_ev(const char *s)
{
  alarm_ev_t i;

  for (i = (alarm_ev_t)0; i < ALARM_EV_COUNT; i = (alarm_ev_t)(i + 1)) {
    if (strcmp(s, alarm_ev_name(i)) == 0) {
      return i;
    }
  }
  return ALARM_EV_COUNT;
}

static int cmd_alarm(int argc, char **argv)
{
  if (argc < 2) {
    LOG_I(LOG_TAG, "usage: alarm boot|warn|lowbatt|stop|degraded|crit|fall");
    LOG_I(LOG_TAG, "       alarm off | alarm mute 0|1");
    LOG_I(LOG_TAG, "now: enable=%u active=%s",
          (unsigned)alarm_get_enable(), alarm_ev_name(alarm_active()));
    return ERR_OK;
  }
  if (strcmp(argv[1], "off") == 0) {
    alarm_stop();
    LOG_I(LOG_TAG, "stopped");
    return ERR_OK;
  }
  if (strcmp(argv[1], "mute") == 0) {
    int32_t v = 0;

    if ((argc < 3) || (svc_shell_parse_i32(argv[2], &v) == 0u)) {
      return ERR_SHELL_ARG;
    }
    alarm_set_enable((v != 0) ? 0u : 1u);      /* mute 1 = 静音 */
    return ERR_OK;
  }
  {
    alarm_ev_t ev = alarm_parse_ev(argv[1]);

    if (ev >= ALARM_EV_COUNT) {
      return ERR_SHELL_ARG;
    }
    LOG_I(LOG_TAG, "alarm %s -> %s", argv[1], err_str(alarm_play(ev)));
  }
  return ERR_OK;
}

/* buzz/vib：直接点位测试（阻塞给定时长，用户显式命令才走这里） */
static int cmd_buzz(int argc, char **argv)
{
  int32_t ms = 0;

  if ((argc < 2) || (svc_shell_parse_i32(argv[1], &ms) == 0u) || (ms < 0) || (ms > 3000)) {
    return ERR_SHELL_ARG;
  }
  if (ms > 0) {
    bsp_buzz(1u);
    Delay_ms((uint32_t)ms);
    bsp_buzz(0u);
  }
  LOG_I(LOG_TAG, "buzz %ldms done", (long)ms);
  return ERR_OK;
}

static int cmd_vib(int argc, char **argv)
{
  int32_t ms = 0;

  if ((argc < 2) || (svc_shell_parse_i32(argv[1], &ms) == 0u) || (ms < 0) || (ms > 3000)) {
    return ERR_SHELL_ARG;
  }
  if (ms > 0) {
    bsp_vib(1u);
    Delay_ms((uint32_t)ms);
    bsp_vib(0u);
  }
  LOG_I(LOG_TAG, "vib %ldms done", (long)ms);
  return ERR_OK;
}

static const shell_cmd_t s_alarm_cmds[] = {
  { "alarm", "alarm <ev>|off|mute 0|1  - play/stop alarm pattern",  cmd_alarm },
  { "buzz",  "buzz <ms>                - beep N ms (0~3000)",      cmd_buzz  },
  { "vib",   "vib <ms>                 - vibrate N ms (0~3000)",   cmd_vib   }
};

err_t alarm_shell_register(void)
{
  return svc_shell_register_table(s_alarm_cmds,
                                  (uint16_t)(sizeof(s_alarm_cmds) / sizeof(s_alarm_cmds[0])));
}
