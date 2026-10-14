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

#ifdef __EMSCRIPTEN__
#include "render/audio_input.h"

#include <emscripten.h>
#include <math.h>

static audio *queue;
static audio_input *input;
static bool controlled_clock;
static uint64_t test_ticks;
static double ready_deadline;
static float pcm[2048];

/* Control elapsed audio time without sleeping or changing the production
 * transport. Other selectors continue to use SDL's real clock. */
uint64_t __real_SDL_GetTicks();
uint64_t __wrap_SDL_GetTicks()
{
    return controlled_clock ? test_ticks : __real_SDL_GetTicks();
}

// clang-format off
EM_JS(void, browser_audio_install, (), {
    const saved = [];
    const replace = (object, name, value) => {
        saved.push([object, name, Object.getOwnPropertyDescriptor(object, name)]);
        Object.defineProperty(object, name, { configurable: true, value });
    };
    // Only permission/device endpoints are replaced. Production capture_start,
    // its asynchronous setup and its port handler still process every sample.
    class Context {
        constructor() {
            this.currentTime = 10;
            this.sampleRate = 48000;
            this.state = 'running';
            this.destination = {};
            this.audioWorklet = { addModule: async () => {} };
        }
        resume() { return Promise.resolve(); }
        close() { this.state = 'closed'; return Promise.resolve(); }
        createMediaStreamSource() { return { connect() {}, disconnect() {} }; }
    }
    class Worklet {
        constructor() { this.port = { close() {}, onmessage: null }; }
        connect() {}
        disconnect() {}
    }
    class Track extends EventTarget {
        constructor() { super(); this.readyState = 'live'; }
        stop() { this.readyState = 'ended'; }
    }
    class Stream {
        constructor(tracks) { this.tracks = tracks; }
        getAudioTracks() { return this.tracks; }
        getTracks() { return this.tracks; }
    }
    replace(globalThis, 'AudioContext', Context);
    replace(globalThis, 'AudioWorkletNode', Worklet);
    replace(globalThis, 'MediaStream', Stream);
    replace(navigator, 'userActivation', { isActive: true });
    replace(navigator.mediaDevices, 'getDisplayMedia', async () => new Stream([new Track()]));
    Module['chibiAudioTestRestore'] = () => {
        for (const [object, name, descriptor] of saved.reverse()) {
            if (descriptor) Object.defineProperty(object, name, descriptor);
            else delete object[name];
        }
    };
})

EM_JS(void, browser_audio_deliver, (const float *pcm, int count, int age_ms), {
    const capture = Module['chibiAudioCapture'];
    capture.node.port.onmessage({ data: {
        samples: HEAPF32.slice(pcm >>> 2, (pcm >>> 2) + count),
        channels: 1,
        endTime: capture.context.currentTime - age_ms / 1000,
    } });
})

EM_JS(void, browser_audio_report, (int status), {
    Module['chibiAudioTestRestore']();
    Module['chibiAudioTestResult'](status);
})
// clang-format on

TEST_CASE(delayed_browser_pcm_expires_from_its_production_time)
{
    REQUIRE(audio_get_analysis(queue).status == AUDIO_READY);
    test_ticks = 1000;
    browser_audio_deliver(pcm, 2048, 220);
    audio_input_update(input, queue, true);
    audio_measurement measured = audio_get_analysis(queue);
    REQUIRE(measured.active && measured.level > 0.1f);
    test_ticks += 31;
    audio_input_update(input, queue, true);
    measured = audio_get_analysis(queue);
    REQUIRE(!measured.active && measured.level == 0 && measured.onset == 0);
    return true;
}

TEST_CASE(delayed_browser_pcm_expires_while_queued_for_the_next_tick)
{
    REQUIRE(audio_get_analysis(queue).status == AUDIO_READY);
    test_ticks = 2000;
    audio_input_update(input, queue, true);
    browser_audio_deliver(pcm, 2048, 220);
    test_ticks += 31;
    audio_input_update(input, queue, true);
    audio_measurement measured = audio_get_analysis(queue);
    REQUIRE(!measured.active && measured.level == 0 && measured.bass == 0);
    REQUIRE(measured.mid == 0 && measured.treble == 0 && measured.onset == 0);
    return true;
}

