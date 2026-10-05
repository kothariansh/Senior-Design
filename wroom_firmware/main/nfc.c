#include <stdbool.h>
#include <stdint.h>

#include "esp_log.h"
#include "pn532.h"
#include "nfc.h"

#define PN532_SCK   18
#define PN532_MISO  19
#define PN532_MOSI  23
#define PN532_SS    21

static const char *TAG = "CATAN_NFC";

static pn532_t pn532;

bool nfc_init(void)
{
    ESP_LOGI(TAG, "Starting PN532 NFC");

    pn532_spi_init(
        &pn532,
        PN532_SCK,
        PN532_MISO,
        PN532_MOSI,
        PN532_SS
    );

    pn532_begin(&pn532);

    uint32_t version = pn532_getFirmwareVersion(&pn532);

    if (!version) {
        ESP_LOGE(TAG, "PN532 not detected!");
        return false;
    }

    ESP_LOGI(TAG, "PN532 detected!");

    if (!pn532_SAMConfig(&pn532)) {
        ESP_LOGE(TAG, "SAM configuration failed!");
        return false;
    }

    ESP_LOGI(TAG, "SAM configured successfully");
    ESP_LOGI(TAG, "NFC ready");

    return true;
}

bool nfc_read_uid(
    uint8_t *uid,
    uint8_t *uid_length,
    uint16_t timeout_ms
)
{
    return pn532_readPassiveTargetID(
        &pn532,
        PN532_MIFARE_ISO14443A,
        uid,
        uid_length,
        timeout_ms
    );
}
