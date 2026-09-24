/**
 * @file    drv_key.c
 * @brief   按键驱动实现（非阻塞去抖 + 事件）
 * @version 0.1  (2026-09-24)
 */
#include <stdio.h>
#include <string.h>

#include "drv_key.h"
#include "svc_log.h"
#include "svc_shell.h"
#include "bsp_gpio.h"
#include "bsp_time.h"
#include "cfg.h"
#include "Delay.h"

#define LOG_TAG "KEY "

#define KEY_TASK_PERIOD_MS  20u

typedef struct {
  uint8_t  last_raw;
  uint8_t  stable;          /* 1 = 按下 */
  uint8_t  cnt;
  uint32_t press_ms;
  uint8_t  ev_ready;
  key_ev_t ev;
  uint32_t n_short;
  uint32_t n_long;
  uint32_t n_vlong;
} key_st_t;

static key_st_t s_key[KEY_ID_MAX];

const char *key_id_name(key_id_t id)
{
  static const char *const n[KEY_ID_MAX] = { "SOS ", "MODE" };

  return (id < KEY_ID_MAX) ? n[id] : "?";
}

const char *key_ev_name(key_ev_t ev)
{
  static const char *const n[5] = { "NONE", "PRESS", "SHORT", "LONG", "VLONG" };

  return ((uint8_t)ev < 5u) ? n[ev] : "?";
}

err_t key_init(void)
{
  memset(s_key, 0, sizeof(s_key));
  LOG_I(LOG_TAG, "init ok: %u key(s) (SOS=PA11 MODE=PA12), debounce=%ldms long=%ldms vlong=%ldms",
        (unsigned)CFG_KEY_COUNT, (long)g_cfg.key_debounce_ms,
        (long)g_cfg.key_long_ms, (long)g_cfg.key_vlong_ms);
  return ERR_OK;
}

static uint8_t key_debounce_steps(void)
{
  uint32_t ms = (uint32_t)g_cfg.key_debounce_ms;
  uint32_t st = ms / KEY_TASK_PERIOD_MS;

  if (st < 1u) {
    st = 1u;
  }
  if (st > 50u) {
    st = 50u;
  }
  return (uint8_t)st;
}

err_t key_update(void)
{
  uint8_t i;
  uint8_t steps = key_debounce_steps();

  for (i = 0u; i < KEY_ID_MAX; i++) {
    key_st_t *k = &s_key[i];
    uint8_t   raw;

    if (i >= (uint8_t)CFG_KEY_COUNT) {
      break;                              /* 未配置的键不处理 */
    }
    raw = bsp_key_raw(i);

    if (raw != k->last_raw) {             /* 电平变化：重新计数 */
      k->last_raw = raw;
      k->cnt      = 0u;
      continue;
    }
    if (k->cnt < steps) {
      k->cnt++;
    }
    if ((k->cnt >= steps) && (k->stable != raw)) {
      uint32_t now = bsp_time_ms();

      k->stable = raw;
      if (raw != 0u) {                    /* 按下 */
        k->press_ms = now;
        k->ev       = KEY_EV_PRESS;
        k->ev_ready = 1u;
      } else {                            /* 松手：按保持时长分类 */
        uint32_t held = (uint32_t)(now - k->press_ms);

        if (held >= (uint32_t)g_cfg.key_vlong_ms) {
          k->ev = KEY_EV_VLONG;
          k->n_vlong++;
        } else if (held >= (uint32_t)g_cfg.key_long_ms) {
          k->ev = KEY_EV_LONG;
          k->n_long++;
        } else {
          k->ev = KEY_EV_SHORT;
          k->n_short++;
        }
        k->ev_ready = 1u;
        LOG_D(LOG_TAG, "%s released after %lums -> %s",
              key_id_name((key_id_t)i), (unsigned long)held, key_ev_name(k->ev));
      }
    }
  }
  return ERR_OK;
}

uint8_t key_is_down(key_id_t id)
{
  return ((id < KEY_ID_MAX) && (s_key[id].stable != 0u)) ? 1u : 0u;
}

key_ev_t key_take_event(key_id_t id)
{
  key_ev_t ev;

  if (id >= KEY_ID_MAX) {
    return KEY_EV_NONE;
  }
  if (s_key[id].ev_ready == 0u) {
    return KEY_EV_NONE;
  }
  ev = s_key[id].ev;
  s_key[id].ev_ready = 0u;
  s_key[id].ev       = KEY_EV_NONE;
  return ev;
}

err_t key_selftest(char *out, uint16_t n)
{
  if ((out == NULL) || (n == 0u)) {
    return ERR_PARAM;
  }
  if ((uint8_t)CFG_KEY_COUNT == 0u) {
    (void)snprintf(out, n, "SKIP: no key configured");
    return ERR_UNSUPPORTED;
  }
  (void)snprintf(out, n, "OK: SOS=%s(%lu) MODE=%s(%lu) [press to test]",
                 key_is_down(KEY_ID_SOS) ? "down" : "up", (unsigned long)s_key[KEY_ID_SOS].n_short,
                 key_is_down(KEY_ID_MODE) ? "down" : "up", (unsigned long)s_key[KEY_ID_MODE].n_short);
  return ERR_OK;
}

/* ==================== shell 命令 ==================== */
/* key          打印当前电平与事件计数
 * key mon [n]  连续打印 n 次（默认 10，最多 40），间隔 200ms —— 按着键就能看到 down
 */
static int cmd_key(int argc, char **argv)
{
  uint8_t i;

  if ((argc >= 2) && (strcmp(argv[1], "mon") == 0)) {
    int32_t cnt = 10;
    int32_t k;

    if ((argc >= 3) && (svc_shell_parse_i32(argv[2], &cnt) == 0u)) {
      return ERR_SHELL_ARG;
    }
    if (cnt < 1) { cnt = 1; }
    if (cnt > 40) { cnt = 40; }

    for (k = 0; k < cnt; k++) {
      LOG_I(LOG_TAG, "[%ld] SOS=%s MODE=%s", (long)k,
            key_is_down(KEY_ID_SOS) ? "DOWN" : "up",
            key_is_down(KEY_ID_MODE) ? "DOWN" : "up");
      Delay_ms(200u);
    }
    return ERR_OK;
  }

  for (i = 0u; i < (uint8_t)CFG_KEY_COUNT; i++) {
    LOG_I(LOG_TAG, "%s : %s  short=%lu long=%lu vlong=%lu", key_id_name((key_id_t)i),
          key_is_down((key_id_t)i) ? "DOWN" : "up",
          (unsigned long)s_key[i].n_short, (unsigned long)s_key[i].n_long,
          (unsigned long)s_key[i].n_vlong);
  }
  return ERR_OK;
}

static const shell_cmd_t s_key_cmds[] = {
  { "key", "key | key mon [n]         - key states and event counters", cmd_key }
};

err_t key_shell_register(void)
{
  return svc_shell_register_table(s_key_cmds,
                                  (uint16_t)(sizeof(s_key_cmds) / sizeof(s_key_cmds[0])));
}
