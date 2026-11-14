#ifndef SHIELD_BCSTM_VALIDATE_H
#define SHIELD_BCSTM_VALIDATE_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t sample_rate, loop_start, loop_end, file_size;
    uint8_t encoding, channels, looping;
} BcstmInfo;

/* Bounds-checks every referenced block and the stream's sample region. */
bool bcstm_validate(const char *path, BcstmInfo *result);

#endif
