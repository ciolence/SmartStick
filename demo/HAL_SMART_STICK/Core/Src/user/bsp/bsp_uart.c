/**
 * @file    bsp_uart.c
 * @brief   串口收发实现（单生产者—单消费者环形缓冲）
 * @note    接收链：USARTx_IRQHandler（CubeMX/ stm32f1xx_it.c）→ HAL_UART_IRQHandler
 *                  → HAL_UART_RxCpltCallback（本文件）→ ring_push → 任务里取出解析
 *          这样 stm32f1xx_it.c 完全不需要我们改动。
 * @version 0.1  (2026-09-24)
 */
#include <string.h>

#include "bsp_uart.h"
#include "board.h"

typedef struct {
  UART_HandleTypeDef *huart;
  uint8_t             buf[UART_RX_RING_SIZE];
  volatile uint16_t   head;          /* 生产者（ISR）写 */
  volatile uint16_t   tail;          /* 消费者（任务）读 */
  uint8_t             rx_byte;
  uint32_t            rx_cnt;
  uint32_t            ovf_cnt;
} uart_port_t;

static uart_port_t s_port[UART_PORT_COUNT];

#define RING_MASK  (UART_RX_RING_SIZE - 1u)

static uart_port_t *port_of(uint8_t port)
{
  return (port < UART_PORT_COUNT) ? &s_port[port] : NULL;
}

static void ring_push(uart_port_t *p, uint8_t b)
{
  uint16_t next = (uint16_t)((p->head + 1u) & RING_MASK);

  p->rx_cnt++;
  if (next == p->tail) {              /* 缓冲满：丢弃新字节并计数（宁可丢新也不覆盖旧） */
    p->ovf_cnt++;
    return;
  }
  p->buf[p->head] = b;
  p->head = next;
}

static void port_arm(uart_port_t *p)
{
  if (p->huart != NULL) {
    (void)HAL_UART_Receive_IT(p->huart, &p->rx_byte, 1u);
  }
}

err_t bsp_uart_init(void)
{
  uint8_t i;

  for (i = 0u; i < UART_PORT_COUNT; i++) {
    s_port[i].head    = 0u;
    s_port[i].tail    = 0u;
    s_port[i].rx_cnt  = 0u;
    s_port[i].ovf_cnt = 0u;
  }
  s_port[UART_PORT_SHELL].huart = BOARD_UART_SHELL;
  s_port[UART_PORT_BT].huart    = BOARD_UART_BT;

  for (i = 0u; i < UART_PORT_COUNT; i++) {
    if ((s_port[i].huart == NULL) || (s_port[i].huart->Instance == NULL)) {
      return ERR_HW_NOT_READY;
    }
    port_arm(&s_port[i]);
  }
  return ERR_OK;
}

uint16_t bsp_uart_avail(uint8_t port)
{
  uart_port_t *p = port_of(port);

  if (p == NULL) {
    return 0u;
  }
  return (uint16_t)((p->head - p->tail) & RING_MASK);
}

int bsp_uart_getc(uint8_t port)
{
  uart_port_t *p = port_of(port);

  if ((p == NULL) || (p->head == p->tail)) {
    return -1;
  }
  {
    uint8_t b = p->buf[p->tail];
    p->tail = (uint16_t)((p->tail + 1u) & RING_MASK);
    return (int)b;
  }
}

uint16_t bsp_uart_read(uint8_t port, uint8_t *dst, uint16_t max)
{
  uint16_t n = 0u;

  if (dst == NULL) {
    return 0u;
  }
  while (n < max) {
    int c = bsp_uart_getc(port);
    if (c < 0) {
      break;
    }
    dst[n++] = (uint8_t)c;
  }
  return n;
}

err_t bsp_uart_write(uint8_t port, const uint8_t *src, uint16_t len)
{
  uart_port_t *p = port_of(port);

  if ((p == NULL) || (p->huart == NULL) || (src == NULL)) {
    return ERR_PARAM;
  }
  if (len == 0u) {
    return ERR_OK;
  }
  if (HAL_UART_Transmit(p->huart, (uint8_t *)(void *)src, len, BOARD_UART_TX_TIMEOUT) != HAL_OK) {
    return ERR_TIMEOUT;
  }
  return ERR_OK;
}

err_t bsp_uart_puts(uint8_t port, const char *s)
{
  if (s == NULL) {
    return ERR_PARAM;
  }
  return bsp_uart_write(port, (const uint8_t *)(const void *)s, (uint16_t)strlen(s));
}

uint32_t bsp_uart_rx_count(uint8_t port)
{
  uart_port_t *p = port_of(port);

  return (p != NULL) ? p->rx_cnt : 0u;
}

uint32_t bsp_uart_ovf_count(uint8_t port)
{
  uart_port_t *p = port_of(port);

  return (p != NULL) ? p->ovf_cnt : 0u;
}

void bsp_uart_flush(uint8_t port)
{
  uart_port_t *p = port_of(port);

  if (p != NULL) {
    p->tail = p->head;
  }
}

/* ==================== HAL 回调覆盖（弱函数） ==================== */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  uint8_t i;

  for (i = 0u; i < UART_PORT_COUNT; i++) {
    if (s_port[i].huart == huart) {
      ring_push(&s_port[i], s_port[i].rx_byte);
      port_arm(&s_port[i]);              /* 立即重新武装，否则只收到 1 字节 */
      return;
    }
  }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
  uint8_t i;

  /* 过载/帧错误/噪声后必须重新武装，否则该端口从此收不到数据 */
  for (i = 0u; i < UART_PORT_COUNT; i++) {
    if (s_port[i].huart == huart) {
      port_arm(&s_port[i]);
      return;
    }
  }
}
