/**
 * @file    drv_oled.c
 * @brief   OLED 适配层实现（帧缓冲 + 按页批量写）
 * @version 0.1  (2026-09-24)
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "drv_oled.h"
#include "svc_log.h"
#include "svc_shell.h"
#include "bsp_i2c.h"
#include "bsp_time.h"
#include "cfg.h"
#include "board.h"
#include "OLED.h"                 /* 用户提供的驱动：OLED_Init() */

/* 字模表在 OLED_Font.h 里是"定义"（不是 extern），只能有一处包含它（OLED.c）→ 这里外部声明 */
extern const uint8_t OLED_F8x16[][16];

#define LOG_TAG "OLED"

#define OLED_W          128u
#define OLED_PAGES      8u
#define OLED_CTRL_CMD   0x00u     /* SSD1306 控制字节：后续为命令 */
#define OLED_CTRL_DATA  0x40u     /* SSD1306 控制字节：后续为显存数据 */

/* 若实物是 1.3" SH1106，把列偏移改成 2（SSD1306 用 0） */
#define OLED_COL_OFFSET 0u

static uint8_t s_fb[OLED_PAGES][OLED_W];
static uint8_t s_dirty[OLED_PAGES];
static uint8_t s_present;
static uint32_t s_flush_err;

/* ---------------- 内部：写一页命令（设置页 + 列地址） ---------------- */
static err_t oled_set_page(uint8_t page)
{
  uint8_t cmd[3];

  cmd[0] = (uint8_t)(0xB0u | page);
  cmd[1] = (uint8_t)(0x00u | (OLED_COL_OFFSET & 0x0Fu));
  cmd[2] = (uint8_t)(0x10u | ((OLED_COL_OFFSET >> 4) & 0x0Fu));
  return bsp_i2c_mem_write(BSP_I2C_ADDR_OLED, OLED_CTRL_CMD, cmd, 3u);
}

/* ---------------- 初始化 ---------------- */
err_t oled_init(void)
{
  s_present = 0u;
  s_flush_err = 0u;
  memset(s_fb, 0, sizeof(s_fb));
  memset(s_dirty, 0, sizeof(s_dirty));

#if (CFG_OLED_ENABLE == 0)
  LOG_I(LOG_TAG, "disabled by CFG_OLED_ENABLE=0");
  return ERR_UNSUPPORTED;
#else
  /* 先探测：不在线就彻底放弃，避免后续每次写入 256ms 超时 */
  if (bsp_i2c_probe(BSP_I2C_ADDR_OLED) == 0u) {
    LOG_W(LOG_TAG, "not present at 0x3C -> skip (no OLED traffic)");
    err_record(MOD_OLED, ERR_OLED_INIT);
    return ERR_OLED_INIT;
  }

  OLED_Init();                    /* 用户驱动的 SSD1306 初始化序列（内部含一次清屏） */
  s_present = 1u;
  LOG_I(LOG_TAG, "init ok: 128x64 SSD1306 @0x3C, framebuffer 1KB, batch %u page(s)/flush",
        (unsigned)CFG_OLED_PAGES_PER_FLUSH);
  return ERR_OK;
#endif
}

uint8_t oled_present(void)
{
  return s_present;
}

/* ---------------- 绘制（只改帧缓冲 + 标脏页） ---------------- */
void oled_clear(void)
{
  memset(s_fb, 0, sizeof(s_fb));
  memset(s_dirty, 0xFF, sizeof(s_dirty));
}

static void oled_char(uint8_t line, uint8_t col, char c)
{
  const uint8_t *g;
  uint8_t        page;
  uint8_t        x;
  uint8_t        i;

  if ((line < 1u) || (line > OLED_LINE_MAX) || (col < 1u) || (col > OLED_COL_MAX)) {
    return;
  }
  if ((c < ' ') || (c > '~')) {
    c = ' ';
  }
  page = (uint8_t)((line - 1u) * 2u);
  x    = (uint8_t)((col - 1u) * 8u);
  g    = OLED_F8x16[(uint8_t)(c - ' ')];

  for (i = 0u; i < 8u; i++) {
    s_fb[page][x + i]         = g[i];
    s_fb[page + 1u][x + i]    = g[i + 8u];
  }
  s_dirty[page]      = 1u;
  s_dirty[page + 1u] = 1u;
}

void oled_text(uint8_t line, uint8_t col, const char *s)
{
  if (s == NULL) {
    return;
  }
  while ((*s != '\0') && (col <= OLED_COL_MAX)) {
    oled_char(line, col, *s);
    col++;
    s++;
  }
}

void oled_printf(uint8_t line, uint8_t col, const char *fmt, ...)
{
  char    buf[OLED_COL_MAX + 2u];      /* 16 字符 + '\0' + 余量 */
  va_list ap;

  if (fmt == NULL) {
    return;
  }
  va_start(ap, fmt);
  (void)vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  buf[OLED_COL_MAX] = '\0';            /* 超长裁剪，防止越界写屏 */

  oled_text(line, col, buf);
}

