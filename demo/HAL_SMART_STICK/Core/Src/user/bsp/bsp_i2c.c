/**
 * @file    bsp_i2c.c
 * @brief   I²C1 统一访问层实现（HAL 返回码 → 统一 err_t）
 * @version 0.1  (2026-09-24)
 */
#include "bsp_i2c.h"
#include "board.h"

#define BSP_I2C_TIMEOUT_MS  BOARD_I2C_TIMEOUT

static err_t i2c_map(HAL_StatusTypeDef st)
{
  switch (st) {
    case HAL_OK:      return ERR_OK;
    case HAL_TIMEOUT: return ERR_TIMEOUT;
    case HAL_BUSY:    return ERR_BUSY;
    default:          return ERR_I2C_NACK;      /* HAL_ERROR：多为从机无应答 */
  }
}

err_t bsp_i2c_init(void)
{
  if (bsp_i2c_probe(BSP_I2C_ADDR_OLED) != 0u) {
    /* 不在这里判失败：设备是否在线由各驱动自行判定 */
  }
  return ERR_OK;
}

uint8_t bsp_i2c_probe(uint8_t addr7)
{
  if (HAL_I2C_IsDeviceReady(BOARD_I2C, (uint16_t)(addr7 << 1), 3u,
                            BOARD_I2C_PROBE_TIMEOUT) == HAL_OK) {
    return 1u;
  }
  return 0u;
}

err_t bsp_i2c_mem_write(uint8_t addr7, uint8_t mem, const uint8_t *buf, uint16_t len)
{
  if (buf == NULL) {
    return ERR_PARAM;
  }
  return i2c_map(HAL_I2C_Mem_Write(BOARD_I2C, (uint16_t)(addr7 << 1), (uint16_t)mem,
                                   I2C_MEMADD_SIZE_8BIT, (uint8_t *)(void *)buf, len,
                                   BSP_I2C_TIMEOUT_MS));
}

err_t bsp_i2c_mem_read(uint8_t addr7, uint8_t mem, uint8_t *buf, uint16_t len)
{
  if (buf == NULL) {
    return ERR_PARAM;
  }
  return i2c_map(HAL_I2C_Mem_Read(BOARD_I2C, (uint16_t)(addr7 << 1), (uint16_t)mem,
                                  I2C_MEMADD_SIZE_8BIT, buf, len, BSP_I2C_TIMEOUT_MS));
}

err_t bsp_i2c_write(uint8_t addr7, const uint8_t *buf, uint16_t len)
{
  if (buf == NULL) {
    return ERR_PARAM;
  }
  return i2c_map(HAL_I2C_Master_Transmit(BOARD_I2C, (uint16_t)(addr7 << 1),
                                         (uint8_t *)(void *)buf, len, BSP_I2C_TIMEOUT_MS));
}

err_t bsp_i2c_read(uint8_t addr7, uint8_t *buf, uint16_t len)
{
  if (buf == NULL) {
    return ERR_PARAM;
  }
  return i2c_map(HAL_I2C_Master_Receive(BOARD_I2C, (uint16_t)(addr7 << 1), buf, len,
                                        BSP_I2C_TIMEOUT_MS));
}

uint8_t bsp_i2c_scan(uint8_t *found, uint8_t max)
{
  uint8_t addr;
  uint8_t n = 0u;

  if (found == NULL) {
    return 0u;
  }
  for (addr = 0x08u; addr <= 0x77u; addr++) {
    if (bsp_i2c_probe(addr) != 0u) {
      if (n < max) {
        found[n] = addr;
      }
      n++;
    }
  }
  return n;
}
