/**
 * @file    app_fsm.h
 * @brief   全局状态机：IDLE / GUIDE / ALARM_SOS / DEGRADED / FAULT
 * @note    迁移规则（见 ARCHITECTURE.md 8.1）：
 *            IDLE ──SOS 短按 / `guide start`──► GUIDE
 *            GUIDE ──SOS 短按 / `guide stop`──► IDLE
 *            任意 ──跌倒事件──► ALARM_SOS（电机刹车 + 循环 SOS）
 *            ALARM_SOS ──SOS 长按 3s / `alarm clear`──► IDLE
 *            GUIDE ──超声波持续失效 > 3s──► DEGRADED（限速 30%）
 *            DEGRADED ──传感器恢复 + `sys clear`──► IDLE
 *            motor_init 失败 ──► FAULT（只允许复位）
 *          MODE 键：短按 = 报警静音开关。
 * @version 0.1  (2026-09-24)
 */
#ifndef USER_APP_FSM_H
#define USER_APP_FSM_H

#include <stdint.h>
#include "err.h"

typedef enum {
  ST_BOOT = 0,
  ST_IDLE,
  ST_GUIDE,
  ST_ALARM_SOS,
  ST_DEGRADED,
  ST_FAULT
} app_state_t;

void         app_fsm_init(void);
void         app_fsm_task(void);              /* 20ms：按键 + 传感器事件 + 迁移 */
app_state_t  app_fsm_state(void);
const char  *app_fsm_state_str(void);
err_t        app_fsm_cmd(const char *cmd);    /* shell：guide start/stop、alarm clear、sys clear、fault */
uint8_t      app_fsm_guide_allowed(void);     /* 1 = 允许牵引输出（GUIDE/DEGRADED） */
err_t        app_fsm_shell_register(void);    /* 注册 sys 命令 */

#endif /* USER_APP_FSM_H */
