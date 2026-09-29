#include <stdio.h>
#include <stdint.h>

#include "driver/dac_continuous.h"
#include "driver/uart.h"

#include "audio.c"

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

void audio_init(void);
void play_sound(const uint8_t *start, const uint8_t *end);

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