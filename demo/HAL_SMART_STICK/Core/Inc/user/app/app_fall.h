/**
 * @file    app_fall.h
 * @brief   跌倒检测（MPU6050 三要素状态机）
 * @note    判定链（阈值全部来自 g_cfg，可在线调）：
 *            F_IDLE    ──|a| < fall_freefall_mg 且持续 fall_freefall_ms──► F_FREEFALL
 *            F_FREEFALL──500ms 内 |a| > fall_impact_mg──► F_IMPACT
 *            F_IMPACT  ──fall_confirm_ms 内 tilt_sin_pct ≥ sin(fall_tilt_deg10)──► 触发事件
 *            任一环节不满足 → 回 F_IDLE；触发后 fall_cooldown_ms 内不重复触发
 *          局限（写进 VERIFY）：剧烈晃动可能误报、缓慢躺倒可能漏报；v1 目标是"能演示、可调参"。
 * @version 0.1  (2026-09-24)
 */
#ifndef USER_APP_FALL_H
#define USER_APP_FALL_H

#include <stdint.h>

void    app_fall_task(void);        /* 20ms 周期任务 */
uint8_t app_fall_take_event(void);  /* 1 = 有新跌倒事件（读后自动清除） */
void    app_fall_clear(void);       /* 丢弃当前判定状态（人工确认后） */
const char *app_fall_state_str(void);

#endif /* USER_APP_FALL_H */
