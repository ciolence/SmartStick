#ifndef __DELAY_H
#define __DELAY_H

#include <stdint.h>          /* 显式包含，避免依赖包含顺序（2026-09-24 补充） */

void Delay_us(uint32_t us);
void Delay_ms(uint32_t ms);
void Delay_s(uint32_t s);

#endif
