/**
 * @file    drv_mag.c
 * @brief   QMC5883L 驱动实现（读数据 + 硬磁校准；不做航向）
 * @version 0.1  (2026-09-24)
 */
#include <stdio.h>
#include <string.h>

#include "drv_mag.h"
#include "svc_log.h"
#include "svc_shell.h"
#include "bsp_i2c.h"
#include "cfg.h"
#include "board.h"

#define LOG_TAG "MAG "

/* QMC5883L 寄存器 */
#define QMC_REG_DATA    0x00u       /* 0x00~0x05: X/Y/Z 各 16 位（小端） */
#define QMC_REG_STATUS  0x06u       /* bit0 = DRDY */
#define QMC_REG_CTRL1   0x09u
#define QMC_REG_CTRL2   0x0Au
#define QMC_REG_SETRES  0x0Bu
#define QMC_REG_CHIPID  0x0Du       /* 固定 0xFF */

#define QMC_CTRL1_CFG   0x1Du       /* 连续 / 200Hz / 8G / OSR512 */

static mag_data_t s_mag;

err_t mag_init(void)
{
  uint8_t id = 0u;
  uint8_t v;

  memset(&s_mag, 0, sizeof(s_mag));

  if (bsp_i2c_mem_read(BSP_I2C_ADDR_QMC, QMC_REG_CHIPID, &id, 1u) != ERR_OK) {
    LOG_W(LOG_TAG, "chip ID read failed (no device at 0x0D?)");
    err_record(MOD_MAG, ERR_MAG_ID);
    return ERR_MAG_ID;
  }
  if (id != 0xFFu) {
    LOG_W(LOG_TAG, "chip ID = 0x%02X, expect 0xFF (QMC5883L; HMC5883L is 0x1E@0x1E)",
          (unsigned)id);
    err_record(MOD_MAG, ERR_MAG_ID);
    return ERR_MAG_ID;
  }

  v = 0x01u;                                            /* SET/RESET period */
  if (bsp_i2c_mem_write(BSP_I2C_ADDR_QMC, QMC_REG_SETRES, &v, 1u) != ERR_OK) {
    return ERR_MAG_CFG;
  }
  v = 0x00u;                                            /* 不使能中断 */
  if (bsp_i2c_mem_write(BSP_I2C_ADDR_QMC, QMC_REG_CTRL2, &v, 1u) != ERR_OK) {
    return ERR_MAG_CFG;
  }
  v = QMC_CTRL1_CFG;
  if (bsp_i2c_mem_write(BSP_I2C_ADDR_QMC, QMC_REG_CTRL1, &v, 1u) != ERR_OK) {
    return ERR_MAG_CFG;
  }

  LOG_I(LOG_TAG, "init ok: QMC5883L @0x0D, continuous 200Hz, +-8G (0.1uT = raw/3)");
  LOG_I(LOG_TAG, "note: keep it away from motors/wires; run 'mag cal start' then rotate 360");
  return ERR_OK;
}

err_t mag_update(void)
{
  uint8_t raw[6];

  if (bsp_i2c_mem_read(BSP_I2C_ADDR_QMC, QMC_REG_DATA, raw, 6u) != ERR_OK) {
    s_mag.valid = 0u;
    err_record(MOD_MAG, ERR_MAG_READ);
    return ERR_MAG_READ;
  }

  s_mag.raw[0] = (int16_t)(((uint16_t)raw[1] << 8) | raw[0]);   /* 小端 */
  s_mag.raw[1] = (int16_t)(((uint16_t)raw[3] << 8) | raw[2]);
  s_mag.raw[2] = (int16_t)(((uint16_t)raw[5] << 8) | raw[4]);

  /* 硬磁偏移（cfg 里是 ×100）+ 软铁比例（×100），再换算到 0.1µT */
  {
    int32_t cx = ((int32_t)s_mag.raw[0] * 100 - g_cfg.mag_off_x_x100) * g_cfg.mag_scale_x_x100 / 100;
    int32_t cy = ((int32_t)s_mag.raw[1] * 100 - g_cfg.mag_off_y_x100) * g_cfg.mag_scale_y_x100 / 100;
    int32_t cz = ((int32_t)s_mag.raw[2] * 100 - g_cfg.mag_off_z_x100) * g_cfg.mag_scale_z_x100 / 100;

    s_mag.xyz_d1ut[0] = (int16_t)(cx / 300);     /* 0.01LSB → 0.1µT：/300 */
    s_mag.xyz_d1ut[1] = (int16_t)(cy / 300);
    s_mag.xyz_d1ut[2] = (int16_t)(cz / 300);
  }

  /* 校准采集：记录各轴极值（原地转 360° 即可覆盖） */
  if (s_mag.cal_state == (uint8_t)MAG_CAL_RUNNING) {
    uint8_t i;

    for (i = 0u; i < 3u; i++) {
      if (s_mag.raw[i] < s_mag.min_raw[i]) { s_mag.min_raw[i] = s_mag.raw[i]; }
      if (s_mag.raw[i] > s_mag.max_raw[i]) { s_mag.max_raw[i] = s_mag.raw[i]; }
    }
  }

  s_mag.valid = 1u;
  s_mag.cnt++;
  return ERR_OK;
}

