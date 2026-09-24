/**
 * @file    drv_bt.c
 * @brief   蓝牙串口骨架实现
 * @version 0.1  (2026-09-24)
 */
#include <stdio.h>
#include <string.h>

#include "drv_bt.h"
#include "svc_log.h"
#include "svc_shell.h"
#include "bsp_uart.h"
#include "cfg.h"

#define LOG_TAG "BT  "

#define BT_LINE_MAX   64u

static uint8_t  s_ready;
static uint8_t  s_echo;
static char     s_line[BT_LINE_MAX];
static uint8_t  s_len;
static uint8_t  s_over;
static uint32_t s_lines;

err_t bt_init(void)
{
  s_ready = 1u;                      /* 端口由 bsp_uart_init 统一启动中断接收 */
  s_echo  = 0u;
  s_len   = 0u;
  s_over  = 0u;
  s_lines = 0u;
  LOG_I(LOG_TAG, "skeleton ready: USART3 9600 (v1 has no phone protocol yet)");
  return ERR_OK;
}

uint8_t bt_ready(void)
{
  return s_ready;
}

uint16_t bt_avail(void)
{
  return bsp_uart_avail(UART_PORT_BT);
}

uint16_t bt_read(uint8_t *dst, uint16_t max)
{
  return bsp_uart_read(UART_PORT_BT, dst, max);
}

err_t bt_send(const char *s)
{
  if ((s_ready == 0u) || (s == NULL)) {
    return ERR_BT_UART;
  }
  return bsp_uart_puts(UART_PORT_BT, s);
}

err_t bt_send_bytes(const uint8_t *data, uint16_t len)
{
  if (s_ready == 0u) {
    return ERR_BT_UART;
  }
  return bsp_uart_write(UART_PORT_BT, data, len);
}

const char *bt_last_line(void)
{
  return s_line;
}

uint32_t bt_rx_count(void)
{
  return bsp_uart_rx_count(UART_PORT_BT);
}

uint32_t bt_line_count(void)
{
  return s_lines;
}

/**
 * @brief 一行接收完成后的钩子（v2 的手机协议从这里接）
 * @note  v1 只做：日志 + 可选回显
 */
static void bt_on_line(const char *line)
{
  s_lines++;
  LOG_D(LOG_TAG, "rx[%lu]: %s", (unsigned long)s_lines, line);
  if (s_echo != 0u) {
    (void)bt_send(line);
    (void)bt_send("\r\n");
  }
}

void bt_task(void)
{
  int c;

  if (s_ready == 0u) {
    return;
  }
  while ((c = bsp_uart_getc(UART_PORT_BT)) >= 0) {
    if ((c == '\r') || (c == '\n')) {
      if (s_over != 0u) {
        s_over = 0u;
        s_len  = 0u;
        LOG_W(LOG_TAG, "line too long, dropped");
        continue;
      }
      if (s_len > 0u) {
        s_line[s_len] = '\0';
        bt_on_line(s_line);
        s_len = 0u;
      }
      continue;
    }
    if ((c < 0x20) || (c > 0x7E)) {
      continue;
    }
    if (s_len >= (BT_LINE_MAX - 1u)) {
      s_over = 1u;
      continue;
    }
    s_line[s_len++] = (char)c;
  }
}

err_t bt_selftest(char *out, uint16_t n)
{
  if ((out == NULL) || (n == 0u)) {
    return ERR_PARAM;
  }
  (void)snprintf(out, n, "OK: port up, rx=%lu bytes, %lu line(s)%s",
                 (unsigned long)bt_rx_count(), (unsigned long)s_lines,
                 (s_echo != 0u) ? ", echo ON" : "");
  return ERR_OK;
}

/* ==================== shell 命令 ==================== */
static int cmd_bt(int argc, char **argv)
{
  if (argc < 2) {
    LOG_I(LOG_TAG, "usage: bt status | echo 0|1 | send <text>");
    return ERR_OK;
  }
  if (strcmp(argv[1], "status") == 0) {
    LOG_I(LOG_TAG, "ready=%u rx=%lu lines=%lu echo=%u last='%s'",
          (unsigned)s_ready, (unsigned long)bt_rx_count(), (unsigned long)s_lines,
          (unsigned)s_echo, s_line);
    return ERR_OK;
  }
  if (strcmp(argv[1], "echo") == 0) {
    int32_t v = 0;

    if ((argc < 3) || (svc_shell_parse_i32(argv[2], &v) == 0u)) {
      return ERR_SHELL_ARG;
    }
    s_echo = (v != 0) ? 1u : 0u;
    LOG_I(LOG_TAG, "echo %s", (s_echo != 0u) ? "on" : "off");
    return ERR_OK;
  }
  if ((strcmp(argv[1], "send") == 0) && (argc >= 3)) {
    err_t e = bt_send(argv[2]);

    LOG_I(LOG_TAG, "send '%s' -> %s", argv[2], err_str(e));
    return ERR_OK;
  }
  return ERR_SHELL_ARG;
}

static const shell_cmd_t s_bt_cmds[] = {
  { "bt", "bt status|echo 0|1|send S - bluetooth skeleton (USART3 9600)", cmd_bt }
};

err_t bt_shell_register(void)
{
  return svc_shell_register_table(s_bt_cmds,
                                  (uint16_t)(sizeof(s_bt_cmds) / sizeof(s_bt_cmds[0])));
}
