/**
 * @file    svc_log.c
 * @brief   分级日志实现（阻塞式短写，不做缓冲池——保持简单可预测）
 * @version 0.1  (2026-09-24)
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "svc_log.h"
#include "board.h"
#include "bsp_time.h"

#define LOG_BODY_MAX   96u      /* 单行正文上限（超出截断） */
#define LOG_PREFIX_MAX 40u

static uint8_t s_level = CFG_LOG_LEVEL;

static void log_write(const char *s, uint16_t n)
{
  if ((s == NULL) || (n == 0u)) {
    return;
  }
  (void)HAL_UART_Transmit(BOARD_UART_SHELL, (uint8_t *)(void *)s, n, BOARD_UART_TX_TIMEOUT);

#if (CFG_BT_MIRROR_SHELL != 0)
  (void)HAL_UART_Transmit(BOARD_UART_BT, (uint8_t *)(void *)s, n, BOARD_UART_TX_TIMEOUT);
#endif
}

static char log_level_char(uint8_t lvl)
{
  static const char c[5] = { 'E', 'W', 'I', 'D', 'T' };

  return (lvl < 5u) ? c[lvl] : '?';
}

void svc_log_init(void)
{
  s_level = CFG_LOG_LEVEL;
}

void svc_log_set_level(uint8_t lvl)
{
  s_level = (lvl <= LOG_LVL_TRACE) ? lvl : LOG_LVL_TRACE;
}

uint8_t svc_log_get_level(void)
{
  return s_level;
}

void svc_log_puts_raw(const char *s)
{
  if (s != NULL) {
    log_write(s, (uint16_t)strlen(s));
  }
}

void svc_log_printf(uint8_t lvl, const char *tag, const char *fmt, ...)
{
  char    body[LOG_BODY_MAX];
  char    prefix[LOG_PREFIX_MAX];
  va_list ap;
  int     n;

  if ((lvl > s_level) || (fmt == NULL)) {
    return;
  }

  va_start(ap, fmt);
  n = vsnprintf(body, sizeof(body), fmt, ap);
  va_end(ap);
  if (n < 0) {
    return;                                   /* 格式化失败：静默丢弃，绝不递归报错 */
  }
  body[sizeof(body) - 1u] = '\0';

  (void)snprintf(prefix, sizeof(prefix), "[t=%lu][%c][%s] ",
                 (unsigned long)bsp_time_ms(),
                 log_level_char(lvl),
                 (tag != NULL) ? tag : "    ");

  log_write(prefix, (uint16_t)strlen(prefix));
  log_write(body,   (uint16_t)strlen(body));
  log_write("\r\n", 2u);
}

void svc_log_hexdump(uint8_t lvl, const char *tag, const uint8_t *buf, uint16_t len)
{
  uint16_t i;

  if ((lvl > s_level) || (buf == NULL)) {
    return;
  }
  for (i = 0u; i < len; i += 16u) {
    char     line[64];
    uint16_t j;
    int      pos = 0;

    pos += snprintf(&line[pos], sizeof(line) - (size_t)pos, "%04X:", (unsigned)i);
    for (j = i; (j < (i + 16u)) && (j < len); j++) {
      pos += snprintf(&line[pos], sizeof(line) - (size_t)pos, " %02X", buf[j]);
    }
    svc_log_printf(lvl, tag, "%s", line);
  }
}
