/**
 * @file    drv_imu.c
 * @brief   MPU6050 驱动实现（WHO_AM_I 校验 → 配置 ±2g/±250dps → 读 14 字节 → 整数换
 *          算 + 模长/倾角指标）
 * @version 0.1  (2026-09-24)
 */
#include <stdio.h>
#include <string.h>

#include "drv_imu.h"
#include "svc_log.h"
#include "svc_shell.h"
#include "bsp_i2c.h"
#include "bsp_time.h"
#include "cfg.h"
#include "board.h"
#include "Delay.h"

#define LOG_TAG "IMU "

/* MPU6050 寄存器 */
#define MPU_REG_SMPLRT_DIV   0x19u
#define MPU_REG_CONFIG       0x1Au
#define MPU_REG_GYRO_CFG     0x1Bu
#define MPU_REG_ACCEL_CFG    0x1Cu
#define MPU_REG_DATA_START   0x3Bu
#define MPU_REG_PWR_MGMT_1   0x6Bu
#define MPU_REG_WHO_AM_I     0x75u

/* 量程与灵敏度（±2g / ±250dps） */
#define MPU_ACC_LSB_PER_G    16384     /* LSB/g */
#define MPU_GYRO_LSB_PER_DPS 131       /* LSB/(°/s) */

static imu_data_t s_imu;

/* ---------------- 整数平方根（避免 math.h） ---------------- */
static uint32_t isqrt_u32(uint32_t v)
{
  uint32_t r = 0u;
  uint32_t bit = 1u << 30;

  while (bit > v) {
    bit >>= 2;
  }
  while (bit != 0u) {
    if (v >= (r + bit)) {
      v -= (r + bit);
      r  = (r >> 1) + bit;
    } else {
      r >>= 1;
    }
    bit >>= 2;
  }
  return r;
}

/* sin°×100 查表（0/15/30/45/60/75/90），用于把 tilt_sin_pct 反查成角度 */
static const uint16_t s_sin_tbl[7] = { 0u, 26u, 50u, 71u, 87u, 97u, 100u };

static uint16_t tilt_deg_from_sin(uint16_t sin_pct)
{
  uint8_t i;

  if (sin_pct >= 100u) {
    return 90u;
  }
  for (i = 0u; i < 6u; i++) {
    if (sin_pct <= s_sin_tbl[i + 1u]) {
      uint16_t s0 = s_sin_tbl[i];
      uint16_t s1 = s_sin_tbl[i + 1u];
      uint16_t a0 = (uint16_t)(i * 15u);
      /* 线性插值：angle = a0 + 15 × (v - s0) / (s1 - s0) */
      return (uint16_t)(a0 + (uint16_t)((15u * (sin_pct - s0)) / (s1 - s0)));
    }
  }
  return 90u;
}

