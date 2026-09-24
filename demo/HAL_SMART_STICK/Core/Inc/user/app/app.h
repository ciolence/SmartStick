/**
 * @file    app.h
 * @brief   应用层对外入口：main.c 只认这几个函数
 * @note    main.c 的 USER CODE 区只做两件事：app_init() 与 app_poll()
 * @version 0.1  (2026-09-24)
 */
#ifndef USER_APP_H
#define USER_APP_H

#include "err.h"

/* 上电初始化：日志 → 时间基准 → 参数 → BSP → 服务 → 驱动 → 任务注册 */
void app_init(void);

/* 主循环：合作式调度器单轮 */
void app_poll(void);

/* 心跳灯任务（500ms 翻转一次，证明固件在运行） */
void app_hb_task(void);

/* 当前状态名（供 OLED / shell 显示；v1 阶段 4-D 由 app_fsm 接管） */
const char *app_state_str(void);

/* 报警是否被静音（供 UI 显示；未接报警驱动时恒返回 0） */
uint8_t alarm_muted(void);

#endif /* USER_APP_H */
