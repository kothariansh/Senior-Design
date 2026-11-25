#pragma once
#include <stdint.h>
#include <stdbool.h>
bool board_get(int slot,uint8_t *cells);
bool board_put(int slot,const uint8_t *cells);
void board_ble_start(void);
