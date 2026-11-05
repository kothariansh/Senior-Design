#include "uart_link.h"

#include <stdbool.h>
#include <string.h>

#include "driver/uart.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define UART_LINK_PORT UART_NUM_2
#define UART_LINK_TX_GPIO 17
#define UART_LINK_RX_GPIO 16
#define UART_LINK_BAUD 115200

static const char *TAG = "WROOM_UART";
static StaticSemaphore_t tx_mutex_storage;
static SemaphoreHandle_t tx_mutex;

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
    tx_mutex = xSemaphoreCreateMutexStatic(&tx_mutex_storage);
    if (tx_mutex == NULL) {
        ESP_ERROR_CHECK(ESP_ERR_NO_MEM);
    }
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
    if (tx_mutex == NULL || xSemaphoreTake(tx_mutex, portMAX_DELAY) != pdTRUE) {
        ESP_LOGE(TAG, "UART TX mutex unavailable");
        return;
    }
    if (uart_write_bytes(UART_LINK_PORT, message, length) != (int)length) {
        ESP_LOGE(TAG, "UART write failed");
    }
    xSemaphoreGive(tx_mutex);
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
