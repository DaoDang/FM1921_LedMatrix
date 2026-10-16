#pragma once
#include <stdint.h>
void emu_reg_write(uint32_t reg, uint32_t val);
#define REG_WRITE(r,v) emu_reg_write((uint32_t)(r),(uint32_t)(v))
