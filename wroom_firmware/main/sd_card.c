#include "sd_card.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "driver/spi_common.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "sdmmc_cmd.h"

#define SD_MISO_GPIO 19
#define SD_MOSI_GPIO 23
#define SD_CLK_GPIO 18
#define SD_CS_GPIO 5
#define SD_MOUNT_POINT "/sdcard"
#define SD_PROBE_A "catan_sd_probe_a.bin"
#define SD_PROBE_B "catan_sd_probe_b.bin"

static const char *TAG = "CATAN_SD";
static sdmmc_card_t *card;
static bool mounted;
static bool verified;
static bool bus_initialized;
static StaticSemaphore_t sd_mutex_storage;
static SemaphoreHandle_t sd_mutex;

esp_err_t sd_prepare(void)
{
    // Called once from app_main before any task uses SD storage.
    if (sd_mutex == NULL) {
        sd_mutex = xSemaphoreCreateMutexStatic(&sd_mutex_storage);
    }
    return sd_mutex != NULL ? ESP_OK : ESP_ERR_NO_MEM;
}

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t sequence;
    uint32_t checksum;
} sd_probe_t;

static uint32_t probe_checksum(const sd_probe_t *probe)
{
    const uint8_t *bytes = (const uint8_t *)probe;
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < offsetof(sd_probe_t, checksum); ++i) {
        hash = (hash ^ bytes[i]) * 16777619u;
    }
    return hash;
}

static esp_err_t file_path(const char *name, char path[64])
{
    if (name == NULL || name[0] == '\0' || name[0] == '.') {
        return ESP_ERR_INVALID_ARG;
    }
    size_t length = 0;
    for (const char *p = name; *p != '\0'; ++p) {
        const char c = *p;
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.')) {
            return ESP_ERR_INVALID_ARG;
        }
        if (++length > 40) {
            return ESP_ERR_INVALID_ARG;
        }
    }
    int written = snprintf(path, 64, SD_MOUNT_POINT "/%s", name);
    return written > 0 && written < 64 ? ESP_OK : ESP_ERR_INVALID_ARG;
}

bool sd_is_ready(void)
{
    if (sd_mutex == NULL) {
        return false;
    }
    xSemaphoreTake(sd_mutex, portMAX_DELAY);
    bool ready = mounted && verified;
    xSemaphoreGive(sd_mutex);
    return ready;
}

static esp_err_t sd_init_unlocked(void)
{
    if (mounted) {
        return ESP_OK;
    }

    const spi_bus_config_t bus_config = {
        .mosi_io_num = SD_MOSI_GPIO,
        .miso_io_num = SD_MISO_GPIO,
        .sclk_io_num = SD_CLK_GPIO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };
    ESP_LOGI(TAG, "SPI2 CLK GPIO%d, DI GPIO%d, DO GPIO%d, CS GPIO%d",
             SD_CLK_GPIO, SD_MOSI_GPIO, SD_MISO_GPIO, SD_CS_GPIO);
    esp_err_t err = ESP_OK;
    if (!bus_initialized) {
        err = spi_bus_initialize(SPI2_HOST, &bus_config, SPI_DMA_CH_AUTO);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "SPI bus initialization failed: %s", esp_err_to_name(err));
            return err;
        }
        bus_initialized = true;
    }

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI2_HOST;
    sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot.gpio_cs = SD_CS_GPIO;
    slot.host_id = SPI2_HOST;
    const esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
    };
    err = esp_vfs_fat_sdspi_mount(SD_MOUNT_POINT, &host, &slot,
                                 &mount_config, &card);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SD mount failed: %s", esp_err_to_name(err));
        card = NULL;
        esp_err_t cleanup = spi_bus_free(SPI2_HOST);
        if (cleanup == ESP_OK) {
            bus_initialized = false;
        } else {
            ESP_LOGW(TAG, "SPI bus cleanup failed: %s", esp_err_to_name(cleanup));
        }
        return err;
    }
    mounted = true;
    verified = false;
    ESP_LOGI(TAG, "SD mounted");
    return ESP_OK;
}

