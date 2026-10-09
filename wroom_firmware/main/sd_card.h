#pragma once

#include "esp_err.h"

esp_err_t sd_init(void);
void load_saved_game(void);
void save_game_state(void);
void autosave_task(void *arg);