/* ---------------- 刷新（脏页批量写） ---------------- */
void oled_flush(void)
{
  uint8_t page;
  uint8_t budget = (uint8_t)CFG_OLED_PAGES_PER_FLUSH;

  if (s_present == 0u) {
    return;
  }
  for (page = 0u; (page < OLED_PAGES) && (budget > 0u); page++) {
    if (s_dirty[page] == 0u) {
      continue;
    }
    if (oled_set_page(page) != ERR_OK) {
      s_flush_err++;
      return;                                     /* 总线异常：本轮到此为止，下轮再试 */
    }
    if (bsp_i2c_mem_write(BSP_I2C_ADDR_OLED, OLED_CTRL_DATA, s_fb[page], OLED_W) != ERR_OK) {
      s_flush_err++;
      return;
    }
    s_dirty[page] = 0u;
    budget--;
  }
}

void oled_flush_all(void)
{
  uint8_t i;

  if (s_present == 0u) {
    return;
  }
  memset(s_dirty, 0xFF, sizeof(s_dirty));
  for (i = 0u; i < OLED_PAGES; i++) {
    oled_flush();
  }
}

uint8_t oled_dirty_pages(void)
{
  uint8_t i;
  uint8_t n = 0u;

  for (i = 0u; i < OLED_PAGES; i++) {
    if (s_dirty[i] != 0u) {
      n++;
    }
  }
  return n;
}

err_t oled_selftest(char *out, uint16_t n)
{
  if ((out == NULL) || (n == 0u)) {
    return ERR_PARAM;
  }
  if (s_present == 0u) {
    (void)snprintf(out, n, "FAIL: not detected at 0x3C (check power/pull-ups/wiring)");
    return ERR_OLED_INIT;
  }
  (void)snprintf(out, n, "OK: present, dirty=%u page(s), flush_err=%lu",
                 (unsigned)oled_dirty_pages(), (unsigned long)s_flush_err);
  return ERR_OK;
}

/* ==================== shell 命令 ==================== */
static int cmd_oled(int argc, char **argv)
{
  if (argc < 2) {
    LOG_I(LOG_TAG, "usage: oled test | clear | stat | text <line> <col> <str...>");
    return ERR_OK;
  }

  if (strcmp(argv[1], "test") == 0) {
    if (s_present == 0u) {
      LOG_W(LOG_TAG, "not present");
      return ERR_OLED_INIT;
    }
    oled_clear();
    oled_text(1u, 1u, "SmartStick OLED");
    oled_text(2u, 1u, "1234567890-+.:");
    oled_text(3u, 1u, "TEST OK  128x64");
    oled_text(4u, 1u, "0123456789 ABCDE");
    oled_flush_all();
    LOG_I(LOG_TAG, "test pattern written (%lu err)", (unsigned long)s_flush_err);
    return ERR_OK;
  }

  if (strcmp(argv[1], "clear") == 0) {
    oled_clear();
    oled_flush_all();
    LOG_I(LOG_TAG, "cleared");
    return ERR_OK;
  }

  if (strcmp(argv[1], "stat") == 0) {
    LOG_I(LOG_TAG, "present=%u dirty=%u pages flush_err=%lu",
          (unsigned)oled_present(), (unsigned)oled_dirty_pages(), (unsigned long)s_flush_err);
    return ERR_OK;
  }

  if ((strcmp(argv[1], "text") == 0) && (argc >= 5)) {
    char    buf[40];
    int32_t line = 1;
    int32_t col = 1;
    int     i;
    uint8_t pos = 0u;

    if ((svc_shell_parse_i32(argv[2], &line) == 0u) ||
        (svc_shell_parse_i32(argv[3], &col) == 0u)) {
      return ERR_SHELL_ARG;
    }
    buf[0] = '\0';
    for (i = 4; i < argc; i++) {
      uint8_t k;

      if (i > 4) {
        if (pos < sizeof(buf) - 1u) { buf[pos++] = ' '; }
      }
      for (k = 0u; (argv[i][k] != '\0') && (pos < sizeof(buf) - 1u); k++) {
        buf[pos++] = argv[i][k];
      }
    }
    buf[pos] = '\0';
    oled_text((uint8_t)line, (uint8_t)col, buf);
    oled_flush_all();
    LOG_I(LOG_TAG, "text written at %ld,%ld", (long)line, (long)col);
    return ERR_OK;
  }
  return ERR_SHELL_ARG;
}

static const shell_cmd_t s_oled_cmds[] = {
  { "oled", "oled test|clear|stat|text L C S  - OLED test & status", cmd_oled }
};

err_t oled_shell_register(void)
{
  return svc_shell_register_table(s_oled_cmds,
                                  (uint16_t)(sizeof(s_oled_cmds) / sizeof(s_oled_cmds[0])));
}
