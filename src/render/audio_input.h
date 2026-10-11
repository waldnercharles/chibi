#ifndef CHIBI_RENDER_AUDIO_INPUT_H
#define CHIBI_RENDER_AUDIO_INPUT_H

#include "runtime/audio.h"

typedef struct audio_input audio_input;
audio_input *audio_input_new();
void audio_input_free(audio_input *input);
void audio_input_reset(audio_input *input);
void audio_input_update(audio_input *input, audio *audio, bool playing);

#endif