/* ---------------- 初始化 ---------------- */
err_t imu_init(void)
{
  uint8_t id = 0u;
  uint8_t buf[2];

  memset(&s_imu, 0, sizeof(s_imu));

  if (bsp_i2c_mem_read(BSP_I2C_ADDR_MPU, MPU_REG_WHO_AM_I, &id, 1u) != ERR_OK) {
    LOG_E(LOG_TAG, "WHO_AM_I read failed: %s", err_str(ERR_I2C_NACK));
    err_record(MOD_IMU, ERR_IMU_WHOAMI);
    return ERR_IMU_WHOAMI;
  }
  if ((id & 0x7Eu) != 0x68u) {                 /* 兼容 0x68/0x69/0x70/0x71/0x73 */
    LOG_E(LOG_TAG, "WHO_AM_I = 0x%02X, expect 0x68", (unsigned)id);
    err_record(MOD_IMU, ERR_IMU_WHOAMI);
    return ERR_IMU_WHOAMI;
  }

  /* 唤醒 + 时钟源 = X 轴陀螺 PLL */
  buf[0] = 0x01u;
  if (bsp_i2c_mem_write(BSP_I2C_ADDR_MPU, MPU_REG_PWR_MGMT_1, buf, 1u) != ERR_OK) {
    return ERR_IMU_CFG;
  }
  /* 采样率分频 = 9 → 1kHz/(1+9) = 100Hz（任务 50Hz 读取，留余量） */
  buf[0] = 0x09u;
  if (bsp_i2c_mem_write(BSP_I2C_ADDR_MPU, MPU_REG_SMPLRT_DIV, buf, 1u) != ERR_OK) {
    return ERR_IMU_CFG;
  }
  /* DLPF = 3（44Hz） */
  buf[0] = 0x03u;
  if (bsp_i2c_mem_write(BSP_I2C_ADDR_MPU, MPU_REG_CONFIG, buf, 1u) != ERR_OK) {
    return ERR_IMU_CFG;
  }
  /* 陀螺 ±250°/s */
  buf[0] = 0x00u;
  if (bsp_i2c_mem_write(BSP_I2C_ADDR_MPU, MPU_REG_GYRO_CFG, buf, 1u) != ERR_OK) {
    return ERR_IMU_CFG;
  }
  /* 加速度 ±2g */
  buf[0] = 0x00u;
  if (bsp_i2c_mem_write(BSP_I2C_ADDR_MPU, MPU_REG_ACCEL_CFG, buf, 1u) != ERR_OK) {
    return ERR_IMU_CFG;
  }

  LOG_I(LOG_TAG, "init ok: WHO_AM_I=0x%02X, +-2g / +-250dps, 100Hz, period=%ldms",
        (unsigned)id, (long)g_cfg.imu_period_ms);
  return ERR_OK;
}

/* ---------------- 周期采样 ---------------- */
err_t imu_update(void)
{
  uint8_t  raw[14];
  int32_t  ax;
  int32_t  ay;
  int32_t  az;
  uint32_t n;
  uint32_t horiz;

  if (bsp_i2c_mem_read(BSP_I2C_ADDR_MPU, MPU_REG_DATA_START, raw, 14u) != ERR_OK) {
    s_imu.valid = 0u;
    err_record(MOD_IMU, ERR_IMU_READ);
    return ERR_IMU_READ;
  }

  /* 大端 16 位有符号 */
  ax = (int32_t)(int16_t)(((uint16_t)raw[0] << 8) | raw[1]);
  ay = (int32_t)(int16_t)(((uint16_t)raw[2] << 8) | raw[3]);
  az = (int32_t)(int16_t)(((uint16_t)raw[4] << 8) | raw[5]);

  s_imu.acc_mg[0] = (int16_t)(ax * 1000 / MPU_ACC_LSB_PER_G);
  s_imu.acc_mg[1] = (int16_t)(ay * 1000 / MPU_ACC_LSB_PER_G);
  s_imu.acc_mg[2] = (int16_t)(az * 1000 / MPU_ACC_LSB_PER_G);

  s_imu.temp_d10 = (uint16_t)((int32_t)(int16_t)(((uint16_t)raw[6] << 8) | raw[7]) * 10 / 340 + 365);

  s_imu.gyro_d10[0] = (int16_t)((int32_t)(int16_t)(((uint16_t)raw[8] << 8) | raw[9]) * 10 / MPU_GYRO_LSB_PER_DPS);
  s_imu.gyro_d10[1] = (int16_t)((int32_t)(int16_t)(((uint16_t)raw[10] << 8) | raw[11]) * 10 / MPU_GYRO_LSB_PER_DPS);
  s_imu.gyro_d10[2] = (int16_t)((int32_t)(int16_t)(((uint16_t)raw[12] << 8) | raw[13]) * 10 / MPU_GYRO_LSB_PER_DPS);

  /* 模长（mg / 0.1°/s） */
  {
    int32_t a = s_imu.acc_mg[0];
    int32_t b = s_imu.acc_mg[1];
    int32_t c = s_imu.acc_mg[2];
    int32_t gx = s_imu.gyro_d10[0];
    int32_t gy = s_imu.gyro_d10[1];
    int32_t gz = s_imu.gyro_d10[2];

    n = isqrt_u32((uint32_t)(a * a + b * b + c * c));
    s_imu.acc_norm_mg = (uint16_t)((n > 0xFFFFu) ? 0xFFFFu : n);

    n = isqrt_u32((uint32_t)(gx * gx + gy * gy + gz * gz));
    s_imu.gyro_norm_d10 = (uint16_t)((n > 0xFFFFu) ? 0xFFFFu : n);

    /* 倾角：水平分量 / 总模长 = sin(与竖直的夹角) */
    horiz = isqrt_u32((uint32_t)(a * a + b * b));
    if (s_imu.acc_norm_mg > 0u) {
      uint32_t pct = (horiz * 100u) / (uint32_t)s_imu.acc_norm_mg;

      s_imu.tilt_sin_pct = (uint16_t)((pct > 100u) ? 100u : pct);
    } else {
      s_imu.tilt_sin_pct = 0u;
    }
  }
  s_imu.tilt_deg = tilt_deg_from_sin(s_imu.tilt_sin_pct);

  s_imu.valid = 1u;
  s_imu.cnt++;
  return ERR_OK;
}

