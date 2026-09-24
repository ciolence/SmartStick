/**
 * @file    drv_alarm.h
 * @brief   报警执行器（有源蜂鸣器 + 振动马达）：非阻塞 pattern 状态机
 * @note    设计要点：
 *            1) alarm_update() 以 10ms 步进推进，绝不使用 delay
 *            2) 事件带优先级：高优先级抢占低优先级；同事件按去重窗口抑制
 *            3) FALL_SOS 为循环 pattern，需显式 alarm_stop() 才停
 *            4) bsp_buzz/bsp_vib 内部已处理"低电平触发"极性与全局静音开关
 * @version 0.1  (2026-09-24)
 */
#ifndef USER_DRV_ALARM_H
#define USER_DRV_ALARM_H

#include <stdint.h>
#include "err.h"

typedef enum {
  ALARM_EV_BOOT_OK = 0,        /* 上电就绪：短鸣 2 声 */
  ALARM_EV_OBSTACLE_WARN,      /* 避障减速提示 */
  ALARM_EV_LOW_BATT,           /* 低电量 */
  ALARM_EV_OBSTACLE_STOP,      /* 避障停车 */
  ALARM_EV_SYS_DEGRADED,       /* 降级运行提示 */
  ALARM_EV_BATT_CRIT,          /* 严重低电 */
  ALARM_EV_FALL_SOS,           /* 跌倒 SOS（最高优先级，循环） */
  ALARM_EV_COUNT
} alarm_ev_t;

err_t       alarm_init(void);
err_t       alarm_update(void);              /* 10ms 周期推进 */
err_t       alarm_play(alarm_ev_t ev);       /* 触发事件（含优先级/去重） */
void        alarm_stop(void);                /* 立即停止一切输出 */
alarm_ev_t  alarm_active(void);              /* 当前正在播放的事件（NONE 表示空闲） */
void        alarm_set_enable(uint8_t on);    /* 运行期静音开关（调试用） */
uint8_t     alarm_get_enable(void);
const char *alarm_ev_name(alarm_ev_t ev);
err_t       alarm_selftest(char *out, uint16_t n);
err_t       alarm_shell_register(void);

#endif /* USER_DRV_ALARM_H */
