/**
 * @file    app_avoid.h
 * @brief   避障分级策略（正前方单路超声波）
 * @note    分级（阈值全部来自 g_cfg，可在 shell 在线改）：
 *            L0 通畅  d > slow_mm                 速度 ×1.00
 *            L1 减速  stop_mm < d ≤ slow_mm       速度线性降到 avoid_min_scale
 *            L2 停车  d ≤ stop_mm（带 hyst 回滞）  速度 ×0
 *            L3 失效  连续 avoid_invalid_ms 无有效数据 → 速度 ×0（fail-safe：测不到＝当有障碍）
 * @version 0.1  (2026-09-24)
 */
#ifndef USER_APP_AVOID_H
#define USER_APP_AVOID_H

#include <stdint.h>

typedef enum {
  AVOID_L0 = 0,        /* 通畅 */
  AVOID_L1,            /* 减速 */
  AVOID_L2,            /* 停车 */
  AVOID_L3             /* 传感器失效 */
} avoid_level_t;

typedef struct {
  avoid_level_t level;
  uint16_t      scale;      /* 速度系数：千分比（1000 = ×1.0） */
  uint16_t      mm;         /* 参与判定的距离（无效时 = 0xFFFF） */
  uint8_t       valid;
} avoid_out_t;

void               app_avoid_task(void);        /* 20ms 周期任务 */
const avoid_out_t *app_avoid_out(void);
const char        *app_avoid_level_str(avoid_level_t lv);

#endif /* USER_APP_AVOID_H */
