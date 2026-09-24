/**
 * @file    app_ui.h
 * @brief   OLED 现场仪表盘：4 行 16 列，显示距离/姿态/电量/错误
 * @note    刷新策略：app_ui_task() 以 200ms 周期重建 4 行文本；
 *          drv_oled 只把变化的页批量推给屏（每页 ≈3ms）。
 * @version 0.1  (2026-09-24)
 */
#ifndef USER_APP_UI_H
#define USER_APP_UI_H

#include <stdint.h>

void app_ui_task(void);          /* 200ms 周期任务 */
void app_ui_boot_screen(void);   /* 上电首屏（确认屏通路） */

#endif /* USER_APP_UI_H */
