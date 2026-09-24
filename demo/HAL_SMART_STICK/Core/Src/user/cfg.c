/**
 * @file    cfg.c
 * @brief   参数表实现：默认值 / 表驱动读写 / 范围校验 / 列表打印
 * @note    v1 参数掉电丢失（不写 Flash）；cfg_save() 预留（CFG_ENABLE_NV_SAVE）
 * @version 0.1  (2026-09-24)
 */
#include <string.h>
#include <stdio.h>

#include "cfg.h"
#include "svc_log.h"

cfg_t g_cfg;

#define LOG_TAG "CFG "

/* ---------------- 参数表（新增参数请同步在此登记） ---------------- */
typedef struct {
  const char *name;
  int32_t    *pval;
  int32_t     vmin;
  int32_t     vmax;
  const char *unit;
} cfg_item_t;

#define CFG_ITEM(field, vmin, vmax, unit) \
  { #field, &g_cfg.field, (vmin), (vmax), (unit) }

static const cfg_item_t s_items[] = {
  /* 周期 */
  CFG_ITEM(imu_period_ms,        10,    1000, "ms"),
  CFG_ITEM(us_period_ms,         60,    5000, "ms"),
  CFG_ITEM(batt_period_ms,      100,   10000, "ms"),
  CFG_ITEM(ui_period_ms,         50,    2000, "ms"),
  /* 避障 */
  CFG_ITEM(avoid_slow_mm,       300,    5000, "mm"),
  CFG_ITEM(avoid_stop_mm,       100,    3000, "mm"),
  CFG_ITEM(avoid_hyst_mm,         0,    2000, "mm"),
  CFG_ITEM(avoid_invalid_ms,     50,    3000, "ms"),
  CFG_ITEM(avoid_beep_ms,       100,    5000, "ms"),
  /* 牵引 */
  CFG_ITEM(guide_base_duty,       0,    1000, "duty"),
  CFG_ITEM(motor_max_duty,        0,    1000, "duty"),
  CFG_ITEM(motor_min_duty,        0,     400, "duty"),
  CFG_ITEM(motor_ramp_per10ms,    1,     200, "duty/10ms"),
  CFG_ITEM(motor_trim_l,       -200,     200, "duty"),
  CFG_ITEM(motor_trim_r,       -200,     200, "duty"),
  CFG_ITEM(motor_l_invert,        0,       1, ""),
  CFG_ITEM(motor_r_invert,        0,       1, ""),
  /* 跌倒 */
  CFG_ITEM(fall_freefall_mg,    100,     900, "mg"),
  CFG_ITEM(fall_freefall_ms,     10,     200, "ms"),
  CFG_ITEM(fall_impact_mg,     1200,    8000, "mg"),
  CFG_ITEM(fall_tilt_deg10,     200,    1700, "0.1deg"),
  CFG_ITEM(fall_confirm_ms,     500,   10000, "ms"),
  CFG_ITEM(fall_cooldown_ms,   1000,   60000, "ms"),
  /* 电池 */
  CFG_ITEM(batt_div_ratio_x1e4, 10000, 100000, "x1e-4"),
  CFG_ITEM(batt_low_mv,          6000,  15000, "mV"),
  CFG_ITEM(batt_crit_mv,         5000,  14000, "mV"),
  CFG_ITEM(batt_avg_n,              1,     32, ""),
  /* 按键 */
  CFG_ITEM(key_debounce_ms,         5,    200, "ms"),
  CFG_ITEM(key_long_ms,           200,   5000, "ms"),
  CFG_ITEM(key_vlong_ms,          500,  10000, "ms"),
  /* 报警 */
  CFG_ITEM(alarm_dedup_ms,        200,  30000, "ms"),
  /* 磁力计校准 */
  CFG_ITEM(mag_off_x_x100,     -20000,  20000, "x100"),
  CFG_ITEM(mag_off_y_x100,     -20000,  20000, "x100"),
  CFG_ITEM(mag_off_z_x100,     -20000,  20000, "x100"),
  CFG_ITEM(mag_scale_x_x100,       10,    400, "x100"),
  CFG_ITEM(mag_scale_y_x100,       10,    400, "x100"),
  CFG_ITEM(mag_scale_z_x100,       10,    400, "x100")
};

#define CFG_ITEM_COUNT ((uint16_t)(sizeof(s_items) / sizeof(s_items[0])))

/* ---------------- 默认值 ---------------- */
void cfg_load_defaults(void)
{
  /* 周期 */
  g_cfg.imu_period_ms        = 20;
  g_cfg.us_period_ms         = 80;
  g_cfg.batt_period_ms       = 500;
  g_cfg.ui_period_ms         = 200;
  /* 避障 */
  g_cfg.avoid_slow_mm        = 1200;
  g_cfg.avoid_stop_mm        = 400;
  g_cfg.avoid_hyst_mm        = 200;
  g_cfg.avoid_invalid_ms     = 300;
  g_cfg.avoid_beep_ms        = 700;
  /* 牵引 */
  g_cfg.guide_base_duty      = 600;
  g_cfg.motor_max_duty       = 700;
  g_cfg.motor_min_duty       = 120;
  g_cfg.motor_ramp_per10ms   = 40;
  g_cfg.motor_trim_l         = 0;
  g_cfg.motor_trim_r         = 0;
  g_cfg.motor_l_invert       = 0;
  g_cfg.motor_r_invert       = 0;
  /* 跌倒 */
  g_cfg.fall_freefall_mg     = 400;
  g_cfg.fall_freefall_ms     = 30;
  g_cfg.fall_impact_mg       = 2200;
  g_cfg.fall_tilt_deg10      = 550;
  g_cfg.fall_confirm_ms      = 2000;
  g_cfg.fall_cooldown_ms     = 10000;
  /* 电池（暂按 3S 锂电：11.1V 标称 / 12.6V 满 / 9.0V 空） */
  g_cfg.batt_div_ratio_x1e4  = 43000;
  g_cfg.batt_low_mv          = 10500;
  g_cfg.batt_crit_mv         = 9900;
  g_cfg.batt_avg_n           = 8;
  /* 按键 */
  g_cfg.key_debounce_ms      = 20;
  g_cfg.key_long_ms          = 1000;
  g_cfg.key_vlong_ms         = 3000;
  /* 报警 */
  g_cfg.alarm_dedup_ms       = 5000;
  /* 磁力计校准 */
  g_cfg.mag_off_x_x100       = 0;
  g_cfg.mag_off_y_x100       = 0;
  g_cfg.mag_off_z_x100       = 0;
  g_cfg.mag_scale_x_x100     = 100;
  g_cfg.mag_scale_y_x100     = 100;
  g_cfg.mag_scale_z_x100     = 100;
}

void cfg_init(void)
{
  cfg_load_defaults();
  LOG_I(LOG_TAG, "cfg loaded: %u items, bounds checked", (unsigned)CFG_ITEM_COUNT);
}

/* ---------------- 查找 ---------------- */
static const cfg_item_t *cfg_find(const char *key)
{
  uint16_t i;

  if (key == NULL) {
    return NULL;
  }
  for (i = 0u; i < CFG_ITEM_COUNT; i++) {
    if (strcmp(s_items[i].name, key) == 0) {
      return &s_items[i];
    }
  }
  return NULL;
}

/* ---------------- 读写 ---------------- */
err_t cfg_get(const char *key, int32_t *out)
{
  const cfg_item_t *it = cfg_find(key);

  if (it == NULL) {
    return ERR_CFG_KEY;
  }
  if (out == NULL) {
    return ERR_PARAM;
  }
  *out = *(it->pval);
  return ERR_OK;
}

err_t cfg_set(const char *key, int32_t value)
{
  const cfg_item_t *it = cfg_find(key);

  if (it == NULL) {
    return ERR_CFG_KEY;
  }
  if ((value < it->vmin) || (value > it->vmax)) {
    LOG_W(LOG_TAG, "%s=%ld out of range [%ld,%ld]",
          it->name, (long)value, (long)it->vmin, (long)it->vmax);
    return ERR_CFG_RANGE;
  }
  *(it->pval) = value;
  LOG_I(LOG_TAG, "set %s=%ld %s", it->name, (long)value, it->unit);
  return ERR_OK;
}

void cfg_list(void)
{
  uint16_t i;

  LOG_I(LOG_TAG, "---- cfg (%u items) ----", (unsigned)CFG_ITEM_COUNT);
  for (i = 0u; i < CFG_ITEM_COUNT; i++) {
    LOG_I(LOG_TAG, "%-20s = %-8ld %s",
          s_items[i].name, (long)*(s_items[i].pval), s_items[i].unit);
  }
}

uint16_t cfg_count(void)
{
  return CFG_ITEM_COUNT;
}

const char *cfg_key_at(uint16_t idx)
{
  return (idx < CFG_ITEM_COUNT) ? s_items[idx].name : NULL;
}

err_t cfg_reset(void)
{
  cfg_load_defaults();
  LOG_I(LOG_TAG, "cfg reset to defaults");
  return ERR_OK;
}

err_t cfg_save(void)
{
#if (CFG_ENABLE_NV_SAVE == 0)
  LOG_W(LOG_TAG, "cfg_save: NV storage disabled (CFG_ENABLE_NV_SAVE=0)");
  return ERR_UNSUPPORTED;
#else
  /* ★预留：使用 Flash 最后一页（0x0800FC00）保存参数快照，阶段 4 后段实现 */
  return ERR_UNSUPPORTED;
#endif
}
