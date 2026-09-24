/**
 * @file    app.c
 * @brief   应用初始化与主循环（上电顺序见 ARCHITECTURE.md 6.4）
 * @note    原则：任何"非安全关键"外设初始化失败都不允许卡死启动；只记错误码 + 日志。
 *          电机子系统是唯一例外：初始化失败即禁止一切运动（阶段 4-D 由 FSM 接管为 ST_FAULT）。
 * @version 0.1  (2026-09-24)
 */
#include "app.h"
#include "app_ui.h"
#include "svc_log.h"
#include "svc_sched.h"
#include "svc_shell.h"
#include "bsp_time.h"
#include "bsp_gpio.h"
#include "bsp_uart.h"
#include "bsp_i2c.h"
#include "drv_motor.h"
#include "drv_alarm.h"
#include "drv_us.h"
#include "drv_imu.h"
#include "drv_oled.h"
#include "drv_batt.h"
#include "drv_key.h"
#include "drv_mag.h"
#include "drv_bt.h"
#include "cfg.h"
#include "board.h"

#define LOG_TAG "APP "

/* ==================== 状态/静音查询（供 UI 显示） ==================== */
static const char *s_state_name = "IDLE";      /* 阶段 4-D 由 app_fsm 更新 */

const char *app_state_str(void)
{
  return s_state_name;
}

uint8_t alarm_muted(void)
{
  return (alarm_get_enable() == 0u) ? 1u : 0u;
}

/* ==================== 调度器任务包装 ====================
 * 调度器任务签名是 void(void)，而驱动的 update 返回 err_t（错误已在驱动内部记账），
 * 这里用一行包装函数转换，避免在调度器里引入返回值语义。
 */
static void app_motor_task(void) { (void)motor_update(); }
static void app_alarm_task(void) { (void)alarm_update(); }
static void app_us_task(void)    { (void)us_update();    }
static void app_batt_task(void)  { (void)batt_update();  }
static void app_key_task(void)   { (void)key_update();   }
static void app_imu_task(void)   { (void)imu_update();   }
static void app_mag_task(void)   { (void)mag_update();   }

void app_hb_task(void)
{
  bsp_led_hb_toggle();
}

static void app_i2c_report(void)
{
  uint8_t found[8];
  uint8_t n;
  uint8_t i;

  n = bsp_i2c_scan(found, (uint8_t)(sizeof(found) / sizeof(found[0])));
  if (n == 0u) {
    LOG_W(LOG_TAG, "I2C: no device (check pull-ups / power / PB6-PB7)");
    err_record(MOD_HW, ERR_I2C_NACK);
    return;
  }
  for (i = 0u; (i < n) && (i < 8u); i++) {
    LOG_I(LOG_TAG, "I2C found 0x%02X", (unsigned)found[i]);
  }
}

/* ==================== 对外接口 ==================== */

