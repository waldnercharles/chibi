#ifndef CBLUEPRINT_APP_RECORDING_H
#define CBLUEPRINT_APP_RECORDING_H

#include "cute_host.h"

typedef enum
{
    APP_RECORDING_IDLE,
    APP_RECORDING_ACTIVE,
    APP_RECORDING_FAILED,
} app_recording_state;

typedef struct
{
    CF_VideoEncoder *encoder;
    const char *path;
    int width;
    int height;
    int frames;
    float click_timer;
    app_recording_state state;
    char error[256];
} app_recording;

int app_recording_start(app_recording *recording, const char *path, int width, int height, int fps);
int app_recording_capture(app_recording *recording, CF_Canvas canvas, float dt);
int app_recording_stop(app_recording *recording);
app_recording_state app_recording_get_state(const app_recording *recording);
int app_recording_frame_count(const app_recording *recording);
const char *app_recording_error(const app_recording *recording);

#endif
