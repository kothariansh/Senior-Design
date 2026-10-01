#ifndef CATAN_AUDIO_H
#define CATAN_AUDIO_H

#include <stdint.h>

void audio_init(void);

void play_sound(
    const uint8_t *start,
    const uint8_t *end
);

#endif
