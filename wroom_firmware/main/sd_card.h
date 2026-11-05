#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

// Prepare the storage lock before starting tasks that use the card.
esp_err_t sd_prepare(void);

// Mount on WROOM's SPI2 bus; safe to retry after a failed mount.
esp_err_t sd_init(void);
// Send a direct status command to an already mounted card.
esp_err_t sd_check_media(void);
// Remove the FAT mount and release its SPI device/bus before retrying.
esp_err_t sd_unmount(void);
bool sd_is_ready(void);

// Read and write one named file below /sdcard. Names cannot contain separators.
esp_err_t sd_write_file(const char *name, const void *data, size_t length);
esp_err_t sd_read_file(const char *name, void *data, size_t capacity, size_t *length);

// Non-destructive persistent read/write check using a reserved diagnostic file.
esp_err_t sd_self_test(void);
