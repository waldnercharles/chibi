#include "host/player_device.h"

#include "render/device_controls.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>

static CF_V2 neutral;
static int sample_epoch;
static bool calibrated;

// clang-format off
EM_JS(void, web_device_start, (), {
    if (Module['chibiDeviceHost']) return;
    const host = { enabled: false, permitted: false, status: 0, epoch: 0, generation: 0,
                   beta: 0, gamma: 0, angle: 0, sample: 0, started: 0,
                   listening: false, prompt: null, coarse: matchMedia('(pointer: coarse)') };
    Module['chibiDeviceHost'] = host;
    const current = () => Module['chibiDeviceHost'] === host;
    host.foreground = () => !document.hidden && document.hasFocus();
    host.screenAngle = () => Number(screen.orientation?.angle ?? window.orientation ?? 0);
    host.clear = () => {
        host.sample = 0;
        host.started = performance.now();
        ++host.epoch;
        if (host.enabled) host.status = 1;
    };
    host.removePrompt = () => {
        host.prompt?.remove();
        host.prompt = null;
    };
    host.unlisten = () => {
        window.removeEventListener('deviceorientation', host.orientation);
        host.listening = false;
    };
    host.disable = () => {
        ++host.generation;
        host.enabled = false;
        host.permitted = false;
        host.status = 0;
        host.clear();
        host.unlisten();
        host.removePrompt();
    };
    host.orientation = event => {
        if (!host.enabled || !host.foreground() ||
            !Number.isFinite(event.beta) || !Number.isFinite(event.gamma)) return;
        const angle = host.screenAngle();
        if (angle !== host.angle) {
            host.angle = angle;
            host.clear();
        }
        host.beta = event.beta;
        host.gamma = event.gamma;
        host.sample = performance.now();
        host.status = 2;
    };
    host.listen = () => {
        if (!host.enabled || !host.permitted || host.listening || !host.foreground()) return;
        host.clear();
        host.angle = host.screenAngle();
        window.addEventListener('deviceorientation', host.orientation);
        host.listening = true;
    };
    host.lifecycle = event => {
        if ((document.hidden || event.type === 'pagehide') && host.prompt) {
            host.disable();
        }
        host.clear();
        if (!host.foreground()) {
            host.unlisten();
            if (typeof navigator.vibrate === 'function') navigator.vibrate(0);
            return;
        }
        host.listen();
    };
    host.request = () => {
        if (host.enabled && (host.listening || host.prompt)) return;
        host.disable();
        if (!globalThis.isSecureContext || typeof DeviceOrientationEvent === 'undefined') {
            host.status = 3;
            return;
        }
        host.enabled = true;
        host.status = 1;
        if (typeof DeviceOrientationEvent.requestPermission !== 'function') {
            host.permitted = true;
            host.listen();
            return;
        }
        // A simulation tick is not a trusted browser gesture. Use a real
        // button so iOS can ask while its click handler is on the stack.
        const panel = document.createElement('div');
        panel.setAttribute('role', 'dialog');
        panel.setAttribute('aria-label', 'Tilt controls');
        panel.style.cssText = 'position:fixed;z-index:10000;left:50%;top:50%;transform:translate(-50%,-50%);box-sizing:border-box;width:min(320px,calc(100% - 40px));padding:20px;border:1px solid #62706b;border-radius:12px;background:#111c19;color:#edf5ef;font:16px/1.45 system-ui;text-align:center;box-shadow:0 12px 48px #0008';
        const label = document.createElement('p');
        label.textContent = 'Allow motion access to steer by tilting your phone.';
        label.style.margin = '0 0 16px';
        panel.append(label);
        const enable = document.createElement('button');
        enable.textContent = 'Enable tilt';
        enable.style.cssText = 'display:block;width:100%;min-height:48px;border:0;border-radius:7px;background:#e5f3dc;color:#142018;font:inherit;font-weight:600';
        const cancel = document.createElement('button');
        cancel.textContent = 'Not now';
        cancel.style.cssText = 'display:block;width:100%;min-height:48px;margin-top:8px;border:1px solid #62706b;border-radius:7px;background:transparent;color:inherit;font:inherit';
        cancel.onclick = event => {
            if (!event.isTrusted) return;
            host.disable();
            host.status = 4;
            Module['canvas'].focus();
        };
        enable.onclick = event => {
            if (!event.isTrusted) return;
            const generation = host.generation;
            enable.disabled = true;
            let permission;
            try { permission = DeviceOrientationEvent.requestPermission(); }
            catch (_) {
                host.disable();
                host.status = 4;
                Module['canvas'].focus();
                return;
            }
            Promise.resolve(permission).then(result => {
                if (!current() || generation !== host.generation) return;
                host.removePrompt();
                Module['canvas'].focus();
                if (result !== 'granted') {
                    host.disable();
                    host.status = 4;
                    return;
                }
                host.permitted = true;
                host.listen();
            }, () => {
                if (!current() || generation !== host.generation) return;
                host.disable();
                host.status = 4;
                Module['canvas'].focus();
            });
        };
        panel.append(enable, cancel);
        host.prompt = panel;
        document.body.append(panel);
        enable.focus();
    };
    window.addEventListener('blur', host.lifecycle);
    window.addEventListener('focus', host.lifecycle);
    window.addEventListener('pagehide', host.lifecycle);
    window.addEventListener('pageshow', host.lifecycle);
    window.addEventListener('orientationchange', host.lifecycle);
    document.addEventListener('visibilitychange', host.lifecycle);
})

