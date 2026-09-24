/**
 * @file    app_ui.c
 * @brief   OLED 仪表盘实现（4 行 × 16 列，强制补齐空格以便覆盖旧内容）
 * @version 0.1  (2026-09-24)
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "app_ui.h"
#include "app.h"
#include "drv_oled.h"
#include "drv_us.h"
#include "drv_imu.h"
#include "drv_batt.h"
#include "cfg.h"
#include "board.h"        /* US_DIST_INVALID 等量程常量 */

#define UI_COLS   16u
#define UI_LINES  4u

/* 把一行格式化到 16 字符（不足补空格），再写进帧缓冲 */
static void ui_line(uint8_t line, const char *fmt, ...)
{
  char    buf[UI_COLS + 1u];
  va_list ap;
  uint8_t n;

  va_start(ap, fmt);
  (void)vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);

  n = (uint8_t)strlen(buf);
  while (n < UI_COLS) {
    buf[n] = ' ';
    n++;
  }
  buf[UI_COLS] = '\0';
  oled_text(line, 1u, buf);
}

void app_ui_boot_screen(void)
{
  if (oled_present() == 0u) {
    return;
  }
  oled_clear();
  ui_line(1u, "SmartStick %s", CFG_FW_VERSION);
  ui_line(2u, "SYSCLK 72MHz");
  ui_line(3u, "I2C1 400k PWM18k");
  ui_line(4u, "booting...");
  oled_flush_all();
}

void app_ui_task(void)
{
  const imu_data_t  *imu;
  const batt_data_t *bat;
  uint16_t           d;

  if (oled_present() == 0u) {
    return;
  }

  imu = imu_data();
  bat = batt_data();
  d   = us_get_mm();

  /* L1: 状态 + 距离 */
  if (d == US_DIST_INVALID) {
    ui_line(1u, "S:%-4s D: --  ", app_state_str());
  } else {
    ui_line(1u, "S:%-4s D:%4umm", app_state_str(), (unsigned)d);
  }

  /* L2: 倾角 + 加速度模长 */
  if (imu->valid != 0u) {
    ui_line(2u, "T%3u |a%4umg", (unsigned)imu->tilt_deg, (unsigned)imu->acc_norm_mg);
  } else {
    ui_line(2u, "T -- |a --   ");
  }

  /* L3: 电池 */
  if (bat->valid != 0u) {
    static const char *const lvl[3] = { "OK ", "LOW", "CRT" };

    ui_line(3u, "V%2u.%uV %s %s", (unsigned)(bat->mv / 1000u),
            (unsigned)((bat->mv % 1000u) / 100u), lvl[bat->level],
            (alarm_muted() != 0u) ? "MUTE" : "    ");
  } else {
    ui_line(3u, "V --.-V    ");
  }

  /* L4: 设备可用性 + 错误计数 */
  ui_line(4u, "U%s I%s E%lu", us_is_valid() != 0u ? "+" : "-",
          imu->valid != 0u ? "+" : "-", (unsigned long)err_total());

  /* 只把有变化的页推给屏（drv_oled 内部限流：每次 ≤ CFG_OLED_PAGES_PER_FLUSH 页） */
  if (oled_dirty_pages() > 0u) {
    oled_flush();
  }
}
