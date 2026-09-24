/**
 * @file    svc_shell.c
 * @brief   串口命令行实现（行编辑 + 命令表 + 基础命令）
 * @note    基础命令：help / ver / status / log / err / cfg / i2c / tasks / reset
 *          各驱动与业务模块通过 svc_shell_register_table() 挂自己的命令。
 *
 * @warning 本文件所有字符串字面量必须为 ASCII！
 *          ARM Compiler 5 遇到 UTF-8 多字节字符（中文）会报
 *          "#8: missing closing quote" 并连锁报错（实测 2026-09-24）。
 *          中文只能出现在注释里。见 CODE_STANDARD 第 9 节。
 * @version 0.1  (2026-09-24)
 */
#include <string.h>
#include <stdio.h>

#include "svc_shell.h"
#include "svc_log.h"
#include "svc_sched.h"
#include "bsp_uart.h"
#include "bsp_i2c.h"
#include "bsp_time.h"
#include "cfg.h"
#include "board.h"
#include "Delay.h"

#define LOG_TAG "SHEL"

#define SHELL_LINE_MAX   96u
#define SHELL_ARG_MAX    8u
#define SHELL_TBL_MAX    8u

/* 行编辑状态 */
static char     s_line[SHELL_LINE_MAX];
static uint16_t s_len;
static uint8_t  s_over;

/* 已注册的命令表 */
typedef struct {
  const shell_cmd_t *tbl;
  uint16_t           count;
} shell_tbl_t;

static shell_tbl_t s_tbls[SHELL_TBL_MAX];
static uint8_t     s_tbl_cnt;

/* ==================== 命令实现（前向声明） ==================== */
static int cmd_help(int argc, char **argv);
static int cmd_ver(int argc, char **argv);
static int cmd_status(int argc, char **argv);
static int cmd_log(int argc, char **argv);
static int cmd_err(int argc, char **argv);
static int cmd_cfg(int argc, char **argv);
static int cmd_i2c(int argc, char **argv);
static int cmd_tasks(int argc, char **argv);
static int cmd_reset(int argc, char **argv);

/* ==================== 基础命令表 ==================== */
static const shell_cmd_t s_base_cmds[] = {
  { "help",   "help                    - list all commands",                     cmd_help   },
  { "ver",    "ver                     - version and build info",                cmd_ver    },
  { "status", "status                  - system status summary",                 cmd_status },
  { "log",    "log [0-4]               - show/set log level (E/W/I/D/T)",        cmd_log    },
  { "err",    "err [clear]             - error counters / clear",                cmd_err    },
  { "cfg",    "cfg [list|get k|set k v|reset|save]  - parameters",              cmd_cfg    },
  { "i2c",    "i2c                     - scan I2C bus (expect 3C/68/0D)",         cmd_i2c    },
  { "tasks",  "tasks                   - task table and timing",                 cmd_tasks  },
  { "reset",  "reset                   - soft reset (NVIC_SystemReset)",         cmd_reset  }
};

/* ==================== 小工具 ==================== */

static uint8_t chr_eq_ci(char a, char b)
{
  if ((a >= 'A') && (a <= 'Z')) {
    a = (char)(a + ('a' - 'A'));
  }
  if ((b >= 'A') && (b <= 'Z')) {
    b = (char)(b + ('a' - 'A'));
  }
  return (a == b) ? 1u : 0u;
}

static uint8_t str_eq_ci(const char *a, const char *b)
{
  while ((*a != '\0') && (*b != '\0')) {
    if (chr_eq_ci(*a, *b) == 0u) {
      return 0u;
    }
    a++;
    b++;
  }
  return (*a == *b) ? 1u : 0u;
}

static uint8_t str_starts_ci(const char *s, const char *prefix)
{
  while (*prefix != '\0') {
    if (chr_eq_ci(*s, *prefix) == 0u) {
      return 0u;
    }
    s++;
    prefix++;
  }
  return 1u;
}

