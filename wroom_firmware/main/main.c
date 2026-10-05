#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdatomic.h>
#include "uart_link.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"

#include "audio.h"
#include "nfc.h"

static const char *TAG = "CATAN_WROOM";

/*
 * Embedded sound files.
 * These linker symbols match the existing audio_test implementation.
 */
extern const uint8_t ka_ching_start[]
    asm("_binary_ka_ching_raw_start");

extern const uint8_t ka_ching_end[]
    asm("_binary_ka_ching_raw_end");

extern const uint8_t error_start[]
    asm("_binary_error_raw_start");

extern const uint8_t error_end[]
    asm("_binary_error_raw_end");


static bool is_player_1(
    const uint8_t *uid,
    uint8_t uid_length
)
{
    return (
        uid_length == 4 &&
        uid[0] == 0xB3 &&
        uid[1] == 0xC9 &&
        uid[2] == 0xB6 &&
        uid[3] == 0x1D
    );
}


// Only NFC/controller code changes player identity. UI actions are intents.
static atomic_int current_player;
static atomic_bool last_nfc_unknown;

static void uart_receive_task(void *arg)
{
    (void)arg;
    char line[UART_LINK_LINE_SIZE];
    for (;;) {
        if (uart_link_read_line(line, sizeof(line), 100) <= 0) {
            continue;
        }
        ESP_LOGI(TAG, "S3 -> WROOM: %s", line);
        if (strcmp(line, "S3:READY") == 0) {
            uart_link_send("WROOM:READY\n");
            if (atomic_load(&current_player) == 1) {
                uart_link_send("PLAYER:1\n");
            }
            if (atomic_load(&last_nfc_unknown)) {
                uart_link_send("NFC:UNKNOWN\n");
            }
        } else if (strcmp(line, "ACTION:START") == 0 ||
                   strcmp(line, "ACTION:BACK") == 0) {
            ESP_LOGI(TAG, "Touchscreen navigation intent: %s", line);
            // Future game logic belongs here on WROOM. Navigation alone
            // does not change resources, player identity, or game state.
        } else {
            ESP_LOGW(TAG, "Ignoring unsupported S3 message");
        }
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "Starting CATAN WROOM controller");

    /*
     * WROOM owns the primary hardware/backend peripherals.
     */
    audio_init();

    if (!nfc_init()) {
        ESP_LOGE(TAG, "NFC initialization failed");
        return;
    }

    uart_link_init();
    if (xTaskCreate(uart_receive_task, "uart_receive", 4096, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Unable to create UART receive task");
        ESP_ERROR_CHECK(ESP_ERR_NO_MEM);
    }
    uart_link_send("WROOM:READY\n");

    ESP_LOGI(TAG, "Audio ready");
    ESP_LOGI(TAG, "Waiting for NFC fob");

    uint8_t uid[7];
    uint8_t uid_length = 0;

    while (1)
    {
        if (nfc_read_uid(uid, &uid_length, 500))
        {
            printf("UID: ");

            for (uint8_t i = 0; i < uid_length; i++) {
                printf("%02X ", uid[i]);
            }

            printf("\n");

            if (is_player_1(uid, uid_length))
            {
                ESP_LOGI(TAG, "Player 1 detected");
                atomic_store(&current_player, 1);
                atomic_store(&last_nfc_unknown, false);
                uart_link_send("PLAYER:1\n");

                play_sound(
                    ka_ching_start,
                    ka_ching_end
                );
            }
            else
            {
                ESP_LOGW(TAG, "Unknown NFC fob");
                atomic_store(&last_nfc_unknown, true);
                uart_link_send("NFC:UNKNOWN\n");

                play_sound(
                    error_start,
                    error_end
                );
            }

            /*
             * Prevent continuous scans while the same
             * fob remains on the reader.
             */
            vTaskDelay(pdMS_TO_TICKS(1500));
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
