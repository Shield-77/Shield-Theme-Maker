#ifndef SHIELD_AUDIO_CONVERT_H
#define SHIELD_AUDIO_CONVERT_H

#include <stdbool.h>

/* Convert Ogg Vorbis or MP3 to a looping, stereo PCM16 BCSTM on the console.
   The output is capped to 44 seconds so it fits the Home Menu BGM limit. */
bool convert_audio_to_bcstm(const char *source, const char *destination);

#endif
