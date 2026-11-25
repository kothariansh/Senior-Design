#include <stdbool.h>
#include <stdint.h>

#include "esp_log.h"
#include "pn532.h"
#include "nfc.h"

#define PN532_SDA 32
#define PN532_SCL 33

static const char *TAG = "CATAN_NFC";

static pn532_t pn532;

bool nfc_init(void)
{
    ESP_LOGI(TAG, "Starting PN532 NFC");

    ESP_LOGI(TAG, "I2C SDA GPIO%d, SCL GPIO%d, address 0x%02X, 100 kHz",
             PN532_SDA, PN532_SCL, PN532_I2C_ADDRESS);
    esp_err_t err = pn532_i2c_init(&pn532, PN532_SDA, PN532_SCL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "PN532 I2C initialization failed: %s", esp_err_to_name(err));
        return false;
    }

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
