#include "audio.h"
#include <stdio.h>
#include <stdint.h>

#include "driver/dac_continuous.h"
#include "esp_log.h"
#include "esp_timer.h"

#define SAMPLE_RATE 44100

static dac_continuous_handle_t dac_handle;
static const char *TAG = "CATAN_AUDIO";

void audio_init(void)
{
    dac_continuous_config_t config = {
        .chan_mask = DAC_CHANNEL_MASK_CH0,   // GPIO25
        .desc_num = 8,
        .buf_size = 1024,
        .freq_hz = SAMPLE_RATE,
        .offset = 0,
        .clk_src = DAC_DIGI_CLK_SRC_DEFAULT,
        .chan_mode = DAC_CHANNEL_MODE_SIMUL,
    };

    ESP_ERROR_CHECK(
        dac_continuous_new_channels(&config, &dac_handle)
    );

    ESP_ERROR_CHECK(
        dac_continuous_enable(dac_handle)
    );
    ESP_LOGI(TAG, "DAC ready: GPIO25, %d Hz, unsigned 8-bit PCM", SAMPLE_RATE);
}


void play_sound(const uint8_t *start, const uint8_t *end)
{
    if (start == NULL || end == NULL || end <= start) {
        ESP_LOGE(TAG, "Invalid sound buffer");
        return;
    }
    size_t sound_size = end - start;
    size_t bytes_written = 0;
    const int timeout_ms = (sound_size * 1000 / SAMPLE_RATE) + 1000;
    const int64_t started_us = esp_timer_get_time();
    ESP_LOGI(TAG, "Playing %u bytes on GPIO25", (unsigned)sound_size);

    esp_err_t err = dac_continuous_write(
            dac_handle,
            (uint8_t *)start,
            sound_size,
            &bytes_written,
            timeout_ms
        );
    if (err != ESP_OK || bytes_written != sound_size) {
        ESP_LOGE(TAG, "DAC write failed: %s, %u/%u bytes queued",
                 esp_err_to_name(err), (unsigned)bytes_written, (unsigned)sound_size);
        return;
    }
    ESP_LOGI(TAG, "DAC queued %u bytes in %lld ms",
             (unsigned)bytes_written, (esp_timer_get_time() - started_us) / 1000);
}
