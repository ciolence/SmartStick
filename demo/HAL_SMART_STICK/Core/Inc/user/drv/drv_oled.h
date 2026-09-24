/**
 * @file    drv_oled.h
 * @brief   OLED 显示适配层（128×64 SSD1306，复用用户既有 OLED.c 驱动）
 * @note    为什么需要适配层：
 *            用户的 OLED.c 是"无帧缓冲 + 每字节一次 I²C 事务"的写法，
 *            全屏刷新要 1024 次事务（约 100~200ms），无法用于周期刷新。
 *          本适配层做法：
 *            1) init 时先探测 0x3C，不在线则彻底不碰（避免每次 256ms 超时）
 *            2) 自建 1KB 帧缓冲（8 页 × 128 字节，与 SSD1306 页式显存同构）
 *            3) 按"脏页"整页批量写：129 字节/事务 ≈ 3ms/页，每次最多刷
 *               CFG_OLED_PAGES_PER_FLUSH 页（默认 2 页）
 *          字模复用用户既有的 OLED_F8x16（8×16，外部声明，不重复定义）。
 * @version 0.1  (2026-09-24)
 */
#ifndef USER_DRV_OLED_H
#define USER_DRV_OLED_H

#include <stdint.h>
#include "err.h"

#define OLED_LINE_MAX   4u
#define OLED_COL_MAX    16u

err_t   oled_init(void);                       /* 探测 + 调用户 OLED_Init() + 建立帧缓冲 */
uint8_t oled_present(void);                    /* 1 = 屏在线 */
void    oled_clear(void);                      /* 清帧缓冲（不立即刷） */
void    oled_text(uint8_t line, uint8_t col, const char *s);   /* line 1~4, col 1~16 */
void    oled_printf(uint8_t line, uint8_t col, const char *fmt, ...);  /* 整数格式化，自动裁剪到 16 字符 */
void    oled_flush(void);                      /* 按脏页批量刷（每次 ≤ CFG_OLED_PAGES_PER_FLUSH 页） */
void    oled_flush_all(void);                  /* 阻塞刷完整屏（仅 shell 命令/上电用） */
uint8_t oled_dirty_pages(void);
err_t   oled_selftest(char *out, uint16_t n);
err_t   oled_shell_register(void);

#endif /* USER_DRV_OLED_H */
