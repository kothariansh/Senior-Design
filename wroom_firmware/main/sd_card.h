#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

// Mount the card on WROOM's SPI2 bus. Safe to call again after a failed mount.
esp_err_t sd_init(void);
bool sd_is_ready(void);

// Read and write one named file below /sdcard. Names cannot contain separators.
esp_err_t sd_write_file(const char *name, const void *data, size_t length);
esp_err_t sd_read_file(const char *name, void *data, size_t capacity, size_t *length);

// Non-destructive persistent read/write check using a reserved diagnostic file.
esp_err_t sd_self_test(void);
