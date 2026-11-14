#ifndef SHIELD_AUDIO_CONVERT_H
#define SHIELD_AUDIO_CONVERT_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t duration_ms;
    bool approximate;
} AudioInfo;

bool audio_probe(const char *source, AudioInfo *info);
/* Convert only [start_ms,end_ms], at most 50 seconds, with an optional
   crossfade baked into the PCM data. All output is validated before success. */
bool convert_audio_to_bcstm(const char *source, const char *destination,
                            uint32_t start_ms, uint32_t end_ms, uint32_t fade_ms,
                            bool automatic,
                            void (*progress)(unsigned percent));
/* Plays the actual encoded seam, not a separate approximation. */
bool audio_preview_bcstm(const char *path);
/* Stream the selected passage from the source without writing BCSTM.
   Y or B stops playback; cancelled reports a deliberate stop. */
bool audio_preview_source(const char *source, uint32_t start_ms, uint32_t end_ms,
                          uint32_t fade_ms, void (*progress)(unsigned percent),
                          bool *cancelled);

#endif