void app_init(void)
{
  err_t e;

  /* 1. 日志（最先行，后面所有步骤的失败才有人报） */
  svc_log_init();
  err_clear();

  /* 2. 微秒/毫秒时间基准（TIM4 + SysTick） */
  e = bsp_time_init();
  if (e != ERR_OK) {
    err_record(MOD_SYS, e);
    LOG_E(LOG_TAG, "bsp_time_init failed: %s", err_str(e));
  }

  /* 3. 参数表（后面所有模块都读 g_cfg） */
  cfg_init();

  /* 4. 报警输出安全态 + 超声波引脚自检 */
  e = bsp_gpio_init();
  if (e == ERR_US_NOT_CFG) {
    LOG_W(LOG_TAG, "US pins NOT configured in CubeMX (PA0/PA1) -> %s", err_str(e));
    err_record(MOD_US, e);
  } else if (e != ERR_OK) {
    err_record(MOD_HW, e);
    LOG_E(LOG_TAG, "bsp_gpio_init failed: %s", err_str(e));
  }

  /* 5. 串口 + 命令行（驱动随后挂各自的命令表） */
  e = bsp_uart_init();
  if (e != ERR_OK) {
    err_record(MOD_SYS, e);
  }
  svc_shell_init();
  svc_shell_print_banner();

  /* 6. 电机：必须尽早进入安全态（PWM=0、IN 全低） */
  e = motor_init();
  if (e != ERR_OK) {
    err_record(MOD_MOTOR, e);
    LOG_E(LOG_TAG, "motor_init failed: %s -> motion disabled", err_str(e));
  } else {
    (void)motor_shell_register();
  }

  /* 7. 报警（先注册，最后再放"就绪提示音"） */
  e = alarm_init();
  if (e != ERR_OK) {
    err_record(MOD_ALM, e);
    LOG_E(LOG_TAG, "alarm_init failed: %s", err_str(e));
  } else {
    (void)alarm_shell_register();
  }

  /* 8. 超声波（HC-SR04）：失败只降级，不阻断 */
  e = us_init();
  if (e != ERR_OK) {
    err_record(MOD_US, e);
  } else {
    (void)us_shell_register();
  }

  /* 9. 电池电压 */
  e = batt_init();
  if (e != ERR_OK) {
    err_record(MOD_BATT, e);
    LOG_W(LOG_TAG, "batt_init failed: %s", err_str(e));
  } else {
    (void)batt_shell_register();
  }

  /* 10. 按键 */
  e = key_init();
  if (e != ERR_OK) {
    err_record(MOD_KEY, e);
  } else {
    (void)key_shell_register();
  }

  /* 11. 蓝牙骨架（P2 预留：只留收发与钩子） */
  e = bt_init();
  if (e != ERR_OK) {
    err_record(MOD_BT, e);
  } else {
    (void)bt_shell_register();
  }

  /* 12. I²C 总线自检 + 挂总线设备（MPU6050 / QMC5883L / OLED） */
  (void)bsp_i2c_init();
  app_i2c_report();

  e = imu_init();
  if (e != ERR_OK) {
    err_record(MOD_IMU, e);
    LOG_W(LOG_TAG, "imu_init failed: %s (fall detection degraded)", err_str(e));
  } else {
    (void)imu_shell_register();
  }

  e = mag_init();
  if (e != ERR_OK) {
    err_record(MOD_MAG, e);
    LOG_W(LOG_TAG, "mag_init failed: %s (P2 feature)",
          err_str(e));
  } else {
    (void)mag_shell_register();
  }

  e = oled_init();
  if (e != ERR_OK) {
    LOG_W(LOG_TAG, "oled_init failed: %s (continuing headless)", err_str(e));
  } else {
    (void)oled_shell_register();
    app_ui_boot_screen();
  }

  /* 13. 任务注册 */
  svc_sched_init();
  (void)svc_sched_add("shell",  svc_shell_task,  10u);
  (void)svc_sched_add("motor",  app_motor_task,  10u);   /* 软启动斜坡/换向保护 */
  (void)svc_sched_add("alarm",  app_alarm_task,  10u);   /* 报警 pattern 推进 */
  (void)svc_sched_add("us",     app_us_task,     20u);   /* 内部按 us_period_ms 节流触发 */
  (void)svc_sched_add("key",    app_key_task,    20u);   /* 去抖 + 事件 */
  (void)svc_sched_add("imu",    app_imu_task, (uint32_t)g_cfg.imu_period_ms);
  (void)svc_sched_add("ui",     app_ui_task,  (uint32_t)g_cfg.ui_period_ms);
  (void)svc_sched_add("batt",   app_batt_task, (uint32_t)g_cfg.batt_period_ms);
  (void)svc_sched_add("mag",    app_mag_task,   100u);
  (void)svc_sched_add("bt",     bt_task,        100u);
  (void)svc_sched_add("hb",     app_hb_task,    500u);   /* 心跳灯 1Hz */

  LOG_I(LOG_TAG, "app_init done, %u task(s) running", (unsigned)svc_sched_count());
  LOG_I(LOG_TAG, "tips: help | us | imu | batt | key | oled | mag | bt | motor | alarm | cfg | err | tasks");

  /* 14. 就绪提示音（两短声）：听得见就说明蜂鸣器通路 OK */
  (void)alarm_play(ALARM_EV_BOOT_OK);
}

void app_poll(void)
{
  svc_sched_run();
}
