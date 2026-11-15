#ifndef SHIELD_AUDIO_CONVERT_H
#define SHIELD_AUDIO_CONVERT_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t duration_ms;
    bool approximate;
} AudioInfo;

#define AUDIO_QUALITY_COUNT 7
typedef struct {
    uint32_t rate, max_ms;
} AudioProfile;
AudioProfile audio_profile(unsigned quality);

bool audio_probe(const char *source, AudioInfo *info);
/* Convert only [start_ms,end_ms], limited by the selected quality profile, with an optional
   crossfade baked into the PCM data. All output is validated before success. */
bool convert_audio_to_bcstm(const char *source, const char *destination,
                            uint32_t start_ms, uint32_t end_ms, uint32_t fade_ms,
                            bool automatic, unsigned quality,
                            void (*progress)(unsigned percent));
/* Play up to twenty seconds of a PCM BCSTM without decoding the whole file. */
bool audio_preview_bcstm(const char *path, void (*progress)(unsigned percent),
                         bool *cancelled);
/* Stream the selected passage without writing BCSTM. The loop mode applies
   the selected crossfade; Y or B stops playback. */
bool audio_preview_source(const char *source, uint32_t start_ms, uint32_t end_ms,
                          uint32_t fade_ms, bool repeat, unsigned quality,
                          void (*progress)(unsigned percent),
                          bool *cancelled);

#endif
