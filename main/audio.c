#include <stdio.h>
#include <stdint.h>

#include "driver/dac_continuous.h"

#define SAMPLE_RATE 44100

static dac_continuous_handle_t dac_handle;

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
}


void play_sound(const uint8_t *start, const uint8_t *end)
{
    size_t sound_size = end - start;
    size_t bytes_written = 0;

    ESP_ERROR_CHECK(
        dac_continuous_write(
            dac_handle,
            (uint8_t *)start,
            sound_size,
            &bytes_written,
            -1
        )
    );
}