#include "uart_link.h"

#include <stdbool.h>
#include <string.h>

#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define UART_LINK_PORT UART_NUM_2
// Waveshare UART2 connector routes GPIO43/44 (select UART2 on the board).
// Keep the console on USB Serial/JTAG so it does not share these pins.
#define UART_LINK_TX_GPIO 43
#define UART_LINK_RX_GPIO 44
#define UART_LINK_BAUD 115200

static const char *TAG = "S3_UART";

void uart_link_init(void)
{
    const uart_config_t config = {
        .baud_rate = UART_LINK_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_driver_install(UART_LINK_PORT, 1024, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(UART_LINK_PORT, &config));
    ESP_ERROR_CHECK(uart_set_pin(UART_LINK_PORT, UART_LINK_TX_GPIO,
                                UART_LINK_RX_GPIO, UART_PIN_NO_CHANGE,
                                UART_PIN_NO_CHANGE));
    ESP_LOGI(TAG, "UART2 TX%d RX%d, %d baud, 8N1", UART_LINK_TX_GPIO,
             UART_LINK_RX_GPIO, UART_LINK_BAUD);
}

void uart_link_send(const char *message)
{
    if (message == NULL) {
        return;
    }
    const size_t length = strlen(message);
    if (length == 0 || message[length - 1] != '\n') {
        ESP_LOGW(TAG, "Ignoring message without newline terminator");
        return;
    }
    if (uart_write_bytes(UART_LINK_PORT, message, length) != (int)length) {
        ESP_LOGE(TAG, "UART write failed");
    }
}

int uart_link_read_line(char *buffer, size_t capacity, uint32_t timeout_ms)
{
    static char line[UART_LINK_LINE_SIZE];
    static size_t used;
    static bool discard;
    if (buffer == NULL || capacity < UART_LINK_LINE_SIZE) {
        return -1;
    }
    const TickType_t start = xTaskGetTickCount();
    const TickType_t timeout = pdMS_TO_TICKS(timeout_ms);
    for (;;) {
        const TickType_t elapsed = xTaskGetTickCount() - start;
        if (elapsed >= timeout) {
            return 0;
        }
        uint8_t byte;
        int count = uart_read_bytes(UART_LINK_PORT, &byte, 1, timeout - elapsed);
        if (count <= 0) {
            return count;
        }
        if (byte == '\n') {
            if (!discard && used > 0) {
                if (line[used - 1] == '\r') {
                    --used;
                }
                memcpy(buffer, line, used);
                buffer[used] = '\0';
                int length = (int)used;
                used = 0;
                if (length > 0) {
                    return length;
                }
            }
            used = 0;
            discard = false;
        } else if (!discard) {
            if (byte == 0 || used == sizeof(line) - 1) {
                ESP_LOGW(TAG, "Discarding invalid or oversized UART line");
                discard = true;
                used = 0;
            } else {
                line[used++] = (char)byte;
            }
        }
    }
}
