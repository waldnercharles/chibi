#ifndef CHIBI_RENDER_AUDIO_H
#define CHIBI_RENDER_AUDIO_H
#include "runtime/audio.h"
typedef struct render_audio render_audio;
render_audio *render_audio_new();
void render_audio_free(render_audio *renderer);
/* Plays a session's requests through Cute; a NULL queue ends the session. */
void render_audio_update(render_audio *renderer, audio *queue, bool playing);
#endif
