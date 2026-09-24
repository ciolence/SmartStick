/**
 * @file    svc_log.h
 * @brief   分级日志：E/W/I/D/T，单行输出到调试串口（USART1）
 * @note    输出格式：[t=12345][E][US  ] 描述 (err=-0x1101)
 *          * 禁止在中断里调用
 *          * 禁止打印浮点（CFG_USE_FLOAT_PRINT=0）
 *          * 每个 .c 文件约定定义 LOG_TAG 后传入，例如 #define LOG_TAG "US  "
 * @version 0.1  (2026-09-24)
 */
#ifndef USER_SVC_LOG_H
#define USER_SVC_LOG_H

#include <stdint.h>
#include "cfg.h"

#define LOG_LVL_ERROR   0u
#define LOG_LVL_WARN    1u
#define LOG_LVL_INFO    2u
#define LOG_LVL_DEBUG   3u
#define LOG_LVL_TRACE   4u

void        svc_log_init(void);
void        svc_log_set_level(uint8_t lvl);
uint8_t     svc_log_get_level(void);

/* 不带任何前缀的原始输出（shell 回显 / banner 用） */
void        svc_log_puts_raw(const char *s);

/* 带级别与模块标签的日志（lvl > 当前级别时直接丢弃） */
void        svc_log_printf(uint8_t lvl, const char *tag, const char *fmt, ...);

/* 十六进制转储（排查 I²C / 串口协议帧） */
void        svc_log_hexdump(uint8_t lvl, const char *tag, const uint8_t *buf, uint16_t len);

/* ---------------- 便捷宏 ---------------- */
#define LOG_E(tag, ...)   svc_log_printf(LOG_LVL_ERROR, (tag), __VA_ARGS__)
#define LOG_W(tag, ...)   svc_log_printf(LOG_LVL_WARN,  (tag), __VA_ARGS__)
#define LOG_I(tag, ...)   svc_log_printf(LOG_LVL_INFO,  (tag), __VA_ARGS__)

#if (CFG_LOG_COMPILE_LEVEL >= 3)
  #define LOG_D(tag, ...) svc_log_printf(LOG_LVL_DEBUG, (tag), __VA_ARGS__)
#else
  #define LOG_D(tag, ...) ((void)0)
#endif

#if (CFG_LOG_COMPILE_LEVEL >= 4)
  #define LOG_T(tag, ...) svc_log_printf(LOG_LVL_TRACE, (tag), __VA_ARGS__)
#else
  #define LOG_T(tag, ...) ((void)0)
#endif

#endif /* USER_SVC_LOG_H */
