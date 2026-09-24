/**
 * @file    svc_sched.c
 * @brief   合作式任务调度实现
 * @note    调度语义：now - last >= period 时执行，并把 last 更新为 now
 *          （不是 last += period：宁可轻微抖动，也不允许"补跑"造成雪崩）
 * @version 0.1  (2026-09-24)
 */
#include <string.h>

#include "svc_sched.h"
#include "svc_log.h"
#include "bsp_time.h"

#define LOG_TAG "SCHD"

typedef struct {
  char      name[SCHED_NAME_MAX];
  sched_fn_t fn;
  uint32_t  period_ms;
  uint32_t  last_ms;
  uint32_t  run_cnt;
  uint16_t  max_us;          /* 单次最长耗时（µs；>65535 会回绕，仅作量级参考） */
} task_t;

static task_t   s_tasks[SCHED_MAX_TASKS];
static uint16_t s_cnt;

void svc_sched_init(void)
{
  memset(s_tasks, 0, sizeof(s_tasks));
  s_cnt = 0u;
}

err_t svc_sched_add(const char *name, sched_fn_t fn, uint32_t period_ms)
{
  uint16_t i;
  task_t  *t = NULL;

  if ((name == NULL) || (fn == NULL) || (period_ms == 0u)) {
    return ERR_PARAM;
  }

  for (i = 0u; i < s_cnt; i++) {                 /* 同名 → 更新 */
    if (strcmp(s_tasks[i].name, name) == 0) {
      t = &s_tasks[i];
      break;
    }
  }
  if (t == NULL) {
    if (s_cnt >= SCHED_MAX_TASKS) {
      return ERR_OVERFLOW;
    }
    t = &s_tasks[s_cnt++];
    (void)strncpy(t->name, name, SCHED_NAME_MAX - 1u);
    t->name[SCHED_NAME_MAX - 1u] = '\0';
  }
  t->fn        = fn;
  t->period_ms = period_ms;
  t->last_ms   = bsp_time_ms();                  /* 注册后满一个周期才首次执行 */
  t->run_cnt   = 0u;
  t->max_us    = 0u;
  return ERR_OK;
}

void svc_sched_run(void)
{
  uint16_t i;

  for (i = 0u; i < s_cnt; i++) {
    task_t *t = &s_tasks[i];

    if (bsp_time_reached(&t->last_ms, t->period_ms) != 0u) {
      uint16_t t0 = bsp_time_us_raw();
      uint16_t dt;

      t->fn();
      dt = bsp_time_us_diff(bsp_time_us_raw(), t0);
      if (dt > t->max_us) {
        t->max_us = dt;
      }
      t->run_cnt++;
    }
  }
}

void svc_sched_dump(void)
{
  uint16_t i;

  LOG_I(LOG_TAG, "---- tasks (%u) ----", (unsigned)s_cnt);
  for (i = 0u; i < s_cnt; i++) {
    LOG_I(LOG_TAG, "%-11s period=%-6lums runs=%-8lu max=%uus",
          s_tasks[i].name,
          (unsigned long)s_tasks[i].period_ms,
          (unsigned long)s_tasks[i].run_cnt,
          (unsigned)s_tasks[i].max_us);
  }
}

uint16_t svc_sched_count(void)
{
  return s_cnt;
}
