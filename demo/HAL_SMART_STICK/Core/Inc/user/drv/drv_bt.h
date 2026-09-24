/**
 * @file    drv_bt.h
 * @brief   蓝牙串口骨架（USART3 / PB10-PB11，9600）
 * @note    v1 只做"能收能发 + 回显测试"，**不定义任何手机侧业务协议**（v2 再定）。
 *          将来接手机上报时，把协议解析挂在 bt_on_line() 这一个钩子上即可。
 * @version 0.1  (2026-09-24)
 */
#ifndef USER_DRV_BT_H
#define USER_DRV_BT_H

#include <stdint.h>
#include "err.h"

err_t       bt_init(void);
uint8_t     bt_ready(void);
uint16_t    bt_avail(void);
uint16_t    bt_read(uint8_t *dst, uint16_t max);
err_t       bt_send(const char *s);
err_t       bt_send_bytes(const uint8_t *data, uint16_t len);
void        bt_task(void);                    /* 100ms 周期任务：收行 + 处理 */
const char *bt_last_line(void);               /* 最近收到的一行（调试/上报钩子用） */
uint32_t    bt_rx_count(void);
uint32_t    bt_line_count(void);
err_t       bt_selftest(char *out, uint16_t n);
err_t       bt_shell_register(void);

#endif /* USER_DRV_BT_H */
