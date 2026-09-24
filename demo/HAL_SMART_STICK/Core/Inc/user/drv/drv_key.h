/**
 * @file    drv_key.h
 * @brief   按键驱动（非阻塞去抖 + 短按/长按/超长按事件）
 * @note    v1 只有 2 个键：SOS(PA11) / MODE(PA12)，见 CFG_KEY_COUNT。
 *          事件语义：
 *            KEY_EV_PRESS  按下瞬间（供"按下即响应"用）
 *            KEY_EV_SHORT  松手且按住时长 < key_long_ms
 *            KEY_EV_LONG   按住 ≥ key_long_ms
 *            KEY_EV_VLONG  按住 ≥ key_vlong_ms（例如长按 3s 清除跌倒报警）
 * @version 0.1  (2026-09-24)
 */
#ifndef USER_DRV_KEY_H
#define USER_DRV_KEY_H

#include <stdint.h>
#include "err.h"

typedef enum {
  KEY_ID_SOS  = 0,
  KEY_ID_MODE = 1,
  KEY_ID_MAX  = 2
} key_id_t;

typedef enum {
  KEY_EV_NONE = 0,
  KEY_EV_PRESS,
  KEY_EV_SHORT,
  KEY_EV_LONG,
  KEY_EV_VLONG
} key_ev_t;

err_t    key_init(void);
err_t    key_update(void);                     /* 20ms 周期任务 */
uint8_t  key_is_down(key_id_t id);
key_ev_t key_take_event(key_id_t id);          /* 取事件（读后清除） */
const char *key_id_name(key_id_t id);
const char *key_ev_name(key_ev_t ev);
err_t    key_selftest(char *out, uint16_t n);
err_t    key_shell_register(void);

#endif /* USER_DRV_KEY_H */
