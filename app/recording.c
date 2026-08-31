#include "recording.h"

#include <stdio.h>
#include <string.h>

static int recording_fail(app_recording *recording, const char *message)
{
    if (recording->encoder) {
        cf_destroy_video_encoder(recording->encoder);
        recording->encoder = NULL;
    }
    snprintf(recording->error, sizeof recording->error, "%s", message);
    recording->state = APP_RECORDING_FAILED;
    return -1;
}

int app_recording_start(app_recording *recording, const char *path, int width, int height, int fps)
{
    if (!recording || recording->state == APP_RECORDING_ACTIVE) {
        return -1;
    }
    *recording = (app_recording){ 0 };
    if (!path || !path[0]) {
        return recording_fail(recording, "recording output path is empty");
    }
    if (width <= 0 || height <= 0 || (width & 1) || (height & 1) || fps <= 0) {
        return recording_fail(recording, "recording dimensions must be positive and even");
    }
    recording->encoder = cf_make_video_encoder(width, height, fps);
    if (!recording->encoder) {
        return recording_fail(recording, "could not create video encoder");
    }
    recording->path = path;
    recording->width = width;
    recording->height = height;
    recording->state = APP_RECORDING_ACTIVE;
    return 0;
}

int app_recording_capture(app_recording *recording, CF_Canvas canvas, float dt)
{
    if (!recording || recording->state != APP_RECORDING_ACTIVE) {
        return -1;
    }
    if (!canvas.id) {
        return recording_fail(recording, "recording canvas is invalid");
    }
    int captured = cf_video_encoder_update(recording->encoder, canvas, dt);
    if (captured < 0) {
        return recording_fail(recording, "video encoder rejected a frame");
    }
    recording->frames += captured;
    return 0;
}

int app_recording_stop(app_recording *recording)
{
    if (!recording || recording->state != APP_RECORDING_ACTIVE) {
        return -1;
    }
    if (recording->frames == 0) {
        cf_destroy_video_encoder(recording->encoder);
        recording->encoder = NULL;
        return recording_fail(recording, "recording contains no frames");
    }
    CF_Result result = cf_video_encoder_save(recording->encoder, recording->path);
    cf_destroy_video_encoder(recording->encoder);
    recording->encoder = NULL;
    if (cf_is_error(result)) {
        return recording_fail(recording, result.details ? result.details : "could not save recording");
    }
    recording->state = APP_RECORDING_IDLE;
    return 0;
}

app_recording_state app_recording_get_state(const app_recording *recording)
{
    return recording ? recording->state : APP_RECORDING_FAILED;
}

int app_recording_frame_count(const app_recording *recording)
{
    return recording ? recording->frames : 0;
}

const char *app_recording_error(const app_recording *recording)
{
    return recording && recording->error[0] ? recording->error : "";
}
