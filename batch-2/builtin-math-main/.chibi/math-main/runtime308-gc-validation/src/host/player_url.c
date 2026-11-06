#include "host/player_url.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>

// clang-format off
EM_JS(void, player_url_start, (), {
    if (Module['chibiUrlHost']) return;
    const host = { generation: 0, gesture: null, pending: null, timer: null };
    const MAX_GESTURE_AGE_MS = 5000;
    Module['chibiUrlHost'] = host;
    host.cancel = () => {
        host.gesture = null;
        host.pending = null;
        clearTimeout(host.timer);
        host.timer = null;
    };
    host.launch = () => {
        const gesture = host.gesture, request = host.pending;
        if (!gesture || !request || gesture.used ||
            gesture.generation !== request.generation || document.hidden ||
            performance.now() - gesture.time > MAX_GESTURE_AGE_MS ||
            !navigator.userActivation?.isActive) return false;
        gesture.used = true;
        host.pending = null;
        clearTimeout(host.timer);
        host.timer = null;
        // noopener intentionally returns null even when opening succeeds.
        // Never turn that return value into a claimed opened/blocked status.
        window.open(request.url, '_blank', 'noopener,noreferrer');
        return true;
    };
    const begin = (key, pointer) => {
        host.cancel();
        host.gesture = { generation: ++host.generation, time: performance.now(),
                         key, pointer, released: false, used: false };
    };
    host.keydown = event => {
        if (!event.isTrusted || event.repeat) return;
        if (event.key === 'Escape') {
            host.cancel();
            return;
        }
        begin(event.code, null);
    };
    host.pointerdown = event => {
        if (event.isTrusted) begin(null, event.pointerId);
    };
    const release = event => {
        const gesture = host.gesture;
        if (!event.isTrusted || !gesture) return;
        if (event.type === 'keyup' ? event.code !== gesture.key
                                   : event.pointerId !== gesture.pointer) return;
        gesture.released = true;
        // Non-mouse pointers receive activation on pointerup. A request can
        // wait for this exact release, never for a later unrelated gesture.
        if (host.pending && !host.launch()) host.cancel();
    };
    host.keyup = release;
    host.pointerup = release;
    host.hidden = () => {
        if (document.hidden) host.cancel();
    };
    window.addEventListener('keydown', host.keydown, true);
    window.addEventListener('keyup', host.keyup, true);
    window.addEventListener('pointerdown', host.pointerdown, true);
    window.addEventListener('pointerup', host.pointerup, true);
    window.addEventListener('pointercancel', host.cancel, true);
    window.addEventListener('pagehide', host.cancel);
    document.addEventListener('visibilitychange', host.hidden);
})

EM_JS(void, player_url_stop, (), {
    const host = Module['chibiUrlHost'];
    if (!host) return;
    host.cancel();
    window.removeEventListener('keydown', host.keydown, true);
    window.removeEventListener('keyup', host.keyup, true);
    window.removeEventListener('pointerdown', host.pointerdown, true);
    window.removeEventListener('pointerup', host.pointerup, true);
    window.removeEventListener('pointercancel', host.cancel, true);
    window.removeEventListener('pagehide', host.cancel);
    document.removeEventListener('visibilitychange', host.hidden);
    Module['chibiUrlHost'] = null;
})

EM_JS(void, player_url_submit, (const char *url), {
    const host = Module['chibiUrlHost'];
    if (!host) return;
    if (!url) {
        host.cancel();
        return;
    }
    clearTimeout(host.timer);
    host.pending = null;
    const gesture = host.gesture;
    if (!gesture || gesture.used || document.hidden ||
        performance.now() - gesture.time > 5000) {
        host.cancel();
        return;
    }
    host.pending = { url: UTF8ToString(url), generation: gesture.generation };
    // Authored actions run in animation frames, often after a quick click or
    // keyup has finished. Its transient activation can still be consumed now.
    if (host.launch()) return;
    if (gesture.released) {
        host.cancel();
        return;
    }
    host.timer = setTimeout(host.cancel, 5000 - (performance.now() - gesture.time));
})
// clang-format on
#else
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_misc.h>
#include <stdio.h>

void player_url_start() {}
void player_url_stop() {}

void player_url_submit(const char *url)
{
    if (url && !SDL_OpenURL(url)) {
        fprintf(stderr, "Cannot open URL: %s\n", SDL_GetError());
    }
}
#endif
