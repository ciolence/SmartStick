/**
 * @file    bsp_i2c.h
 * @brief   I²C1 统一访问层（400kHz）：OLED / MPU6050 / QMC5883L 共用
 * @note    所有函数使用 7 位从机地址（内部左移 1 位转 HAL 的 8 位格式）
 * @version 0.1  (2026-09-24)
 */
#ifndef USER_BSP_I2C_H
#define USER_BSP_I2C_H

#include <stdint.h>
#include "err.h"

#define BSP_I2C_ADDR_OLED   0x3Cu
#define BSP_I2C_ADDR_MPU    0x68u
#define BSP_I2C_ADDR_QMC    0x0Du

err_t   bsp_i2c_init(void);

/* 设备在线探测：1 = 应答（5ms 超时） */
uint8_t bsp_i2c_probe(uint8_t addr7);

/* 带寄存器地址的读写（最常用） */
err_t   bsp_i2c_mem_write(uint8_t addr7, uint8_t mem, const uint8_t *buf, uint16_t len);
err_t   bsp_i2c_mem_read(uint8_t addr7, uint8_t mem, uint8_t *buf, uint16_t len);

/* 无寄存器地址的裸读写（少数器件用） */
err_t   bsp_i2c_write(uint8_t addr7, const uint8_t *buf, uint16_t len);
err_t   bsp_i2c_read(uint8_t addr7, uint8_t *buf, uint16_t len);

/**
 * @brief 总线扫描（0x08~0x77）
 * @param found 结果数组；max 数组容量
 * @return 实际找到的设备个数
 */
uint8_t bsp_i2c_scan(uint8_t *found, uint8_t max);

#endif /* USER_BSP_I2C_H */