static uint8_t parse_i32(const char *s, int32_t *out)
{
  int32_t v = 0;
  uint8_t neg = 0u;

  if ((s == NULL) || (out == NULL) || (*s == '\0')) {
    return 0u;
  }
  if (*s == '-') {
    neg = 1u;
    s++;
  }
  if (*s == '\0') {
    return 0u;
  }
  while (*s != '\0') {
    if ((*s < '0') || (*s > '9')) {
      return 0u;
    }
    v = (v * 10) + (int32_t)(*s - '0');
    s++;
  }
  *out = (neg != 0u) ? (-v) : v;
  return 1u;
}

static void shell_echo(const char *s)
{
  (void)bsp_uart_puts(UART_PORT_SHELL, s);
}

/* ==================== 基础命令实现 ==================== */

static int cmd_help(int argc, char **argv)
{
  uint8_t  i;
  uint16_t j;

  (void)argc; (void)argv;
  shell_echo("---- commands ----\r\n");
  for (i = 0u; i < s_tbl_cnt; i++) {
    for (j = 0u; j < s_tbls[i].count; j++) {
      const shell_cmd_t *c = &s_tbls[i].tbl[j];

      if (c->help != NULL) {
        shell_echo(c->help);
        shell_echo("\r\n");
      }
    }
  }
  return ERR_OK;
}

static int cmd_ver(int argc, char **argv)
{
  (void)argc; (void)argv;
  LOG_I(LOG_TAG, "SmartStick %s  build %s %s", CFG_FW_VERSION, __DATE__, __TIME__);
  LOG_I(LOG_TAG, "SYSCLK 72MHz | I2C1 400kHz | PWM 18kHz | US %ldms (HC-SR04)",
        (long)g_cfg.us_period_ms);
  return ERR_OK;
}

static int cmd_status(int argc, char **argv)
{
  (void)argc; (void)argv;
  LOG_I(LOG_TAG, "uptime   : %lu ms", (unsigned long)bsp_time_ms());
  LOG_I(LOG_TAG, "log level: %u", (unsigned)svc_log_get_level());
  LOG_I(LOG_TAG, "errors   : total=%lu last=%s",
        (unsigned long)err_total(), err_str(err_last()));
  LOG_I(LOG_TAG, "tasks    : %u", (unsigned)svc_sched_count());
  LOG_I(LOG_TAG, "cfg key  : avoid_stop=%ldmm slow=%ldmm max_duty=%ld",
        (long)g_cfg.avoid_stop_mm, (long)g_cfg.avoid_slow_mm, (long)g_cfg.motor_max_duty);
  LOG_I(LOG_TAG, "us pins  : %s", (BOARD_US1_READY != 0) ? "configured" : "NOT configured");
  return ERR_OK;
}

static int cmd_log(int argc, char **argv)
{
  int32_t v;

  if (argc < 2) {
    LOG_I(LOG_TAG, "log level = %u (0=E 1=W 2=I 3=D 4=T)", (unsigned)svc_log_get_level());
    return ERR_OK;
  }
  if (parse_i32(argv[1], &v) == 0u) {
    return ERR_SHELL_ARG;
  }
  svc_log_set_level((uint8_t)v);
  LOG_I(LOG_TAG, "log level -> %u", (unsigned)svc_log_get_level());
  return ERR_OK;
}

static int cmd_err(int argc, char **argv)
{
  uint8_t i;
  uint8_t any = 0u;

  if ((argc >= 2) && (str_eq_ci(argv[1], "clear") != 0u)) {
    err_clear();
    LOG_I(LOG_TAG, "error stats cleared");
    return ERR_OK;
  }
  LOG_I(LOG_TAG, "last=%s total=%lu", err_str(err_last()), (unsigned long)err_total());
  for (i = 0u; i < (uint8_t)MOD_COUNT; i++) {
    if (err_count(i) != 0u) {
      LOG_I(LOG_TAG, "  %s : %u", err_mod_str(i), (unsigned)err_count(i));
      any = 1u;
    }
  }
  if (any == 0u) {
    LOG_I(LOG_TAG, "  (no error recorded)");
  }
  return ERR_OK;
}