const imu_data_t *imu_data(void)
{
  return &s_imu;
}

err_t imu_selftest(char *out, uint16_t n)
{
  if ((out == NULL) || (n == 0u)) {
    return ERR_PARAM;
  }
  if (s_imu.valid == 0u) {
    (void)snprintf(out, n, "FAIL: no valid sample (check I2C 0x68 / AD0 / wiring)");
    return ERR_IMU_READ;
  }
  (void)snprintf(out, n, "OK: |a|=%umg tilt=%u%% (%udeg) T=%u.%uC  [static: |a|~1000mg]",
                 (unsigned)s_imu.acc_norm_mg, (unsigned)s_imu.tilt_sin_pct,
                 (unsigned)s_imu.tilt_deg, (unsigned)(s_imu.temp_d10 / 10u),
                 (unsigned)(s_imu.temp_d10 % 10u));
  return ERR_OK;
}

/* ==================== shell 命令 ==================== */
/* imu          打印一帧姿态
 * imu mon [n]  连续打印（默认 10，最多 50），间隔 200ms —— 晃一下就能看到变化
 */
static void imu_print_one(uint32_t idx)
{
  const imu_data_t *d = imu_data();

  if (d->valid == 0u) {
    LOG_W(LOG_TAG, "[%lu] invalid", (unsigned long)idx);
    return;
  }
  LOG_I(LOG_TAG, "[%lu] acc=(%d,%d,%d)mg |a|=%umg | gyro=(%d,%d,%d) | tilt=%u%%(%udeg)",
        (unsigned long)idx,
        (int)d->acc_mg[0], (int)d->acc_mg[1], (int)d->acc_mg[2], (unsigned)d->acc_norm_mg,
        (int)d->gyro_d10[0], (int)d->gyro_d10[1], (int)d->gyro_d10[2],
        (unsigned)d->tilt_sin_pct, (unsigned)d->tilt_deg);
}

static int cmd_imu(int argc, char **argv)
{
  if ((argc >= 2) && (strcmp(argv[1], "mon") == 0)) {
    int32_t cnt = 10;
    int32_t i;

    if ((argc >= 3) && (svc_shell_parse_i32(argv[2], &cnt) == 0u)) {
      return ERR_SHELL_ARG;
    }
    if (cnt < 1) { cnt = 1; }
    if (cnt > 50) { cnt = 50; }

    for (i = 0; i < cnt; i++) {
      imu_print_one((uint32_t)i);
      Delay_ms(200u);
    }
    return ERR_OK;
  }
  imu_print_one(imu_data()->cnt);
  return ERR_OK;
}

static const shell_cmd_t s_imu_cmds[] = {
  { "imu", "imu | imu mon [n]         - attitude frame(s): acc/gyro/tilt", cmd_imu }
};

err_t imu_shell_register(void)
{
  return svc_shell_register_table(s_imu_cmds,
                                  (uint16_t)(sizeof(s_imu_cmds) / sizeof(s_imu_cmds[0])));
}