static void browser_audio_ready(void *)
{
    audio_input_update(input, queue, true);
    if (audio_get_analysis(queue).status == AUDIO_WAITING &&
        emscripten_get_now() < ready_deadline) {
        emscripten_async_call(browser_audio_ready, NULL, 1);
        return;
    }
    RUN_TEST_CASE(delayed_browser_pcm_expires_from_its_production_time);
    RUN_TEST_CASE(delayed_browser_pcm_expires_while_queued_for_the_next_tick);
    audio_input_free(input);
    audio_free(queue);
    controlled_clock = false;
    pu_print_stats();
    browser_audio_report(pu_test_failed() ? 1 : 0);
}

int test_web_audio_start()
{
    controlled_clock = true;
    test_ticks = 1000;
    for (int i = 0; i < 2048; i++) {
        pcm[i] = 0.8f * sinf(6.283185307179586f * 1000 * i / 48000);
    }
    browser_audio_install();
    queue = audio_new();
    input = audio_input_new();
    pin_value source = { .type = PIN_I32, .i = 1 };
    assert(!AUDIO_SOURCE_CALL.invoke(queue, &source, NULL));
    ready_deadline = emscripten_get_now() + 2000;
    audio_input_update(input, queue, true);
    emscripten_async_call(browser_audio_ready, NULL, 0);
    return 0;
}
#else
#include "test/tools/browser.h"
#include "test/tools/fixture.h"

#include <limits.h>
#include <stdio.h>
#include <yyjson.h>

static audio *queue;
static render_audio *output;
static audio *replacement_queue;
static render_audio *replacement_output;

