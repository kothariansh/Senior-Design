#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

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
                ESP_LOGI(TAG, "PLAYER 1 detected");

                play_sound(
                    ka_ching_start,
                    ka_ching_end
                );
            }
            else
            {
                ESP_LOGW(TAG, "Unknown NFC fob");

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
