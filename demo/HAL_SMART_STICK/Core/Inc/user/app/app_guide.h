/**
 * @file    app_guide.h
 * @brief   手扶牵引控制（v1：直行 + 遇障分级调速；★绕行已实现但默认关闭）
 * @note    输出唯一入口是 motor_set_pair()，其余模块不得直接写电机。
 *          直行：duty = guide_base_duty × avoid_scale / 1000 + trim，受 motor_max_duty 约束。
 *          绕行（detour）：遇障停车持续 avoid_stuck_ms 后，做「原地转向 → 直行试探 → 重新测距」
 *            循环，最多 detour_retry 次；仍然不通 → 停车 + 报警（交回避障层）。
 *            启用方式（无需重新编译）：cfg set detour_enable 1
 *          预留（v2，需第二路超声波/磁力计）：GUIDE_SPIN_LEFT/RIGHT 与 GUIDE_ARC_LEFT/RIGHT 已定义枚举。
 * @version 0.1  (2026-09-24)
 */
#ifndef USER_APP_GUIDE_H
#define USER_APP_GUIDE_H

#include <stdint.h>
#include "err.h"

typedef enum {
  GUIDE_STOP = 0,
  GUIDE_FORWARD,
  GUIDE_SPIN_LEFT,      /* ★已实现（绕行用） */
  GUIDE_SPIN_RIGHT,     /* ★已实现（绕行用） */
  GUIDE_ARC_LEFT,       /* ★预留：差速圆弧（需侧向感知） */
  GUIDE_ARC_RIGHT       /* ★预留 */
} guide_cmd_t;

void        app_guide_task(void);              /* 50ms 周期任务 */
err_t       app_guide_set(guide_cmd_t cmd);    /* 上层强制指令（shell / FSM） */
guide_cmd_t app_guide_get(void);
uint8_t     app_guide_is_moving(void);
uint8_t     app_guide_is_detouring(void);
const char *app_guide_state_str(void);
const char *app_guide_detour_state_str(void);
err_t       app_guide_shell_register(void);    /* guide start|stop|fwd|spin L|R|detour on|off */

#endif /* USER_APP_GUIDE_H */
