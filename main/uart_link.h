#ifndef CATAN_UART_LINK_H
#define CATAN_UART_LINK_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define UART_LINK_LINE_SIZE 128

void uart_link_init(void);
// Send one complete newline-terminated message in a single driver write.
void uart_link_send(const char *message);
// Single-reader API: preserves partial lines across timeouts, strips CRLF,
// and discards oversized/invalid lines through their terminating newline.
// Returns line length, zero on timeout, or -1 on a driver/argument error.
int uart_link_read_line(char *buffer, size_t capacity, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif
