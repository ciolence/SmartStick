/**
 * @file    drv_batt.c
 * @brief   电池电压监测实现（ADC1 连续转换 + 滑动平均 + 两级阈值）
 * @version 0.1  (2026-09-24)
 */
#include <stdio.h>
#include <string.h>

#include "drv_batt.h"
#include "svc_log.h"
#include "svc_shell.h"
#include "cfg.h"
#include "board.h"

#define LOG_TAG "BATT"

#define BATT_AVG_MAX   32u

static batt_data_t s_batt;
static uint32_t    s_sum;
static uint8_t     s_n;

static void batt_classify(void)
{
  if (s_batt.mv == 0u) {
    s_batt.level = BATT_LEVEL_NORMAL;
    return;
  }
  if (s_batt.mv < (uint16_t)g_cfg.batt_crit_mv) {
    s_batt.level = BATT_LEVEL_CRIT;
  } else if (s_batt.mv < (uint16_t)g_cfg.batt_low_mv) {
    s_batt.level = BATT_LEVEL_LOW;
  } else {
    s_batt.level = BATT_LEVEL_NORMAL;
  }
}

err_t batt_init(void)
{
  memset(&s_batt, 0, sizeof(s_batt));
  s_sum = 0u;
  s_n   = 0u;

  if (HAL_ADCEx_Calibration_Start(BOARD_ADC) != HAL_OK) {
    LOG_E(LOG_TAG, "ADC calibration failed");
    return ERR_BATT_ADC;
  }
  if (HAL_ADC_Start(BOARD_ADC) != HAL_OK) {      /* 连续转换模式：启动后一直跑 */
    LOG_E(LOG_TAG, "ADC start failed");
    return ERR_BATT_ADC;
  }

  LOG_I(LOG_TAG, "init ok: ratio=%ld/10000, low=%ldmV crit=%ldmV avg=%ld",
        (long)g_cfg.batt_div_ratio_x1e4, (long)g_cfg.batt_low_mv,
        (long)g_cfg.batt_crit_mv, (long)g_cfg.batt_avg_n);
  return ERR_OK;
}

err_t batt_update(void)
{
  uint16_t raw;
  uint32_t pin_mv;
  uint32_t v_mv;
  uint8_t  navg;

  if (HAL_ADC_PollForConversion(BOARD_ADC, 5u) != HAL_OK) {
    s_batt.valid = 0u;
    err_record(MOD_BATT, ERR_BATT_ADC);
    return ERR_BATT_ADC;
  }
  raw = (uint16_t)HAL_ADC_GetValue(BOARD_ADC);

  /* 分两步算，避免 int32 溢出：(raw*3300)/4095 → 引脚电压，再乘分压比 */
  pin_mv = ((uint32_t)raw * 3300u) / 4095u;
  v_mv   = (pin_mv * (uint32_t)g_cfg.batt_div_ratio_x1e4) / 10000u;
  if (v_mv > 30000u) {
    v_mv = 30000u;                                  /* 防呆上限 */
  }

  /* 滑动平均（窗口 g_cfg.batt_avg_n，上限 BATT_AVG_MAX） */
  navg = (uint8_t)g_cfg.batt_avg_n;
  if ((navg == 0u) || (navg > BATT_AVG_MAX)) {
    navg = 8u;
  }
  s_sum += v_mv;
  if (s_n < navg) {
    s_n++;
  } else {
    s_sum -= (uint32_t)s_batt.mv;                   /* 去掉最旧的近似值（窗口滑动） */
  }

  s_batt.raw   = raw;
  s_batt.mv    = (uint16_t)(s_sum / (uint32_t)((s_n == 0u) ? 1u : s_n));
  s_batt.valid = 1u;
  batt_classify();

  /* 低电/严重低电只记一次日志（避免每 500ms 刷屏），由报警层做重复提示 */
  if (s_batt.level == BATT_LEVEL_CRIT) {
    err_record(MOD_BATT, ERR_BATT_RANGE);
  }
  return ERR_OK;
}

const batt_data_t *batt_data(void)
{
  return &s_batt;
}

err_t batt_selftest(char *out, uint16_t n)
{
  if ((out == NULL) || (n == 0u)) {
    return ERR_PARAM;
  }
  if (s_batt.raw < 8u) {
    (void)snprintf(out, n, "FAIL: raw=%u (~0V) - check PA4 divider or battery not connected",
                   (unsigned)s_batt.raw);
    return ERR_BATT_RANGE;
  }
  (void)snprintf(out, n, "OK: %umV (raw=%u) level=%u",
                 (unsigned)s_batt.mv, (unsigned)s_batt.raw, (unsigned)s_batt.level);
  return ERR_OK;
}

/* ==================== shell 命令 ==================== */
static int cmd_batt(int argc, char **argv)
{
  const batt_data_t *b = batt_data();
  static const char *const lvl[3] = { "normal", "LOW", "CRITICAL" };

  (void)argc; (void)argv;
  if (b->mv == 0u) {
    LOG_W(LOG_TAG, "no reading (raw=%u) - check PA4 / divider / battery", (unsigned)b->raw);
    return ERR_OK;
  }
  LOG_I(LOG_TAG, "%u mV  raw=%u  level=%s  (%ldmV/%ldmV thresholds)",
        (unsigned)b->mv, (unsigned)b->raw, lvl[b->level],
        (long)g_cfg.batt_low_mv, (long)g_cfg.batt_crit_mv);
  return ERR_OK;
}

static const shell_cmd_t s_batt_cmds[] = {
  { "batt", "batt                    - battery voltage (mV) and level", cmd_batt }
};

err_t batt_shell_register(void)
{
  return svc_shell_register_table(s_batt_cmds,
                                  (uint16_t)(sizeof(s_batt_cmds) / sizeof(s_batt_cmds[0])));
}
