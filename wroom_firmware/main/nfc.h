#ifndef CATAN_NFC_H
#define CATAN_NFC_H

#include <stdbool.h>
#include <stdint.h>

bool nfc_init(void);

bool nfc_read_uid(
    uint8_t *uid,
    uint8_t *uid_length,
    uint16_t timeout_ms
);

#endif
