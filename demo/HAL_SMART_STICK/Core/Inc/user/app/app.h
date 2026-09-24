/**
 * @file    app.h
 * @brief   应用层对外入口：main.c 只认这两个函数
 * @note    main.c 的 USER CODE 区只做两件事：app_init() 与 app_poll()
 * @version 0.1  (2026-09-24)
 */
#ifndef USER_APP_H
#define USER_APP_H

#include "err.h"

/* 上电初始化：日志 → 时间基准 → 参数 → BSP → 服务 → 任务注册 */
void app_init(void);

/* 主循环：合作式调度器单轮 */
void app_poll(void);

/* 心跳灯任务（500ms 翻转一次，证明固件在运行） */
void app_hb_task(void);

#endif /* USER_APP_H */
