/*
 * SPDX-FileCopyrightText: 2023-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <assert.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "uart_link.h"

#include "esp_log.h"
#include "esp_lv_adapter.h"
#include "lvgl.h"
#include "waveshare_rgb_lcd_port.h"

static const char *TAG = "lvgl9_demo";
void catan_ui_init(void);
void catan_ui_set_status(const char *text);

static void uart_receive_task(void *arg)
{
    (void)arg;
    char line[UART_LINK_LINE_SIZE];
    bool wroom_ready = false;
    TickType_t last_ready = xTaskGetTickCount();
    uart_link_send("S3:READY\n");
    for (;;) {
        int length = uart_link_read_line(line, sizeof(line), 100);
        if (length > 0) {
            ESP_LOGI(TAG, "WROOM -> S3: %s", line);
            if (strcmp(line, "WROOM:READY") == 0) {
                wroom_ready = true;
            }
            if (strcmp(line, "WROOM:READY") == 0 ||
                strcmp(line, "PLAYER:1") == 0 ||
                strcmp(line, "NFC:UNKNOWN") == 0) {
                ESP_ERROR_CHECK(esp_lv_adapter_lock(-1));
                catan_ui_set_status(line);
                esp_lv_adapter_unlock();
            } else {
                ESP_LOGW(TAG, "Ignoring unsupported WROOM message");
            }
        }
        if (!wroom_ready && xTaskGetTickCount() - last_ready >= pdMS_TO_TICKS(1000)) {
            uart_link_send("S3:READY\n");
            last_ready = xTaskGetTickCount();
        }
    }
}

void app_main(void)
{
    uart_link_init();
    const esp_lv_adapter_rotation_t rotation = ESP_LV_ADAPTER_ROTATE_0;
    const esp_lv_adapter_tear_avoid_mode_t tear_mode = ESP_LV_ADAPTER_TEAR_AVOID_MODE_DEFAULT_RGB;

    esp_lcd_panel_handle_t panel_handle = NULL;
    esp_lcd_touch_handle_t touch_handle = NULL;

    ESP_ERROR_CHECK(waveshare_esp32_s3_rgb_lcd_init(
        tear_mode,
        rotation,
        &panel_handle,
        &touch_handle));
    ESP_ERROR_CHECK(waveshare_rgb_lcd_backlight_on());

    esp_lv_adapter_config_t adapter_config = ESP_LV_ADAPTER_DEFAULT_CONFIG();
    adapter_config.task_stack_size = 12 * 1024;
    adapter_config.stack_in_psram = true;
    ESP_ERROR_CHECK(esp_lv_adapter_init(&adapter_config));

    esp_lv_adapter_display_config_t disp_config = ESP_LV_ADAPTER_DISPLAY_RGB_DEFAULT_CONFIG(
        panel_handle,
        NULL,
        EXAMPLE_LCD_H_RES,
        EXAMPLE_LCD_V_RES,
        rotation);
    disp_config.profile.use_psram = true;

    lv_display_t *disp = esp_lv_adapter_register_display(&disp_config);
    assert(disp != NULL);

    if (touch_handle != NULL) {
        esp_lv_adapter_touch_config_t touch_config = ESP_LV_ADAPTER_TOUCH_DEFAULT_CONFIG(disp, touch_handle);
        lv_indev_t *touch = esp_lv_adapter_register_touch(&touch_config);
        assert(touch != NULL);
    }

    ESP_ERROR_CHECK(esp_lv_adapter_start());

    ESP_LOGI(TAG, "Starting CATAN welcome screen");
    ESP_ERROR_CHECK(esp_lv_adapter_lock(-1));
    catan_ui_init();
    esp_lv_adapter_unlock();

    if (xTaskCreate(uart_receive_task, "uart_receive", 4096, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Unable to create UART receive task");
        ESP_ERROR_CHECK(ESP_ERR_NO_MEM);
    }
}