static int cmd_cfg(int argc, char **argv)
{
  if ((argc < 2) || (str_eq_ci(argv[1], "list") != 0u)) {
    cfg_list();
    return ERR_OK;
  }
  if (str_eq_ci(argv[1], "get") != 0u) {
    int32_t v;

    if (argc < 3) {
      return ERR_SHELL_ARG;
    }
    if (cfg_get(argv[2], &v) != ERR_OK) {
      return ERR_CFG_KEY;
    }
    LOG_I(LOG_TAG, "%s = %ld", argv[2], (long)v);
    return ERR_OK;
  }
  if (str_eq_ci(argv[1], "set") != 0u) {
    int32_t v;

    if (argc < 4) {
      return ERR_SHELL_ARG;
    }
    if (parse_i32(argv[3], &v) == 0u) {
      return ERR_SHELL_ARG;
    }
    return cfg_set(argv[2], v);
  }
  if (str_eq_ci(argv[1], "reset") != 0u) {
    return cfg_reset();
  }
  if (str_eq_ci(argv[1], "save") != 0u) {
    return cfg_save();
  }
  return ERR_SHELL_ARG;
}

static int cmd_i2c(int argc, char **argv)
{
  uint8_t found[16];
  uint8_t n;
  uint8_t i;

  (void)argc; (void)argv;
  n = bsp_i2c_scan(found, (uint8_t)(sizeof(found) / sizeof(found[0])));
  LOG_I(LOG_TAG, "I2C scan: %u device(s) on 0x08~0x77", (unsigned)n);
  for (i = 0u; (i < n) && (i < 16u); i++) {
    const char *what = "(unknown)";

    if (found[i] == BSP_I2C_ADDR_OLED)     { what = "OLED  SSD1306 (expected)"; }
    else if (found[i] == BSP_I2C_ADDR_MPU) { what = "MPU6050 (expected)"; }
    else if (found[i] == BSP_I2C_ADDR_QMC) { what = "QMC5883L (expected)"; }
    LOG_I(LOG_TAG, "  0x%02X  %s", (unsigned)found[i], what);
  }
  if (n == 0u) {
    LOG_W(LOG_TAG, "  none found: check 4.7k pull-ups, module power, PB6/PB7 wiring");
  } else {
    if (bsp_i2c_probe(BSP_I2C_ADDR_OLED) == 0u) { LOG_W(LOG_TAG, "  missing 0x3C OLED"); }
    if (bsp_i2c_probe(BSP_I2C_ADDR_MPU) == 0u)  { LOG_W(LOG_TAG, "  missing 0x68 MPU6050"); }
    if (bsp_i2c_probe(BSP_I2C_ADDR_QMC) == 0u)  { LOG_W(LOG_TAG, "  missing 0x0D QMC5883L"); }
  }
  return ERR_OK;
}

static int cmd_tasks(int argc, char **argv)
{
  (void)argc; (void)argv;
  svc_sched_dump();
  return ERR_OK;
}

static int cmd_reset(int argc, char **argv)
{
  (void)argc; (void)argv;
  LOG_W(LOG_TAG, "system reset ...");
  Delay_ms(20u);                 /* 等日志发完；这是少数允许阻塞的场景 */
  NVIC_SystemReset();
  return ERR_OK;                 /* 不会执行到这里 */
}

/* ==================== 行编辑与分发 ==================== */

static void shell_execute(void)
{
  char   *argv[SHELL_ARG_MAX];
  int     argc = 0;
  char   *p = s_line;
  uint8_t  i;
  uint16_t j;

  s_line[s_len] = '\0';

  /* 分词（把空格换成 '\0'） */
  while ((*p != '\0') && (argc < (int)SHELL_ARG_MAX)) {
    while (*p == ' ') {
      *p = '\0';
      p++;
    }
    if (*p == '\0') {
      break;
    }
    argv[argc++] = p;
    while ((*p != '\0') && (*p != ' ')) {
      p++;
    }
  }
  if (argc == 0) {
    return;
  }

  /* 查找并执行 */
  for (i = 0u; i < s_tbl_cnt; i++) {
    for (j = 0u; j < s_tbls[i].count; j++) {
      const shell_cmd_t *c = &s_tbls[i].tbl[j];

      if ((c->fn != NULL) && (str_eq_ci(argv[0], c->name) != 0u)) {
        int r = c->fn(argc, argv);

        if (r != (int)ERR_OK) {
          LOG_W(LOG_TAG, "ERR: %s (%s)", err_str((err_t)r), c->name);
        }
        return;
      }
    }
  }

  /* 未知命令：给个近似提示 */
  for (i = 0u; i < s_tbl_cnt; i++) {
    for (j = 0u; j < s_tbls[i].count; j++) {
      const shell_cmd_t *c = &s_tbls[i].tbl[j];

      if (str_starts_ci(c->name, argv[0]) != 0u) {
        LOG_I(LOG_TAG, "unknown cmd '%s'; did you mean '%s'?  (help)", argv[0], c->name);
        err_record(MOD_SHELL, ERR_SHELL_CMD);
        return;
      }
    }
  }
  LOG_W(LOG_TAG, "unknown cmd '%s'  (help)  [%s]", argv[0], err_str(ERR_SHELL_CMD));
  err_record(MOD_SHELL, ERR_SHELL_CMD);
}

