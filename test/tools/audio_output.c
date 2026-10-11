#include "platform/cute.h"
#include "render/audio.h"
#include "third_party/pico_headers/pico_unit.h"

#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_timer.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>

static game_audio *queue;
static render_audio *output;
static game_audio *replacement_queue;
static render_audio *replacement_output;

static void setup()
{
    queue = game_audio_new();
    output = render_audio_new();
}
static void teardown()
{
    if (replacement_output) {
        render_audio_free(replacement_output);
        game_audio_free(replacement_queue);
        replacement_output = NULL;
        replacement_queue = NULL;
    }
    if (output) {
        render_audio_free(output);
    }
    game_audio_free(queue);
}
TEST_CASE(audio_output_measures_the_real_mixer_and_resets_across_sources)
{
    REQUIRE(game_audio_tone(
        queue,
        (game_tone){ .frequency = 100, .end_frequency = 100, .duration = 1, .volume = 0.8f, .pan = 0.5f }
    ));
    game_audio_analysis frame = { 0 };
    uint64_t deadline = SDL_GetTicks() + 2000;
    while (SDL_GetTicks() < deadline) {
        render_audio_update(output, queue, true);
        frame = game_audio_get_analysis(queue);
        if (frame.level > 0.03f) {
            break;
        }
        SDL_Delay(5);
    }
    REQUIRE(frame.active && frame.status == GAME_AUDIO_READY && frame.source == 0);
    REQUIRE(frame.level > 0.03f && frame.bass > frame.treble * 4);
    render_audio_update(output, queue, false);
    frame = game_audio_get_analysis(queue);
    REQUIRE(!frame.active && !frame.level && !frame.onset);
    pin_value source = { .type = PIN_I32, .i = 1 };
    REQUIRE(!GAME_AUDIO_SOURCE_CALL.invoke(queue, &source, NULL));
    render_audio_update(output, queue, true);
    frame = game_audio_get_analysis(queue);
    REQUIRE(frame.source == 1 && frame.status == GAME_AUDIO_UNAVAILABLE);
    REQUIRE(!frame.active && !frame.level && !frame.bass && !frame.onset);
    source.i = 0;
    REQUIRE(!GAME_AUDIO_SOURCE_CALL.invoke(queue, &source, NULL));
    render_audio_update(output, queue, true);
    REQUIRE(game_audio_get_analysis(queue).source == 0);
    /* Only the dummy driver is running. Its fake recording device checks
     * disconnect handling without opening a microphone or loopback input. */
    int count;
    SDL_AudioDeviceID *devices = SDL_GetAudioRecordingDevices(&count);
    REQUIRE(count > 0);
    SDL_AudioDeviceID device = devices[0];
    SDL_free(devices);
    SDL_setenv_unsafe("CHIBI_AUDIO_INPUT", SDL_GetAudioDeviceName(device), 1);
    source.i = 1;
    REQUIRE(!GAME_AUDIO_SOURCE_CALL.invoke(queue, &source, NULL));
    render_audio_update(output, queue, true);
    REQUIRE(game_audio_get_analysis(queue).status == GAME_AUDIO_READY);
    SDL_Event removed = { .adevice = { .type = SDL_EVENT_AUDIO_DEVICE_REMOVED,
                                       .which = device,
                                       .recording = true } };
    REQUIRE(SDL_PushEvent(&removed));
    render_audio_update(output, queue, true);
    frame = game_audio_get_analysis(queue);
    REQUIRE(frame.status == GAME_AUDIO_ENDED && !frame.active && !frame.level);
    game_audio_reset(queue);
    render_audio_update(output, queue, false);
    frame = game_audio_get_analysis(queue);
    REQUIRE(!frame.active && !frame.level && !frame.onset);
    render_audio_update(output, NULL, false);
    /* Replacing an observer transfers the single mixer callback. Destroying
     * its previous owner must not unregister the replacement's callback. */
    render_audio_update(output, queue, true);
    replacement_queue = game_audio_new();
    replacement_output = render_audio_new();
    REQUIRE(game_audio_tone(
        replacement_queue,
        (game_tone){ .frequency = 100, .end_frequency = 100, .duration = 1, .volume = 0.8f, .pan = 0.5f }
    ));
    render_audio_update(replacement_output, replacement_queue, true);
    render_audio_update(output, queue, true);
    REQUIRE(game_audio_get_analysis(queue).status == GAME_AUDIO_ENDED);
    render_audio_free(output);
    output = NULL;
    render_audio_update(replacement_output, replacement_queue, false);
    frame = game_audio_get_analysis(replacement_queue);
    REQUIRE(!frame.active && !frame.level);
    while (SDL_GetTicks() < deadline) {
        render_audio_update(replacement_output, replacement_queue, true);
        frame = game_audio_get_analysis(replacement_queue);
        if (frame.level > 0.03f) {
            break;
        }
        SDL_Delay(5);
    }
    REQUIRE(frame.active && frame.level > 0.03f);
    return true;
}
static void restore_hint(const char *name, char *value)
{
    if (value) {
        SDL_SetHint(name, value);
    } else {
        SDL_ResetHint(name);
    }
    SDL_free(value);
}
void test_tool_audio_output()
{
    const char *value = SDL_GetHint(SDL_HINT_AUDIO_DRIVER);
    char *audio_driver = value ? SDL_strdup(value) : NULL;
    value = SDL_GetHint(SDL_HINT_VIDEO_DRIVER);
    char *video_driver = value ? SDL_strdup(value) : NULL;
    value = getenv("CHIBI_AUDIO_INPUT");
    char *device = value ? SDL_strdup(value) : NULL;
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    SDL_setenv_unsafe("CHIBI_AUDIO_INPUT", "__chibi_missing_test_input__", 1);
    CF_Result made = cf_make_app("audio-output", 0, 0, 0, 64, 64, CF_APP_OPTIONS_NO_GFX_BIT | CF_APP_OPTIONS_HIDDEN_BIT, NULL);
    assert(!cf_is_error(made));
    assert(!strcmp(SDL_GetCurrentAudioDriver(), "dummy"));
    pu_setup(setup, teardown);
    RUN_TEST_CASE(audio_output_measures_the_real_mixer_and_resets_across_sources);
    pu_clear_setup();
    cf_destroy_app();
    restore_hint(SDL_HINT_AUDIO_DRIVER, audio_driver);
    restore_hint(SDL_HINT_VIDEO_DRIVER, video_driver);
    if (device) {
        SDL_setenv_unsafe("CHIBI_AUDIO_INPUT", device, 1);
    } else {
        SDL_unsetenv_unsafe("CHIBI_AUDIO_INPUT");
    }
    SDL_free(device);
}
