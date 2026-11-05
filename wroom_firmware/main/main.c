#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdatomic.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"

#include "uart_link.h"
#include "audio.h"
#include "nfc.h"
#include "sd_card.h"

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
// 0 = checking, 1 = mounted and read/write verified, -1 = unavailable.
static atomic_int sd_state;

#define SD_READY_POLL_MS 750
#define SD_RETRY_MS 1500
#define SD_REMOVAL_CONFIRMATIONS 2

static const char *sd_status_message(void)
{
    int state = atomic_load(&sd_state);
    return state > 0 ? "SD:READY\n" : state < 0 ? "SD:ERROR\n" : "SD:CHECKING\n";
}

static void sd_publish_state(int state)
{
    if (atomic_exchange(&sd_state, state) != state) {
        uart_link_send(sd_status_message());
    }
}

static void sd_monitor_task(void *arg)
{
    (void)arg;
    int missing_checks = 0;
    for (;;) {
        if (!sd_is_ready()) {
            // Failed mounts stay at ERROR; do not flicker to CHECKING on every retry.
            esp_err_t err = sd_init();
            if (err == ESP_OK) {
                sd_publish_state(0);
                // Give the S3 one display refresh before the read/write check.
                vTaskDelay(pdMS_TO_TICKS(300));
                err = sd_self_test();
                if (err == ESP_OK) {
                    missing_checks = 0;
                    sd_publish_state(1);
                    vTaskDelay(pdMS_TO_TICKS(SD_READY_POLL_MS));
                    continue;
                }
                esp_err_t cleanup = sd_unmount();
                if (cleanup != ESP_OK) {
                    ESP_LOGW(TAG, "SD cleanup failed: %s", esp_err_to_name(cleanup));
                }
            }
            sd_publish_state(-1);
            ESP_LOGW(TAG, "SD unavailable: %s", esp_err_to_name(err));
            vTaskDelay(pdMS_TO_TICKS(SD_RETRY_MS));
            continue;
        }

        esp_err_t err = sd_check_media();
        if (err == ESP_OK) {
            missing_checks = 0;
        } else if (++missing_checks >= SD_REMOVAL_CONFIRMATIONS) {
            ESP_LOGW(TAG, "SD media lost: %s", esp_err_to_name(err));
            sd_publish_state(-1);
            esp_err_t cleanup = sd_unmount();
            if (cleanup != ESP_OK) {
                ESP_LOGW(TAG, "SD cleanup failed: %s", esp_err_to_name(cleanup));
            }
            missing_checks = 0;
            vTaskDelay(pdMS_TO_TICKS(SD_RETRY_MS));
            continue;
        }
        vTaskDelay(pdMS_TO_TICKS(SD_READY_POLL_MS));
    }
}

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
            uart_link_send(sd_status_message());
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

    // The SD monitor owns mount/retry work; NFC and audio continue independently.
    esp_err_t sd_err = sd_prepare();
    if (sd_err == ESP_OK) {
        if (xTaskCreate(sd_monitor_task, "sd_monitor", 8192, NULL, 4, NULL) != pdPASS) {
            sd_err = ESP_ERR_NO_MEM;
        }
    }
    if (sd_err != ESP_OK) {
        sd_publish_state(-1);
        ESP_LOGE(TAG, "SD monitor unavailable: %s", esp_err_to_name(sd_err));
    }

    ESP_LOGI(TAG, "Audio ready");
    ESP_LOGI(TAG, "Waiting for NFC fob");

    uint8_t uid[7];
    uint8_t uid_length = 0;

    while (1)
    {
        // debuging the sound Oct08:
        ESP_LOGI(TAG, "NFC_DEBUG: scan starting");
        bool card_found = nfc_read_uid(uid, &uid_length, 500);
        ESP_LOGI(TAG, "NFC_DEBUG: scan returned %d", card_found);

        if (card_found)
        // if (nfc_read_uid(uid, &uid_length, 500))
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