const mag_data_t *mag_data(void)
{
  return &s_mag;
}

err_t mag_cal_start(void)
{
  uint8_t i;

  s_mag.min_raw[0] = s_mag.min_raw[1] = s_mag.min_raw[2] = 32767;
  s_mag.max_raw[0] = s_mag.max_raw[1] = s_mag.max_raw[2] = -32768;
  s_mag.cal_state  = (uint8_t)MAG_CAL_RUNNING;
  (void)i;
  LOG_I(LOG_TAG, "cal started: rotate the stick slowly 360 deg (in place), then 'mag cal stop'");
  return ERR_OK;
}

err_t mag_cal_stop(void)
{
  int32_t ox;
  int32_t oy;
  int32_t oz;

  if (s_mag.cal_state != (uint8_t)MAG_CAL_RUNNING) {
    return ERR_MAG_CAL;
  }
  if ((s_mag.min_raw[0] >= s_mag.max_raw[0]) ||
      (s_mag.min_raw[1] >= s_mag.max_raw[1]) ||
      (s_mag.min_raw[2] >= s_mag.max_raw[2])) {
    LOG_W(LOG_TAG, "cal failed: no valid extremes captured (rotate more)");
    s_mag.cal_state = (uint8_t)MAG_CAL_IDLE;
    return ERR_MAG_CAL;
  }

  ox = ((int32_t)s_mag.min_raw[0] + s_mag.max_raw[0]) / 2;
  oy = ((int32_t)s_mag.min_raw[1] + s_mag.max_raw[1]) / 2;
  oz = ((int32_t)s_mag.min_raw[2] + s_mag.max_raw[2]) / 2;

  (void)cfg_set("mag_off_x_x100", ox * 100);
  (void)cfg_set("mag_off_y_x100", oy * 100);
  (void)cfg_set("mag_off_z_x100", oz * 100);

  s_mag.cal_state = (uint8_t)MAG_CAL_DONE;
  LOG_I(LOG_TAG, "cal done: offset x=%ld y=%ld z=%ld (raw LSB); stored in cfg (RAM only)",
        (long)ox, (long)oy, (long)oz);
  LOG_I(LOG_TAG, "range x=%d y=%d z=%d", (int)(s_mag.max_raw[0] - s_mag.min_raw[0]),
        (int)(s_mag.max_raw[1] - s_mag.min_raw[1]), (int)(s_mag.max_raw[2] - s_mag.min_raw[2]));
  return ERR_OK;
}

err_t mag_selftest(char *out, uint16_t n)
{
  if ((out == NULL) || (n == 0u)) {
    return ERR_PARAM;
  }
  if (s_mag.valid == 0u) {
    (void)snprintf(out, n, "FAIL: no valid sample (0x0D not answering?)");
    return ERR_MAG_READ;
  }
  (void)snprintf(out, n, "OK: %d,%d,%d (0.1uT) raw=%d,%d,%d cnt=%lu",
                 (int)s_mag.xyz_d1ut[0], (int)s_mag.xyz_d1ut[1], (int)s_mag.xyz_d1ut[2],
                 (int)s_mag.raw[0], (int)s_mag.raw[1], (int)s_mag.raw[2],
                 (unsigned long)s_mag.cnt);
  return ERR_OK;
}

/* ==================== shell 命令 ==================== */
static int cmd_mag(int argc, char **argv)
{
  const mag_data_t *d = mag_data();

  if ((argc >= 2) && (strcmp(argv[1], "cal") == 0)) {
    if (argc < 3) {
      LOG_I(LOG_TAG, "usage: mag cal start | stop");
      return ERR_OK;
    }
    if (strcmp(argv[2], "start") == 0) {
      return mag_cal_start();
    }
    if (strcmp(argv[2], "stop") == 0) {
      return mag_cal_stop();
    }
    return ERR_SHELL_ARG;
  }

  if (d->valid == 0u) {
    LOG_W(LOG_TAG, "no data (0x0D)");
    return ERR_OK;
  }
  LOG_I(LOG_TAG, "x=%d y=%d z=%d (0.1uT)  raw=%d,%d,%d  cal=%u",
        (int)d->xyz_d1ut[0], (int)d->xyz_d1ut[1], (int)d->xyz_d1ut[2],
        (int)d->raw[0], (int)d->raw[1], (int)d->raw[2], (unsigned)d->cal_state);
  return ERR_OK;
}

static const shell_cmd_t s_mag_cmds[] = {
  { "mag", "mag | mag cal start|stop   - magnetometer read & hard-iron cal", cmd_mag }
};

err_t mag_shell_register(void)
{
  return svc_shell_register_table(s_mag_cmds,
                                  (uint16_t)(sizeof(s_mag_cmds) / sizeof(s_mag_cmds[0])));
}
