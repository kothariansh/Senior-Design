#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "esp_log.h"
#include "esp_err.h"
#include "esp_timer.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/spi_common.h"
#include "sdmmc_cmd.h"
#include "esp_vfs_fat.h"

#define MISO 19 // DO
#define MOSI 23 // DI
#define CLK 18 
#define CS 5

#define SAVE_FILE "/sdcard/game_state.txt"

static const char *TAG = "STORAGE";
static sdmmc_card_t *card = NULL;

esp_err_t sd_init(void) {
    esp_err_t ret;

    spi_bus_config_t bus_cfg = {
        .mosi_io_num = MOSI,
        .miso_io_num = MISO,
        .sclk_io_num = CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };

    ret = spi_bus_initialize(SPI2_HOST, &bus_cfg, SPI_DMA_CH_AUTO);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SPI bus: %s",
                 esp_err_to_name(ret));
        return ret;
    }

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI2_HOST;

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = CS;
    slot_config.host_id = SPI2_HOST;

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };

    ret = esp_vfs_fat_sdspi_mount(
        "/sdcard",
        &host,
        &slot_config,
        &mount_config,
        &card
    );

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount SD card: %s",
                 esp_err_to_name(ret));

        return ret;
    }

    ESP_LOGI(TAG, "SD card mounted successfully");

    return ESP_OK;
}

void save_game_state(void) {
    int64_t time = esp_timer_get_time() / 1000000;

    FILE *f = fopen(SAVE_FILE, "w");

    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file");
        return;
    }

    fprintf(f, "Saved game state at %lld seconds\n", time);
    fclose(f);

    ESP_LOGI(TAG, "Game state saved at %lld seconds", time);
}

void load_saved_game(void) {
    FILE *f = fopen(SAVE_FILE, "r");

    if (f == NULL) {
        ESP_LOGE(TAG, "No saved game found.");
        return;
    }

    char line[128];

    ESP_LOGI(TAG, "Saved game found!");

    while (fgets(line, sizeof(line), f)) {
        printf("%s", line);
    }

    fclose(f);
}

void autosave_task(void *arg) {
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        save_game_state();
    }
}