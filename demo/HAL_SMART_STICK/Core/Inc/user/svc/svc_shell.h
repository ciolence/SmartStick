/**
 * @file    svc_shell.h
 * @brief   串口命令行：行编辑 + 命令分发（驱动/业务模块可注册自己的命令表）
 * @note    命令函数约定：返回 ERR_OK 表示成功；非 0 时由 shell 打印 "ERR: <名字>"
 * @version 0.1  (2026-09-24)
 */
#ifndef USER_SVC_SHELL_H
#define USER_SVC_SHELL_H

#include <stdint.h>
#include "err.h"

typedef int (*shell_fn_t)(int argc, char **argv);

typedef struct {
  const char *name;      /* 命令名（大小写不敏感） */
  const char *help;      /* 一行帮助 */
  shell_fn_t  fn;
} shell_cmd_t;

void   svc_shell_init(void);

/**
 * @brief 注册一批命令（可多次调用；总容量见 svc_shell.c 的 SHELL_TBL_MAX）
 */
err_t  svc_shell_register_table(const shell_cmd_t *tbl, uint16_t count);

/* 10ms 周期任务：取字节 → 行编辑 → 解析 → 执行 */
void   svc_shell_task(void);

/* 打印 banner（版本/构建时间/关键外设参数） */
void   svc_shell_print_banner(void);

/* 整数解析（供各驱动命令复用）：1 = 成功；支持前导 '-' */
uint8_t svc_shell_parse_i32(const char *s, int32_t *out);

#endif /* USER_SVC_SHELL_H */