static esp_err_t sd_check_media_unlocked(void)
{
    if (!mounted || card == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return sdmmc_get_status(card);
}

static esp_err_t sd_unmount_unlocked(void)
{
    esp_err_t result = ESP_OK;
    if (mounted) {
        result = esp_vfs_fat_sdcard_unmount(SD_MOUNT_POINT, card);
        if (result != ESP_OK) {
            ESP_LOGW(TAG, "SD unmount failed: %s", esp_err_to_name(result));
        }
        mounted = false;
        card = NULL;
    }
    verified = false;
    if (bus_initialized) {
        esp_err_t cleanup = spi_bus_free(SPI2_HOST);
        if (cleanup == ESP_OK) {
            bus_initialized = false;
        } else {
            ESP_LOGW(TAG, "SPI bus cleanup failed: %s", esp_err_to_name(cleanup));
            if (result == ESP_OK) {
                result = cleanup;
            }
        }
    }
    return result;
}

static esp_err_t sd_write_file_unlocked(const char *name, const void *data, size_t length)
{
    if (!mounted) {
        return ESP_ERR_INVALID_STATE;
    }
    if (length != 0 && data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    char path[64];
    esp_err_t err = file_path(name, path);
    if (err != ESP_OK) {
        return err;
    }
    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        ESP_LOGE(TAG, "Open for write failed (%s): errno %d", name, errno);
        return ESP_FAIL;
    }
    err = ESP_OK;
    if (length != 0 && fwrite(data, 1, length, file) != length) {
        err = ESP_FAIL;
    }
    if (err == ESP_OK && fflush(file) != 0) {
        err = ESP_FAIL;
    }
    if (err == ESP_OK && fsync(fileno(file)) != 0) {
        err = ESP_FAIL;
    }
    if (fclose(file) != 0) {
        err = ESP_FAIL;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Write/sync failed (%s): errno %d", name, errno);
    }
    return err;
}

static esp_err_t sd_read_file_unlocked(const char *name, void *data, size_t capacity, size_t *length)
{
    if (length != NULL) {
        *length = 0;
    }
    if (!mounted) {
        return ESP_ERR_INVALID_STATE;
    }
    if (length == NULL || (capacity != 0 && data == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }
    char path[64];
    esp_err_t err = file_path(name, path);
    if (err != ESP_OK) {
        return err;
    }
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return errno == ENOENT ? ESP_ERR_NOT_FOUND : ESP_FAIL;
    }
    err = ESP_OK;
    if (fseek(file, 0, SEEK_END) != 0) {
        err = ESP_FAIL;
    }
    long size = err == ESP_OK ? ftell(file) : -1;
    if (size < 0) {
        err = ESP_FAIL;
    } else if ((size_t)size > capacity) {
        err = ESP_ERR_INVALID_SIZE;
    }
    if (err == ESP_OK && fseek(file, 0, SEEK_SET) != 0) {
        err = ESP_FAIL;
    }
    if (err == ESP_OK && size != 0 && fread(data, 1, (size_t)size, file) != (size_t)size) {
        err = ESP_FAIL;
    }
    if (fclose(file) != 0) {
        err = ESP_FAIL;
    }
    if (err == ESP_OK) {
        *length = (size_t)size;
    }
    return err;
}

static esp_err_t sd_self_test_unlocked(void)
{
    if (!mounted) {
        return ESP_ERR_INVALID_STATE;
    }
    verified = false;
    const char *const files[2] = {SD_PROBE_A, SD_PROBE_B};
    uint32_t last_sequence = 0;
    int last_slot = -1;
    bool found_file = false;
    size_t length = 0;
    for (int slot = 0; slot < 2; ++slot) {
        sd_probe_t previous = {0};
        esp_err_t err = sd_read_file_unlocked(files[slot], &previous, sizeof(previous), &length);
        if (err == ESP_ERR_NOT_FOUND) {
            continue;
        }
        found_file = true;
        if (err != ESP_OK && err != ESP_ERR_INVALID_SIZE) {
            ESP_LOGE(TAG, "Reading SD test record failed: %s", esp_err_to_name(err));
            return err;
        }
        if (err != ESP_OK || length != sizeof(previous) ||
            previous.magic != 0x43415453u || previous.version != 1 ||
            previous.sequence == 0 || previous.checksum != probe_checksum(&previous)) {
            ESP_LOGW(TAG, "Ignoring invalid SD test record in %s", files[slot]);
            continue;
        }
        if (last_slot < 0 || previous.sequence > last_sequence) {
            last_sequence = previous.sequence;
            last_slot = slot;
        }
    }
    if (found_file && last_slot < 0) {
        ESP_LOGE(TAG, "No valid SD test record; leaving both files untouched");
        return ESP_ERR_INVALID_RESPONSE;
    }
    if (last_slot >= 0) {
        ESP_LOGI(TAG, "Previous boot's SD test record verified: %lu",
                 (unsigned long)last_sequence);
    }

    sd_probe_t next = {
        .magic = 0x43415453u,
        .version = 1,
        .sequence = last_sequence == UINT32_MAX ? 1 : last_sequence + 1,
    };
    next.checksum = probe_checksum(&next);
    const char *target = files[last_slot == 0 ? 1 : 0];
    esp_err_t err = sd_write_file_unlocked(target, &next, sizeof(next));
    if (err != ESP_OK) {
        return err;
    }
    sd_probe_t reread = {0};
    err = sd_read_file_unlocked(target, &reread, sizeof(reread), &length);
    if (err != ESP_OK || length != sizeof(reread) ||
        memcmp(&reread, &next, sizeof(next)) != 0) {
        ESP_LOGE(TAG, "SD read-back verification failed");
        return err == ESP_OK ? ESP_ERR_INVALID_RESPONSE : err;
    }
    ESP_LOGI(TAG, "SD write/read/sync verified: test record %lu",
             (unsigned long)next.sequence);
    verified = true;
    return ESP_OK;
}

static esp_err_t sd_take_lock(void)
{
    if (sd_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return xSemaphoreTake(sd_mutex, portMAX_DELAY) == pdTRUE ? ESP_OK : ESP_FAIL;
}

esp_err_t sd_init(void)
{
    esp_err_t err = sd_take_lock();
    if (err != ESP_OK) {
        return err;
    }
    err = sd_init_unlocked();
    xSemaphoreGive(sd_mutex);
    return err;
}

esp_err_t sd_check_media(void)
{
    esp_err_t err = sd_take_lock();
    if (err != ESP_OK) {
        return err;
    }
    err = sd_check_media_unlocked();
    xSemaphoreGive(sd_mutex);
    return err;
}

esp_err_t sd_unmount(void)
{
    esp_err_t err = sd_take_lock();
    if (err != ESP_OK) {
        return err;
    }
    err = sd_unmount_unlocked();
    xSemaphoreGive(sd_mutex);
    return err;
}

esp_err_t sd_write_file(const char *name, const void *data, size_t length)
{
    esp_err_t err = sd_take_lock();
    if (err != ESP_OK) {
        return err;
    }
    err = sd_write_file_unlocked(name, data, length);
    xSemaphoreGive(sd_mutex);
    return err;
}

esp_err_t sd_read_file(const char *name, void *data, size_t capacity, size_t *length)
{
    if (length != NULL) {
        *length = 0;
    }
    esp_err_t err = sd_take_lock();
    if (err != ESP_OK) {
        return err;
    }
    err = sd_read_file_unlocked(name, data, capacity, length);
    xSemaphoreGive(sd_mutex);
    return err;
}

esp_err_t sd_self_test(void)
{
    esp_err_t err = sd_take_lock();
    if (err != ESP_OK) {
        return err;
    }
    err = sd_self_test_unlocked();
    xSemaphoreGive(sd_mutex);
    return err;
}
