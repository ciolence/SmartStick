/**
 * @file    svc_sched.h
 * @brief   合作式任务调度（无 RTOS）：按周期轮询执行，单轮总耗时目标 < 3ms
 * @note    所有任务函数必须非阻塞；用 bsp_time_reached() 语义实现
 * @version 0.1  (2026-09-24)
 */
#ifndef USER_SVC_SCHED_H
#define USER_SVC_SCHED_H

#include <stdint.h>
#include "err.h"

#define SCHED_MAX_TASKS   20u
#define SCHED_NAME_MAX    12u

typedef void (*sched_fn_t)(void);

void       svc_sched_init(void);

/**
 * @brief 注册任务（同名任务重复注册时更新参数，不新增）
 * @param name      任务名（≤11 字符，仅用于打印）
 * @param fn        任务函数
 * @param period_ms 周期
 */
err_t      svc_sched_add(const char *name, sched_fn_t fn, uint32_t period_ms);

/* 主循环调用：执行所有到点任务 */
void       svc_sched_run(void);

/* 打印任务表与统计（shell: tasks） */
void       svc_sched_dump(void);

uint16_t   svc_sched_count(void);

#endif /* USER_SVC_SCHED_H */