EM_JS(void, web_device_stop, (), {
    const host = Module['chibiDeviceHost'];
    if (!host) return;
    host.disable();
    window.removeEventListener('blur', host.lifecycle);
    window.removeEventListener('focus', host.lifecycle);
    window.removeEventListener('pagehide', host.lifecycle);
    window.removeEventListener('pageshow', host.lifecycle);
    window.removeEventListener('orientationchange', host.lifecycle);
    document.removeEventListener('visibilitychange', host.lifecycle);
    if (typeof navigator.vibrate === 'function') navigator.vibrate(0);
    Module['chibiDeviceHost'] = null;
})

EM_JS(void, web_device_sample, (float *values), {
    const host = Module['chibiDeviceHost'];
    const rect = Module['canvas'].getBoundingClientRect();
    const active = !document.hidden && document.hasFocus();
    const out = values >> 2;
    HEAPF32[out] = rect.width;
    HEAPF32[out + 1] = rect.height;
    HEAPF32[out + 2] = navigator.maxTouchPoints > 0 || !!host?.coarse.matches;
    HEAPF32[out + 3] = active;
    HEAPF32[out + 4] = host?.status ?? 0;
    HEAPF32[out + 5] = typeof navigator.vibrate === 'function';
    if (!host) return;
    const now = performance.now();
    // Never hold an old lean through a stalled sensor stream, screen turn or
    // foreground transition. The next fresh sample establishes a new neutral.
    if (host.sample && (!active || now - host.sample > 500)) host.clear();
    if (host.enabled && host.listening && !host.sample && now - host.started > 2500) host.status = 3;
    HEAPF32[out + 4] = host.status;
    HEAPF32[out + 6] = host.beta;
    HEAPF32[out + 7] = host.gamma;
    HEAPF32[out + 8] = host.angle;
    HEAPF32[out + 9] = host.epoch;
})

EM_JS(void, web_device_submit, (int motion, int disable, int recenter, int vibration), {
    const host = Module['chibiDeviceHost'];
    if (!host) return;
    if (disable) host.disable();
    else if (motion) host.request();
    if (recenter) host.clear();
    if (vibration > 0 && host.foreground() && typeof navigator.vibrate === 'function') {
        navigator.vibrate(vibration);
    }
})
// clang-format on

void player_device_start()
{
    calibrated = false;
    web_device_start();
}

void player_device_stop()
{
    calibrated = false;
    web_device_stop();
}

input_device_state player_device_sample(int, int, bool active)
{
    float values[10] = { 0 };
    web_device_sample(values);
    input_device_state state = { .width = values[0],
                                 .height = values[1],
                                 .touch = values[2] != 0,
                                 .active = active && values[3] != 0,
                                 .motion_status = (int)values[4],
                                 .haptics = values[5] != 0 };
    if (!state.active || state.motion_status != INPUT_MOTION_READY) {
        calibrated = false;
        return state;
    }
    CF_V2 tilt = controls_tilt(values[6], values[7], (int)values[8]);
    int epoch = (int)values[9];
    if (!calibrated || epoch != sample_epoch) {
        neutral = tilt;
        sample_epoch = epoch;
        calibrated = true;
    }
    state.tilt = (CF_V2){ tilt.x - neutral.x, tilt.y - neutral.y };
    return state;
}

void player_device_submit(input_device_requests requests, bool active)
{
    web_device_submit(
        requests.motion && active,
        requests.disable_motion,
        requests.recenter && active,
        active ? requests.vibration_ms : 0
    );
}
#else
static int motion_status;

void player_device_start()
{
    motion_status = INPUT_MOTION_DISABLED;
}

void player_device_stop() {}

input_device_state player_device_sample(int width, int height, bool active)
{
    return (input_device_state){ .width = width,
                                 .height = height,
                                 .active = active,
                                 .motion_status = motion_status };
}

void player_device_submit(input_device_requests requests, bool)
{
    if (requests.disable_motion) {
        motion_status = INPUT_MOTION_DISABLED;
    }
    if (requests.motion) {
        motion_status = INPUT_MOTION_UNAVAILABLE;
    }
}
#endif