static void setup()
{
    queue = audio_new();
    output = render_audio_new();
}
static void teardown()
{
    if (replacement_output) {
        render_audio_free(replacement_output);
        audio_free(replacement_queue);
        replacement_output = NULL;
        replacement_queue = NULL;
    }
    if (output) {
        render_audio_free(output);
    }
    audio_free(queue);
}
TEST_CASE(audio_output_measures_the_real_mixer_and_resets_across_sources)
{
    REQUIRE(audio_tone(
        queue,
        (tone){ .frequency = 100, .end_frequency = 100, .duration = 1, .volume = 0.8f, .pan = 0.5f }
    ));
    audio_measurement frame = { 0 };
    uint64_t deadline = SDL_GetTicks() + 2000;
    while (SDL_GetTicks() < deadline) {
        render_audio_update(output, queue, true);
        frame = audio_get_analysis(queue);
        if (frame.level > 0.03f) {
            break;
        }
        SDL_Delay(5);
    }
    REQUIRE(frame.active && frame.status == AUDIO_READY && frame.source == 0);
    REQUIRE(frame.level > 0.03f && frame.bass > frame.treble * 4);
    render_audio_update(output, queue, false);
    frame = audio_get_analysis(queue);
    REQUIRE(!frame.active && !frame.level && !frame.onset);
    pin_value source = { .type = PIN_I32, .i = 1 };
    REQUIRE(!AUDIO_SOURCE_CALL.invoke(queue, &source, NULL));
    render_audio_update(output, queue, true);
    frame = audio_get_analysis(queue);
    REQUIRE(frame.source == 1 && frame.status == AUDIO_UNAVAILABLE);
    REQUIRE(!frame.active && !frame.level && !frame.bass && !frame.onset);
    source.i = 0;
    REQUIRE(!AUDIO_SOURCE_CALL.invoke(queue, &source, NULL));
    render_audio_update(output, queue, true);
    REQUIRE(audio_get_analysis(queue).source == 0);
    /* Only the dummy driver is running. Its fake recording device checks
     * disconnect handling without opening a microphone or loopback input. */
    int count;
    SDL_AudioDeviceID *devices = SDL_GetAudioRecordingDevices(&count);
    REQUIRE(count > 0);
    SDL_AudioDeviceID device = devices[0];
    SDL_free(devices);
    SDL_setenv_unsafe("CHIBI_AUDIO_INPUT", SDL_GetAudioDeviceName(device), 1);
    source.i = 1;
    REQUIRE(!AUDIO_SOURCE_CALL.invoke(queue, &source, NULL));
    render_audio_update(output, queue, true);
    REQUIRE(audio_get_analysis(queue).status == AUDIO_READY);
    SDL_Event removed = { .adevice = { .type = SDL_EVENT_AUDIO_DEVICE_REMOVED,
                                       .which = device,
                                       .recording = true } };
    REQUIRE(SDL_PushEvent(&removed));
    render_audio_update(output, queue, true);
    frame = audio_get_analysis(queue);
    REQUIRE(frame.status == AUDIO_ENDED && !frame.active && !frame.level);
    audio_reset(queue);
    render_audio_update(output, queue, false);
    frame = audio_get_analysis(queue);
    REQUIRE(!frame.active && !frame.level && !frame.onset);
    render_audio_update(output, NULL, false);
    /* Replacing an observer transfers the single mixer callback. Destroying
     * its previous owner must not unregister the replacement's callback. */
    render_audio_update(output, queue, true);
    replacement_queue = audio_new();
    replacement_output = render_audio_new();
    REQUIRE(audio_tone(
        replacement_queue,
        (tone){ .frequency = 100, .end_frequency = 100, .duration = 1, .volume = 0.8f, .pan = 0.5f }
    ));
    render_audio_update(replacement_output, replacement_queue, true);
    render_audio_update(output, queue, true);
    REQUIRE(audio_get_analysis(queue).status == AUDIO_ENDED);
    render_audio_free(output);
    output = NULL;
    render_audio_update(replacement_output, replacement_queue, false);
    frame = audio_get_analysis(replacement_queue);
    REQUIRE(!frame.active && !frame.level);
    while (SDL_GetTicks() < deadline) {
        render_audio_update(replacement_output, replacement_queue, true);
        frame = audio_get_analysis(replacement_queue);
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

/* The page boots the same Wasm C dispatcher. It only transports its output;
 * the production-age assertions execute in C beside the real audio bridge. */
static const char *browser_page =
    "<!doctype html><meta charset=utf-8><title>Audio transport</title><script>"
    "let output='',errors='',finished=false;"
    "const append=(text,error)=>{text=String(text)+'\\n';"
    "if(output.length+errors.length+text.length>16384)return;"
    "if(error)errors+=text;else output+=text;};"
    "const finish=status=>{if(finished)return;finished=true;"
    "fetch('/"
    "result',{method:'POST',body:JSON.stringify({status,output,errors})});};"
    "window.Module={arguments:['--audio-browser'],"
    "print:text=>append(text,false),printErr:text=>append(text,true),"
    "chibiAudioTestResult:finish,onAbort:reason=>{append(reason,true);finish("
    "255);}};"
    "window.addEventListener('error',event=>{append(event.message,true);finish("
    "255);});"
    "window.addEventListener('unhandledrejection',event=>{append(event.reason,"
    "true);finish(255);});"
    "const script=document.createElement('script');script.src='/test_tools.js';"
    "script.onerror=()=>{append('driver failed to load',true);finish(255);};"
    "document.head.append(script);</script>";

static yyjson_doc *browser_reply;

static void browser_cleanup()
{
    yyjson_doc_free(browser_reply);
    browser_reply = NULL;
}

TEST_CASE(browser_preserves_pcm_production_age)
{
    char candidate[PATH_MAX], root[PATH_MAX];
    snprintf(candidate, sizeof candidate, "%s/build-wasm-tools", tool_root());
    const char *selected = getenv("CHIBI_AUDIO_WEB_TEST_ROOT");
    if (!selected) {
        selected = candidate;
    }
    REQUIRE(selected[0] == '/' && realpath(selected, root));
    static const char *files[] = { "test_tools.js",
                                   "test_tools.wasm",
                                   "test_tools.worker.js",
                                   NULL };
    char result[32768] = { 0 };
    REQUIRE(tool_browser(root, browser_page, files, result, sizeof result));
    browser_reply = yyjson_read(result, strlen(result), 0);
    REQUIRE(browser_reply != NULL);
    yyjson_val *record = yyjson_doc_get_root(browser_reply);
    const char *output = yyjson_get_str(yyjson_obj_get(record, "output"));
    const char *errors = yyjson_get_str(yyjson_obj_get(record, "errors"));
    REQUIRE(output && errors);
    fputs(output, stdout);
    fputs(errors, stderr);
    yyjson_val *status = yyjson_obj_get(record, "status");
    REQUIRE(yyjson_is_int(status) && yyjson_get_int(status) == 0);
    const char *summary = strstr(output, "Summary: Passed:");
    unsigned passed, failed;
    REQUIRE(summary != NULL);
    REQUIRE(sscanf(summary, "Summary: Passed: %u Failed: %u", &passed, &failed) == 2);
    REQUIRE(passed == 2 && failed == 0);
    return true;
}

void test_tool_audio_browser()
{
    pu_setup(NULL, browser_cleanup);
    RUN_TEST_CASE(browser_preserves_pcm_production_age);
    pu_clear_setup();
}
#endif