static void shell_putc_edit(int c)
{
  char ch = (char)c;

  if ((c == '\r') || (c == '\n')) {
    shell_echo("\r\n");
    if (s_over != 0u) {                      /* 超长行：整行丢弃，避免半条命令被执行 */
      s_over = 0u;
      s_len  = 0u;
      LOG_W(LOG_TAG, "line too long, dropped [%s]", err_str(ERR_SHELL_FULL));
      err_record(MOD_SHELL, ERR_SHELL_FULL);
      return;
    }
    shell_execute();
    s_len = 0u;
    return;
  }

  if ((c == 0x08) || (c == 0x7F)) {          /* Backspace / DEL */
    if ((s_len > 0u) && (s_over == 0u)) {
      s_len--;
      shell_echo("\b \b");
    }
    return;
  }

  if ((c < 0x20) || (c > 0x7E)) {            /* 忽略其它控制字符 */
    return;
  }

  if (s_len >= (SHELL_LINE_MAX - 1u)) {
    s_over = 1u;
    return;
  }
  s_line[s_len++] = ch;
  (void)bsp_uart_write(UART_PORT_SHELL, (const uint8_t *)(const void *)&ch, 1u);
}

void svc_shell_task(void)
{
  int c;

  while ((c = bsp_uart_getc(UART_PORT_SHELL)) >= 0) {
    shell_putc_edit(c);
  }

#if (CFG_BT_MIRROR_SHELL == 0)
  /* 蓝牙端口 v1 不处理业务，仅清空缓冲防止积压 */
  while (bsp_uart_getc(UART_PORT_BT) >= 0) {
    /* drop */
  }
#endif
}

void svc_shell_print_banner(void)
{
  shell_echo("\r\n");
  shell_echo("=====================================\r\n");
  shell_echo(" SmartStick - smart cane demo " CFG_FW_VERSION "\r\n");
  shell_echo(" build " __DATE__ " " __TIME__ "\r\n");
  shell_echo(" SYSCLK 72MHz | I2C1 400k | PWM 18k\r\n");
  shell_echo(" type 'help' for commands\r\n");
  shell_echo("=====================================\r\n");
}

err_t svc_shell_register_table(const shell_cmd_t *tbl, uint16_t count)
{
  if ((tbl == NULL) || (count == 0u)) {
    return ERR_PARAM;
  }
  if (s_tbl_cnt >= SHELL_TBL_MAX) {
    return ERR_OVERFLOW;
  }
  s_tbls[s_tbl_cnt].tbl   = tbl;
  s_tbls[s_tbl_cnt].count = count;
  s_tbl_cnt++;
  return ERR_OK;
}

void svc_shell_init(void)
{
  static uint8_t inited = 0u;

  if (inited == 0u) {
    (void)svc_shell_register_table(s_base_cmds,
                                   (uint16_t)(sizeof(s_base_cmds) / sizeof(s_base_cmds[0])));
    inited = 1u;
  }
  s_len  = 0u;
  s_over = 0u;
  bsp_uart_flush(UART_PORT_SHELL);
}

uint8_t svc_shell_parse_i32(const char *s, int32_t *out)
{
  return parse_i32(s, out);          /* 供各驱动的命令复用，避免重复实现 */
}
