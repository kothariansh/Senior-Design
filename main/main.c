#include <stdio.h>
#include <stdint.h>

#include "driver/dac_continuous.h"
#include "driver/uart.h"

#define SAMPLE_RATE 44100
#define UART_PORT UART_NUM_0

extern const uint8_t ka_ching_start[]
    asm("_binary_ka_ching_raw_start");
extern const uint8_t ka_ching_end[]
    asm("_binary_ka_ching_raw_end");

extern const uint8_t error_start[]
    asm("_binary_error_raw_start");
extern const uint8_t error_end[]
    asm("_binary_error_raw_end");

extern const uint8_t dice_start[]
    asm("_binary_dice_raw_start");
extern const uint8_t dice_end[]
    asm("_binary_dice_raw_end");

extern const uint8_t build_start[]
    asm("_binary_build_raw_start");
extern const uint8_t build_end[]
    asm("_binary_build_raw_end");


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


void app_main(void)
{
    audio_init();

    ESP_ERROR_CHECK(
        uart_driver_install(
            UART_NUM_0,
            256,
            0,
            0,
            NULL,
            0
        )
    );

    printf("\nCatan Sound Test\n");
    printf("1 = Ka-ching\n");
    printf("2 = Error\n");
    printf("3 = Dice\n");
    printf("4 = Build\n");

    uint8_t c;

    while (1)
    {
        int len = uart_read_bytes(
            UART_PORT,
            &c,
            1,
            pdMS_TO_TICKS(100)
        );

        if (len > 0)
        {
            switch (c)
            {
                case '1':
                    printf("Playing ka-ching\n");
                    play_sound(ka_ching_start, ka_ching_end);
                    break;

                case '2':
                    printf("Playing error\n");
                    play_sound(error_start, error_end);
                    break;

                case '3':
                    printf("Playing dice\n");
                    play_sound(dice_start, dice_end);
                    break;

                case '4':
                    printf("Playing build\n");
                    play_sound(build_start, build_end);
                    break;

                default:
                    break;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}