/**
 * @file    bsp_uart.h
 * @brief   串口收发：环形缓冲 + 中断接收，支撑 shell（USART1）与蓝牙（USART3）
 * @note    ISR 只做"搬字节进环形缓冲"，解析全部在任务里做
 * @version 0.1  (2026-09-24)
 */
#ifndef USER_BSP_UART_H
#define USER_BSP_UART_H

#include <stdint.h>
#include "err.h"

/* 端口编号（与 board.h 的句柄对应） */
#define UART_PORT_SHELL   0u      /* USART1  PA9/PA10  115200 */
#define UART_PORT_BT      1u      /* USART3  PB10/PB11 9600   */
#define UART_PORT_COUNT   2u

#define UART_RX_RING_SIZE 128u    /* 必须是 2 的幂 */

/**
 * @brief 初始化两个端口并启动中断接收（在 MX_USARTx_UART_Init 之后调用）
 */
err_t    bsp_uart_init(void);

/* 环形缓冲中可读字节数 */
uint16_t bsp_uart_avail(uint8_t port);

/* 取 1 字节；返回 -1 表示缓冲空 */
int      bsp_uart_getc(uint8_t port);

/* 批量取出，返回实际取出数量 */
uint16_t bsp_uart_read(uint8_t port, uint8_t *dst, uint16_t max);

/* 阻塞发送（带超时）；返回 ERR_OK / ERR_TIMEOUT */
err_t    bsp_uart_write(uint8_t port, const uint8_t *src, uint16_t len);
err_t    bsp_uart_puts(uint8_t port, const char *s);

/* 统计（排查丢字节） */
uint32_t bsp_uart_rx_count(uint8_t port);
uint32_t bsp_uart_ovf_count(uint8_t port);

/* 清空接收缓冲 */
void     bsp_uart_flush(uint8_t port);

#endif /* USER_BSP_UART_H */
