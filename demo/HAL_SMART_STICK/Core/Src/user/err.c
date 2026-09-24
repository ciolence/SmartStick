/**
 * @file    err.c
 * @brief   错误码 → 字符串映射 + 按模块错误统计
 * @note    新增错误码时，请同步在 s_err_map[] 里补一行
 * @version 0.1  (2026-09-24)
 */
#include "err.h"

/* ---------------- 错误码表 ---------------- */
typedef struct {
  err_t       code;
  const char *name;
} err_map_t;

static const err_map_t s_err_map[] = {
  /* 通用 */
  { ERR_NOT_INIT,       "NOT_INIT"      },
  { ERR_PARAM,          "PARAM"         },
  { ERR_TIMEOUT,        "TIMEOUT"       },
  { ERR_BUSY,           "BUSY"          },
  { ERR_UNSUPPORTED,    "UNSUPPORTED"   },
  { ERR_OVERFLOW,       "OVERFLOW"      },
  { ERR_NO_DATA,        "NO_DATA"       },
  /* 硬件抽象 */
  { ERR_HW_INIT,        "HW_INIT"       },
  { ERR_HW_TXFER,       "HW_TXFER"      },
  { ERR_HW_NOT_READY,   "HW_NOT_READY"  },
  /* I2C */
  { ERR_I2C_NACK,       "I2C_NACK"      },
  { ERR_I2C_BUS,        "I2C_BUS"       },
  /* 电机 */
  { ERR_MOTOR_CFG,      "MOTOR_CFG"     },
  { ERR_MOTOR_INIT,     "MOTOR_INIT"    },
  /* 超声波 */
  { ERR_US_NO_ECHO,     "US_NO_ECHO"    },
  { ERR_US_RANGE,       "US_RANGE"      },
  { ERR_US_UART,        "US_UART"       },
  { ERR_US_MODE,        "US_MODE"       },
  { ERR_US_NOT_CFG,     "US_NOT_CFG"    },
  /* IMU */
  { ERR_IMU_WHOAMI,     "IMU_WHOAMI"    },
  { ERR_IMU_CFG,        "IMU_CFG"       },
  { ERR_IMU_READ,       "IMU_READ"      },
  /* 磁力计 */
  { ERR_MAG_ID,         "MAG_ID"        },
  { ERR_MAG_CFG,        "MAG_CFG"       },
  { ERR_MAG_READ,       "MAG_READ"      },
  { ERR_MAG_CAL,        "MAG_CAL"       },
  /* OLED */
  { ERR_OLED_INIT,      "OLED_INIT"     },
  { ERR_OLED_BUS,       "OLED_BUS"      },
  /* 电池 */
  { ERR_BATT_ADC,       "BATT_ADC"      },
  { ERR_BATT_RANGE,     "BATT_RANGE"    },
  /* 按键 */
  { ERR_KEY_INIT,       "KEY_INIT"      },
  /* 蓝牙 */
  { ERR_BT_UART,        "BT_UART"       },
  /* 控制台 */
  { ERR_SHELL_CMD,      "SHELL_CMD"     },
  { ERR_SHELL_ARG,      "SHELL_ARG"     },
  { ERR_SHELL_FULL,     "SHELL_FULL"    },
  /* 应用 */
  { ERR_APP_STATE,      "APP_STATE"     },
  { ERR_APP_NO_SENSOR,  "APP_NO_SENSOR" },
  /* 配置 */
  { ERR_CFG_KEY,        "CFG_KEY"       },
  { ERR_CFG_RANGE,      "CFG_RANGE"     },
  { ERR_CFG_SAVE,       "CFG_SAVE"      }
};

/* ---------------- 模块名表（顺序必须与 mod_t 一致） ---------------- */
static const char *const s_mod_name[MOD_COUNT] = {
  "SYS ", "CFG ", "HW  ", "MOT ", "US  ", "IMU ", "MAG ",
  "OLED", "BATT", "KEY ", "BT  ", "SHEL", "FSM ", "ALM "
};

/* ---------------- 统计 ---------------- */
static uint16_t s_cnt[MOD_COUNT];
static uint32_t s_total;
static err_t    s_last = ERR_OK;

/* ---------------- 接口实现 ---------------- */
const char *err_str(err_t e)
{
  uint16_t i;

  if (e == ERR_OK) {
    return "OK";
  }
  for (i = 0u; i < (uint16_t)(sizeof(s_err_map) / sizeof(s_err_map[0])); i++) {
    if (s_err_map[i].code == e) {
      return s_err_map[i].name;
    }
  }
  return "UNKNOWN";
}

const char *err_mod_str(uint8_t mod)
{
  return (mod < (uint8_t)MOD_COUNT) ? s_mod_name[mod] : "?   ";
}

void err_record(uint8_t mod, err_t e)
{
  if (e == ERR_OK) {
    return;
  }
  if (mod < (uint8_t)MOD_COUNT) {
    if (s_cnt[mod] < 0xFFFFu) {
      s_cnt[mod]++;
    }
  }
  if (s_total < 0xFFFFFFFFu) {
    s_total++;
  }
  s_last = e;
}

err_t err_last(void)
{
  return s_last;
}

uint16_t err_count(uint8_t mod)
{
  return (mod < (uint8_t)MOD_COUNT) ? s_cnt[mod] : 0u;
}

uint32_t err_total(void)
{
  return s_total;
}

void err_clear(void)
{
  uint8_t i;

  for (i = 0u; i < (uint8_t)MOD_COUNT; i++) {
    s_cnt[i] = 0u;
  }
  s_total = 0u;
  s_last  = ERR_OK;
}
