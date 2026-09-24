/**
 * @file    app_alarm.h
 * @brief   报警策略层：把"避障等级 / 电量等级 / 状态机状态"翻译成报警事件
 * @note    drv_alarm 负责"怎么响"，本模块负责"什么时候响哪些"（含重复节奏与去重）。
 * @version 0.1  (2026-09-24)
 */
#ifndef USER_APP_ALARM_H
#define USER_APP_ALARM_H

#include <stdint.h>

void app_alarm_task(void);        /* 100ms 周期任务：策略判定 */
void app_alarm_fall_start(void);  /* 跌倒：播放循环 SOS */
void app_alarm_clear(void);       /* 人工确认：停止一切报警 */
uint8_t app_alarm_is_sos(void);   /* 1 = 正在循环 SOS */

#endif /* USER_APP_ALARM_H */
